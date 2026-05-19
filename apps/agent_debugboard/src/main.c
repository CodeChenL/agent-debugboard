/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * agent-debugboard RP2040 controller firmware.
 */

#include "debugboard_model.h"

#include <inttypes.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/adc.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/shell/shell.h>
#include <zephyr/sys/util.h>

#if defined(CONFIG_SOC_SERIES_RP2040) || defined(CONFIG_SOC_SERIES_RP2350)
#include <pico/bootrom.h>
#endif

LOG_MODULE_REGISTER(agent_debugboard, LOG_LEVEL_INF);

#define GPIO0_NODE DT_NODELABEL(gpio0)
#define ADC_INPUTS_NODE DT_PATH(zephyr_user)

#if !DT_NODE_EXISTS(ADC_INPUTS_NODE) || !DT_NODE_HAS_PROP(ADC_INPUTS_NODE, io_channels)
#error "agent-debugboard firmware requires zephyr,user io-channels for current ADCs"
#endif

#define ADC_SPEC_AND_COMMA(node_id, prop, idx) \
	COND_CODE_1(DT_PHA_HAS_CELL_AT_IDX(node_id, prop, idx, input), \
		    (ADC_DT_SPEC_GET_BY_IDX(node_id, idx),), ())

enum sd_route {
	SD_ROUTE_TARGET = 0,
	SD_ROUTE_USB_READER = 1,
};

struct adc_sample {
	int32_t raw;
	int32_t mv;
	int32_t ma_est;
};

static const struct device *const gpio0 = DEVICE_DT_GET(GPIO0_NODE);

static const struct adc_dt_spec adc_channels[] = {
	DT_FOREACH_PROP_ELEM(ADC_INPUTS_NODE, io_channels, ADC_SPEC_AND_COMMA)
};

static bool streq(const char *a, const char *b)
{
	return strcmp(a, b) == 0;
}

static int configure_rail_defaults(void)
{
	int ret;

	for (size_t i = 0; i < debugboard_rail_count; i++) {
		if (!debugboard_rails[i].controllable) {
			continue;
		}

		ret = gpio_pin_configure(gpio0, debugboard_rails[i].pin, GPIO_OUTPUT_INACTIVE);
		if (ret < 0) {
			return ret;
		}
	}

	return 0;
}

static int configure_sd_default(void)
{
	return gpio_pin_configure(gpio0, 6, GPIO_OUTPUT_INACTIVE);
}

static int setup_adc_channels(void)
{
	int ret;

	for (size_t i = 0; i < ARRAY_SIZE(adc_channels); i++) {
		if (!adc_is_ready_dt(&adc_channels[i])) {
			return -ENODEV;
		}

		ret = adc_channel_setup_dt(&adc_channels[i]);
		if (ret < 0) {
			return ret;
		}
	}

	return 0;
}

static int read_current(const struct debugboard_current_desc *current, struct adc_sample *sample)
{
	uint32_t buf = 0;
	int32_t mv;
	int ret;
	struct adc_sequence sequence = {
		.buffer = &buf,
		.buffer_size = sizeof(buf),
	};
	const struct adc_dt_spec *spec;

	if (current->adc_index >= ARRAY_SIZE(adc_channels)) {
		return -EINVAL;
	}

	spec = &adc_channels[current->adc_index];
	ret = adc_sequence_init_dt(spec, &sequence);
	if (ret < 0) {
		return ret;
	}

	ret = adc_read_dt(spec, &sequence);
	if (ret < 0) {
		return ret;
	}

	sample->raw = (int32_t)buf;
	mv = sample->raw;
	ret = adc_raw_to_millivolts_dt(spec, &mv);
	if (ret < 0) {
		uint8_t resolution = spec->resolution != 0U ? spec->resolution : 12U;

		mv = (sample->raw * 3300) / ((1 << resolution) - 1);
	}

	sample->mv = mv;
	sample->ma_est = debugboard_estimate_current_ma(mv, current->ma_per_mv);

	return 0;
}

static void print_rail(const struct shell *sh, const struct debugboard_rail_desc *rail)
{
	int value = rail->controllable ? gpio_pin_get(gpio0, rail->pin) : 0;

	shell_print(sh, "%s signal=%s gp=%u controllable=%s state=%s",
		    rail->name, rail->signal, (unsigned int)rail->pin,
		    rail->controllable ? "yes" : "no",
		    rail->controllable ? (value > 0 ? "on" : "off") : "locked");
}

static int cmd_status(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	shell_print(sh, "project=agent-debugboard");
	shell_print(sh, "mcu=rp2040");
	shell_print(sh, "usb=cdc-acm-shell");
	shell_print(sh, "5v_fin controllable=no measured=no");
	for (size_t i = 0; i < debugboard_rail_count; i++) {
		print_rail(sh, &debugboard_rails[i]);
	}
	shell_print(sh, "sd route=%s", gpio_pin_get(gpio0, 6) > 0 ? "usb-reader" : "target");
	shell_print(sh, "adc current channels: 5v_out 12v_out 20v_out");
	shell_print(sh, "gpio allowlist: GP13 GP14 GP15 GP22 GP23 GP24");

	return 0;
}

