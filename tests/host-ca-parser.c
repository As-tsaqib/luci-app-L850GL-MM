/*
 * SPDX-FileCopyrightText: 2026 As Tsaqib
 * SPDX-License-Identifier: Apache-2.0
 */

#include "l850_ca.h"

#include <assert.h>
#include <limits.h>
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

static enum L850GLL850CaParseResult
parse_fixture(const char *directory, const char *name,
	      struct L850GLL850CaInfo *info)
{
	size_t length;
	char *data = read_fixture(directory, name, &length);
	enum L850GLL850CaParseResult result =
		l850gl_l850_ca_parse(data, length, info);

	free(data);
	return result;
}

int
main(int argc, char **argv)
{
	struct L850GLL850CaInfo info;
	char *oversized;
	const char embedded_nul[] = {
		'+', 'G', 'T', 'C', 'A', 'I', 'N', 'F', 'O', ':', ' ', '1',
		'\0', '\n'
	};
	const char valid_crlf[] =
		"\r\n+GTCAINFO: 1\r\n"
		"+GTCAINFO: 1,3,1,1,1,1,0,45,9,-4,"
		"1325,19325,0,1\r\n";
	const char invalid_hex[] =
		"+GTCAINFO: 1\n"
		"+GTCAINFO: 1,3,1,1,1,0x1,381,45,9,4,"
		"1325,19325,5,5\n";
	const char invalid_unsigned_sign[] =
		"+GTCAINFO: 1\n"
		"+GTCAINFO: 1,3,1,1,-1,1,381,45,9,4,"
		"1325,19325,5,5\n";
	const char duplicate_header[] =
		"+GTCAINFO: 1\n+GTCAINFO: 1\n";
	const struct {
		const char *name;
		enum L850GLL850CaParseResult expected;
	} invalid[] = {
		{ "invalid-band-mismatch.txt", L850GL_L850_CA_PARSE_RANGE },
		{ "invalid-ul-band-mismatch.txt", L850GL_L850_CA_PARSE_RANGE },
		{ "invalid-pci-504.txt", L850GL_L850_CA_PARSE_RANGE },
		{ "invalid-bandwidth.txt", L850GL_L850_CA_PARSE_RANGE },
		{ "invalid-inactive-sentinel.txt", L850GL_L850_CA_PARSE_SENTINEL },
		{ "invalid-inactive-earfcn.txt", L850GL_L850_CA_PARSE_SENTINEL },
		{ "invalid-duplicate-index.txt", L850GL_L850_CA_PARSE_DUPLICATE },
		{ "invalid-duplicate-carrier.txt", L850GL_L850_CA_PARSE_DUPLICATE },
		{ "invalid-count-mismatch.txt", L850GL_L850_CA_PARSE_COUNT_MISMATCH },
		{ "invalid-missing-primary.txt", L850GL_L850_CA_PARSE_MISSING_PRIMARY },
		{ "invalid-secondary-index-one.txt", L850GL_L850_CA_PARSE_RANGE },
		{ "invalid-trailing-ok.txt", L850GL_L850_CA_PARSE_MALFORMED },
		{ "invalid-active-sentinel.txt", L850GL_L850_CA_PARSE_SENTINEL },
		{ "invalid-active-secondary-ul-sentinel.txt",
			L850GL_L850_CA_PARSE_SENTINEL },
		{ "invalid-field-count.txt", L850GL_L850_CA_PARSE_MALFORMED },
	};
	size_t index;

	assert(argc == 2);
	assert(parse_fixture(argv[1], "valid-live-l850.txt", &info) ==
		L850GL_L850_CA_PARSE_OK);
	assert(info.declared_slots == 2U);
	assert(info.length == 1U);
	assert(info.carriers[0].index == 1U && info.carriers[0].primary &&
		info.carriers[0].has_cell_identity);
	assert(info.carriers[0].band == 3U);
	assert(info.carriers[0].mcc == 1U && info.carriers[0].mnc == 1U);
	assert(info.carriers[0].tac == 1U);
	assert(info.carriers[0].cell_id == 1U);
	assert(info.carriers[0].pci == 381U);
	assert(info.carriers[0].rsrp_dbm == -96);
	assert(info.carriers[0].rsrq_tenths_db == -150);
	assert(info.carriers[0].sinr_tenths_db == 20);
	assert(info.carriers[0].dl_earfcn == 1325U);
	assert(info.carriers[0].ul_earfcn == 19325U);
	assert(info.carriers[0].dl_bandwidth_tenths_mhz == 200U);
	assert(info.carriers[0].ul_bandwidth_tenths_mhz == 200U);

	assert(parse_fixture(argv[1], "valid-rooter-secondary-shape.txt", &info) ==
		L850GL_L850_CA_PARSE_OK);
	assert(info.declared_slots == 3U && info.length == 2U);
	assert(info.carriers[0].index == 1U && info.carriers[0].primary &&
		info.carriers[0].band == 3U);
	assert(info.carriers[1].index == 2U && !info.carriers[1].primary &&
		!info.carriers[1].has_cell_identity && info.carriers[1].band == 1U);
	assert(info.carriers[1].mcc == 0U && info.carriers[1].mnc == 0U &&
		info.carriers[1].tac == 0U && info.carriers[1].cell_id == 0U);
	assert(info.carriers[1].dl_earfcn == 325U &&
		info.carriers[1].ul_earfcn == 18325U);
	assert(info.carriers[1].dl_bandwidth_tenths_mhz == 100U);

	assert(parse_fixture(argv[1], "valid-max-eight.txt", &info) ==
		L850GL_L850_CA_PARSE_OK);
	assert(info.declared_slots == L850GL_L850_CA_MAX_SLOTS);
	assert(info.length == L850GL_L850_CA_MAX_SLOTS);
	assert(info.length <= L850GL_L850_CA_MAX_SLOTS);
	assert(info.carriers[1].index == 2U &&
		info.carriers[1].pci == 0U && info.carriers[1].rsrp_dbm == -141 &&
		info.carriers[1].rsrq_tenths_db == -195 &&
		info.carriers[1].sinr_tenths_db == -500 &&
		info.carriers[1].dl_bandwidth_tenths_mhz == 14U &&
		info.carriers[1].ul_bandwidth_tenths_mhz == 14U);
	assert(info.carriers[7].index == 8U &&
		info.carriers[7].pci == 503U && info.carriers[7].rsrp_dbm == -44 &&
		info.carriers[7].rsrq_tenths_db == -25 &&
		info.carriers[7].sinr_tenths_db == 500 &&
		info.carriers[7].dl_bandwidth_tenths_mhz == 200U &&
		info.carriers[7].ul_bandwidth_tenths_mhz == 200U);

	assert(l850gl_l850_ca_parse(valid_crlf, strlen(valid_crlf), &info) ==
		L850GL_L850_CA_PARSE_OK);
	assert(info.length == 1U && info.carriers[0].pci == 0U);
	assert(info.carriers[0].sinr_tenths_db == -20);
	assert(info.carriers[0].dl_bandwidth_tenths_mhz == 14U);
	assert(info.carriers[0].ul_bandwidth_tenths_mhz == 30U);

	for (index = 0U; index < sizeof(invalid) / sizeof(invalid[0]); index++) {
		assert(parse_fixture(argv[1], invalid[index].name, &info) ==
			invalid[index].expected);
		assert(info.length == 0U && info.declared_slots == 0U);
	}
	memset(&info, 0xa5, sizeof(info));
	assert(l850gl_l850_ca_parse("", 0U, &info) ==
		L850GL_L850_CA_PARSE_EMPTY);
	assert(info.length == 0U && info.declared_slots == 0U);
	memset(&info, 0xa5, sizeof(info));
	assert(l850gl_l850_ca_parse(NULL, 1U, &info) ==
		L850GL_L850_CA_PARSE_EMPTY);
	assert(info.length == 0U && info.declared_slots == 0U);
	assert(l850gl_l850_ca_parse("x", 1U, NULL) ==
		L850GL_L850_CA_PARSE_MALFORMED);
	assert(l850gl_l850_ca_parse(" \r\n\t", strlen(" \r\n\t"), &info) ==
		L850GL_L850_CA_PARSE_EMPTY);
	assert(l850gl_l850_ca_parse("+GTCAINFO: 9\n",
		strlen("+GTCAINFO: 9\n"), &info) ==
		L850GL_L850_CA_PARSE_TOO_MANY);
	assert(l850gl_l850_ca_parse(embedded_nul, sizeof(embedded_nul), &info) ==
		L850GL_L850_CA_PARSE_MALFORMED);
	assert(l850gl_l850_ca_parse(invalid_hex, strlen(invalid_hex), &info) ==
		L850GL_L850_CA_PARSE_MALFORMED);
	assert(l850gl_l850_ca_parse(invalid_unsigned_sign,
		strlen(invalid_unsigned_sign), &info) ==
		L850GL_L850_CA_PARSE_MALFORMED);
	assert(l850gl_l850_ca_parse(duplicate_header,
		strlen(duplicate_header), &info) ==
		L850GL_L850_CA_PARSE_MALFORMED);
	oversized = malloc(L850GL_L850_CA_MAX_RESPONSE + 2U);
	assert(oversized != NULL);
	memset(oversized, '0', L850GL_L850_CA_MAX_RESPONSE + 1U);
	memset(&info, 0xa5, sizeof(info));
	assert(l850gl_l850_ca_parse(oversized,
		L850GL_L850_CA_MAX_RESPONSE + 1U, &info) ==
		L850GL_L850_CA_PARSE_OVERSIZED);
	assert(info.length == 0U && info.declared_slots == 0U);
	free(oversized);

	assert(strcmp(l850gl_l850_ca_query_command(), "AT+GTCAINFO?") == 0);
	assert(strcmp(l850gl_l850_ca_parse_result_name(
		L850GL_L850_CA_PARSE_COUNT_MISMATCH), "count_mismatch") == 0);
	assert(strcmp(l850gl_l850_ca_parse_result_name(
		(enum L850GLL850CaParseResult)999), "unknown") == 0);
	assert(L850GL_L850_CA_QUERY_COOLDOWN_SECONDS == 5U);
	assert(l850gl_l850_ca_retry_after_ms(1000000, 0) == 0U);
	assert(l850gl_l850_ca_retry_after_ms(1000000, 1000000) == 5000U);
	assert(l850gl_l850_ca_retry_after_ms(1000001, 1000000) == 5000U);
	assert(l850gl_l850_ca_retry_after_ms(1001000, 1000000) == 4999U);
	assert(l850gl_l850_ca_retry_after_ms(5999999, 1000000) == 1U);
	assert(l850gl_l850_ca_retry_after_ms(6000000, 1000000) == 0U);
	assert(l850gl_l850_ca_retry_after_ms(999999, 1000000) == 5000U);
	assert(l850gl_l850_ca_retry_after_ms(INT64_MAX,
		INT64_MAX - INT64_C(1000)) == 4999U);

	puts("L850 carrier aggregation parser tests passed");
	return 0;
}
