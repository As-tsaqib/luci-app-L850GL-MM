/*
 * SPDX-FileCopyrightText: 2026 As Tsaqib
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef L850GL_L850_CA_H
#define L850GL_L850_CA_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define L850GL_L850_CA_MAX_RESPONSE 4096U
#define L850GL_L850_CA_MAX_SLOTS 8U
#define L850GL_L850_CA_QUERY_COOLDOWN_SECONDS 5U

enum L850GLL850CaParseResult {
	L850GL_L850_CA_PARSE_OK = 0,
	L850GL_L850_CA_PARSE_EMPTY,
	L850GL_L850_CA_PARSE_OVERSIZED,
	L850GL_L850_CA_PARSE_MALFORMED,
	L850GL_L850_CA_PARSE_SENTINEL,
	L850GL_L850_CA_PARSE_RANGE,
	L850GL_L850_CA_PARSE_TOO_MANY,
	L850GL_L850_CA_PARSE_DUPLICATE,
	L850GL_L850_CA_PARSE_COUNT_MISMATCH,
	L850GL_L850_CA_PARSE_MISSING_PRIMARY,
};

struct L850GLL850CaCarrier {
	uint8_t index;
	bool primary;
	bool has_cell_identity;
	uint16_t band;
	uint16_t mcc;
	uint16_t mnc;
	uint16_t tac;
	uint32_t cell_id;
	uint16_t pci;
	int16_t rsrp_dbm;
	int16_t rsrq_tenths_db;
	int16_t sinr_tenths_db;
	uint32_t dl_earfcn;
	uint32_t ul_earfcn;
	uint16_t dl_bandwidth_tenths_mhz;
	uint16_t ul_bandwidth_tenths_mhz;
};

struct L850GLL850CaInfo {
	struct L850GLL850CaCarrier carriers[L850GL_L850_CA_MAX_SLOTS];
	size_t length;
	uint8_t declared_slots;
};

enum L850GLL850CaParseResult l850gl_l850_ca_parse(
	const char *response, size_t response_length,
	struct L850GLL850CaInfo *info);
const char *l850gl_l850_ca_parse_result_name(
	enum L850GLL850CaParseResult result);
const char *l850gl_l850_ca_query_command(void);
uint32_t l850gl_l850_ca_retry_after_ms(int64_t now_us,
					int64_t last_query_completed_us);

#endif /* L850GL_L850_CA_H */
