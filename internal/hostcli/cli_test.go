package hostcli

import (
	"bytes"
	"encoding/json"
	"errors"
	"strings"
	"testing"
	"time"
)

func TestCleanOutputStripsEchoPromptAndANSI(t *testing.T) {
	raw := "\r\ndebugboard status\r\nproject=agent-debugboard\r\n\x1b[1;32mdebugboard:~$ \x1b[m\r\n"

	got := CleanOutput(raw, "debugboard status")
	if got != "project=agent-debugboard" {
		t.Fatalf("CleanOutput() = %q", got)
	}
}

func TestRunPrefixesDebugboardCommand(t *testing.T) {
	var stdout bytes.Buffer
	var stderr bytes.Buffer
	var calls []string

	app := App{
		FindPort: func() (string, error) {
			return "/dev/cu.debugboard", nil
		},
		Transact: func(port string, command string, timeout time.Duration) (string, error) {
			calls = append(calls, port+"|"+command+"|"+timeout.String())
			return "debugboard status\r\nproject=agent-debugboard\r\ndebugboard:~$ ", nil
		},
	}

	code := app.Run([]string{"status"}, &stdout, &stderr)
	if code != 0 {
		t.Fatalf("Run() exit code = %d stderr=%q", code, stderr.String())
	}
	if len(calls) != 1 || calls[0] != "/dev/cu.debugboard|debugboard status|2s" {
		t.Fatalf("calls = %#v", calls)
	}
	if strings.TrimSpace(stdout.String()) != "project=agent-debugboard" {
		t.Fatalf("stdout = %q", stdout.String())
	}
}

func TestRunRawCommandAndTimeout(t *testing.T) {
	var stdout bytes.Buffer
	var stderr bytes.Buffer
	var gotPort string
	var gotCommand string
	var gotTimeout time.Duration

	app := App{
		FindPort: func() (string, error) {
			t.Fatal("FindPort should not be called when --port is provided")
			return "", nil
		},
		Transact: func(port string, command string, timeout time.Duration) (string, error) {
			gotPort = port
			gotCommand = command
			gotTimeout = timeout
			return "version\r\nok\r\ndebugboard:~$ ", nil
		},
	}

	code := app.Run([]string{"--port", "/dev/cu.fake", "--timeout", "0.5", "--raw", "version"}, &stdout, &stderr)
	if code != 0 {
		t.Fatalf("Run() exit code = %d stderr=%q", code, stderr.String())
	}
	if gotPort != "/dev/cu.fake" || gotCommand != "version" || gotTimeout != 500*time.Millisecond {
		t.Fatalf("got port=%q command=%q timeout=%s", gotPort, gotCommand, gotTimeout)
	}
	if strings.TrimSpace(stdout.String()) != "ok" {
		t.Fatalf("stdout = %q", stdout.String())
	}
}

func TestRunJSONCommandRequestsAndValidatesFirmwareJSON(t *testing.T) {
	var stdout bytes.Buffer
	var stderr bytes.Buffer
	var gotCommand string

	response := `{"schema":"agent-debugboard.v1","ok":true,"command":"status","project":"agent-debugboard"}`
	app := App{
		FindPort: func() (string, error) {
			return "/dev/cu.debugboard", nil
		},
		Transact: func(port string, command string, timeout time.Duration) (string, error) {
			gotCommand = command
			return command + "\r\n" + response + "\r\n" + PromptText + " ", nil
		},
	}

	code := app.Run([]string{"--json", "status"}, &stdout, &stderr)
	if code != 0 {
		t.Fatalf("Run() exit code = %d stderr=%q stdout=%q", code, stderr.String(), stdout.String())
	}
	if gotCommand != "debugboard status --json" {
		t.Fatalf("command = %q", gotCommand)
	}
	if strings.TrimSpace(stdout.String()) != response {
		t.Fatalf("stdout = %q", stdout.String())
	}
}

func TestRunVerboseCommandRequestsFirmwareVerbose(t *testing.T) {
	var stdout bytes.Buffer
	var stderr bytes.Buffer
	var gotCommand string

	response := `5v_out signal=S_C_5V raw=22 mv=17 ma_est=540`
	app := App{
		FindPort: func() (string, error) {
			return "/dev/cu.debugboard", nil
		},
		Transact: func(port string, command string, timeout time.Duration) (string, error) {
			gotCommand = command
			return command + "\r\n" + response + "\r\n" + PromptText + " ", nil
		},
	}

	code := app.Run([]string{"-v", "adc", "read", "5v_out"}, &stdout, &stderr)
	if code != 0 {
		t.Fatalf("Run() exit code = %d stderr=%q stdout=%q", code, stderr.String(), stdout.String())
	}
	if gotCommand != "debugboard adc read 5v_out -v" {
		t.Fatalf("command = %q", gotCommand)
	}
	if strings.TrimSpace(stdout.String()) != response {
		t.Fatalf("stdout = %q", stdout.String())
	}
}

