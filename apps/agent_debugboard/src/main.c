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
#define JSON_SCHEMA "agent-debugboard.v1"
#define RESERVED_GPIOS "GP02 GP03 GP04 GP05 GP06 GP09 GP10 GP16-GP21 GP26-GP28"

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

static bool json_requested(size_t argc, char **argv)
{
	return argc > 1 && streq(argv[argc - 1], "--json");
}

static size_t effective_argc(size_t argc, char **argv)
{
	return json_requested(argc, argv) ? argc - 1 : argc;
}

static const char *json_bool(bool value)
{
	return value ? "true" : "false";
}

static const char *sd_route_name(void)
{
	return gpio_pin_get(gpio0, 6) > 0 ? "usb-reader" : "target";
}

static void json_string(const struct shell *sh, const char *value)
{
	shell_fprintf(sh, SHELL_NORMAL, "\"");
	for (const char *p = value; *p != '\0'; p++) {
		unsigned char ch = (unsigned char)*p;

		switch (ch) {
		case '"':
			shell_fprintf(sh, SHELL_NORMAL, "\\\"");
			break;
		case '\\':
			shell_fprintf(sh, SHELL_NORMAL, "\\\\");
			break;
		case '\b':
			shell_fprintf(sh, SHELL_NORMAL, "\\b");
			break;
		case '\f':
			shell_fprintf(sh, SHELL_NORMAL, "\\f");
			break;
		case '\n':
			shell_fprintf(sh, SHELL_NORMAL, "\\n");
			break;
		case '\r':
			shell_fprintf(sh, SHELL_NORMAL, "\\r");
			break;
		case '\t':
			shell_fprintf(sh, SHELL_NORMAL, "\\t");
			break;
		default:
			if (ch < 0x20) {
				shell_fprintf(sh, SHELL_NORMAL, "\\u%04x", ch);
			} else {
				shell_fprintf(sh, SHELL_NORMAL, "%c", ch);
			}
			break;
		}
	}
	shell_fprintf(sh, SHELL_NORMAL, "\"");
}

static void json_begin(const struct shell *sh, const char *command, bool ok)
{
	shell_fprintf(sh, SHELL_NORMAL, "{\"schema\":\"" JSON_SCHEMA
		      "\",\"ok\":%s,\"command\":", json_bool(ok));
	json_string(sh, command);
}

static int json_error(const struct shell *sh, const char *command, const char *code,
		      const char *message, int ret)
{
	json_begin(sh, command, false);
	shell_fprintf(sh, SHELL_NORMAL, ",\"error\":{\"code\":");
	json_string(sh, code);
	shell_fprintf(sh, SHELL_NORMAL, ",\"message\":");
	json_string(sh, message);
	shell_fprintf(sh, SHELL_NORMAL, "}}\n");
	return ret;
}

static void json_print_rail_object(const struct shell *sh,
				   const struct debugboard_rail_desc *rail)
{
	int value = rail->controllable ? gpio_pin_get(gpio0, rail->pin) : 0;

	shell_fprintf(sh, SHELL_NORMAL, "{\"name\":");
	json_string(sh, rail->name);
	shell_fprintf(sh, SHELL_NORMAL, ",\"signal\":");
	json_string(sh, rail->signal);
	shell_fprintf(sh, SHELL_NORMAL, ",\"gp\":%u,\"controllable\":%s",
		      (unsigned int)rail->pin, json_bool(rail->controllable));

	if (!rail->controllable) {
		shell_fprintf(sh, SHELL_NORMAL, ",\"state\":\"locked\",\"value\":null}");
		return;
	}

	if (value < 0) {
		shell_fprintf(sh, SHELL_NORMAL, ",\"state\":\"unknown\",\"value\":null}");
		return;
	}

	shell_fprintf(sh, SHELL_NORMAL, ",\"state\":");
	json_string(sh, value > 0 ? "on" : "off");
	shell_fprintf(sh, SHELL_NORMAL, ",\"value\":%d}", value > 0 ? 1 : 0);
}

