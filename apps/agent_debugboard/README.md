# agent-debugboard Zephyr App

This directory contains the Zephyr application for `agent-debugboard`. The root
[README.md](../../README.md) contains workspace setup, flashing, and usage
instructions.

Agent/AI operators should read the repository skill first:
[skills/agent-debugboard/SKILL.md](../../skills/agent-debugboard/SKILL.md).

Build:

```sh
west build -p always -b rpi_pico/rp2040 apps/agent_debugboard -d build/agent_debugboard
```

Tests:

```sh
./apps/agent_debugboard/tests/run_unit_tests.sh
```

Schematic:

```text
doc/agent-debugboard-schematic.pdf
```

The USB interface enumerates as a CDC ACM serial device named `Agent DebugBoard`.
Open it with a serial terminal and use the `debugboard` shell command.

Useful commands:

```text
debugboard status
debugboard status --json
debugboard rail list
debugboard rail list --json
debugboard rail set 12v_out on
debugboard rail set 12v_out off
debugboard rail set 5v_out on
debugboard rail set 5v_out off
debugboard rail set 5v_ws on
debugboard rail set 5v_ws off
debugboard rail set 20v_out on
debugboard rail set 20v_out off
debugboard adc read
debugboard adc read --json
debugboard adc read 5v_out
debugboard adc read -v 5v_out
debugboard sd get
debugboard sd get --json
debugboard sd route target
debugboard sd route usb-reader
debugboard gpio list
debugboard gpio list --json
debugboard gpio set GP13 1
debugboard gpio input GP13
debugboard bootloader
```

Build the host CLI:

```sh
go build -o agent-debugboardctl ./cmd/agent-debugboardctl
```

Host helper:

```sh
./agent-debugboardctl status
./agent-debugboardctl doctor
./agent-debugboardctl --json status
./agent-debugboardctl adc read
./agent-debugboardctl adc read -v 5v_out
./agent-debugboardctl rail set 12v_out on
./agent-debugboardctl rail set 20v_out on
./agent-debugboardctl sd route usb-reader
```

OpenOCD:

```sh
./agent-debugboardctl rail set 5v_out on
openocd -f interface/<ch347-interface>.cfg -f target/<target>.cfg
```

JTAG/SWD goes through the onboard CH347F path, which is wired directly to the
target debug connector. The RP2040 firmware does not act as a debug probe.

Rail naming intentionally distinguishes controllable 5V rails from `5V_FIN`.
The firmware does not control `5V_FIN`.

Current schematic mapping:

- `12v_out`: `GP02_12V_EN`
- `5v_out`: `GP05_5V_EN`
- `5v_ws`: `GP09_5V_WS_EN`
- `20v_out`: `GP10_20V_EN`
- TF/SD route switch: `GP06_TF_SW`
- ADC current monitor inputs: `S_C_5V`, `S_C_12V`, `S_C_20V`

All ADC current monitor inputs use an INA139 with a 0.2 mOhm shunt and a
100 kOhm output load. The ideal conversion is 20 mV/A at the ADC input, or
`1 mV = 50 mA`. The `5v_out` channel uses a piecewise-linear table from local
0.1 A step measurements, with `mv <= 11` treated as 0 mA.
