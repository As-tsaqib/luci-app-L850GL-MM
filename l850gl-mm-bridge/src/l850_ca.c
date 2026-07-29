/*
 * SPDX-FileCopyrightText: 2026 As Tsaqib
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "l850_ca.h"

#include <limits.h>
#include <stdlib.h>
#include <string.h>

#define CA_PREFIX "+GTCAINFO:"
#define CA_PRIMARY_FIELD_COUNT 14U
#define CA_SECONDARY_FIELD_COUNT 10U
#define CA_RSRP_SENTINEL 255U
#define CA_RSRQ_SENTINEL 255U
#define CA_SINR_SENTINEL 127
#define CA_BAND_SENTINEL 255U
#define CA_MCC_SENTINEL 65535U
#define CA_PCI_SENTINEL 65535U
#define CA_BANDWIDTH_SENTINEL 255U
#define CA_CELL_ID_MAX 0x0fffffffU
#define CA_LTE_EARFCN_MAX 262143U
#define CA_QUERY_COMMAND "AT+GTCAINFO?"
#define MICROSECONDS_PER_SECOND INT64_C(1000000)

struct CaBandRange {
	uint16_t band;
	uint32_t dl_first;
	uint32_t dl_last;
	uint32_t ul_first;
	uint32_t ul_last;
};

/*
 * Paired and TDD bands advertised by the L850-GL. Downlink-only bands 29 and
 * 32 deliberately remain fail-closed until their active GTCAINFO uplink
 * sentinel shape is captured on the allowlisted firmware. Runtime integration
 * additionally confirms that every band occurs in live SupportedBands.
 */
static const struct CaBandRange ca_band_ranges[] = {
	{ 1U, 0U, 599U, 18000U, 18599U },
	{ 2U, 600U, 1199U, 18600U, 19199U },
	{ 3U, 1200U, 1949U, 19200U, 19949U },
	{ 4U, 1950U, 2399U, 19950U, 20399U },
	{ 5U, 2400U, 2649U, 20400U, 20649U },
	{ 7U, 2750U, 3449U, 20750U, 21449U },
	{ 8U, 3450U, 3799U, 21450U, 21799U },
	{ 11U, 4750U, 4949U, 22750U, 22949U },
	{ 12U, 5010U, 5179U, 23010U, 23179U },
	{ 13U, 5180U, 5279U, 23180U, 23279U },
	{ 17U, 5730U, 5849U, 23730U, 23849U },
	{ 18U, 5850U, 5999U, 23850U, 23999U },
	{ 19U, 6000U, 6149U, 24000U, 24149U },
	{ 20U, 6150U, 6449U, 24150U, 24449U },
	{ 21U, 6450U, 6599U, 24450U, 24599U },
	{ 26U, 8690U, 9039U, 26690U, 27039U },
	{ 28U, 9210U, 9659U, 27210U, 27659U },
	{ 30U, 9770U, 9869U, 27660U, 27759U },
	{ 38U, 37750U, 38249U, 37750U, 38249U },
	{ 39U, 38250U, 38649U, 38250U, 38649U },
	{ 40U, 38650U, 39649U, 38650U, 39649U },
	{ 41U, 39650U, 41589U, 39650U, 41589U },
	{ 66U, 66436U, 67335U, 131972U, 132671U },
};