static void json_print_rails(const struct shell *sh)
{
	shell_fprintf(sh, SHELL_NORMAL, "[");
	for (size_t i = 0; i < debugboard_rail_count; i++) {
		if (i > 0) {
			shell_fprintf(sh, SHELL_NORMAL, ",");
		}
		json_print_rail_object(sh, &debugboard_rails[i]);
	}
	shell_fprintf(sh, SHELL_NORMAL, "]");
}

static void json_print_power_inputs(const struct shell *sh)
{
	shell_fprintf(sh, SHELL_NORMAL,
		      "[{\"name\":\"5v_fin\",\"controllable\":false,\"measured\":false}]");
}

static void json_print_current_channel(const struct shell *sh,
				       const struct debugboard_current_desc *current)
{
	shell_fprintf(sh, SHELL_NORMAL, "{\"name\":");
	json_string(sh, current->name);
	shell_fprintf(sh, SHELL_NORMAL, ",\"signal\":");
	json_string(sh, current->signal);
	shell_fprintf(sh, SHELL_NORMAL, ",\"adc_index\":%u,\"ma_per_mv\":%" PRId32 "}",
		      (unsigned int)current->adc_index, current->ma_per_mv);
}

static void json_print_adc_channels(const struct shell *sh)
{
	shell_fprintf(sh, SHELL_NORMAL, "[");
	for (size_t i = 0; i < debugboard_current_count; i++) {
		if (i > 0) {
			shell_fprintf(sh, SHELL_NORMAL, ",");
		}
		json_print_current_channel(sh, &debugboard_currents[i]);
	}
	shell_fprintf(sh, SHELL_NORMAL, "]");
}

static void json_print_reading(const struct shell *sh,
			       const struct debugboard_current_desc *current,
			       const struct adc_sample *sample)
{
	shell_fprintf(sh, SHELL_NORMAL, "{\"name\":");
	json_string(sh, current->name);
	shell_fprintf(sh, SHELL_NORMAL, ",\"signal\":");
	json_string(sh, current->signal);
	shell_fprintf(sh, SHELL_NORMAL,
		      ",\"raw\":%" PRId32 ",\"mv\":%" PRId32 ",\"ma_est\":%" PRId32 "}",
		      sample->raw, sample->mv, sample->ma_est);
}

static void json_print_gpio_desc(const struct shell *sh,
				 const struct debugboard_safe_gpio_desc *desc)
{
	shell_fprintf(sh, SHELL_NORMAL, "{\"name\":");
	json_string(sh, desc->name);
	shell_fprintf(sh, SHELL_NORMAL, ",\"pin\":%u,\"note\":",
		      (unsigned int)desc->pin);
	json_string(sh, desc->note);
	shell_fprintf(sh, SHELL_NORMAL, "}");
}

static void json_print_gpio_state(const struct shell *sh,
				  const struct debugboard_safe_gpio_desc *desc,
				  const char *direction, int value)
{
	shell_fprintf(sh, SHELL_NORMAL, "{\"name\":");
	json_string(sh, desc->name);
	shell_fprintf(sh, SHELL_NORMAL, ",\"pin\":%u,\"direction\":",
		      (unsigned int)desc->pin);
	json_string(sh, direction);
	shell_fprintf(sh, SHELL_NORMAL, ",\"value\":");
	if (value >= 0) {
		shell_fprintf(sh, SHELL_NORMAL, "%d", value > 0 ? 1 : 0);
	} else {
		shell_fprintf(sh, SHELL_NORMAL, "null");
	}
	shell_fprintf(sh, SHELL_NORMAL, "}");
}

static void json_print_gpio_allowlist(const struct shell *sh)
{
	shell_fprintf(sh, SHELL_NORMAL, "[");
	for (size_t i = 0; i < debugboard_safe_gpio_count; i++) {
		if (i > 0) {
			shell_fprintf(sh, SHELL_NORMAL, ",");
		}
		json_print_gpio_desc(sh, &debugboard_safe_gpios[i]);
	}
	shell_fprintf(sh, SHELL_NORMAL, "]");
}

