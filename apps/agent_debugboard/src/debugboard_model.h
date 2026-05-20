/*
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef AGENT_DEBUGBOARD_MODEL_H_
#define AGENT_DEBUGBOARD_MODEL_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

struct debugboard_rail_desc {
	const char *name;
	const char *signal;
	uint8_t pin;
	bool controllable;
};

struct debugboard_current_cal_point {
	int32_t mv;
	int32_t ma;
};

struct debugboard_current_desc {
	const char *name;
	const char *signal;
	const char *sensor;
	const struct debugboard_current_cal_point *cal_points;
	size_t cal_point_count;
	size_t adc_index;
	uint32_t shunt_uohm;
	uint32_t load_ohm;
	uint32_t gm_ua_per_v;
	int32_t offset_mv;
	int32_t ma_per_mv;
};

struct debugboard_safe_gpio_desc {
	const char *name;
	uint8_t pin;
	const char *note;
};

extern const struct debugboard_rail_desc debugboard_rails[];
extern const size_t debugboard_rail_count;

extern const struct debugboard_current_desc debugboard_currents[];
extern const size_t debugboard_current_count;

extern const struct debugboard_safe_gpio_desc debugboard_safe_gpios[];
extern const size_t debugboard_safe_gpio_count;

bool debugboard_parse_bool_arg(const char *arg, bool *value);
bool debugboard_parse_gpio_pin(const char *arg, uint8_t *pin);

const struct debugboard_rail_desc *debugboard_find_rail(const char *name);
const struct debugboard_current_desc *debugboard_find_current(const char *name);
const struct debugboard_safe_gpio_desc *debugboard_find_safe_gpio_by_pin(uint8_t pin);

int32_t debugboard_estimate_current_ma(int32_t millivolts,
				       const struct debugboard_current_desc *current);

#endif /* AGENT_DEBUGBOARD_MODEL_H_ */