struct CaSlot {
	bool seen;
	bool active;
	struct L850GLL850CaCarrier carrier;
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
parse_uint32_decimal(const char *value, uint32_t *number)
{
	uint64_t result = 0U;
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
	*number = (uint32_t)result;
	return true;
}

static bool
parse_int32_decimal(const char *value, int32_t *number)
{
	uint64_t result = 0U;
	uint64_t limit;
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
	limit = negative ? (uint64_t)INT32_MAX + 1U : (uint64_t)INT32_MAX;
	for (; value[index] != '\0'; index++) {
		unsigned int digit;
		unsigned char character = (unsigned char)value[index];

		if (character < '0' || character > '9')
			return false;
		digit = (unsigned int)(character - '0');
		if (result > (limit - digit) / 10U)
			return false;
		result = result * 10U + digit;
	}
	if (negative && result == (uint64_t)INT32_MAX + 1U)
		*number = INT32_MIN;
	else
		*number = negative ? -(int32_t)result : (int32_t)result;
	return true;
}

static size_t
split_fields(char *body, char **fields, size_t capacity)
{
	size_t count = 0U;
	char *cursor = body;

	while (true) {
		char *separator;

		if (count >= capacity)
			return capacity + 1U;
		separator = strchr(cursor, ',');
		if (separator != NULL)
			*separator = '\0';
		fields[count] = trim_ascii(cursor);
		if (fields[count][0] == '\0')
			return 0U;
		count++;
		if (separator == NULL)
			return count;
		cursor = separator + 1;
	}
}

static const struct CaBandRange *
find_band_range(uint32_t band)
{
	size_t index;

	for (index = 0U;
	     index < sizeof(ca_band_ranges) / sizeof(ca_band_ranges[0]);
	     index++) {
		if (ca_band_ranges[index].band == band)
			return &ca_band_ranges[index];
	}
	return NULL;
}

static bool
band_earfcns_are_valid(uint32_t band, uint32_t dl_earfcn,
		       uint32_t ul_earfcn)
{
	const struct CaBandRange *range = find_band_range(band);

	return range != NULL &&
		dl_earfcn >= range->dl_first && dl_earfcn <= range->dl_last &&
		ul_earfcn >= range->ul_first && ul_earfcn <= range->ul_last;
}

static bool
bandwidth_code_to_tenths_mhz(uint32_t code, uint16_t *tenths_mhz)
{
	static const uint16_t values[] = { 14U, 30U, 50U, 100U, 150U, 200U };

	if (tenths_mhz == NULL || code >= sizeof(values) / sizeof(values[0]))
		return false;
	*tenths_mhz = values[code];
	return true;
}

/*
 * The L850 primary slot has 14 fields:
 * index, band, MCC, MNC, TAC, cell ID, PCI, RSRP, RSRQ, SINR,
 * downlink EARFCN, uplink EARFCN, downlink bandwidth, uplink bandwidth.
 * Secondary slots omit MCC/MNC/TAC/cell ID and therefore have 10 fields.
 */

static enum L850GLL850CaParseResult
validate_common_carrier(struct L850GLL850CaCarrier *carrier,
			uint32_t band, uint32_t pci, uint32_t rsrp,
			uint32_t rsrq, int32_t sinr, uint32_t dl_earfcn,
			uint32_t ul_earfcn, uint32_t dl_bandwidth,
			uint32_t ul_bandwidth)
{
	if (band == CA_BAND_SENTINEL || pci == CA_PCI_SENTINEL ||
	    rsrp == CA_RSRP_SENTINEL || rsrq == CA_RSRQ_SENTINEL ||
	    sinr == CA_SINR_SENTINEL || sinr == 255 ||
	    dl_earfcn == UINT16_MAX || ul_earfcn == UINT16_MAX ||
	    dl_bandwidth == CA_BANDWIDTH_SENTINEL ||
	    ul_bandwidth == CA_BANDWIDTH_SENTINEL)
		return L850GL_L850_CA_PARSE_SENTINEL;
	if (band == 0U || band > 85U || pci > 503U || rsrp > 97U ||
	    rsrq > 34U || sinr < -100 || sinr > 100 ||
	    !band_earfcns_are_valid(band, dl_earfcn, ul_earfcn) ||
	    !bandwidth_code_to_tenths_mhz(
		dl_bandwidth, &carrier->dl_bandwidth_tenths_mhz) ||
	    !bandwidth_code_to_tenths_mhz(
		ul_bandwidth, &carrier->ul_bandwidth_tenths_mhz))
		return L850GL_L850_CA_PARSE_RANGE;
	carrier->band = (uint16_t)band;
	carrier->pci = (uint16_t)pci;
	carrier->rsrp_dbm = (int16_t)((int32_t)rsrp - 141);
	carrier->rsrq_tenths_db = (int16_t)((int32_t)rsrq * 5 - 195);
	carrier->sinr_tenths_db = (int16_t)(sinr * 5);
	carrier->dl_earfcn = dl_earfcn;
	carrier->ul_earfcn = ul_earfcn;
	return L850GL_L850_CA_PARSE_OK;
}

static enum L850GLL850CaParseResult
parse_primary_record(char **fields, struct L850GLL850CaCarrier *carrier)
{
	uint32_t values[CA_PRIMARY_FIELD_COUNT] = {};
	int32_t sinr;
	enum L850GLL850CaParseResult result;
	size_t index;

	for (index = 0U; index < CA_PRIMARY_FIELD_COUNT; index++) {
		if (index == 9U)
			continue;
		if (!parse_uint32_decimal(fields[index], &values[index]))
			return L850GL_L850_CA_PARSE_MALFORMED;
	}
	if (!parse_int32_decimal(fields[9], &sinr))
		return L850GL_L850_CA_PARSE_MALFORMED;
	if (values[0] != 1U)
		return L850GL_L850_CA_PARSE_RANGE;
	if (values[2] == CA_MCC_SENTINEL || values[4] == UINT16_MAX)
		return L850GL_L850_CA_PARSE_SENTINEL;
	if (values[2] == 0U || values[2] > 999U || values[3] > 999U ||
	    values[4] > UINT16_MAX || values[5] > CA_CELL_ID_MAX)
		return L850GL_L850_CA_PARSE_RANGE;
	result = validate_common_carrier(carrier, values[1], values[6],
		values[7], values[8], sinr, values[10], values[11],
		values[12], values[13]);
	if (result != L850GL_L850_CA_PARSE_OK)
		return result;
	carrier->index = 1U;
	carrier->primary = true;
	carrier->has_cell_identity = true;
	carrier->mcc = (uint16_t)values[2];
	carrier->mnc = (uint16_t)values[3];
	carrier->tac = (uint16_t)values[4];
	carrier->cell_id = values[5];
	return L850GL_L850_CA_PARSE_OK;
}

static enum L850GLL850CaParseResult
parse_secondary_record(char **fields, uint8_t *slot_index, bool *active,
		       struct L850GLL850CaCarrier *carrier)
{
	uint32_t values[CA_SECONDARY_FIELD_COUNT] = {};
	int32_t sinr;
	enum L850GLL850CaParseResult result;
	size_t index;