static void json_print_status(const struct shell *sh)
{
	json_begin(sh, "status", true);
	shell_fprintf(sh, SHELL_NORMAL,
		      ",\"project\":\"agent-debugboard\",\"mcu\":\"rp2040\","
		      "\"usb\":\"cdc-acm-shell\",\"power_inputs\":");
	json_print_power_inputs(sh);
	shell_fprintf(sh, SHELL_NORMAL, ",\"rails\":");
	json_print_rails(sh);
	shell_fprintf(sh, SHELL_NORMAL, ",\"sd\":{\"route\":");
	json_string(sh, sd_route_name());
	shell_fprintf(sh, SHELL_NORMAL, "},\"adc_channels\":");
	json_print_adc_channels(sh);
	shell_fprintf(sh, SHELL_NORMAL, ",\"gpios\":");
	json_print_gpio_allowlist(sh);
	shell_fprintf(sh, SHELL_NORMAL, "}\n");
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
	if (json_requested(argc, argv)) {
		json_print_status(sh);
		return 0;
	}

	shell_print(sh, "project=agent-debugboard");
	shell_print(sh, "mcu=rp2040");
	shell_print(sh, "usb=cdc-acm-shell");
	shell_print(sh, "5v_fin controllable=no measured=no");
	for (size_t i = 0; i < debugboard_rail_count; i++) {
		print_rail(sh, &debugboard_rails[i]);
	}
	shell_print(sh, "sd route=%s", sd_route_name());
	shell_print(sh, "adc current channels: 5v_out 12v_out 20v_out");
	shell_print(sh, "gpio allowlist: GP13 GP14 GP15 GP22 GP23 GP24");

	return 0;
}

static int cmd_rail(const struct shell *sh, size_t argc, char **argv)
{
	const struct debugboard_rail_desc *rail;
	bool want_json = json_requested(argc, argv);
	size_t eff_argc = effective_argc(argc, argv);
	bool value;
	int ret;

	if (eff_argc < 2) {
		if (want_json) {
			return json_error(sh, "rail", "usage",
					  "usage: debugboard rail list|get|set", -EINVAL);
		}
		shell_help(sh);
		return SHELL_CMD_HELP_PRINTED;
	}

	if (streq(argv[1], "list")) {
		if (want_json) {
			json_begin(sh, "rail", true);
			shell_fprintf(sh, SHELL_NORMAL, ",\"action\":\"list\",\"rails\":");
			json_print_rails(sh);
			shell_fprintf(sh, SHELL_NORMAL, ",\"power_inputs\":");
			json_print_power_inputs(sh);
			shell_fprintf(sh, SHELL_NORMAL, "}\n");
			return 0;
		}

		for (size_t i = 0; i < debugboard_rail_count; i++) {
			print_rail(sh, &debugboard_rails[i]);
		}
		shell_print(sh, "5v_fin controllable=no measured=no");
		return 0;
	}

	if (eff_argc < 3) {
		if (want_json) {
			return json_error(sh, "rail", "usage",
					  "usage: debugboard rail get <rail> | set <rail> <on|off>",
					  -EINVAL);
		}
		shell_error(sh, "usage: debugboard rail get <rail> | set <rail> <on|off>");
		return -EINVAL;
	}

	rail = debugboard_find_rail(argv[2]);
	if (rail == NULL) {
		if (want_json) {
			return json_error(sh, "rail", "unknown_rail", "unknown rail", -ENOENT);
		}
		shell_error(sh, "unknown rail: %s", argv[2]);
		return -ENOENT;
	}

	if (streq(argv[1], "get")) {
		if (want_json) {
			json_begin(sh, "rail", true);
			shell_fprintf(sh, SHELL_NORMAL, ",\"action\":\"get\",\"rail\":");
			json_print_rail_object(sh, rail);
			shell_fprintf(sh, SHELL_NORMAL, "}\n");
			return 0;
		}
		print_rail(sh, rail);
		return 0;
	}

	if (!streq(argv[1], "set")) {
		if (want_json) {
			return json_error(sh, "rail", "unknown_action",
					  "unknown rail action", -EINVAL);
		}
		shell_error(sh, "unknown rail action: %s", argv[1]);
		return -EINVAL;
	}

	if (eff_argc < 4) {
		if (want_json) {
			return json_error(sh, "rail", "missing_state",
					  "missing rail state", -EINVAL);
		}
		shell_error(sh, "missing rail state");
		return -EINVAL;
	}

	if (!rail->controllable) {
		if (want_json) {
			return json_error(sh, "rail", "rail_locked",
					  "rail is locked in this build", -EPERM);
		}
		shell_error(sh, "%s is locked in this build", rail->name);
		return -EPERM;
	}

	if (!debugboard_parse_bool_arg(argv[3], &value)) {
		if (want_json) {
			return json_error(sh, "rail", "invalid_state",
					  "state must be on/off or 1/0", -EINVAL);
		}
		shell_error(sh, "state must be on/off or 1/0");
		return -EINVAL;
	}

	ret = gpio_pin_set(gpio0, rail->pin, value ? 1 : 0);
	if (ret < 0) {
		if (want_json) {
			return json_error(sh, "rail", "set_failed",
					  "failed to set rail", ret);
		}
		shell_error(sh, "failed to set %s: %d", rail->name, ret);
		return ret;
	}

	if (want_json) {
		json_begin(sh, "rail", true);
		shell_fprintf(sh, SHELL_NORMAL, ",\"action\":\"set\",\"rail\":");
		json_print_rail_object(sh, rail);
		shell_fprintf(sh, SHELL_NORMAL, "}\n");
		return 0;
	}

	shell_print(sh, "%s=%s", rail->name, value ? "on" : "off");
	return 0;
}

