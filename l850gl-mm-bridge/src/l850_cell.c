/*
 * SPDX-FileCopyrightText: 2026 As Tsaqib
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "l850_cell.h"

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define LTE_FIELD_COUNT 14U
#define LTE_RSRP_SENTINEL 255U
#define LTE_RSRQ_SENTINEL 255U
#define LTE_RSSNR_SENTINEL 255
#define LTE_CI_SENTINEL UINT32_MAX
#define LTE_UL_EARFCN_SENTINEL UINT32_MAX
#define LTE_PATHLOSS_SENTINEL UINT32_MAX
#define LTE_TA_SENTINEL INT32_MAX
#define MICROSECONDS_PER_SECOND INT64_C(1000000)

#define L850_SET_SUCCESS \
	"Frequency Lock Configuration Success CPS_MSG_TYPE_ASM_EM_CTRL_CNF"
#define L850_CLEAR_COMMAND \
	"AT@SIC:FREQ_LOCK(0,3,255,0,65535,65535)"
#define L850_RESET_COMMAND "AT+CFUN=15"
#define L850_SCAN_COMMAND "AT+XMCI=1"
#define L850_NVM_QUERY_COMMAND \
	"AT@NVM:DYN_CPS.NAS_ASM.FREQ_LOCK_PARAMS.*??"

struct EarfcnRange {
	uint32_t first;
	uint32_t last;
	uint16_t band;
};

static const struct EarfcnRange earfcn_ranges[] = {
	{ 0U, 599U, 1U }, { 600U, 1199U, 2U }, { 1200U, 1949U, 3U },
	{ 1950U, 2399U, 4U }, { 2400U, 2649U, 5U }, { 2650U, 2749U, 6U },
	{ 2750U, 3449U, 7U }, { 3450U, 3799U, 8U }, { 3800U, 4149U, 9U },
	{ 4150U, 4749U, 10U }, { 4750U, 4949U, 11U }, { 5010U, 5179U, 12U },
	{ 5180U, 5279U, 13U }, { 5280U, 5379U, 14U }, { 5730U, 5849U, 17U },
	{ 5850U, 5999U, 18U }, { 6000U, 6149U, 19U }, { 6150U, 6449U, 20U },
	{ 6450U, 6599U, 21U }, { 6600U, 7399U, 22U }, { 7500U, 7699U, 23U },
	{ 7700U, 8039U, 24U }, { 8040U, 8689U, 25U }, { 8690U, 9039U, 26U },
	{ 9040U, 9209U, 27U }, { 9210U, 9659U, 28U }, { 9660U, 9769U, 29U },
	{ 9770U, 9869U, 30U }, { 9870U, 9919U, 31U }, { 9920U, 10359U, 32U },
	{ 36000U, 36199U, 33U }, { 36200U, 36349U, 34U },
	{ 36350U, 36949U, 35U }, { 36950U, 37549U, 36U },
	{ 37550U, 37749U, 37U }, { 37750U, 38249U, 38U },
	{ 38250U, 38649U, 39U }, { 38650U, 39649U, 40U },
	{ 39650U, 41589U, 41U }, { 41590U, 43589U, 42U },
	{ 43590U, 45589U, 43U }, { 45590U, 46589U, 44U },
	{ 46590U, 46789U, 45U }, { 46790U, 54539U, 46U },
	{ 54540U, 55239U, 47U }, { 55240U, 56739U, 48U },
	{ 65536U, 66435U, 65U }, { 66436U, 67335U, 66U },
	{ 67336U, 67535U, 67U }, { 67536U, 67835U, 68U },
	{ 67836U, 68335U, 69U }, { 68336U, 68585U, 70U },
	{ 68586U, 68935U, 71U }, { 68936U, 68985U, 72U },
	{ 68986U, 69035U, 73U }, { 69036U, 69465U, 74U },
	{ 69466U, 70315U, 75U }, { 70316U, 70365U, 76U },
	{ 70366U, 70545U, 85U },
};

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
parse_uint32(const char *value, uint32_t *number)
{
	uint64_t result = 0U;
	unsigned int base = 10U;
	size_t index = 0U;

	if (value == NULL || value[0] == '\0' || number == NULL)
		return false;
	if (value[0] == '0' && (value[1] == 'x' || value[1] == 'X')) {
		base = 16U;
		index = 2U;
		if (value[index] == '\0')
			return false;
	}
	for (; value[index] != '\0'; index++) {
		unsigned int digit;
		unsigned char character = (unsigned char)value[index];

		if (character >= '0' && character <= '9')
			digit = character - '0';
		else if (base == 16U && character >= 'a' && character <= 'f')
			digit = character - 'a' + 10U;
		else if (base == 16U && character >= 'A' && character <= 'F')
			digit = character - 'A' + 10U;
		else
			return false;
		if (result > (UINT32_MAX - digit) / base)
			return false;
		result = result * base + digit;
	}
	*number = (uint32_t)result;
	return true;
}

static bool
parse_int32(const char *value, int32_t *number)
{
	uint64_t result = 0U;
	uint64_t limit;
	unsigned int base = 10U;
	size_t index = 0U;
	bool negative = false;

	if (value == NULL || value[0] == '\0' || number == NULL)
		return false;
	if (value[index] == '-') {
		negative = true;
		index++;
		if (value[index] == '\0')
			return false;
	}
	if (value[index] == '0' &&
	    (value[index + 1U] == 'x' || value[index + 1U] == 'X')) {
		base = 16U;
		index += 2U;
		if (value[index] == '\0')
			return false;
	}
	limit = negative ? (uint64_t)INT32_MAX + 1U : (uint64_t)INT32_MAX;
	for (; value[index] != '\0'; index++) {
		unsigned int digit;
		unsigned char character = (unsigned char)value[index];

		if (character >= '0' && character <= '9')
			digit = character - '0';
		else if (base == 16U && character >= 'a' && character <= 'f')
			digit = character - 'a' + 10U;
		else if (base == 16U && character >= 'A' && character <= 'F')
			digit = character - 'A' + 10U;
		else
			return false;
		if (result > (limit - digit) / base)
			return false;
		result = result * base + digit;
	}
	if (negative && result == (uint64_t)INT32_MAX + 1U)
		*number = INT32_MIN;
	else
		*number = negative ? -(int32_t)result : (int32_t)result;
	return true;
}

static char *
unquote_numeric_token(char *value)
{
	size_t length;

	value = trim_ascii(value);
	length = strlen(value);
	if (length == 0U)
		return NULL;
	if (value[0] != '"')
		return strchr(value, '"') == NULL ? value : NULL;
	if (length < 3U || value[length - 1U] != '"')
		return NULL;
	value[length - 1U] = '\0';
	value++;
	if (value[0] == '\0' || strchr(value, '"') != NULL ||
	    value[0] == ' ' || value[0] == '\t' ||
	    value[strlen(value) - 1U] == ' ' ||
	    value[strlen(value) - 1U] == '\t')
		return NULL;
	return value;
}

bool
l850gl_l850_earfcn_to_band(uint32_t earfcn, uint16_t *band)
{
	size_t index;

	if (band == NULL)
		return false;
	for (index = 0U; index < sizeof(earfcn_ranges) / sizeof(earfcn_ranges[0]);
	     index++) {
		if (earfcn >= earfcn_ranges[index].first &&
		    earfcn <= earfcn_ranges[index].last) {
			*band = earfcn_ranges[index].band;
			return true;
		}
	}
	return false;
}

bool
l850gl_l850_band_is_supported(uint16_t band,
			       const char *const *supported_bands,
			       size_t supported_band_count)
{
	char expected[24];
	size_t index;

	if (supported_bands == NULL || supported_band_count == 0U ||
	    snprintf(expected, sizeof(expected), "eutran-%u", (unsigned int)band) < 0)
		return false;
	for (index = 0U; index < supported_band_count; index++) {
		if (supported_bands[index] != NULL &&
		    strcmp(supported_bands[index], expected) == 0)
			return true;
	}
	return false;
}

static enum L850GLL850CellParseResult
parse_cell_line(char *line, struct L850GLL850Cell *cell)
{
	char *fields[LTE_FIELD_COUNT];
	uint32_t values[LTE_FIELD_COUNT];
	int32_t rssnr;
	char *cursor;
	size_t count = 0U;
	uint32_t type;
	uint32_t pci;
	uint16_t band;

	if (strncmp(line, "+XMCI:", 6U) != 0)
		return L850GL_L850_CELL_PARSE_MALFORMED;
	cursor = trim_ascii(line + 6U);
	while (true) {
		char *separator = strchr(cursor, ',');

		if (count >= LTE_FIELD_COUNT)
			return L850GL_L850_CELL_PARSE_MALFORMED;
		if (separator != NULL)
			*separator = '\0';
		fields[count++] = trim_ascii(cursor);
		if (fields[count - 1U][0] == '\0')
			return L850GL_L850_CELL_PARSE_MALFORMED;
		if (separator == NULL)
			break;
		cursor = separator + 1;
	}
	if (count != LTE_FIELD_COUNT)
		return L850GL_L850_CELL_PARSE_MALFORMED;
	for (count = 0U; count < LTE_FIELD_COUNT; count++) {
		fields[count] = unquote_numeric_token(fields[count]);
		if (fields[count] == NULL)
			return L850GL_L850_CELL_PARSE_MALFORMED;
	}
	for (count = 0U; count < LTE_FIELD_COUNT; count++) {
		if (count == 11U)
			continue;
		if (!parse_uint32(fields[count], &values[count]))
			return L850GL_L850_CELL_PARSE_MALFORMED;
	}
	if (!parse_int32(fields[11], &rssnr))
		return L850GL_L850_CELL_PARSE_MALFORMED;
	type = values[0];
	if (type != 4U && type != 5U)
		return L850GL_L850_CELL_PARSE_UNSUPPORTED_TYPE;
	pci = values[5];
	if (pci == UINT32_MAX || values[6] == UINT32_MAX ||
	    values[9] == LTE_RSRP_SENTINEL ||
	    values[10] == LTE_RSRQ_SENTINEL)
		return L850GL_L850_CELL_PARSE_SENTINEL;
	if (pci > 503U)
		return L850GL_L850_CELL_PARSE_RANGE;
	if (!l850gl_l850_earfcn_to_band(values[6], &band))
		return L850GL_L850_CELL_PARSE_RANGE;
	if (values[1] > 999U || values[2] > 999U || values[3] > 65535U ||
	    (values[4] > 0x0fffffffU && values[4] != LTE_CI_SENTINEL) ||
	    (values[7] > 262143U && values[7] != LTE_UL_EARFCN_SENTINEL) ||
	    (values[8] > 254U && values[8] != LTE_PATHLOSS_SENTINEL) ||
	    values[9] > 97U || values[10] > 34U ||
	    (rssnr < -255 || rssnr > LTE_RSSNR_SENTINEL) ||
	    (values[12] > 1282U && values[12] != (uint32_t)LTE_TA_SENTINEL) ||
	    values[13] > 15U)
		return L850GL_L850_CELL_PARSE_RANGE;
	cell->type = (uint8_t)type;
	cell->serving = type == 4U;
	cell->earfcn = values[6];
	cell->pci = (uint16_t)pci;
	cell->band = band;
	cell->rsrp_dbm = (int16_t)((int32_t)values[9] - 141);
	cell->rsrq_tenths_db = (int16_t)((int32_t)values[10] * 5 - 195);
	return L850GL_L850_CELL_PARSE_OK;
}

enum L850GLL850CellParseResult
l850gl_l850_cell_parse(const char *response, size_t response_length,
			struct L850GLL850CellScan *scan)
{
	char *copy;
	char *line;
	char *next;
	bool ok_seen = false;
	bool serving_seen = false;
	enum L850GLL850CellParseResult result = L850GL_L850_CELL_PARSE_OK;

	if (response == NULL || scan == NULL || response_length == 0U)
		return L850GL_L850_CELL_PARSE_EMPTY;
	if (response_length > L850GL_L850_CELL_MAX_RESPONSE)
		return L850GL_L850_CELL_PARSE_OVERSIZED;
	memset(scan, 0, sizeof(*scan));
	copy = malloc(response_length + 1U);
	if (copy == NULL)
		return L850GL_L850_CELL_PARSE_MALFORMED;
	memcpy(copy, response, response_length);
	copy[response_length] = '\0';
	if (memchr(copy, '\0', response_length) != NULL) {
		free(copy);
		return L850GL_L850_CELL_PARSE_MALFORMED;
	}
	line = copy;
	while (line != NULL) {
		char *current;

		next = strchr(line, '\n');
		if (next != NULL)
			*next++ = '\0';
		if (line[0] != '\0' && line[strlen(line) - 1U] == '\r')
			line[strlen(line) - 1U] = '\0';
		current = trim_ascii(line);
		if (current[0] == '\0') {
			line = next;
			continue;
		}
		if (strcmp(current, "OK") == 0) {
			if (ok_seen) {
				result = L850GL_L850_CELL_PARSE_MALFORMED;
				break;
			}
			ok_seen = true;
			line = next;
			continue;
		}
		if (ok_seen || scan->length >= L850GL_L850_CELL_MAX_RESULTS) {
			result = scan->length >= L850GL_L850_CELL_MAX_RESULTS ?
				L850GL_L850_CELL_PARSE_TOO_MANY :
				L850GL_L850_CELL_PARSE_MALFORMED;
			break;
		}
		result = parse_cell_line(current, &scan->cells[scan->length]);
		if (result != L850GL_L850_CELL_PARSE_OK)
			break;
		if (scan->cells[scan->length].serving) {
			if (serving_seen) {
				result = L850GL_L850_CELL_PARSE_MALFORMED;
				break;
			}
			serving_seen = true;
		}
		scan->length++;
		line = next;
	}
	if (result == L850GL_L850_CELL_PARSE_OK && scan->length == 0U)
		result = L850GL_L850_CELL_PARSE_EMPTY;
	if (result != L850GL_L850_CELL_PARSE_OK)
		memset(scan, 0, sizeof(*scan));
	free(copy);
	return result;
}

enum L850GLL850CellParseResult
l850gl_l850_nvm_parse(const char *response, size_t response_length,
		       struct L850GLL850LockState *state)
{
	enum {
		NVM_FREQUENCY,
		NVM_RAT,
		NVM_PCI,
		NVM_BAND_INFO,
		NVM_INTER_FREQUENCY,
		NVM_FIELD_COUNT,
	};
	static const char *const keys[NVM_FIELD_COUNT] = {
		[NVM_FREQUENCY] = "frequency",
		[NVM_RAT] = "rat",
		[NVM_PCI] = "psc_or_pci",
		[NVM_BAND_INFO] = "band_info",
		[NVM_INTER_FREQUENCY] = "inter_freq_lock_support",
	};
	uint32_t values[NVM_FIELD_COUNT] = {};
	bool seen[NVM_FIELD_COUNT] = {};
	char *copy;
	char *line;
	size_t parsed_count = 0U;
	enum L850GLL850CellParseResult result = L850GL_L850_CELL_PARSE_OK;

	if (response == NULL || state == NULL || response_length == 0U)
		return L850GL_L850_CELL_PARSE_EMPTY;
	if (response_length > L850GL_L850_NVM_MAX_RESPONSE)
		return L850GL_L850_CELL_PARSE_OVERSIZED;
	memset(state, 0, sizeof(*state));
	copy = malloc(response_length + 1U);
	if (copy == NULL)
		return L850GL_L850_CELL_PARSE_MALFORMED;
	memcpy(copy, response, response_length);
	copy[response_length] = '\0';
	if (memchr(copy, '\0', response_length) != NULL) {
		result = L850GL_L850_CELL_PARSE_MALFORMED;
		goto out;
	}
	line = copy;
	while (line != NULL) {
		char *next = strchr(line, '\n');
		char *separator;
		char *key;
		char *value;
		size_t index;

		if (next != NULL)
			*next++ = '\0';
		if (line[0] != '\0' && line[strlen(line) - 1U] == '\r')
			line[strlen(line) - 1U] = '\0';
		line = trim_ascii(line);
		if (line[0] == '\0') {
			line = next;
			continue;
		}
		separator = strchr(line, '=');
		if (separator == NULL || strchr(separator + 1, '=') != NULL) {
			result = L850GL_L850_CELL_PARSE_MALFORMED;
			break;
		}
		*separator = '\0';
		key = trim_ascii(line);
		value = trim_ascii(separator + 1);
		for (index = 0U; index < NVM_FIELD_COUNT; index++) {
			if (strcmp(key, keys[index]) == 0)
				break;
		}
		if (index == NVM_FIELD_COUNT || seen[index] ||
		    !parse_uint32(value, &values[index])) {
			result = L850GL_L850_CELL_PARSE_MALFORMED;
			break;
		}
		seen[index] = true;
		parsed_count++;
		line = next;
	}
	if (result != L850GL_L850_CELL_PARSE_OK)
		goto out;
	if (parsed_count != NVM_FIELD_COUNT) {
		result = parsed_count == 0U ? L850GL_L850_CELL_PARSE_EMPTY :
			L850GL_L850_CELL_PARSE_MALFORMED;
		goto out;
	}
	if (values[NVM_RAT] != 3U || values[NVM_BAND_INFO] != 0U ||
	    values[NVM_PCI] > L850GL_L850_PCI_WILDCARD ||
	    values[NVM_INTER_FREQUENCY] > 1U) {
		result = L850GL_L850_CELL_PARSE_RANGE;
		goto out;
	}
	if (values[NVM_INTER_FREQUENCY] == 0U) {
		if (values[NVM_FREQUENCY] != L850GL_L850_CLEAR_FREQUENCY ||
		    values[NVM_PCI] != L850GL_L850_PCI_WILDCARD) {
			result = L850GL_L850_CELL_PARSE_MALFORMED;
			goto out;
		}
		state->enabled = false;
		state->earfcn = L850GL_L850_CLEAR_FREQUENCY;
		state->pci = L850GL_L850_PCI_WILDCARD;
		goto out;
	}
	if (!l850gl_l850_earfcn_to_band(values[NVM_FREQUENCY], &state->band) ||
	    (values[NVM_PCI] > 503U &&
	     values[NVM_PCI] != L850GL_L850_PCI_WILDCARD)) {
		result = values[NVM_FREQUENCY] == L850GL_L850_CLEAR_FREQUENCY ||
			values[NVM_PCI] == UINT32_MAX ?
			L850GL_L850_CELL_PARSE_SENTINEL :
			L850GL_L850_CELL_PARSE_RANGE;
		goto out;
	}
	state->enabled = true;
	state->earfcn = values[NVM_FREQUENCY];
	state->pci = (uint16_t)values[NVM_PCI];
	state->has_pci = state->pci != L850GL_L850_PCI_WILDCARD;

out:
	if (result != L850GL_L850_CELL_PARSE_OK)
		memset(state, 0, sizeof(*state));
	free(copy);
	return result;
}

const char *
l850gl_l850_cell_parse_result_name(enum L850GLL850CellParseResult result)
{
	switch (result) {
	case L850GL_L850_CELL_PARSE_OK: return "ok";
	case L850GL_L850_CELL_PARSE_EMPTY: return "empty";
	case L850GL_L850_CELL_PARSE_OVERSIZED: return "oversized";
	case L850GL_L850_CELL_PARSE_MALFORMED: return "malformed";
	case L850GL_L850_CELL_PARSE_UNSUPPORTED_TYPE: return "unsupported_type";
	case L850GL_L850_CELL_PARSE_SENTINEL: return "sentinel";
	case L850GL_L850_CELL_PARSE_RANGE: return "range";
	case L850GL_L850_CELL_PARSE_TOO_MANY: return "too_many";
	default: return "unknown";
	}
}

const char *
l850gl_l850_state_name(enum L850GLL850State state)
{
	switch (state) {
	case L850GL_L850_STATE_AVAILABLE: return "available";
	case L850GL_L850_STATE_UNSUPPORTED_BUILD: return "unsupported_build";
	case L850GL_L850_STATE_UNSUPPORTED_FIRMWARE: return "unsupported_firmware";
	case L850GL_L850_STATE_SCAN_READY: return "scan_ready";
	case L850GL_L850_STATE_LOCK_APPLIED_RESET_REQUIRED:
		return "lock_applied_reset_required";
	case L850GL_L850_STATE_RESETTING: return "resetting";
	case L850GL_L850_STATE_APPLIED_VERIFIED: return "applied_verified";
	case L850GL_L850_STATE_CLEARED_VERIFIED: return "cleared_verified";
	case L850GL_L850_STATE_REPROBE_TIMEOUT: return "reprobe_timeout";
	case L850GL_L850_STATE_REGISTRATION_TIMEOUT: return "registration_timeout";
	case L850GL_L850_STATE_VERIFICATION_MISMATCH:
		return "verification_mismatch";
	case L850GL_L850_STATE_OUTCOME_UNKNOWN: return "outcome_unknown";
	default: return "unavailable";
	}
}

bool
l850gl_l850_state_transition_is_valid(enum L850GLL850State from,
				       enum L850GLL850State to)
{
	if (from == L850GL_L850_STATE_AVAILABLE)
		return to == L850GL_L850_STATE_SCAN_READY;
	if (from == L850GL_L850_STATE_SCAN_READY)
		return to == L850GL_L850_STATE_LOCK_APPLIED_RESET_REQUIRED ||
			to == L850GL_L850_STATE_OUTCOME_UNKNOWN;
	if (from == L850GL_L850_STATE_LOCK_APPLIED_RESET_REQUIRED)
		return to == L850GL_L850_STATE_RESETTING ||
			to == L850GL_L850_STATE_OUTCOME_UNKNOWN;
	if (from == L850GL_L850_STATE_RESETTING)
		return to == L850GL_L850_STATE_APPLIED_VERIFIED ||
			to == L850GL_L850_STATE_CLEARED_VERIFIED ||
			to == L850GL_L850_STATE_REPROBE_TIMEOUT ||
			to == L850GL_L850_STATE_REGISTRATION_TIMEOUT ||
			to == L850GL_L850_STATE_VERIFICATION_MISMATCH ||
			to == L850GL_L850_STATE_OUTCOME_UNKNOWN;
	return false;
}

bool
l850gl_l850_firmware_is_allowed(const char *revision)
{
	return revision != NULL &&
		strcmp(revision, L850GL_L850_ALLOWED_FIRMWARE) == 0;
}

bool
l850gl_l850_build_set_command(uint32_t earfcn, bool has_pci, uint16_t pci,
			       char *command, size_t command_size)
{
	uint16_t band;
	int length;

	if (command == NULL || command_size == 0U ||
	    !l850gl_l850_earfcn_to_band(earfcn, &band) ||
	    (has_pci && pci > 503U))
		return false;
	length = snprintf(command, command_size,
		"AT@SIC:FREQ_LOCK(0,3,%u,1,%u,%u)", (unsigned int)band,
		(unsigned int)earfcn,
		has_pci ? (unsigned int)pci :
			(unsigned int)L850GL_L850_PCI_WILDCARD);
	if (length < 0 || (size_t)length >= command_size) {
		command[0] = '\0';
		return false;
	}
	return true;
}

const char *
l850gl_l850_clear_command(void)
{
	return L850_CLEAR_COMMAND;
}

const char *
l850gl_l850_reset_command(void)
{
	return L850_RESET_COMMAND;
}

const char *
l850gl_l850_scan_command(void)
{
	return L850_SCAN_COMMAND;
}

const char *
l850gl_l850_nvm_query_command(void)
{
	return L850_NVM_QUERY_COMMAND;
}

bool
l850gl_l850_set_response_is_success(const char *response,
				     size_t response_length)
{
	return response != NULL && response_length == strlen(L850_SET_SUCCESS) &&
		memcmp(response, L850_SET_SUCCESS, response_length) == 0;
}

bool
l850gl_l850_lock_state_matches(const struct L850GLL850LockState *state,
				bool clear, uint32_t earfcn, bool has_pci,
				uint16_t pci)
{
	if (state == NULL)
		return false;
	if (clear)
		return !state->enabled &&
			state->earfcn == L850GL_L850_CLEAR_FREQUENCY &&
			state->pci == L850GL_L850_PCI_WILDCARD;
	return state->enabled && state->earfcn == earfcn &&
		state->has_pci == has_pci &&
		state->pci == (has_pci ? pci : L850GL_L850_PCI_WILDCARD);
}

uint32_t
l850gl_l850_scan_retry_after_ms(int64_t now_us,
				 int64_t last_scan_completed_us)
{
	const int64_t interval =
		(int64_t)L850GL_L850_CELL_SCAN_COOLDOWN_SECONDS *
		MICROSECONDS_PER_SECOND;
	int64_t elapsed;
	int64_t remaining;

	if (now_us < 0 || last_scan_completed_us <= 0)
		return 0U;
	if (now_us < last_scan_completed_us)
		return L850GL_L850_CELL_SCAN_COOLDOWN_SECONDS * 1000U;
	elapsed = now_us - last_scan_completed_us;
	if (elapsed >= interval)
		return 0U;
	remaining = interval - elapsed;
	remaining = (remaining + 999) / 1000;
	return remaining > UINT32_MAX ? UINT32_MAX : (uint32_t)remaining;
}