	for (index = 0U; index < CA_SECONDARY_FIELD_COUNT; index++) {
		if (index == 5U)
			continue;
		if (!parse_uint32_decimal(fields[index], &values[index]))
			return L850GL_L850_CA_PARSE_MALFORMED;
	}
	if (!parse_int32_decimal(fields[5], &sinr))
		return L850GL_L850_CA_PARSE_MALFORMED;
	if (values[0] < 2U || values[0] > L850GL_L850_CA_MAX_SLOTS)
		return L850GL_L850_CA_PARSE_RANGE;
	*slot_index = (uint8_t)values[0];
	if (values[1] == 255U && values[2] == 65535U &&
	    values[3] == 255U && values[4] == 255U && sinr == 127 &&
	    values[6] == 65535U && values[8] == 255U && values[9] == 255U) {
		if (values[7] > CA_LTE_EARFCN_MAX)
			return L850GL_L850_CA_PARSE_SENTINEL;
		*active = false;
		return L850GL_L850_CA_PARSE_OK;
	}
	result = validate_common_carrier(carrier, values[1], values[2],
		values[3], values[4], sinr, values[6], values[7], values[8],
		values[9]);
	if (result != L850GL_L850_CA_PARSE_OK)
		return result;
	carrier->index = *slot_index;
	carrier->primary = false;
	carrier->has_cell_identity = false;
	*active = true;
	return L850GL_L850_CA_PARSE_OK;
}

static bool
carriers_are_duplicate(const struct L850GLL850CaCarrier *left,
		       const struct L850GLL850CaCarrier *right)
{
	return left->band == right->band && left->pci == right->pci &&
		left->dl_earfcn == right->dl_earfcn &&
		left->ul_earfcn == right->ul_earfcn;
}

enum L850GLL850CaParseResult
l850gl_l850_ca_parse(const char *response, size_t response_length,
		      struct L850GLL850CaInfo *info)
{
	struct CaSlot slots[L850GL_L850_CA_MAX_SLOTS] = {};
	char *copy;
	char *line;
	uint32_t declared_slots = 0U;
	size_t record_count = 0U;
	bool header_seen = false;
	enum L850GLL850CaParseResult result = L850GL_L850_CA_PARSE_OK;

