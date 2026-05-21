---
name: agent-debugboard
description: Use the Agent DebugBoard host CLI to diagnose and operate target-board power rails, ADC current monitors, safe GPIOs, TF/SD routing, and RP2040 BOOTSEL mode. Use this when an Agent needs hardware-control access through agent-debugboardctl.
---

# Agent DebugBoard

Use `agent-debugboardctl` for Agent-side hardware control. Prefer JSON output for all automation. Do not parse human-readable command output when `--json` is available.

## First Checks

1. Check whether the CLI exists:

   ```sh
   agent-debugboardctl --version
   ```

2. Check whether the installed CLI is new enough for Agent automation:

   ```sh
   agent-debugboardctl --json doctor
   ```

3. Treat these outcomes as follows:
   - Exit code `0` with `ok: true`: the CLI and board connection are ready.
   - Valid JSON with `ok: false`: read `error.code` and `error.message`; the CLI is installed but the board or serial path needs attention.
   - Unknown `--json` or `doctor`: the installed CLI is too old. Install `agent-debugboardctl` v0.0.3 or newer.
   - Command not found: install the CLI.

## Install CLI

Use the repository install scripts. They download the correct native binary, verify `SHA256SUMS.txt`, and install `agent-debugboardctl`. The JSON and `doctor` workflow requires `agent-debugboardctl` v0.0.3 or newer.

Public repository on macOS or Linux:

```sh
curl -fsSL https://raw.githubusercontent.com/xzl01/agent-debugboard/main/scripts/install.sh | sh
```

If `curl` is unavailable:

```sh
wget -qO- https://raw.githubusercontent.com/xzl01/agent-debugboard/main/scripts/install.sh | sh
```

Private repository on macOS or Linux:

```sh
export GH_TOKEN="$(gh auth token)"
curl -fsSL \
  -H "Authorization: Bearer $GH_TOKEN" \
  -H "Accept: application/vnd.github.raw" \
  "https://api.github.com/repos/xzl01/agent-debugboard/contents/scripts/install.sh?ref=main" | sh
```

Windows PowerShell:

```powershell
irm https://raw.githubusercontent.com/xzl01/agent-debugboard/main/scripts/install.ps1 | iex
```

Private repository on Windows PowerShell:

```powershell
$env:GH_TOKEN = gh auth token
irm `
  -Headers @{Authorization = "Bearer $env:GH_TOKEN"; Accept = "application/vnd.github.raw"} `
  "https://api.github.com/repos/xzl01/agent-debugboard/contents/scripts/install.ps1?ref=main" | iex
```

After installation, run:

```sh
agent-debugboardctl --version
agent-debugboardctl --json doctor
```

If the installed CLI is older than v0.0.3, install the latest release or build from the current checkout:

```sh
go build -trimpath -o agent-debugboardctl ./cmd/agent-debugboardctl
./agent-debugboardctl --json doctor
```

## JSON Contract

Agent automation should expect the top-level fields:

- `schema`: must be `agent-debugboard.v1`
- `ok`: boolean success flag
- `command`: command name
- `error`: present on failure, with `code` and `message`

If `ok` is `false`, do not infer success from partial fields. Handle `error.code` first.

## Common Commands

Diagnose board connection:

```sh
agent-debugboardctl --json doctor
```

Read full board state:

```sh
agent-debugboardctl --json status
```

List power rails:

```sh
agent-debugboardctl --json rail list
```

Control power rails:

```sh
agent-debugboardctl --json rail set 12v_out on
agent-debugboardctl --json rail set 12v_out off
agent-debugboardctl --json rail set 5v_out on
agent-debugboardctl --json rail set 5v_out off
agent-debugboardctl --json rail set 5v_ws on
agent-debugboardctl --json rail set 20v_out on
```

Restart a target board with its normal software reboot or reset interface first.
Use rail power-cycling only as a hard-restart fallback when soft reboot/reset is
unavailable, the target is unresponsive, or no reset line is exposed. Confirm
the rail name first, then turn it off, wait briefly for discharge, and turn it
back on:

