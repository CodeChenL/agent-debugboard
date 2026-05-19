# agent-debugboard Zephyr App

This directory contains the Zephyr application for `agent-debugboard`. The root
[README.md](../../README.md) contains workspace setup, flashing, and usage
instructions.

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
debugboard rail list
debugboard rail set 12v_out on
debugboard rail set 12v_out off
debugboard rail set 5v_out on
debugboard rail set 5v_out off
debugboard rail set 5v_ws on
debugboard rail set 5v_ws off
debugboard rail set 20v_out on
debugboard rail set 20v_out off
debugboard adc read
debugboard adc read 5v_out
debugboard sd get
debugboard sd route target
debugboard sd route usb-reader
debugboard gpio list
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
./agent-debugboardctl adc read
./agent-debugboardctl rail set 12v_out on
./agent-debugboardctl rail set 20v_out on
./agent-debugboardctl sd route usb-reader
```

Rail naming intentionally distinguishes controllable 5V rails from `5V_FIN`.
The firmware does not control `5V_FIN`.

Current schematic mapping:

- `12v_out`: `GP02_12V_EN`
- `5v_out`: `GP05_5V_EN`
- `5v_ws`: `GP09_5V_WS_EN`
- `20v_out`: `GP10_20V_EN`
- TF/SD route switch: `GP06_TF_SW`
- ADC current monitor inputs: `S_C_5V`, `S_C_12V`, `S_C_20V`