	if (info == NULL)
		return L850GL_L850_CA_PARSE_MALFORMED;
	memset(info, 0, sizeof(*info));
	if (response == NULL || response_length == 0U)
		return L850GL_L850_CA_PARSE_EMPTY;
	if (response_length > L850GL_L850_CA_MAX_RESPONSE)
		return L850GL_L850_CA_PARSE_OVERSIZED;
	copy = malloc(response_length + 1U);
	if (copy == NULL)
		return L850GL_L850_CA_PARSE_MALFORMED;
	memcpy(copy, response, response_length);
	copy[response_length] = '\0';
	if (memchr(copy, '\0', response_length) != NULL) {
		result = L850GL_L850_CA_PARSE_MALFORMED;
		goto out;
	}
	line = copy;
	while (line != NULL) {
		char *next = strchr(line, '\n');
		char *current;
		char *body;
		char *fields[CA_PRIMARY_FIELD_COUNT + 1U];
		size_t field_count;
		uint8_t slot_index;
		bool active;
		struct L850GLL850CaCarrier carrier = {};

		if (next != NULL)
			*next++ = '\0';
		if (line[0] != '\0' && line[strlen(line) - 1U] == '\r')
			line[strlen(line) - 1U] = '\0';
		current = trim_ascii(line);
		if (current[0] == '\0') {
			line = next;
			continue;
		}
		if (strncmp(current, CA_PREFIX, strlen(CA_PREFIX)) != 0) {
			result = L850GL_L850_CA_PARSE_MALFORMED;
			break;
		}
		body = trim_ascii(current + strlen(CA_PREFIX));
		field_count = split_fields(body, fields,
			CA_PRIMARY_FIELD_COUNT + 1U);
		if (field_count == 1U) {
			if (header_seen || record_count != 0U ||
			    !parse_uint32_decimal(fields[0], &declared_slots)) {
				result = L850GL_L850_CA_PARSE_MALFORMED;
				break;
			}
			if (declared_slots == 0U) {
				result = L850GL_L850_CA_PARSE_RANGE;
				break;
			}
			if (declared_slots > L850GL_L850_CA_MAX_SLOTS) {
				result = L850GL_L850_CA_PARSE_TOO_MANY;
				break;
			}
			header_seen = true;
			line = next;
			continue;
		}
		if (!header_seen) {
			result = L850GL_L850_CA_PARSE_MALFORMED;
			break;
		}
		if (record_count >= declared_slots) {
			result = L850GL_L850_CA_PARSE_COUNT_MISMATCH;
			break;
		}
		if (field_count == CA_PRIMARY_FIELD_COUNT) {
			result = parse_primary_record(fields, &carrier);
			if (result != L850GL_L850_CA_PARSE_OK)
				break;
			slot_index = carrier.index;
			active = true;
		} else if (field_count == CA_SECONDARY_FIELD_COUNT) {
			result = parse_secondary_record(fields, &slot_index, &active,
				&carrier);
			if (result != L850GL_L850_CA_PARSE_OK)
				break;
		} else {
			result = L850GL_L850_CA_PARSE_MALFORMED;
			break;
		}
		if (slot_index > declared_slots) {
			result = L850GL_L850_CA_PARSE_COUNT_MISMATCH;
			break;
		}
		if (slots[slot_index - 1U].seen) {
			result = L850GL_L850_CA_PARSE_DUPLICATE;
			break;
		}
		slots[slot_index - 1U].seen = true;
		if (active) {
			slots[slot_index - 1U].active = true;
			slots[slot_index - 1U].carrier = carrier;
		}
		record_count++;
		line = next;
	}
	if (result != L850GL_L850_CA_PARSE_OK)
		goto out;
	if (!header_seen)
		result = L850GL_L850_CA_PARSE_EMPTY;
	else if (!slots[0].seen || !slots[0].active)
		result = L850GL_L850_CA_PARSE_MISSING_PRIMARY;
	else if (record_count != declared_slots)
		result = L850GL_L850_CA_PARSE_COUNT_MISMATCH;
	if (result != L850GL_L850_CA_PARSE_OK)
		goto out;
	for (size_t index = 0U; index < declared_slots; index++) {
		size_t compare;

		if (!slots[index].seen) {
			result = L850GL_L850_CA_PARSE_COUNT_MISMATCH;
			break;
		}
		if (!slots[index].active)
			continue;
		for (compare = 0U; compare < index; compare++) {
			if (slots[compare].active && carriers_are_duplicate(
			    &slots[index].carrier, &slots[compare].carrier)) {
				result = L850GL_L850_CA_PARSE_DUPLICATE;
				break;
			}
		}
		if (result != L850GL_L850_CA_PARSE_OK)
			break;
		info->carriers[info->length++] = slots[index].carrier;
	}
	if (result == L850GL_L850_CA_PARSE_OK)
		info->declared_slots = (uint8_t)declared_slots;

out:
	if (result != L850GL_L850_CA_PARSE_OK)
		memset(info, 0, sizeof(*info));
	free(copy);
	return result;
}

const char *
l850gl_l850_ca_parse_result_name(enum L850GLL850CaParseResult result)
{
	switch (result) {
	case L850GL_L850_CA_PARSE_OK: return "ok";
	case L850GL_L850_CA_PARSE_EMPTY: return "empty";
	case L850GL_L850_CA_PARSE_OVERSIZED: return "oversized";
	case L850GL_L850_CA_PARSE_MALFORMED: return "malformed";
	case L850GL_L850_CA_PARSE_SENTINEL: return "sentinel";
	case L850GL_L850_CA_PARSE_RANGE: return "range";
	case L850GL_L850_CA_PARSE_TOO_MANY: return "too_many";
	case L850GL_L850_CA_PARSE_DUPLICATE: return "duplicate";
	case L850GL_L850_CA_PARSE_COUNT_MISMATCH: return "count_mismatch";
	case L850GL_L850_CA_PARSE_MISSING_PRIMARY: return "missing_primary";
	default: return "unknown";
	}
}

const char *
l850gl_l850_ca_query_command(void)
{
	return CA_QUERY_COMMAND;
}

uint32_t
l850gl_l850_ca_retry_after_ms(int64_t now_us,
			       int64_t last_query_completed_us)
{
	const int64_t interval =
		(int64_t)L850GL_L850_CA_QUERY_COOLDOWN_SECONDS *
		MICROSECONDS_PER_SECOND;
	int64_t elapsed;
	int64_t remaining;

	if (now_us < 0 || last_query_completed_us <= 0)
		return 0U;
	if (now_us < last_query_completed_us)
		return L850GL_L850_CA_QUERY_COOLDOWN_SECONDS * 1000U;
	elapsed = now_us - last_query_completed_us;
	if (elapsed >= interval)
		return 0U;
	remaining = interval - elapsed;
	remaining = (remaining + 999) / 1000;
	return remaining > UINT32_MAX ? UINT32_MAX : (uint32_t)remaining;
}