func TestRunVerboseCommandPassesThroughCommandFlag(t *testing.T) {
	var stdout bytes.Buffer
	var stderr bytes.Buffer
	var gotCommand string

	response := `5v_out signal=S_C_5V raw=22 mv=17 ma_est=540`
	app := App{
		FindPort: func() (string, error) {
			return "/dev/cu.debugboard", nil
		},
		Transact: func(port string, command string, timeout time.Duration) (string, error) {
			gotCommand = command
			return command + "\r\n" + response + "\r\n" + PromptText + " ", nil
		},
	}

	code := app.Run([]string{"adc", "read", "-v", "5v_out"}, &stdout, &stderr)
	if code != 0 {
		t.Fatalf("Run() exit code = %d stderr=%q stdout=%q", code, stderr.String(), stdout.String())
	}
	if gotCommand != "debugboard adc read -v 5v_out" {
		t.Fatalf("command = %q", gotCommand)
	}
	if strings.TrimSpace(stdout.String()) != response {
		t.Fatalf("stdout = %q", stdout.String())
	}
}

func TestRunJSONCommandIgnoresGlobalVerbose(t *testing.T) {
	var stdout bytes.Buffer
	var stderr bytes.Buffer
	var gotCommand string

	response := `{"schema":"agent-debugboard.v1","ok":true,"command":"adc","readings":[{"name":"5v_out","ma_est":540}]}`
	app := App{
		FindPort: func() (string, error) {
			return "/dev/cu.debugboard", nil
		},
		Transact: func(port string, command string, timeout time.Duration) (string, error) {
			gotCommand = command
			return command + "\r\n" + response + "\r\n" + PromptText + " ", nil
		},
	}

	code := app.Run([]string{"--json", "-v", "adc", "read", "5v_out"}, &stdout, &stderr)
	if code != 0 {
		t.Fatalf("Run() exit code = %d stderr=%q stdout=%q", code, stderr.String(), stdout.String())
	}
	if gotCommand != "debugboard adc read 5v_out --json" {
		t.Fatalf("command = %q", gotCommand)
	}
	if strings.TrimSpace(stdout.String()) != response {
		t.Fatalf("stdout = %q", stdout.String())
	}
}

func TestRunJSONCommandReturnsFailureOnBoardError(t *testing.T) {
	var stdout bytes.Buffer
	var stderr bytes.Buffer

	response := `{"schema":"agent-debugboard.v1","ok":false,"command":"rail","error":{"code":"unknown_rail","message":"unknown rail"}}`
	app := App{
		FindPort: func() (string, error) {
			return "/dev/cu.debugboard", nil
		},
		Transact: func(port string, command string, timeout time.Duration) (string, error) {
			return command + "\r\n" + response + "\r\n" + PromptText + " ", nil
		},
	}

	code := app.Run([]string{"--json", "rail", "get", "missing"}, &stdout, &stderr)
	if code != 1 {
		t.Fatalf("Run() exit code = %d stdout=%q stderr=%q", code, stdout.String(), stderr.String())
	}
	if strings.TrimSpace(stdout.String()) != response {
		t.Fatalf("stdout = %q", stdout.String())
	}
}

func TestRunJSONRejectsOldTextFirmwareOutput(t *testing.T) {
	var stdout bytes.Buffer
	var stderr bytes.Buffer

	app := App{
		FindPort: func() (string, error) {
			return "/dev/cu.debugboard", nil
		},
		Transact: func(port string, command string, timeout time.Duration) (string, error) {
			return command + "\r\nproject=agent-debugboard\r\n" + PromptText + " ", nil
		},
	}

	code := app.Run([]string{"--json", "status"}, &stdout, &stderr)
	if code != 1 {
		t.Fatalf("Run() exit code = %d stdout=%q stderr=%q", code, stdout.String(), stderr.String())
	}

	var got jsonResponse
	if err := json.Unmarshal(stdout.Bytes(), &got); err != nil {
		t.Fatalf("stdout is not JSON: %v stdout=%q", err, stdout.String())
	}
	if got.OK || got.Command != "status" || got.Error == nil || got.Error.Code != "invalid_json" {
		t.Fatalf("json error = %#v", got)
	}
}