```sh
agent-debugboardctl --json rail set 5v_out off
sleep 2
agent-debugboardctl --json rail set 5v_out on
agent-debugboardctl --json rail get 5v_out
```

Use the rail that actually powers the target, for example `5v_out`, `12v_out`,
or `20v_out`. Do not power-cycle unrelated rails.

Read ADC current monitors:

```sh
agent-debugboardctl --json adc read
agent-debugboardctl --json adc read 5v_out
agent-debugboardctl --json adc read 12v_out
agent-debugboardctl --json adc read 20v_out
```

For manual calibration or hardware debugging, use verbose human-readable ADC
output to inspect raw fields:

```sh
agent-debugboardctl adc read -v 5v_out
```

Do not parse this text in automation. Agents should use `--json adc read ...`
and consume `readings[].ma_est`, `readings[].raw`, and `readings[].mv`.

Switch TF/SD route:

```sh
agent-debugboardctl --json sd get
agent-debugboardctl --json sd route target
agent-debugboardctl --json sd route usb-reader
```

Use allowlisted GPIOs:

```sh
agent-debugboardctl --json gpio list
agent-debugboardctl --json gpio get GP13
agent-debugboardctl --json gpio set GP13 1
agent-debugboardctl --json gpio input GP13
```

Enter RP2040 BOOTSEL mode for flashing:

```sh
agent-debugboardctl bootloader
```

## OpenOCD / JTAG Workflow

Use OpenOCD through the onboard CH347F path when the target board exposes
JTAG/SWD through the debug fixture. CH347F is wired directly to the target debug
connector. The RP2040 firmware controls target power and recovery lines; it does
not sit in the JTAG/SWD path and does not act as a CMSIS-DAP, Picoprobe, or JTAG
probe.

First check OpenOCD availability:

```sh
openocd --version
```

If OpenOCD is missing, install it with the host OS package manager, for example
`brew install open-ocd` on macOS or `sudo apt-get install openocd` on
Ubuntu/Debian.

Power the target first, then start OpenOCD with the CH347F interface script
available in the host OpenOCD installation and the target config for the board
under test:

```sh
agent-debugboardctl --json rail set 5v_out on
agent-debugboardctl --json rail get 5v_out
openocd -f interface/<ch347-interface>.cfg -f target/<target>.cfg
```

CH347F support depends on the OpenOCD build. If the system OpenOCD package does
not include a CH347F interface script, use the WCH/vendor OpenOCD build or add
the matching interface script.

Use the rail that actually powers the target. If the target uses `12v_out` or
`20v_out`, replace `5v_out` accordingly.

When a reset is needed, prefer a target software reboot or OpenOCD reset command
first:

```text
reset halt
reset run
```

Only use rail power-cycling as a hard-restart fallback when soft reset is not
available or the target is unresponsive.

## Safety Rules

- Prefer `--json` for all non-interactive use.
- Do not use `--raw` unless debugging the firmware shell protocol itself.
- Treat `rail set`, `gpio set`, `gpio input`, `sd route`, and `bootloader` as side-effectful operations. Confirm the target and desired state before running them.
- Prefer soft reboot/reset for target-board restarts. Treat rail power-cycling as a hard-restart fallback that is destructive to target runtime state. Confirm the exact rail and only cycle the rail powering the target.
- `5V_FIN` is an input/source rail. Do not present it as a controllable output.
- Only use allowlisted GPIOs reported by `agent-debugboardctl --json gpio list`.
- Do not expose board-internal schematic codenames in user-facing output.

## ADC Calibration Notes

- `5v_out` uses a fixed piecewise-linear calibration table from local load
  measurements. The table covers approximately `0A` through `4.3A`; readings at
  and below the 5V zero point are reported as `0mA`.
- `12v_out` and `20v_out` currently use the ideal INA139 linear model
  (`ma_per_mv=50`) until each rail is measured and calibrated. Treat these
  channels as approximate, not precision current measurements.
- When a controllable rail is off, firmware reports `ma_est=0` for that rail
  while still exposing `raw` and `mv` in JSON/verbose output for diagnostics.
