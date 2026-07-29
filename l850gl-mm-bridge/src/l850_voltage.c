/*
 * SPDX-FileCopyrightText: 2026 As Tsaqib
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "l850_voltage.h"

#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#define VOLTAGE_PREFIX "+CBC:"
#define VOLTAGE_QUERY_COMMAND "AT+CBC"
#define VOLTAGE_STATUS_MAX 3U

static char *
trim_ascii(char *value)
{
	char *end;

	while (*value == ' ' || *value == '\t')
		value++;
	end = value + strlen(value);
	while (end > value && (end[-1] == ' ' || end[-1] == '\t'))
		*--end = '\0';
	return value;
}

static bool
parse_uint32_decimal(const char *value, uint32_t *number)
{
	uint32_t result = 0U;
	size_t index;

	if (value == NULL || value[0] == '\0' || number == NULL)
		return false;
	for (index = 0U; value[index] != '\0'; index++) {
		unsigned int digit;
		unsigned char character = (unsigned char)value[index];

		if (character < '0' || character > '9')
			return false;
		digit = (unsigned int)(character - '0');
		if (result > (UINT32_MAX - digit) / 10U)
			return false;
		result = result * 10U + digit;
	}
	*number = result;
	return true;
}

enum L850GLL850VoltageParseResult
l850gl_l850_voltage_parse(const char *response, size_t response_length,
			   struct L850GLL850Voltage *voltage)
{
	char *copy;
	char *line;
	char *record = NULL;
	char *body;
	char *separator;
	uint32_t status;
	uint32_t millivolts;
	enum L850GLL850VoltageParseResult result =
		L850GL_L850_VOLTAGE_PARSE_OK;

	if (voltage == NULL)
		return L850GL_L850_VOLTAGE_PARSE_MALFORMED;
	memset(voltage, 0, sizeof(*voltage));
	if (response == NULL || response_length == 0U)
		return L850GL_L850_VOLTAGE_PARSE_EMPTY;
	if (response_length > L850GL_L850_VOLTAGE_MAX_RESPONSE)
		return L850GL_L850_VOLTAGE_PARSE_OVERSIZED;
	copy = malloc(response_length + 1U);
	if (copy == NULL)
		return L850GL_L850_VOLTAGE_PARSE_MALFORMED;
	memcpy(copy, response, response_length);
	copy[response_length] = '\0';
	if (memchr(copy, '\0', response_length) != NULL) {
		result = L850GL_L850_VOLTAGE_PARSE_MALFORMED;
		goto out;
	}

	line = copy;
	while (line != NULL) {
		char *next = strchr(line, '\n');
		char *trimmed;
		size_t length;

		if (next != NULL)
			*next++ = '\0';
		length = strlen(line);
		if (length > 0U && line[length - 1U] == '\r')
			line[length - 1U] = '\0';
		trimmed = trim_ascii(line);
		if (trimmed[0] != '\0') {
			if (record != NULL) {
				result = L850GL_L850_VOLTAGE_PARSE_MALFORMED;
				goto out;
			}
			record = trimmed;
		}
		line = next;
	}
	if (record == NULL) {
		result = L850GL_L850_VOLTAGE_PARSE_EMPTY;
		goto out;
	}
	if (strncmp(record, VOLTAGE_PREFIX, strlen(VOLTAGE_PREFIX)) != 0) {
		result = L850GL_L850_VOLTAGE_PARSE_MALFORMED;
		goto out;
	}
	body = trim_ascii(record + strlen(VOLTAGE_PREFIX));
	separator = strchr(body, ',');
	if (separator == NULL || strchr(separator + 1, ',') != NULL) {
		result = L850GL_L850_VOLTAGE_PARSE_MALFORMED;
		goto out;
	}
	*separator++ = '\0';
	if (!parse_uint32_decimal(trim_ascii(body), &status) ||
	    !parse_uint32_decimal(trim_ascii(separator), &millivolts)) {
		result = L850GL_L850_VOLTAGE_PARSE_MALFORMED;
		goto out;
	}
	if (status > VOLTAGE_STATUS_MAX ||
	    millivolts < L850GL_L850_VOLTAGE_MIN_MV ||
	    millivolts > L850GL_L850_VOLTAGE_MAX_MV) {
		result = L850GL_L850_VOLTAGE_PARSE_RANGE;
		goto out;
	}
	voltage->status = (uint8_t)status;
	voltage->millivolts = millivolts;
out:
	if (result != L850GL_L850_VOLTAGE_PARSE_OK)
		memset(voltage, 0, sizeof(*voltage));
	free(copy);
	return result;
}

const char *
l850gl_l850_voltage_parse_result_name(
		enum L850GLL850VoltageParseResult result)
{
	switch (result) {
	case L850GL_L850_VOLTAGE_PARSE_OK:
		return "ok";
	case L850GL_L850_VOLTAGE_PARSE_EMPTY:
		return "empty";
	case L850GL_L850_VOLTAGE_PARSE_OVERSIZED:
		return "oversized";
	case L850GL_L850_VOLTAGE_PARSE_MALFORMED:
		return "malformed";
	case L850GL_L850_VOLTAGE_PARSE_RANGE:
		return "range";
	default:
		return "unknown";
	}
}

const char *
l850gl_l850_voltage_query_command(void)
{
	return VOLTAGE_QUERY_COMMAND;
}
