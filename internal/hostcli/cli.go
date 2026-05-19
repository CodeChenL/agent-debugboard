package hostcli

import (
	"bytes"
	"encoding/json"
	"errors"
	"flag"
	"fmt"
	"io"
	"regexp"
	"strconv"
	"strings"
	"time"

	"go.bug.st/serial"
)

const (
	PromptText        = "debugboard:~$"
	probeCommand      = "debugboard status"
	probeTimeout      = 1200 * time.Millisecond
	projectStatusLine = "project=agent-debugboard"
	JSONSchema        = "agent-debugboard.v1"
)

var ansiRE = regexp.MustCompile(`\x1b\[[0-9;]*m`)

var Version = "dev"

type App struct {
	ListPorts func() ([]string, error)
	FindPort  func() (string, error)
	ProbePort func(portName string) bool
	Transact  func(port string, command string, timeout time.Duration) (string, error)
}

type durationFlag struct {
	value time.Duration
}

type JSONError struct {
	Code    string `json:"code"`
	Message string `json:"message"`
}

type jsonEnvelope struct {
	Schema  string     `json:"schema"`
	OK      *bool      `json:"ok"`
	Command string     `json:"command"`
	Error   *JSONError `json:"error,omitempty"`
}

type jsonResponse struct {
	Schema  string     `json:"schema"`
	OK      bool       `json:"ok"`
	Command string     `json:"command"`
	Error   *JSONError `json:"error,omitempty"`
}

type doctorResult struct {
	Schema       string          `json:"schema"`
	OK           bool            `json:"ok"`
	Command      string          `json:"command"`
	CLIVersion   string          `json:"cli_version"`
	Ports        []string        `json:"ports"`
	Candidates   []string        `json:"candidates"`
	SelectedPort string          `json:"selected_port,omitempty"`
	ProbeOK      bool            `json:"probe_ok"`
	Status       json.RawMessage `json:"status,omitempty"`
	StatusText   string          `json:"status_text,omitempty"`
	Error        *JSONError      `json:"error,omitempty"`
}

type jsonValidationError struct {
	code    string
	message string
}

func (e jsonValidationError) Error() string {
	return e.message
}

func NewApp() App {
	return App{
		ListPorts: ListPorts,
		FindPort:  FindPort,
		ProbePort: ProbePort,
		Transact:  Transact,
	}
}

func (d *durationFlag) String() string {
	return d.value.String()
}

func (d *durationFlag) Set(value string) error {
	if parsed, err := time.ParseDuration(value); err == nil {
		d.value = parsed
		return nil
	}

	seconds, err := strconv.ParseFloat(value, 64)
	if err != nil {
		return fmt.Errorf("timeout must be a Go duration like 2s or seconds like 0.5")
	}
	if seconds <= 0 {
		return errors.New("timeout must be greater than zero")
	}

	d.value = time.Duration(seconds * float64(time.Second))
	return nil
}