static int cmd_rail(const struct shell *sh, size_t argc, char **argv)
{
	const struct debugboard_rail_desc *rail;
	bool value;
	int ret;

	if (argc < 2) {
		shell_help(sh);
		return SHELL_CMD_HELP_PRINTED;
	}

	if (streq(argv[1], "list")) {
		for (size_t i = 0; i < debugboard_rail_count; i++) {
			print_rail(sh, &debugboard_rails[i]);
		}
		shell_print(sh, "5v_fin controllable=no measured=no");
		return 0;
	}

	if (argc < 3) {
		shell_error(sh, "usage: debugboard rail get <rail> | set <rail> <on|off>");
		return -EINVAL;
	}

	rail = debugboard_find_rail(argv[2]);
	if (rail == NULL) {
		shell_error(sh, "unknown rail: %s", argv[2]);
		return -ENOENT;
	}

	if (streq(argv[1], "get")) {
		print_rail(sh, rail);
		return 0;
	}

	if (!streq(argv[1], "set")) {
		shell_error(sh, "unknown rail action: %s", argv[1]);
		return -EINVAL;
	}

	if (argc < 4) {
		shell_error(sh, "missing rail state");
		return -EINVAL;
	}

	if (!rail->controllable) {
		shell_error(sh, "%s is locked in this build", rail->name);
		return -EPERM;
	}

	if (!debugboard_parse_bool_arg(argv[3], &value)) {
		shell_error(sh, "state must be on/off or 1/0");
		return -EINVAL;
	}

	ret = gpio_pin_set(gpio0, rail->pin, value ? 1 : 0);
	if (ret < 0) {
		shell_error(sh, "failed to set %s: %d", rail->name, ret);
		return ret;
	}

	shell_print(sh, "%s=%s", rail->name, value ? "on" : "off");
	return 0;
}

static int cmd_adc(const struct shell *sh, size_t argc, char **argv)
{
	const struct debugboard_current_desc *current;
	struct adc_sample sample;
	int ret;

	if (argc < 2 || streq(argv[1], "read")) {
		if (argc >= 3) {
			current = debugboard_find_current(argv[2]);
			if (current == NULL) {
				shell_error(sh, "unknown adc channel: %s", argv[2]);
				return -ENOENT;
			}

			ret = read_current(current, &sample);
			if (ret < 0) {
				shell_error(sh, "failed to read %s: %d", current->name, ret);
				return ret;
			}

			shell_print(sh, "%s signal=%s raw=%" PRId32 " mv=%" PRId32
				    " ma_est=%" PRId32,
				    current->name, current->signal, sample.raw, sample.mv,
				    sample.ma_est);
			return 0;
		}

		for (size_t i = 0; i < debugboard_current_count; i++) {
			ret = read_current(&debugboard_currents[i], &sample);
			if (ret < 0) {
				shell_error(sh, "failed to read %s: %d",
					    debugboard_currents[i].name, ret);
				return ret;
			}

			shell_print(sh, "%s signal=%s raw=%" PRId32 " mv=%" PRId32
				    " ma_est=%" PRId32,
				    debugboard_currents[i].name, debugboard_currents[i].signal,
				    sample.raw, sample.mv, sample.ma_est);
		}

		return 0;
	}

	shell_error(sh, "usage: debugboard adc read [5v_out|12v_out|20v_out]");
	return -EINVAL;
}

static int cmd_sd(const struct shell *sh, size_t argc, char **argv)
{
	enum sd_route route;
	int ret;

	if (argc < 2 || streq(argv[1], "get")) {
		shell_print(sh, "route=%s", gpio_pin_get(gpio0, 6) > 0 ? "usb-reader" : "target");
		return 0;
	}

	if (!streq(argv[1], "route") || argc < 3) {
		shell_error(sh, "usage: debugboard sd get | route <target|usb-reader>");
		return -EINVAL;
	}

	if (streq(argv[2], "target")) {
		route = SD_ROUTE_TARGET;
	} else if (streq(argv[2], "usb-reader") || streq(argv[2], "reader")) {
		route = SD_ROUTE_USB_READER;
	} else {
		shell_error(sh, "route must be target or usb-reader");
		return -EINVAL;
	}

	ret = gpio_pin_set(gpio0, 6, route == SD_ROUTE_USB_READER ? 1 : 0);
	if (ret < 0) {
		shell_error(sh, "failed to set SD route: %d", ret);
		return ret;
	}

	k_msleep(10);
	shell_print(sh, "route=%s", route == SD_ROUTE_USB_READER ? "usb-reader" : "target");
	return 0;
}

