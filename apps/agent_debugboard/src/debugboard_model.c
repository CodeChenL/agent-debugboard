/*
 * SPDX-License-Identifier: Apache-2.0
 */

#include "debugboard_model.h"

#include <ctype.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>

#define ARRAY_SIZE_LOCAL(array) (sizeof(array) / sizeof((array)[0]))

const struct debugboard_rail_desc debugboard_rails[] = {
	{
		.name = "12v_out",
		.signal = "GP02_12V_EN",
		.pin = 2,
		.controllable = true,
	},
	{
		.name = "5v_out",
		.signal = "GP05_5V_EN",
		.pin = 5,
		.controllable = true,
	},
	{
		.name = "5v_ws",
		.signal = "GP09_5V_WS_EN",
		.pin = 9,
		.controllable = true,
	},
	{
		.name = "20v_out",
		.signal = "GP10_20V_EN",
		.pin = 10,
		.controllable = true,
	},
};

const size_t debugboard_rail_count = ARRAY_SIZE_LOCAL(debugboard_rails);

const struct debugboard_current_desc debugboard_currents[] = {
	{
		.name = "5v_out",
		.signal = "S_C_5V",
		.adc_index = 0,
		.ma_per_mv = 50,
	},
	{
		.name = "12v_out",
		.signal = "S_C_12V",
		.adc_index = 1,
		.ma_per_mv = 50,
	},
	{
		.name = "20v_out",
		.signal = "S_C_20V",
		.adc_index = 2,
		.ma_per_mv = 50,
	},
};

const size_t debugboard_current_count = ARRAY_SIZE_LOCAL(debugboard_currents);

const struct debugboard_safe_gpio_desc debugboard_safe_gpios[] = {
	{ .name = "GP13", .pin = 13, .note = "J17 pin 1" },
	{ .name = "GP14", .pin = 14, .note = "J17 pin 3" },
	{ .name = "GP15", .pin = 15, .note = "J17 pin 5" },
	{ .name = "GP22", .pin = 22, .note = "J17 pin 8" },
	{ .name = "GP23", .pin = 23, .note = "J17 pin 10" },
	{ .name = "GP24", .pin = 24, .note = "J17 pin 12" },
};

const size_t debugboard_safe_gpio_count = ARRAY_SIZE_LOCAL(debugboard_safe_gpios);

static bool streq(const char *a, const char *b)
{
	return strcmp(a, b) == 0;
}

bool debugboard_parse_bool_arg(const char *arg, bool *value)
{
	if (arg == NULL || value == NULL) {
		return false;
	}

	if (streq(arg, "1") || streq(arg, "on") || streq(arg, "enable") ||
	    streq(arg, "enabled")) {
		*value = true;
		return true;
	}

	if (streq(arg, "0") || streq(arg, "off") || streq(arg, "disable") ||
	    streq(arg, "disabled")) {
		*value = false;
		return true;
	}

	return false;
}

bool debugboard_parse_gpio_pin(const char *arg, uint8_t *pin)
{
	const char *p;
	char *end = NULL;
	unsigned long parsed;

	if (arg == NULL || pin == NULL) {
		return false;
	}

	p = arg;
	if ((arg[0] == 'G' || arg[0] == 'g') && (arg[1] == 'P' || arg[1] == 'p')) {
		p = &arg[2];
	}

	if (*p == '\0') {
		return false;
	}

	for (const char *s = p; *s != '\0'; s++) {
		if (!isdigit((unsigned char)*s)) {
			return false;
		}
	}

	errno = 0;
	parsed = strtoul(p, &end, 10);
	if (errno != 0 || end == p || *end != '\0' || parsed > 29) {
		return false;
	}

	*pin = (uint8_t)parsed;
	return true;
}

const struct debugboard_rail_desc *debugboard_find_rail(const char *name)
{
	if (name == NULL) {
		return NULL;
	}

	for (size_t i = 0; i < debugboard_rail_count; i++) {
		if (streq(name, debugboard_rails[i].name) ||
		    streq(name, debugboard_rails[i].signal)) {
			return &debugboard_rails[i];
		}
	}

	return NULL;
}

const struct debugboard_current_desc *debugboard_find_current(const char *name)
{
	if (name == NULL) {
		return NULL;
	}

	for (size_t i = 0; i < debugboard_current_count; i++) {
		if (streq(name, debugboard_currents[i].name) ||
		    streq(name, debugboard_currents[i].signal)) {
			return &debugboard_currents[i];
		}
	}

	return NULL;
}

const struct debugboard_safe_gpio_desc *debugboard_find_safe_gpio_by_pin(uint8_t pin)
{
	for (size_t i = 0; i < debugboard_safe_gpio_count; i++) {
		if (debugboard_safe_gpios[i].pin == pin) {
			return &debugboard_safe_gpios[i];
		}
	}

	return NULL;
}

int32_t debugboard_estimate_current_ma(int32_t millivolts, int32_t ma_per_mv)
{
	return millivolts * ma_per_mv;
}