func (a App) Run(args []string, stdout io.Writer, stderr io.Writer) int {
	timeout := &durationFlag{value: 2 * time.Second}
	var portName string
	var raw bool
	var showVersion bool
	var jsonOutput bool

	fs := flag.NewFlagSet("agent-debugboardctl", flag.ContinueOnError)
	fs.SetOutput(stderr)
	fs.StringVar(&portName, "port", "", "serial port, for example /dev/cu.usbmodemXXXX")
	fs.Var(timeout, "timeout", "command timeout, for example 2s or 0.5")
	fs.BoolVar(&raw, "raw", false, "send args as a raw shell command")
	fs.BoolVar(&jsonOutput, "json", false, "request and validate JSON output")
	fs.BoolVar(&showVersion, "version", false, "print version and exit")
	fs.Usage = func() {
		fmt.Fprintf(stderr, "usage: agent-debugboardctl [--port PORT] [--timeout 2s] [--raw] [--json] [--version] <command> [args...]\n\n")
		fmt.Fprintf(stderr, "examples:\n")
		fmt.Fprintf(stderr, "  agent-debugboardctl status\n")
		fmt.Fprintf(stderr, "  agent-debugboardctl --json status\n")
		fmt.Fprintf(stderr, "  agent-debugboardctl doctor\n")
		fmt.Fprintf(stderr, "  agent-debugboardctl rail set 12v_out on\n")
		fmt.Fprintf(stderr, "  agent-debugboardctl adc read\n\n")
		fs.PrintDefaults()
	}

	if err := fs.Parse(args); err != nil {
		if errors.Is(err, flag.ErrHelp) {
			return 0
		}
		return 2
	}

	if showVersion {
		if jsonOutput {
			writeJSON(stdout, map[string]any{
				"schema":  JSONSchema,
				"ok":      true,
				"command": "version",
				"version": Version,
			})
			return 0
		}
		fmt.Fprintf(stdout, "agent-debugboardctl %s\n", Version)
		return 0
	}

	commandArgs := fs.Args()
	if len(commandArgs) == 0 {
		if jsonOutput {
			writeJSONError(stdout, "agent-debugboardctl", "missing_command",
				"missing command, for example: adc read")
			return 2
		}
		fmt.Fprintln(stderr, "missing command, for example: adc read")
		fs.Usage()
		return 2
	}

	if !raw && commandArgs[0] == "doctor" {
		if len(commandArgs) != 1 {
			if jsonOutput {
				writeJSONError(stdout, "doctor", "usage", "usage: agent-debugboardctl doctor")
			} else {
				fmt.Fprintln(stderr, "usage: agent-debugboardctl doctor")
			}
			return 2
		}
		return a.runDoctor(portName, timeout.value, jsonOutput, stdout, stderr)
	}

	wireArgs := append([]string(nil), commandArgs...)
	if jsonOutput && !raw && !hasArg(wireArgs, "--json") {
		wireArgs = append(wireArgs, "--json")
	}

	if portName == "" {
		var err error
		portName, err = a.findPort()
		if err != nil {
			if jsonOutput {
				writeJSONError(stdout, commandName(commandArgs, raw), "port_not_found", err.Error())
			} else {
				fmt.Fprintln(stderr, err)
			}
			return 1
		}
	}

	command := strings.Join(wireArgs, " ")
	if !raw {
		command = "debugboard " + command
	}

	output, err := a.transact(portName, command, timeout.value)
	if err != nil {
		if jsonOutput {
			writeJSONError(stdout, commandName(commandArgs, raw), "transport_error", err.Error())
		} else {
			fmt.Fprintln(stderr, err)
		}
		return 1
	}

	cleaned := CleanOutput(output, command)
	if jsonOutput {
		return writeValidatedJSON(stdout, cleaned, commandName(commandArgs, raw), !raw)
	}

	if cleaned != "" {
		fmt.Fprintln(stdout, cleaned)
	}
	return 0
}

func (a App) runDoctor(portName string, timeout time.Duration, jsonOutput bool, stdout io.Writer, stderr io.Writer) int {
	ports, err := a.listPorts()
	if ports == nil {
		ports = []string{}
	}
	result := doctorResult{
		Schema:     JSONSchema,
		Command:    "doctor",
		CLIVersion: Version,
		Ports:      ports,
		Candidates: CandidatePorts(ports),
	}
	if err != nil {
		result.Error = &JSONError{Code: "list_ports_failed", Message: err.Error()}
		return finishDoctor(stdout, stderr, result, jsonOutput, 1)
	}

	if portName != "" {
		result.SelectedPort = portName
		result.ProbeOK = a.probePort(portName)
	} else {
		for _, candidate := range result.Candidates {
			if a.probePort(candidate) {
				result.SelectedPort = candidate
				result.ProbeOK = true
				break
			}
		}
	}

	if result.SelectedPort == "" {
		result.Error = &JSONError{
			Code:    "port_not_found",
			Message: "Agent DebugBoard CDC port not found",
		}
		return finishDoctor(stdout, stderr, result, jsonOutput, 1)
	}

	if !result.ProbeOK {
		result.Error = &JSONError{
			Code:    "probe_failed",
			Message: "selected port did not answer as Agent DebugBoard",
		}
		return finishDoctor(stdout, stderr, result, jsonOutput, 1)
	}

	const statusCommand = "debugboard status --json"
	output, err := a.transact(result.SelectedPort, statusCommand, timeout)
	if err != nil {
		result.Error = &JSONError{Code: "status_failed", Message: err.Error()}
		return finishDoctor(stdout, stderr, result, jsonOutput, 1)
	}

	cleaned := CleanOutput(output, statusCommand)
	env, err := parseAgentJSON(cleaned, true)
	if err != nil {
		var validationErr jsonValidationError
		if errors.As(err, &validationErr) {
			result.Error = &JSONError{Code: validationErr.code, Message: validationErr.message}
		} else {
			result.Error = &JSONError{Code: "invalid_json", Message: err.Error()}
		}
		result.StatusText = cleaned
		return finishDoctor(stdout, stderr, result, jsonOutput, 1)
	}

	result.Status = json.RawMessage(cleaned)
	if env.OK != nil && !*env.OK {
		result.Error = env.Error
		if result.Error == nil {
			result.Error = &JSONError{Code: "status_error", Message: "board returned ok=false"}
		}
		return finishDoctor(stdout, stderr, result, jsonOutput, 1)
	}

	result.OK = true
	return finishDoctor(stdout, stderr, result, jsonOutput, 0)
}