static int cmd_adc(const struct shell *sh, size_t argc, char **argv)
{
	const struct debugboard_current_desc *current;
	struct adc_sample sample;
	struct adc_sample samples[ARRAY_SIZE(adc_channels)];
	bool want_json = json_requested(argc, argv);
	size_t eff_argc = effective_argc(argc, argv);
	int ret;

	if (eff_argc < 2 || streq(argv[1], "read")) {
		if (eff_argc >= 3) {
			current = debugboard_find_current(argv[2]);
			if (current == NULL) {
				if (want_json) {
					return json_error(sh, "adc", "unknown_channel",
							  "unknown adc channel", -ENOENT);
				}
				shell_error(sh, "unknown adc channel: %s", argv[2]);
				return -ENOENT;
			}

			ret = read_current(current, &sample);
			if (ret < 0) {
				if (want_json) {
					return json_error(sh, "adc", "read_failed",
							  "failed to read adc channel", ret);
				}
				shell_error(sh, "failed to read %s: %d", current->name, ret);
				return ret;
			}

			if (want_json) {
				json_begin(sh, "adc", true);
				shell_fprintf(sh, SHELL_NORMAL,
					      ",\"action\":\"read\",\"readings\":[");
				json_print_reading(sh, current, &sample);
				shell_fprintf(sh, SHELL_NORMAL, "]}\n");
				return 0;
			}

			shell_print(sh, "%s signal=%s raw=%" PRId32 " mv=%" PRId32
				    " ma_est=%" PRId32,
				    current->name, current->signal, sample.raw, sample.mv,
				    sample.ma_est);
			return 0;
		}

		for (size_t i = 0; i < debugboard_current_count; i++) {
			ret = read_current(&debugboard_currents[i], &samples[i]);
			if (ret < 0) {
				if (want_json) {
					return json_error(sh, "adc", "read_failed",
							  "failed to read adc channel", ret);
				}
				shell_error(sh, "failed to read %s: %d",
					    debugboard_currents[i].name, ret);
				return ret;
			}
		}

		if (want_json) {
			json_begin(sh, "adc", true);
			shell_fprintf(sh, SHELL_NORMAL, ",\"action\":\"read\",\"readings\":[");
			for (size_t i = 0; i < debugboard_current_count; i++) {
				if (i > 0) {
					shell_fprintf(sh, SHELL_NORMAL, ",");
				}
				json_print_reading(sh, &debugboard_currents[i], &samples[i]);
			}
			shell_fprintf(sh, SHELL_NORMAL, "]}\n");
			return 0;
		}

		for (size_t i = 0; i < debugboard_current_count; i++) {
			shell_print(sh, "%s signal=%s raw=%" PRId32 " mv=%" PRId32
				    " ma_est=%" PRId32,
				    debugboard_currents[i].name, debugboard_currents[i].signal,
				    samples[i].raw, samples[i].mv, samples[i].ma_est);
		}

		return 0;
	}

	if (want_json) {
		return json_error(sh, "adc", "usage",
				  "usage: debugboard adc read [5v_out|12v_out|20v_out]",
				  -EINVAL);
	}
	shell_error(sh, "usage: debugboard adc read [5v_out|12v_out|20v_out]");
	return -EINVAL;
}

