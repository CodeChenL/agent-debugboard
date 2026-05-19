# agent-debugboard

[中文](README.zh-CN.md)

RP2040 firmware for **Agent DebugBoard**, a USB-controlled hardware bridge that
lets a PC-side Agent/AI operate target-board power, boot-mode, TF/SD routing,
current-monitor ADC channels, and a small safe GPIO surface.

![Agent Debugger promo](doc/marketing/agent-debugger-promo.png)

## Overview

Agent DebugBoard is designed for automated board bring-up, recovery, production
test, and remote debugging workflows. The firmware exposes a USB CDC ACM shell
named `Agent DebugBoard`, plus a host-side Python helper that turns board
operations into scriptable commands.

This repository contains the Zephyr application, host helper, unit tests,
schematic copy, and project documentation.

## Features

| Area | Supported in this firmware |
| --- | --- |
| USB control | CDC ACM shell with `debugboard` commands |
| Host automation | `agent-debugboardctl` CLI |
| Power rails | `12v_out`, `5v_out`, `5v_ws`, `20v_out` |
| ADC monitor | Current monitor reads for `5v_out`, `12v_out`, `20v_out` |
| TF/SD routing | Switch route between `target` and `usb-reader` |
| GPIO | Safe allowlist: `GP13`, `GP14`, `GP15`, `GP22`, `GP23`, `GP24` |
| Firmware update | USB command to reboot RP2040 into BOOTSEL |

`5V_FIN` is intentionally treated as a separate input/source rail. It is not
exposed as a controllable output rail.

## Install Host CLI

`agent-debugboardctl` is a native Go binary. Users do not need Python, pip, or a
virtual environment. Download the artifact matching your OS and CPU from a
`Build` workflow run:

| OS / CPU | Artifact |
| --- | --- |
| Windows x64 | `agent-debugboardctl_windows_amd64.zip` |
| Windows arm64 | `agent-debugboardctl_windows_arm64.zip` |
| Linux x64 | `agent-debugboardctl_linux_amd64.tar.gz` |
| Linux arm64 | `agent-debugboardctl_linux_arm64.tar.gz` |
| macOS Intel | `agent-debugboardctl_darwin_amd64.tar.gz` |
| macOS Apple Silicon | `agent-debugboardctl_darwin_arm64.tar.gz` |

Windows PowerShell:

```powershell
.\agent-debugboardctl.exe --help
```

Linux / macOS:

```sh
chmod +x ./agent-debugboardctl
sudo install -m 755 ./agent-debugboardctl /usr/local/bin/agent-debugboardctl
agent-debugboardctl --help
```

Developers can build it from source:

```sh
go build -o agent-debugboardctl ./cmd/agent-debugboardctl
./agent-debugboardctl --help
```

## Build Firmware

Create the Python environment and fetch Zephyr:

```sh
python3 -m venv .venv
source .venv/bin/activate
pip install -U pip west

west init -l .
west update
west zephyr-export
pip install -r zephyr/scripts/requirements.txt
```

Install the Zephyr SDK if it is not already installed. The current local build
has been verified with Zephyr SDK `1.0.1`.

Build the RP2040 firmware:

```sh
source .venv/bin/activate
west build -p always -b rpi_pico/rp2040 apps/agent_debugboard -d build/agent_debugboard
```

The generated UF2 is:

```text
build/agent_debugboard/zephyr/zephyr.uf2
```

## Flashing

If the board is already running this firmware, ask it to enter BOOTSEL and then
load the new UF2:

```sh
agent-debugboardctl bootloader
picotool load -v -x build/agent_debugboard/zephyr/zephyr.uf2
```

If the board is already mounted as `RPI-RP2`, only run:

```sh
picotool load -v -x build/agent_debugboard/zephyr/zephyr.uf2
```

## GitHub Actions Artifacts

The `Build` workflow publishes firmware as `agent-debugboard-rp2040-firmware`
and host CLI archives as `agent-debugboardctl-native-packages`.

- `agent-debugboard-rp2040-firmware`: UF2, ELF, and map files for RP2040.
- `agent-debugboardctl_windows_amd64.zip`: native Windows x64 CLI.
- `agent-debugboardctl_windows_arm64.zip`: native Windows arm64 CLI.
- `agent-debugboardctl_linux_amd64.tar.gz`: native Linux x64 CLI.
- `agent-debugboardctl_linux_arm64.tar.gz`: native Linux arm64 CLI.
- `agent-debugboardctl_darwin_amd64.tar.gz`: native macOS Intel CLI.
- `agent-debugboardctl_darwin_arm64.tar.gz`: native macOS Apple Silicon CLI.
- `checksums.txt`: SHA256 checksums for host CLI archives.

After downloading the host CLI artifact, extract it and run:

```sh
agent-debugboardctl --help
```

## Host Usage

Query board status:

```sh
agent-debugboardctl status
```

Control rails:

```sh
agent-debugboardctl rail set 12v_out on
agent-debugboardctl rail set 12v_out off
agent-debugboardctl rail set 5v_out on
agent-debugboardctl rail set 5v_out off
agent-debugboardctl rail set 5v_ws on
agent-debugboardctl rail set 20v_out on
```

Read current-monitor ADC channels:

```sh
agent-debugboardctl adc read
agent-debugboardctl adc read 5v_out
agent-debugboardctl adc read 12v_out
agent-debugboardctl adc read 20v_out
```

Switch TF/SD route:

```sh
agent-debugboardctl sd route target
agent-debugboardctl sd route usb-reader
```

Use safe GPIOs:

```sh
agent-debugboardctl gpio list
agent-debugboardctl gpio set GP13 1
agent-debugboardctl gpio input GP13
```

## Direct Shell

Open the CDC serial device and use the `debugboard` shell command:

```text
debugboard status
debugboard rail list
debugboard adc read
debugboard sd get
debugboard gpio list
debugboard bootloader
```

## Hardware Mapping

| Function | Firmware name | Schematic signal |
| --- | --- | --- |
| 12 V output enable | `12v_out` | `GP02_12V_EN` |
| 5 V output enable | `5v_out` | `GP05_5V_EN` |
| 5 V WS enable | `5v_ws` | `GP09_5V_WS_EN` |
| 20 V output enable | `20v_out` | `GP10_20V_EN` |
| TF/SD route switch | `sd route` | `GP06_TF_SW` |
| 5 V current monitor | `adc read 5v_out` | `S_C_5V` |
| 12 V current monitor | `adc read 12v_out` | `S_C_12V` |
| 20 V current monitor | `adc read 20v_out` | `S_C_20V` |

The current schematic copy is stored at
[doc/agent-debugboard-schematic.pdf](doc/agent-debugboard-schematic.pdf).

## Development

Run unit tests:

```sh
./apps/agent_debugboard/tests/run_unit_tests.sh
```

The test runner covers:

- host C tests for the shared board model.
- Go tests for the host CLI helper.

## Repository Layout

```text
apps/agent_debugboard/        Zephyr application
apps/agent_debugboard/src/    Firmware source and shared board model
apps/agent_debugboard/tests/  Unit tests
cmd/agent-debugboardctl/      Go host CLI entrypoint
internal/hostcli/             Go host CLI implementation
doc/                          Hardware documents and marketing assets
.goreleaser.yaml              GoReleaser host CLI packaging config
go.mod, go.sum                Go module for host CLI
west.yml                      Zephyr workspace manifest
```
