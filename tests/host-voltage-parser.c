/*
 * SPDX-FileCopyrightText: 2026 As Tsaqib
 * SPDX-License-Identifier: Apache-2.0
 */

#include "l850_voltage.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char *
read_fixture(const char *directory, const char *name, size_t *length)
{
	char path[1024];
	FILE *file;
	long size;
	char *data;

	assert(snprintf(path, sizeof(path), "%s/%s", directory, name) > 0);
	file = fopen(path, "rb");
	assert(file != NULL);
	assert(fseek(file, 0L, SEEK_END) == 0);
	size = ftell(file);
	assert(size >= 0);
	assert(fseek(file, 0L, SEEK_SET) == 0);
	data = malloc((size_t)size + 1U);
	assert(data != NULL);
	assert(fread(data, 1U, (size_t)size, file) == (size_t)size);
	assert(fclose(file) == 0);
	data[size] = '\0';
	*length = (size_t)size;
	return data;
}

static enum L850GLL850VoltageParseResult
parse_fixture(const char *directory, const char *name,
	      struct L850GLL850Voltage *voltage)
{
	size_t length;
	char *data = read_fixture(directory, name, &length);
	enum L850GLL850VoltageParseResult result =
		l850gl_l850_voltage_parse(data, length, voltage);

	free(data);
	return result;
}

int
main(int argc, char **argv)
{
	struct L850GLL850Voltage voltage;
	char *oversized;
	const char embedded_nul[] = {
		'+', 'C', 'B', 'C', ':', ' ', '0', ',', '3', '5', '5', '0',
		'\0', '\n'
	};
	const char valid_crlf[] = "\r\n\t+CBC: 3, 5000\t\r\n";
	const struct {
		const char *name;
		enum L850GLL850VoltageParseResult expected;
	} invalid[] = {
		{ "invalid-extra-field.txt", L850GL_L850_VOLTAGE_PARSE_MALFORMED },
		{ "invalid-status.txt", L850GL_L850_VOLTAGE_PARSE_RANGE },
		{ "invalid-low-voltage.txt", L850GL_L850_VOLTAGE_PARSE_RANGE },
		{ "invalid-high-voltage.txt", L850GL_L850_VOLTAGE_PARSE_RANGE },
		{ "invalid-hex.txt", L850GL_L850_VOLTAGE_PARSE_MALFORMED },
		{ "invalid-trailing-ok.txt", L850GL_L850_VOLTAGE_PARSE_MALFORMED },
	};
	size_t index;

	assert(argc == 2);
	assert(parse_fixture(argv[1], "valid-live-l850.txt", &voltage) ==
		L850GL_L850_VOLTAGE_PARSE_OK);
	assert(voltage.status == 0U && voltage.millivolts == 3550U);
	assert(l850gl_l850_voltage_parse(valid_crlf, strlen(valid_crlf),
		&voltage) == L850GL_L850_VOLTAGE_PARSE_OK);
	assert(voltage.status == 3U && voltage.millivolts == 5000U);
	for (index = 0U; index < sizeof(invalid) / sizeof(invalid[0]); index++) {
		memset(&voltage, 0xa5, sizeof(voltage));
		assert(parse_fixture(argv[1], invalid[index].name, &voltage) ==
			invalid[index].expected);
		assert(voltage.status == 0U && voltage.millivolts == 0U);
	}
	memset(&voltage, 0xa5, sizeof(voltage));
	assert(l850gl_l850_voltage_parse("", 0U, &voltage) ==
		L850GL_L850_VOLTAGE_PARSE_EMPTY);
	assert(voltage.status == 0U && voltage.millivolts == 0U);
	assert(l850gl_l850_voltage_parse(NULL, 1U, &voltage) ==
		L850GL_L850_VOLTAGE_PARSE_EMPTY);
	assert(l850gl_l850_voltage_parse("x", 1U, NULL) ==
		L850GL_L850_VOLTAGE_PARSE_MALFORMED);
	assert(l850gl_l850_voltage_parse(embedded_nul, sizeof(embedded_nul),
		&voltage) == L850GL_L850_VOLTAGE_PARSE_MALFORMED);
	oversized = malloc(L850GL_L850_VOLTAGE_MAX_RESPONSE + 2U);
	assert(oversized != NULL);
	memset(oversized, '0', L850GL_L850_VOLTAGE_MAX_RESPONSE + 1U);
	assert(l850gl_l850_voltage_parse(oversized,
		L850GL_L850_VOLTAGE_MAX_RESPONSE + 1U, &voltage) ==
		L850GL_L850_VOLTAGE_PARSE_OVERSIZED);
	free(oversized);
	assert(strcmp(l850gl_l850_voltage_query_command(), "AT+CBC") == 0);
	assert(strcmp(l850gl_l850_voltage_parse_result_name(
		L850GL_L850_VOLTAGE_PARSE_RANGE), "range") == 0);
	assert(strcmp(l850gl_l850_voltage_parse_result_name(
		(enum L850GLL850VoltageParseResult)999), "unknown") == 0);
	assert(L850GL_L850_VOLTAGE_MIN_MV == 2500U);
	assert(L850GL_L850_VOLTAGE_MAX_MV == 5000U);

	puts("L850 voltage parser tests passed");
	return 0;
}