func TestRunRawJSONDoesNotAppendFirmwareJSONFlag(t *testing.T) {
	var stdout bytes.Buffer
	var stderr bytes.Buffer
	var gotCommand string

	app := App{
		FindPort: func() (string, error) {
			return "/dev/cu.debugboard", nil
		},
		Transact: func(port string, command string, timeout time.Duration) (string, error) {
			gotCommand = command
			return command + "\r\n{\"raw\":true}\r\n" + PromptText + " ", nil
		},
	}

	code := app.Run([]string{"--raw", "--json", "version"}, &stdout, &stderr)
	if code != 0 {
		t.Fatalf("Run() exit code = %d stdout=%q stderr=%q", code, stdout.String(), stderr.String())
	}
	if gotCommand != "version" {
		t.Fatalf("command = %q", gotCommand)
	}
	if strings.TrimSpace(stdout.String()) != `{"raw":true}` {
		t.Fatalf("stdout = %q", stdout.String())
	}
}

func TestRunReportsFindPortError(t *testing.T) {
	var stdout bytes.Buffer
	var stderr bytes.Buffer

	app := App{
		FindPort: func() (string, error) {
			return "", errors.New("not found")
		},
		Transact: func(port string, command string, timeout time.Duration) (string, error) {
			t.Fatal("Transact should not be called when FindPort fails")
			return "", nil
		},
	}

	code := app.Run([]string{"status"}, &stdout, &stderr)
	if code != 1 {
		t.Fatalf("Run() exit code = %d", code)
	}
	if !strings.Contains(stderr.String(), "not found") {
		t.Fatalf("stderr = %q", stderr.String())
	}
}

func TestRunHelpReturnsSuccess(t *testing.T) {
	var stdout bytes.Buffer
	var stderr bytes.Buffer

	app := App{
		FindPort: func() (string, error) {
			t.Fatal("FindPort should not be called for --help")
			return "", nil
		},
		Transact: func(port string, command string, timeout time.Duration) (string, error) {
			t.Fatal("Transact should not be called for --help")
			return "", nil
		},
	}

	code := app.Run([]string{"--help"}, &stdout, &stderr)
	if code != 0 {
		t.Fatalf("Run() exit code = %d stderr=%q", code, stderr.String())
	}
	if !strings.Contains(stderr.String(), "usage: agent-debugboardctl") {
		t.Fatalf("stderr = %q", stderr.String())
	}
}

func TestRunVersionReturnsSuccessWithoutBoardAccess(t *testing.T) {
	var stdout bytes.Buffer
	var stderr bytes.Buffer

	app := App{
		FindPort: func() (string, error) {
			t.Fatal("FindPort should not be called for --version")
			return "", nil
		},
		Transact: func(port string, command string, timeout time.Duration) (string, error) {
			t.Fatal("Transact should not be called for --version")
			return "", nil
		},
	}

	code := app.Run([]string{"--version"}, &stdout, &stderr)
	if code != 0 {
		t.Fatalf("Run() exit code = %d stderr=%q", code, stderr.String())
	}
	if strings.TrimSpace(stdout.String()) != "agent-debugboardctl "+Version {
		t.Fatalf("stdout = %q", stdout.String())
	}
}

func TestRunJSONVersionReturnsSuccessWithoutBoardAccess(t *testing.T) {
	var stdout bytes.Buffer
	var stderr bytes.Buffer

	app := App{
		FindPort: func() (string, error) {
			t.Fatal("FindPort should not be called for --version")
			return "", nil
		},
		Transact: func(port string, command string, timeout time.Duration) (string, error) {
			t.Fatal("Transact should not be called for --version")
			return "", nil
		},
	}

	code := app.Run([]string{"--json", "--version"}, &stdout, &stderr)
	if code != 0 {
		t.Fatalf("Run() exit code = %d stderr=%q", code, stderr.String())
	}

	var got map[string]any
	if err := json.Unmarshal(stdout.Bytes(), &got); err != nil {
		t.Fatalf("stdout is not JSON: %v stdout=%q", err, stdout.String())
	}
	if got["schema"] != JSONSchema || got["command"] != "version" || got["version"] != Version {
		t.Fatalf("stdout JSON = %#v", got)
	}
}

