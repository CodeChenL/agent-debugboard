package hostcli

import (
	"bytes"
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