static int cmd_sd(const struct shell *sh, size_t argc, char **argv)
{
	bool want_json = json_requested(argc, argv);
	size_t eff_argc = effective_argc(argc, argv);
	enum sd_route route;
	int ret;

	if (eff_argc < 2 || streq(argv[1], "get")) {
		if (want_json) {
			json_begin(sh, "sd", true);
			shell_fprintf(sh, SHELL_NORMAL, ",\"action\":\"get\",\"route\":");
			json_string(sh, sd_route_name());
			shell_fprintf(sh, SHELL_NORMAL, "}\n");
			return 0;
		}
		shell_print(sh, "route=%s", sd_route_name());
		return 0;
	}

	if (!streq(argv[1], "route") || eff_argc < 3) {
		if (want_json) {
			return json_error(sh, "sd", "usage",
					  "usage: debugboard sd get | route <target|usb-reader>",
					  -EINVAL);
		}
		shell_error(sh, "usage: debugboard sd get | route <target|usb-reader>");
		return -EINVAL;
	}

	if (streq(argv[2], "target")) {
		route = SD_ROUTE_TARGET;
	} else if (streq(argv[2], "usb-reader") || streq(argv[2], "reader")) {
		route = SD_ROUTE_USB_READER;
	} else {
		if (want_json) {
			return json_error(sh, "sd", "invalid_route",
					  "route must be target or usb-reader", -EINVAL);
		}
		shell_error(sh, "route must be target or usb-reader");
		return -EINVAL;
	}

	ret = gpio_pin_set(gpio0, 6, route == SD_ROUTE_USB_READER ? 1 : 0);
	if (ret < 0) {
		if (want_json) {
			return json_error(sh, "sd", "set_failed",
					  "failed to set SD route", ret);
		}
		shell_error(sh, "failed to set SD route: %d", ret);
		return ret;
	}

	k_msleep(10);
	if (want_json) {
		json_begin(sh, "sd", true);
		shell_fprintf(sh, SHELL_NORMAL, ",\"action\":\"route\",\"route\":");
		json_string(sh, route == SD_ROUTE_USB_READER ? "usb-reader" : "target");
		shell_fprintf(sh, SHELL_NORMAL, "}\n");
		return 0;
	}

	shell_print(sh, "route=%s", route == SD_ROUTE_USB_READER ? "usb-reader" : "target");
	return 0;
}