static int cmd_gpio(const struct shell *sh, size_t argc, char **argv)
{
	const struct debugboard_safe_gpio_desc *desc;
	gpio_pin_t pin;
	uint8_t parsed_pin;
	bool value;
	int ret;

	if (argc < 2 || streq(argv[1], "list")) {
		for (size_t i = 0; i < debugboard_safe_gpio_count; i++) {
			shell_print(sh, "%s %s",
				    debugboard_safe_gpios[i].name,
				    debugboard_safe_gpios[i].note);
		}
		shell_print(sh, "reserved: GP02 GP03 GP04 GP05 GP06 GP09 GP10 GP16-GP21 GP26-GP28");
		return 0;
	}

	if (argc < 3 || !debugboard_parse_gpio_pin(argv[2], &parsed_pin)) {
		shell_error(sh, "usage: debugboard gpio get|set|input <GPxx> [0|1]");
		return -EINVAL;
	}
	pin = (gpio_pin_t)parsed_pin;

	desc = debugboard_find_safe_gpio_by_pin(parsed_pin);
	if (desc == NULL) {
		shell_error(sh, "GP%u is not in the GPIO allowlist", pin);
		return -EPERM;
	}

	if (streq(argv[1], "get")) {
		ret = gpio_pin_get(gpio0, pin);
		if (ret < 0) {
			shell_error(sh, "failed to read GP%u: %d", pin, ret);
			return ret;
		}
		shell_print(sh, "GP%u=%d", pin, ret > 0 ? 1 : 0);
		return 0;
	}

	if (streq(argv[1], "input")) {
		ret = gpio_pin_configure(gpio0, pin, GPIO_INPUT);
		if (ret < 0) {
			shell_error(sh, "failed to configure GP%u input: %d", pin, ret);
			return ret;
		}
		shell_print(sh, "GP%u=input", pin);
		return 0;
	}

	if (!streq(argv[1], "set") || argc < 4) {
		shell_error(sh, "usage: debugboard gpio set <GPxx> <0|1>");
		return -EINVAL;
	}

	if (!debugboard_parse_bool_arg(argv[3], &value)) {
		shell_error(sh, "GPIO value must be 0/1 or on/off");
		return -EINVAL;
	}

	ret = gpio_pin_configure(gpio0, pin, value ? GPIO_OUTPUT_ACTIVE : GPIO_OUTPUT_INACTIVE);
	if (ret < 0) {
		shell_error(sh, "failed to configure GP%u output: %d", pin, ret);
		return ret;
	}

	shell_print(sh, "GP%u=%d", pin, value ? 1 : 0);
	return 0;
}

static int cmd_bootloader(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

#if defined(CONFIG_SOC_SERIES_RP2040) || defined(CONFIG_SOC_SERIES_RP2350)
	shell_print(sh, "entering RP2040 BOOTSEL in 250 ms");
	k_msleep(250);
	reset_usb_boot(0, 0);
#else
	shell_error(sh, "BOOTSEL is only implemented for RP2xxx SoCs");
	return -ENOTSUP;
#endif
}

SHELL_STATIC_SUBCMD_SET_CREATE(debugboard_subcmds,
	SHELL_CMD_ARG(status, NULL, "Show board-controller state.", cmd_status, 1, 0),
	SHELL_CMD_ARG(rail, NULL,
		      "Rail control: list | get <rail> | set <rail> <on|off>.",
		      cmd_rail, 2, 2),
	SHELL_CMD_ARG(adc, NULL, "ADC current read: read [5v_out|12v_out|20v_out].",
		      cmd_adc, 1, 2),
	SHELL_CMD_ARG(sd, NULL, "TF/SD route: get | route <target|usb-reader>.",
		      cmd_sd, 1, 2),
	SHELL_CMD_ARG(gpio, NULL, "Safe GPIO: list | get|set|input <GPxx> [0|1].",
		      cmd_gpio, 1, 3),
	SHELL_CMD_ARG(bootloader, NULL, "Enter RP2040 USB BOOTSEL mode.", cmd_bootloader, 1, 0),
	SHELL_SUBCMD_SET_END
);

SHELL_CMD_REGISTER(debugboard, &debugboard_subcmds,
		   "agent-debugboard controller commands.", NULL);

int main(void)
{
	int ret;

	if (!device_is_ready(gpio0)) {
		LOG_ERR("GPIO0 is not ready");
		return 0;
	}

	ret = configure_rail_defaults();
	if (ret < 0) {
		LOG_ERR("Rail default setup failed: %d", ret);
		return 0;
	}

	ret = configure_sd_default();
	if (ret < 0) {
		LOG_ERR("SD route default setup failed: %d", ret);
		return 0;
	}

	ret = setup_adc_channels();
	if (ret < 0) {
		LOG_ERR("ADC setup failed: %d", ret);
		return 0;
	}

	LOG_INF("agent-debugboard controller ready");

	while (true) {
		k_sleep(K_SECONDS(60));
	}
}