func finishDoctor(stdout io.Writer, stderr io.Writer, result doctorResult, jsonOutput bool, exitCode int) int {
	if jsonOutput {
		writeJSON(stdout, result)
		return exitCode
	}
	printDoctorText(stdout, result)
	if result.Error != nil && exitCode != 0 {
		fmt.Fprintf(stderr, "%s: %s\n", result.Error.Code, result.Error.Message)
	}
	return exitCode
}

func printDoctorText(w io.Writer, result doctorResult) {
	fmt.Fprintln(w, "Agent DebugBoard doctor")
	fmt.Fprintf(w, "cli_version=%s\n", result.CLIVersion)
	printStringList(w, "ports", result.Ports)
	printStringList(w, "candidates", result.Candidates)
	if result.SelectedPort != "" {
		fmt.Fprintf(w, "selected_port=%s\n", result.SelectedPort)
	}
	fmt.Fprintf(w, "probe=%s\n", mapBool(result.ProbeOK, "ok", "failed"))
	if len(result.Status) > 0 {
		fmt.Fprintf(w, "status=%s\n", string(result.Status))
	}
	if result.StatusText != "" {
		fmt.Fprintf(w, "status_text=%s\n", result.StatusText)
	}
	if result.Error != nil {
		fmt.Fprintf(w, "error=%s: %s\n", result.Error.Code, result.Error.Message)
	} else {
		fmt.Fprintln(w, "result=ok")
	}
}

func printStringList(w io.Writer, name string, values []string) {
	if len(values) == 0 {
		fmt.Fprintf(w, "%s=[]\n", name)
		return
	}
	fmt.Fprintf(w, "%s:\n", name)
	for _, value := range values {
		fmt.Fprintf(w, "  %s\n", value)
	}
}

func mapBool(value bool, trueText string, falseText string) string {
	if value {
		return trueText
	}
	return falseText
}

func ListPorts() ([]string, error) {
	return serial.GetPortsList()
}

func FindPort() (string, error) {
	ports, err := ListPorts()
	if err != nil {
		return "", fmt.Errorf("failed to enumerate serial ports: %w", err)
	}

	return FindPortFromList(ports, ProbePort)
}

func FindPortFromList(ports []string, probe func(portName string) bool) (string, error) {
	for _, portName := range CandidatePorts(ports) {
		if probe(portName) {
			return portName, nil
		}
	}
	return "", errors.New("Agent DebugBoard CDC port not found; pass --port /dev/cu.usbmodemXXXX")
}

func CandidatePorts(ports []string) []string {
	candidates := make([]string, 0, len(ports))
	seen := make(map[string]bool, len(ports))

	add := func(portName string) {
		if portName == "" || seen[portName] {
			return
		}
		seen[portName] = true
		candidates = append(candidates, portName)
	}

	for _, portName := range ports {
		lower := strings.ToLower(portName)
		upper := strings.ToUpper(portName)
		if strings.Contains(lower, "usbmodem") ||
			strings.Contains(lower, "usbserial") ||
			strings.Contains(lower, "ttyacm") ||
			strings.Contains(lower, "ttyusb") ||
			strings.HasPrefix(upper, "COM") {
			add(portName)
		}
	}

	return candidates
}

func ProbePort(portName string) bool {
	output, err := Transact(portName, probeCommand, probeTimeout)
	if err != nil {
		return false
	}
	return IsDebugBoardStatus(output)
}

func IsDebugBoardStatus(output string) bool {
	return strings.Contains(CleanOutput(output, probeCommand), projectStatusLine)
}

