/*
 * SPDX-License-Identifier: Apache-2.0
 */

#include "debugboard_model.h"

#include <ctype.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>

#define ARRAY_SIZE_LOCAL(array) (sizeof(array) / sizeof((array)[0]))
#define INA139_SENSOR "INA139"
#define INA139_SHUNT_UOHM 200U
#define INA139_LOAD_OHM 100000U
#define INA139_GM_UA_PER_V 1000U
#define INA139_OFFSET_MV 0
#define INA139_MA_PER_MV 50
#define INA139_5V_OUT_OFFSET_MV 11
#define INA139_5V_OUT_MA_PER_MV 50

static const struct debugboard_current_cal_point five_volt_cal_points[] = {
	{ .mv = 11, .ma = 0 },
	{ .mv = 12, .ma = 200 },
	{ .mv = 13, .ma = 300 },
	{ .mv = 16, .ma = 400 },
	{ .mv = 17, .ma = 500 },
	{ .mv = 20, .ma = 600 },
	{ .mv = 21, .ma = 700 },
	{ .mv = 24, .ma = 800 },
	{ .mv = 26, .ma = 900 },
	{ .mv = 27, .ma = 1000 },
	{ .mv = 29, .ma = 1100 },
	{ .mv = 33, .ma = 1250 },
	{ .mv = 36, .ma = 1400 },
	{ .mv = 38, .ma = 1500 },
	{ .mv = 40, .ma = 1600 },
	{ .mv = 41, .ma = 1700 },
	{ .mv = 43, .ma = 1800 },
	{ .mv = 46, .ma = 1900 },
	{ .mv = 48, .ma = 2000 },
	{ .mv = 49, .ma = 2100 },
	{ .mv = 52, .ma = 2200 },
	{ .mv = 53, .ma = 2300 },
	{ .mv = 56, .ma = 2400 },
	{ .mv = 58, .ma = 2500 },
	{ .mv = 61, .ma = 2600 },
	{ .mv = 62, .ma = 2700 },
	{ .mv = 65, .ma = 2800 },
	{ .mv = 66, .ma = 2900 },
	{ .mv = 68, .ma = 3000 },
	{ .mv = 70, .ma = 3100 },
	{ .mv = 74, .ma = 3300 },
	{ .mv = 77, .ma = 3400 },
	{ .mv = 78, .ma = 3500 },
	{ .mv = 81, .ma = 3600 },
	{ .mv = 82, .ma = 3700 },
	{ .mv = 86, .ma = 3800 },
	{ .mv = 87, .ma = 3900 },
	{ .mv = 89, .ma = 4000 },
	{ .mv = 91, .ma = 4100 },
	{ .mv = 92, .ma = 4200 },
	{ .mv = 95, .ma = 4300 },
};

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
		.sensor = INA139_SENSOR,
		.cal_points = five_volt_cal_points,
		.cal_point_count = ARRAY_SIZE_LOCAL(five_volt_cal_points),
		.adc_index = 0,
		.shunt_uohm = INA139_SHUNT_UOHM,
		.load_ohm = INA139_LOAD_OHM,
		.gm_ua_per_v = INA139_GM_UA_PER_V,
		.offset_mv = INA139_5V_OUT_OFFSET_MV,
		.ma_per_mv = INA139_5V_OUT_MA_PER_MV,
	},
	{
		.name = "12v_out",
		.signal = "S_C_12V",
		.sensor = INA139_SENSOR,
		.cal_points = NULL,
		.cal_point_count = 0,
		.adc_index = 1,
		.shunt_uohm = INA139_SHUNT_UOHM,
		.load_ohm = INA139_LOAD_OHM,
		.gm_ua_per_v = INA139_GM_UA_PER_V,
		.offset_mv = INA139_OFFSET_MV,
		.ma_per_mv = INA139_MA_PER_MV,
	},
	{
		.name = "20v_out",
		.signal = "S_C_20V",
		.sensor = INA139_SENSOR,
		.cal_points = NULL,
		.cal_point_count = 0,
		.adc_index = 2,
		.shunt_uohm = INA139_SHUNT_UOHM,
		.load_ohm = INA139_LOAD_OHM,
		.gm_ua_per_v = INA139_GM_UA_PER_V,
		.offset_mv = INA139_OFFSET_MV,
		.ma_per_mv = INA139_MA_PER_MV,
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

int32_t debugboard_estimate_current_ma(int32_t millivolts,
				       const struct debugboard_current_desc *current)
{
	int32_t adjusted_mv;
	const struct debugboard_current_cal_point *points;
	size_t count;

	if (current == NULL) {
		return 0;
	}

	points = current->cal_points;
	count = current->cal_point_count;
	if (points != NULL && count > 0) {
		if (millivolts <= points[0].mv) {
			return points[0].ma;
		}

		for (size_t i = 1; i < count; i++) {
			int32_t prev_mv = points[i - 1].mv;
			int32_t next_mv = points[i].mv;
			int32_t prev_ma = points[i - 1].ma;
			int32_t next_ma = points[i].ma;
			int64_t num;
			int32_t den;

			if (millivolts > next_mv) {
				continue;
			}

			den = next_mv - prev_mv;
			if (den <= 0) {
				return next_ma;
			}

			num = (int64_t)(millivolts - prev_mv) * (next_ma - prev_ma);
			return prev_ma + (int32_t)((num + (den / 2)) / den);
		}

		if (count >= 2) {
			int32_t prev_mv = points[count - 2].mv;
			int32_t next_mv = points[count - 1].mv;
			int32_t prev_ma = points[count - 2].ma;
			int32_t next_ma = points[count - 1].ma;
			int32_t den = next_mv - prev_mv;
			int64_t num;

			if (den <= 0) {
				return next_ma;
			}

			num = (int64_t)(millivolts - next_mv) * (next_ma - prev_ma);
			return next_ma + (int32_t)((num + (den / 2)) / den);
		}

		return points[count - 1].ma;
	}

	adjusted_mv = millivolts - current->offset_mv;
	if (adjusted_mv <= 0) {
		return 0;
	}

	return adjusted_mv * current->ma_per_mv;
}
