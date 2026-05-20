/*
 * SPDX-License-Identifier: Apache-2.0
 */

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "debugboard_model.h"

static void assert_str_eq(const char *actual, const char *expected)
{
	assert(strcmp(actual, expected) == 0);
}

static void test_rail_table_matches_schematic(void)
{
	const struct debugboard_rail_desc *rail;

	assert(debugboard_rail_count == 4);

	rail = debugboard_find_rail("12v_out");
	assert(rail != NULL);
	assert(rail->pin == 2);
	assert(rail->controllable);
	assert_str_eq(rail->signal, "GP02_12V_EN");

	rail = debugboard_find_rail("GP09_5V_WS_EN");
	assert(rail != NULL);
	assert_str_eq(rail->name, "5v_ws");
	assert(rail->pin == 9);

	rail = debugboard_find_rail("20v_out");
	assert(rail != NULL);
	assert(rail->pin == 10);
	assert_str_eq(rail->signal, "GP10_20V_EN");

	assert(debugboard_find_rail("5V_FIN") == NULL);
}

static void test_current_table_and_estimate(void)
{
	const struct debugboard_current_desc *current;
	const struct debugboard_current_desc *five_volt;
	struct debugboard_current_desc offset_current;

	assert(debugboard_current_count == 3);

	five_volt = debugboard_find_current("5v_out");
	assert(five_volt != NULL);
	assert_str_eq(five_volt->signal, "S_C_5V");
	assert_str_eq(five_volt->sensor, "INA139");
	assert(five_volt->adc_index == 0);
	assert(five_volt->shunt_uohm == 200);
	assert(five_volt->load_ohm == 100000);
	assert(five_volt->gm_ua_per_v == 1000);
	assert(five_volt->offset_mv == 11);
	assert(five_volt->ma_per_mv == 50);
	assert(five_volt->cal_points != NULL);
	assert(five_volt->cal_point_count == 41);

	current = debugboard_find_current("S_C_12V");
	assert(current != NULL);
	assert_str_eq(current->name, "12v_out");
	assert_str_eq(current->sensor, "INA139");
	assert(current->adc_index == 1);
	assert(current->shunt_uohm == 200);
	assert(current->load_ohm == 100000);
	assert(current->gm_ua_per_v == 1000);
	assert(current->offset_mv == 0);
	assert(current->ma_per_mv == 50);
	assert(current->cal_points == NULL);
	assert(current->cal_point_count == 0);

	for (size_t i = 0; i < debugboard_current_count; i++) {
		assert_str_eq(debugboard_currents[i].sensor, "INA139");
		assert(debugboard_currents[i].shunt_uohm == 200);
		assert(debugboard_currents[i].load_ohm == 100000);
		assert(debugboard_currents[i].gm_ua_per_v == 1000);
	}

	assert(debugboard_estimate_current_ma(0, current) == 0);
	assert(debugboard_estimate_current_ma(1, current) == 50);
	assert(debugboard_estimate_current_ma(20, current) == 1000);
	assert(debugboard_estimate_current_ma(42, current) == 2100);
	assert(debugboard_estimate_current_ma(42, NULL) == 0);

	offset_current = *current;
	offset_current.offset_mv = 50;
	assert(debugboard_estimate_current_ma(42, &offset_current) == 0);

	assert(debugboard_estimate_current_ma(10, five_volt) == 0);
	assert(debugboard_estimate_current_ma(11, five_volt) == 0);
	assert(debugboard_estimate_current_ma(12, five_volt) == 200);
	assert(debugboard_estimate_current_ma(13, five_volt) == 300);
	assert(debugboard_estimate_current_ma(15, five_volt) == 367);
	assert(debugboard_estimate_current_ma(17, five_volt) == 500);
	assert(debugboard_estimate_current_ma(28, five_volt) == 1050);
	assert(debugboard_estimate_current_ma(33, five_volt) == 1250);
	assert(debugboard_estimate_current_ma(34, five_volt) == 1300);
	assert(debugboard_estimate_current_ma(48, five_volt) == 2000);
	assert(debugboard_estimate_current_ma(49, five_volt) == 2100);
	assert(debugboard_estimate_current_ma(50, five_volt) == 2133);
	assert(debugboard_estimate_current_ma(52, five_volt) == 2200);
	assert(debugboard_estimate_current_ma(74, five_volt) == 3300);
	assert(debugboard_estimate_current_ma(89, five_volt) == 4000);
	assert(debugboard_estimate_current_ma(95, five_volt) == 4300);

	assert(debugboard_find_current("5V_FIN") == NULL);
}

static void test_safe_gpio_allowlist(void)
{
	const struct debugboard_safe_gpio_desc *gpio;

	assert(debugboard_safe_gpio_count == 6);

	gpio = debugboard_find_safe_gpio_by_pin(13);
	assert(gpio != NULL);
	assert_str_eq(gpio->name, "GP13");

	assert(debugboard_find_safe_gpio_by_pin(24) != NULL);
	assert(debugboard_find_safe_gpio_by_pin(10) == NULL);
	assert(debugboard_find_safe_gpio_by_pin(26) == NULL);
}

static void test_bool_parser(void)
{
	bool value = false;

	assert(debugboard_parse_bool_arg("on", &value));
	assert(value);

	assert(debugboard_parse_bool_arg("enabled", &value));
	assert(value);

	assert(debugboard_parse_bool_arg("0", &value));
	assert(!value);

	assert(debugboard_parse_bool_arg("disable", &value));
	assert(!value);

	assert(!debugboard_parse_bool_arg("true", &value));
	assert(!debugboard_parse_bool_arg(NULL, &value));
	assert(!debugboard_parse_bool_arg("on", NULL));
}

static void test_gpio_pin_parser(void)
{
	uint8_t pin = 0xff;

	assert(debugboard_parse_gpio_pin("GP13", &pin));
	assert(pin == 13);

	assert(debugboard_parse_gpio_pin("gp24", &pin));
	assert(pin == 24);

	assert(debugboard_parse_gpio_pin("0", &pin));
	assert(pin == 0);

	assert(debugboard_parse_gpio_pin("GP09", &pin));
	assert(pin == 9);

	assert(!debugboard_parse_gpio_pin("", &pin));
	assert(!debugboard_parse_gpio_pin("GP", &pin));
	assert(!debugboard_parse_gpio_pin("GP30", &pin));
	assert(!debugboard_parse_gpio_pin("-1", &pin));
	assert(!debugboard_parse_gpio_pin("GP13x", &pin));
	assert(!debugboard_parse_gpio_pin(NULL, &pin));
	assert(!debugboard_parse_gpio_pin("GP13", NULL));
}

int main(void)
{
	test_rail_table_matches_schematic();
	test_current_table_and_estimate();
	test_safe_gpio_allowlist();
	test_bool_parser();
	test_gpio_pin_parser();

	puts("debugboard_model: all tests passed");
	return 0;
}
