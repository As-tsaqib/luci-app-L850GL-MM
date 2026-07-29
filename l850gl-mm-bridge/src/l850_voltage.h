/*
 * SPDX-FileCopyrightText: 2026 As Tsaqib
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef L850GL_L850_VOLTAGE_H
#define L850GL_L850_VOLTAGE_H

#include <stddef.h>
#include <stdint.h>

#define L850GL_L850_VOLTAGE_MAX_RESPONSE 128U
#define L850GL_L850_VOLTAGE_MIN_MV 2500U
#define L850GL_L850_VOLTAGE_MAX_MV 5000U

enum L850GLL850VoltageParseResult {
	L850GL_L850_VOLTAGE_PARSE_OK = 0,
	L850GL_L850_VOLTAGE_PARSE_EMPTY,
	L850GL_L850_VOLTAGE_PARSE_OVERSIZED,
	L850GL_L850_VOLTAGE_PARSE_MALFORMED,
	L850GL_L850_VOLTAGE_PARSE_RANGE,
};

struct L850GLL850Voltage {
	uint8_t status;
	uint32_t millivolts;
};

enum L850GLL850VoltageParseResult l850gl_l850_voltage_parse(
	const char *response, size_t response_length,
	struct L850GLL850Voltage *voltage);
const char *l850gl_l850_voltage_parse_result_name(
	enum L850GLL850VoltageParseResult result);
const char *l850gl_l850_voltage_query_command(void);

#endif /* L850GL_L850_VOLTAGE_H */
