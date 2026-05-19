package hostcli

import (
	"bytes"
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
)

var ansiRE = regexp.MustCompile(`\x1b\[[0-9;]*m`)

type App struct {
	FindPort func() (string, error)
	Transact func(port string, command string, timeout time.Duration) (string, error)
}

type durationFlag struct {
	value time.Duration
}

func NewApp() App {
	return App{
		FindPort: FindPort,
		Transact: Transact,
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

	fs := flag.NewFlagSet("agent-debugboardctl", flag.ContinueOnError)
	fs.SetOutput(stderr)
	fs.StringVar(&portName, "port", "", "serial port, for example /dev/cu.usbmodemXXXX")
	fs.Var(timeout, "timeout", "command timeout, for example 2s or 0.5")
	fs.BoolVar(&raw, "raw", false, "send args as a raw shell command")
	fs.Usage = func() {
		fmt.Fprintf(stderr, "usage: agent-debugboardctl [--port PORT] [--timeout 2s] [--raw] <command> [args...]\n\n")
		fmt.Fprintf(stderr, "examples:\n")
		fmt.Fprintf(stderr, "  agent-debugboardctl status\n")
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

	commandArgs := fs.Args()
	if len(commandArgs) == 0 {
		fmt.Fprintln(stderr, "missing command, for example: adc read")
		fs.Usage()
		return 2
	}

	if portName == "" {
		var err error
		portName, err = a.FindPort()
		if err != nil {
			fmt.Fprintln(stderr, err)
			return 1
		}
	}

	command := strings.Join(commandArgs, " ")
	if !raw {
		command = "debugboard " + command
	}

	output, err := a.Transact(portName, command, timeout.value)
	if err != nil {
		fmt.Fprintln(stderr, err)
		return 1
	}

	cleaned := CleanOutput(output, command)
	if cleaned != "" {
		fmt.Fprintln(stdout, cleaned)
	}
	return 0
}

func FindPort() (string, error) {
	ports, err := serial.GetPortsList()
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