func Transact(portName string, command string, timeout time.Duration) (string, error) {
	mode := &serial.Mode{BaudRate: 115200}
	port, err := serial.Open(portName, mode)
	if err != nil {
		return "", fmt.Errorf("open %s: %w", portName, err)
	}
	defer port.Close()

	_ = port.SetDTR(true)
	_ = port.SetReadTimeout(50 * time.Millisecond)
	time.Sleep(200 * time.Millisecond)
	_ = port.ResetInputBuffer()

	if _, err := port.Write([]byte(command + "\r\n")); err != nil {
		return "", fmt.Errorf("write command: %w", err)
	}
	_ = port.Drain()

	deadline := time.Now().Add(timeout)
	var data bytes.Buffer
	buf := make([]byte, 256)
	for time.Now().Before(deadline) {
		n, err := port.Read(buf)
		if err != nil {
			return "", fmt.Errorf("read response: %w", err)
		}
		if n == 0 {
			continue
		}

		data.Write(buf[:n])
		if bytes.Contains(data.Bytes(), []byte(PromptText)) {
			break
		}
	}

	return data.String(), nil
}

func CleanOutput(output string, command string) string {
	lines := make([]string, 0)
	normalized := strings.ReplaceAll(ansiRE.ReplaceAllString(output, ""), "\r", "")
	for _, line := range strings.Split(normalized, "\n") {
		stripped := strings.TrimSpace(line)
		if stripped == "" || stripped == command || stripped == PromptText {
			continue
		}
		if strings.HasSuffix(stripped, PromptText) {
			stripped = strings.TrimSpace(strings.TrimSuffix(stripped, PromptText))
		}
		if stripped != "" {
			lines = append(lines, stripped)
		}
	}
	return strings.Join(lines, "\n")
}

func (a App) listPorts() ([]string, error) {
	if a.ListPorts != nil {
		return a.ListPorts()
	}
	return ListPorts()
}

func (a App) findPort() (string, error) {
	if a.FindPort != nil {
		return a.FindPort()
	}
	return FindPort()
}

func (a App) probePort(portName string) bool {
	if a.ProbePort != nil {
		return a.ProbePort(portName)
	}
	return ProbePort(portName)
}

func (a App) transact(port string, command string, timeout time.Duration) (string, error) {
	if a.Transact != nil {
		return a.Transact(port, command, timeout)
	}
	return Transact(port, command, timeout)
}

func hasArg(args []string, value string) bool {
	for _, arg := range args {
		if arg == value {
			return true
		}
	}
	return false
}

func commandName(args []string, raw bool) string {
	if raw {
		return "raw"
	}
	if len(args) == 0 {
		return "agent-debugboardctl"
	}
	return args[0]
}

func writeValidatedJSON(w io.Writer, output string, command string, requireEnvelope bool) int {
	env, err := parseAgentJSON(output, requireEnvelope)
	if err != nil {
		var validationErr jsonValidationError
		if errors.As(err, &validationErr) {
			writeJSONError(w, command, validationErr.code, validationErr.message)
		} else {
			writeJSONError(w, command, "invalid_json", err.Error())
		}
		return 1
	}

	fmt.Fprintln(w, output)
	if requireEnvelope && env.OK != nil && !*env.OK {
		return 1
	}
	return 0
}

func parseAgentJSON(output string, requireEnvelope bool) (*jsonEnvelope, error) {
	cleaned := strings.TrimSpace(output)
	if cleaned == "" {
		return nil, jsonValidationError{
			code:    "invalid_json",
			message: "firmware returned empty output",
		}
	}
	if !json.Valid([]byte(cleaned)) {
		return nil, jsonValidationError{
			code:    "invalid_json",
			message: "firmware returned non-JSON output",
		}
	}
	if !requireEnvelope {
		return &jsonEnvelope{}, nil
	}

	var env jsonEnvelope
	if err := json.Unmarshal([]byte(cleaned), &env); err != nil {
		return nil, jsonValidationError{
			code:    "invalid_json",
			message: err.Error(),
		}
	}

	if requireEnvelope {
		if env.Schema != JSONSchema || env.OK == nil || env.Command == "" {
			return nil, jsonValidationError{
				code:    "invalid_json",
				message: "firmware returned JSON without agent-debugboard.v1 envelope",
			}
		}
	}

	return &env, nil
}

func writeJSONError(w io.Writer, command string, code string, message string) {
	writeJSON(w, jsonResponse{
		Schema:  JSONSchema,
		OK:      false,
		Command: command,
		Error: &JSONError{
			Code:    code,
			Message: message,
		},
	})
}

func writeJSON(w io.Writer, value any) {
	encoder := json.NewEncoder(w)
	encoder.SetEscapeHTML(false)
	_ = encoder.Encode(value)
}