static int cmd_gpio(const struct shell *sh, size_t argc, char **argv)
{
	const struct debugboard_safe_gpio_desc *desc;
	bool want_json = json_requested(argc, argv);
	size_t eff_argc = effective_argc(argc, argv);
	gpio_pin_t pin;
	uint8_t parsed_pin;
	bool value;
	int ret;

	if (eff_argc < 2 || streq(argv[1], "list")) {
		if (want_json) {
			json_begin(sh, "gpio", true);
			shell_fprintf(sh, SHELL_NORMAL, ",\"action\":\"list\",\"gpios\":");
			json_print_gpio_allowlist(sh);
			shell_fprintf(sh, SHELL_NORMAL, ",\"reserved\":");
			json_string(sh, RESERVED_GPIOS);
			shell_fprintf(sh, SHELL_NORMAL, "}\n");
			return 0;
		}

		for (size_t i = 0; i < debugboard_safe_gpio_count; i++) {
			shell_print(sh, "%s %s",
				    debugboard_safe_gpios[i].name,
				    debugboard_safe_gpios[i].note);
		}
		shell_print(sh, "reserved: " RESERVED_GPIOS);
		return 0;
	}

	if (eff_argc < 3 || !debugboard_parse_gpio_pin(argv[2], &parsed_pin)) {
		if (want_json) {
			return json_error(sh, "gpio", "usage",
					  "usage: debugboard gpio get|set|input <GPxx> [0|1]",
					  -EINVAL);
		}
		shell_error(sh, "usage: debugboard gpio get|set|input <GPxx> [0|1]");
		return -EINVAL;
	}
	pin = (gpio_pin_t)parsed_pin;

	desc = debugboard_find_safe_gpio_by_pin(parsed_pin);
	if (desc == NULL) {
		if (want_json) {
			return json_error(sh, "gpio", "not_allowed",
					  "GPIO is not in the allowlist", -EPERM);
		}
		shell_error(sh, "GP%u is not in the GPIO allowlist", pin);
		return -EPERM;
	}

	if (streq(argv[1], "get")) {
		ret = gpio_pin_get(gpio0, pin);
		if (ret < 0) {
			if (want_json) {
				return json_error(sh, "gpio", "read_failed",
						  "failed to read GPIO", ret);
			}
			shell_error(sh, "failed to read GP%u: %d", pin, ret);
			return ret;
		}

		if (want_json) {
			json_begin(sh, "gpio", true);
			shell_fprintf(sh, SHELL_NORMAL, ",\"action\":\"get\",\"gpio\":");
			json_print_gpio_state(sh, desc, "unknown", ret > 0 ? 1 : 0);
			shell_fprintf(sh, SHELL_NORMAL, "}\n");
			return 0;
		}

		shell_print(sh, "GP%u=%d", pin, ret > 0 ? 1 : 0);
		return 0;
	}

	if (streq(argv[1], "input")) {
		ret = gpio_pin_configure(gpio0, pin, GPIO_INPUT);
		if (ret < 0) {
			if (want_json) {
				return json_error(sh, "gpio", "configure_failed",
						  "failed to configure GPIO input", ret);
			}
			shell_error(sh, "failed to configure GP%u input: %d", pin, ret);
			return ret;
		}

		if (want_json) {
			json_begin(sh, "gpio", true);
			shell_fprintf(sh, SHELL_NORMAL, ",\"action\":\"input\",\"gpio\":");
			json_print_gpio_state(sh, desc, "input", -1);
			shell_fprintf(sh, SHELL_NORMAL, "}\n");
			return 0;
		}

		shell_print(sh, "GP%u=input", pin);
		return 0;
	}

	if (!streq(argv[1], "set") || eff_argc < 4) {
		if (want_json) {
			return json_error(sh, "gpio", "usage",
					  "usage: debugboard gpio set <GPxx> <0|1>",
					  -EINVAL);
		}
		shell_error(sh, "usage: debugboard gpio set <GPxx> <0|1>");
		return -EINVAL;
	}

	if (!debugboard_parse_bool_arg(argv[3], &value)) {
		if (want_json) {
			return json_error(sh, "gpio", "invalid_value",
					  "GPIO value must be 0/1 or on/off", -EINVAL);
		}
		shell_error(sh, "GPIO value must be 0/1 or on/off");
		return -EINVAL;
	}

	ret = gpio_pin_configure(gpio0, pin, value ? GPIO_OUTPUT_ACTIVE : GPIO_OUTPUT_INACTIVE);
	if (ret < 0) {
		if (want_json) {
			return json_error(sh, "gpio", "configure_failed",
					  "failed to configure GPIO output", ret);
		}
		shell_error(sh, "failed to configure GP%u output: %d", pin, ret);
		return ret;
	}

	if (want_json) {
		json_begin(sh, "gpio", true);
		shell_fprintf(sh, SHELL_NORMAL, ",\"action\":\"set\",\"gpio\":");
		json_print_gpio_state(sh, desc, "output", value ? 1 : 0);
		shell_fprintf(sh, SHELL_NORMAL, "}\n");
		return 0;
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
	SHELL_CMD_ARG(status, NULL, "Show board-controller state.", cmd_status, 1, 1),
	SHELL_CMD_ARG(rail, NULL,
		      "Rail control: list | get <rail> | set <rail> <on|off>.",
		      cmd_rail, 2, 3),
	SHELL_CMD_ARG(adc, NULL, "ADC current read: read [5v_out|12v_out|20v_out].",
		      cmd_adc, 1, 3),
	SHELL_CMD_ARG(sd, NULL, "TF/SD route: get | route <target|usb-reader>.",
		      cmd_sd, 1, 3),
	SHELL_CMD_ARG(gpio, NULL, "Safe GPIO: list | get|set|input <GPxx> [0|1].",
		      cmd_gpio, 1, 4),
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