func TestDoctorJSONReportsMissingBoard(t *testing.T) {
	var stdout bytes.Buffer
	var stderr bytes.Buffer

	app := App{
		ListPorts: func() ([]string, error) {
			return []string{"/dev/ttyS0", "/dev/ttyACM0"}, nil
		},
		ProbePort: func(portName string) bool {
			return false
		},
		Transact: func(port string, command string, timeout time.Duration) (string, error) {
			t.Fatal("Transact should not be called without a probed board")
			return "", nil
		},
	}

	code := app.Run([]string{"--json", "doctor"}, &stdout, &stderr)
	if code != 1 {
		t.Fatalf("Run() exit code = %d stdout=%q stderr=%q", code, stdout.String(), stderr.String())
	}

	var got doctorResult
	if err := json.Unmarshal(stdout.Bytes(), &got); err != nil {
		t.Fatalf("stdout is not JSON: %v stdout=%q", err, stdout.String())
	}
	if got.OK || got.Command != "doctor" || got.Error == nil || got.Error.Code != "port_not_found" {
		t.Fatalf("doctor JSON = %#v", got)
	}
	if strings.Join(got.Candidates, ",") != "/dev/ttyACM0" {
		t.Fatalf("candidates = %#v", got.Candidates)
	}
}

func TestDoctorJSONReportsBoardStatus(t *testing.T) {
	var stdout bytes.Buffer
	var stderr bytes.Buffer

	status := `{"schema":"agent-debugboard.v1","ok":true,"command":"status","project":"agent-debugboard"}`
	app := App{
		ListPorts: func() ([]string, error) {
			return []string{"/dev/ttyS0", "/dev/cu.usbmodem21201"}, nil
		},
		ProbePort: func(portName string) bool {
			return portName == "/dev/cu.usbmodem21201"
		},
		Transact: func(port string, command string, timeout time.Duration) (string, error) {
			if port != "/dev/cu.usbmodem21201" || command != "debugboard status --json" {
				t.Fatalf("port=%q command=%q", port, command)
			}
			return command + "\r\n" + status + "\r\n" + PromptText + " ", nil
		},
	}

	code := app.Run([]string{"--json", "doctor"}, &stdout, &stderr)
	if code != 0 {
		t.Fatalf("Run() exit code = %d stdout=%q stderr=%q", code, stdout.String(), stderr.String())
	}

	var got doctorResult
	if err := json.Unmarshal(stdout.Bytes(), &got); err != nil {
		t.Fatalf("stdout is not JSON: %v stdout=%q", err, stdout.String())
	}
	if !got.OK || got.SelectedPort != "/dev/cu.usbmodem21201" || !got.ProbeOK {
		t.Fatalf("doctor JSON = %#v", got)
	}
	if strings.TrimSpace(string(got.Status)) != status {
		t.Fatalf("status = %s", got.Status)
	}
}

func TestCandidatePortsFiltersCommonUSBSerialNames(t *testing.T) {
	got := CandidatePorts([]string{
		"/dev/ttyS0",
		"/dev/cu.Bluetooth-Incoming-Port",
		"/dev/cu.usbmodem21201",
		"/dev/ttyACM0",
		"/dev/ttyUSB0",
		"COM7",
		"/dev/cu.usbmodem21201",
	})
	want := []string{
		"/dev/cu.usbmodem21201",
		"/dev/ttyACM0",
		"/dev/ttyUSB0",
		"COM7",
	}
	if strings.Join(got, ",") != strings.Join(want, ",") {
		t.Fatalf("CandidatePorts() = %#v, want %#v", got, want)
	}
}

func TestFindPortFromListProbesCandidates(t *testing.T) {
	var probed []string
	got, err := FindPortFromList([]string{"/dev/ttyS0", "/dev/ttyACM0", "/dev/ttyUSB0"}, func(portName string) bool {
		probed = append(probed, portName)
		return portName == "/dev/ttyUSB0"
	})
	if err != nil {
		t.Fatalf("FindPortFromList() error = %v", err)
	}
	if got != "/dev/ttyUSB0" {
		t.Fatalf("FindPortFromList() = %q", got)
	}
	if strings.Join(probed, ",") != "/dev/ttyACM0,/dev/ttyUSB0" {
		t.Fatalf("probed = %#v", probed)
	}
}

func TestIsDebugBoardStatus(t *testing.T) {
	output := "debugboard status\r\nproject=agent-debugboard\r\nmcu=rp2040\r\ndebugboard:~$ "
	if !IsDebugBoardStatus(output) {
		t.Fatal("expected debugboard status output to be recognized")
	}
	if IsDebugBoardStatus("project=other") {
		t.Fatal("unexpected match for unrelated status output")
	}
}
