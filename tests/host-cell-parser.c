/*
 * SPDX-FileCopyrightText: 2026 As Tsaqib
 * SPDX-License-Identifier: Apache-2.0
 */

#include "l850_cell.h"

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

static enum L850GLL850CellParseResult
parse_fixture(const char *directory, const char *name,
	      struct L850GLL850CellScan *scan)
{
	size_t length;
	char *data = read_fixture(directory, name, &length);
	enum L850GLL850CellParseResult result =
		l850gl_l850_cell_parse(data, length, scan);

	free(data);
	return result;
}

static enum L850GLL850CellParseResult
parse_nvm_fixture(const char *directory, const char *name,
		  struct L850GLL850LockState *state)
{
	size_t length;
	char *data = read_fixture(directory, name, &length);
	enum L850GLL850CellParseResult result =
		l850gl_l850_nvm_parse(data, length, state);

	free(data);
	return result;
}

int
main(int argc, char **argv)
{
	struct L850GLL850CellScan scan;
	struct L850GLL850LockState lock_state;
	const char *supported[] = { "eutran-1", "eutran-3", "eutran-66" };
	const char success[] =
		"Frequency Lock Configuration Success CPS_MSG_TYPE_ASM_EM_CTRL_CNF";
	char command[L850GL_L850_COMMAND_MAX];
	char short_command[8];
	char *oversized;
	uint16_t band = 0U;
	size_t index;
	const struct {
		const char *name;
		enum L850GLL850CellParseResult expected;
	} invalid[] = {
		{ "invalid-type-6.txt", L850GL_L850_CELL_PARSE_UNSUPPORTED_TYPE },
		{ "invalid-sentinel.txt", L850GL_L850_CELL_PARSE_SENTINEL },
		{ "invalid-malformed.txt", L850GL_L850_CELL_PARSE_MALFORMED },
		{ "invalid-pci-504.txt", L850GL_L850_CELL_PARSE_RANGE },
		{ "invalid-earfcn.txt", L850GL_L850_CELL_PARSE_RANGE },
		{ "invalid-encoding.txt", L850GL_L850_CELL_PARSE_MALFORMED },
		{ "invalid-overflow.txt", L850GL_L850_CELL_PARSE_MALFORMED },
		{ "invalid-required-sentinel.txt", L850GL_L850_CELL_PARSE_SENTINEL },
		{ "invalid-data-after-ok.txt", L850GL_L850_CELL_PARSE_MALFORMED },
	};

	assert(argc == 2);
	assert(parse_fixture(argv[1], "valid-serving-neighbor.txt", &scan) ==
		L850GL_L850_CELL_PARSE_OK);
	assert(scan.length == 2U);
	assert(scan.cells[0].type == 4U && scan.cells[0].serving);
	assert(scan.cells[0].pci == 0U);
	assert(scan.cells[0].earfcn == 1650U && scan.cells[0].band == 3U);
	assert(scan.cells[0].rsrp_dbm == -90);
	assert(scan.cells[0].rsrq_tenths_db == -100);
	assert(scan.cells[1].type == 5U && !scan.cells[1].serving);
	assert(scan.cells[1].pci == 503U);
	assert(parse_fixture(argv[1], "valid-hex-pci-zero.txt", &scan) ==
		L850GL_L850_CELL_PARSE_OK);
	assert(scan.length == 1U && scan.cells[0].pci == 0U);
	assert(parse_fixture(argv[1], "valid-live-shape-no-ok.txt", &scan) ==
		L850GL_L850_CELL_PARSE_OK);
	assert(scan.length == 2U);
	assert(scan.cells[0].serving && scan.cells[0].earfcn == 1325U &&
		scan.cells[0].pci == 381U && scan.cells[0].band == 3U);
	assert(scan.cells[0].rsrp_dbm == -82 &&
		scan.cells[0].rsrq_tenths_db == -120);
	assert(!scan.cells[1].serving && scan.cells[1].earfcn == 325U &&
		scan.cells[1].pci == 0U && scan.cells[1].band == 1U);
	for (index = 0U; index < sizeof(invalid) / sizeof(invalid[0]); index++) {
		assert(parse_fixture(argv[1], invalid[index].name, &scan) ==
			invalid[index].expected);
		assert(scan.length == 0U);
	}
	assert(l850gl_l850_cell_parse("", 0U, &scan) ==
		L850GL_L850_CELL_PARSE_EMPTY);
	oversized = malloc(L850GL_L850_CELL_MAX_RESPONSE + 2U);
	assert(oversized != NULL);
	memset(oversized, '0', L850GL_L850_CELL_MAX_RESPONSE + 1U);
	assert(l850gl_l850_cell_parse(oversized,
		L850GL_L850_CELL_MAX_RESPONSE + 1U, &scan) ==
		L850GL_L850_CELL_PARSE_OVERSIZED);
	free(oversized);

	assert(l850gl_l850_earfcn_to_band(0U, &band) && band == 1U);
	assert(l850gl_l850_earfcn_to_band(1650U, &band) && band == 3U);
	assert(l850gl_l850_earfcn_to_band(66436U, &band) && band == 66U);
	assert(!l850gl_l850_earfcn_to_band(999999U, &band));
	assert(l850gl_l850_band_is_supported(3U, supported, 3U));
	assert(!l850gl_l850_band_is_supported(8U, supported, 3U));
	assert(l850gl_l850_firmware_is_allowed("18500.5001.00.05.27.30"));
	assert(!l850gl_l850_firmware_is_allowed("18500.5001.00.05.27.29"));
	assert(!l850gl_l850_firmware_is_allowed(
		"18500.5001.00.05.27.30 "));
	assert(!l850gl_l850_firmware_is_allowed(NULL));

	assert(parse_nvm_fixture(argv[1], "nvm-clear.txt", &lock_state) ==
		L850GL_L850_CELL_PARSE_OK);
	assert(!lock_state.enabled);
	assert(l850gl_l850_lock_state_matches(&lock_state, true, 0U,
		false, 0U));
	assert(parse_nvm_fixture(argv[1], "nvm-locked-exact.txt",
		&lock_state) == L850GL_L850_CELL_PARSE_OK);
	assert(lock_state.enabled && lock_state.has_pci &&
		lock_state.earfcn == 1325U && lock_state.pci == 381U &&
		lock_state.band == 3U);
	assert(l850gl_l850_lock_state_matches(&lock_state, false, 1325U,
		true, 381U));
	assert(!l850gl_l850_lock_state_matches(&lock_state, false, 1325U,
		true, 0U));
	assert(parse_nvm_fixture(argv[1], "nvm-locked-earfcn.txt",
		&lock_state) == L850GL_L850_CELL_PARSE_OK);
	assert(lock_state.enabled && !lock_state.has_pci &&
		lock_state.earfcn == 325U && lock_state.pci == UINT16_MAX &&
		lock_state.band == 1U);
	assert(parse_nvm_fixture(argv[1], "nvm-invalid-extra-field.txt",
		&lock_state) == L850GL_L850_CELL_PARSE_MALFORMED);
	assert(parse_nvm_fixture(argv[1], "nvm-invalid-inconsistent-clear.txt",
		&lock_state) == L850GL_L850_CELL_PARSE_MALFORMED);
	assert(l850gl_l850_nvm_parse("", 0U, &lock_state) ==
		L850GL_L850_CELL_PARSE_EMPTY);

	assert(l850gl_l850_build_set_command(1325U, true, 381U, command,
		sizeof(command)));
	assert(strcmp(command, "AT@SIC:FREQ_LOCK(0,3,3,1,1325,381)") == 0);
	assert(l850gl_l850_build_set_command(325U, false, 0U, command,
		sizeof(command)));
	assert(strcmp(command, "AT@SIC:FREQ_LOCK(0,3,1,1,325,65535)") == 0);
	assert(!l850gl_l850_build_set_command(999999U, false, 0U, command,
		sizeof(command)));
	assert(!l850gl_l850_build_set_command(325U, true, 504U, command,
		sizeof(command)));
	assert(!l850gl_l850_build_set_command(325U, true, 0U, short_command,
		sizeof(short_command)));
	assert(strcmp(l850gl_l850_clear_command(),
		"AT@SIC:FREQ_LOCK(0,3,255,0,65535,65535)") == 0);
	assert(strcmp(l850gl_l850_reset_command(), "AT+CFUN=15") == 0);
	assert(strcmp(l850gl_l850_scan_command(), "AT+XMCI=1") == 0);
	assert(strcmp(l850gl_l850_nvm_query_command(),
		"AT@NVM:DYN_CPS.NAS_ASM.FREQ_LOCK_PARAMS.*??") == 0);
	assert(l850gl_l850_set_response_is_success(success,
		strlen(success)));
	assert(!l850gl_l850_set_response_is_success("OK", 2U));
	assert(!l850gl_l850_set_response_is_success(success,
		strlen(success) - 1U));

	assert(l850gl_l850_state_transition_is_valid(
		L850GL_L850_STATE_AVAILABLE, L850GL_L850_STATE_SCAN_READY));
	assert(l850gl_l850_state_transition_is_valid(
		L850GL_L850_STATE_SCAN_READY,
		L850GL_L850_STATE_LOCK_APPLIED_RESET_REQUIRED));
	assert(l850gl_l850_state_transition_is_valid(
		L850GL_L850_STATE_RESETTING,
		L850GL_L850_STATE_APPLIED_VERIFIED));
	assert(!l850gl_l850_state_transition_is_valid(
		L850GL_L850_STATE_AVAILABLE,
		L850GL_L850_STATE_APPLIED_VERIFIED));
	assert(L850GL_L850_CELL_SCAN_COOLDOWN_SECONDS == 5U);
	assert(l850gl_l850_scan_retry_after_ms(1000000, 0) == 0U);
	assert(l850gl_l850_scan_retry_after_ms(1000000, 1000000) == 5000U);
	assert(l850gl_l850_scan_retry_after_ms(1000001, 1000000) == 5000U);
	assert(l850gl_l850_scan_retry_after_ms(1001000, 1000000) == 4999U);
	assert(l850gl_l850_scan_retry_after_ms(5999999, 1000000) == 1U);
	assert(l850gl_l850_scan_retry_after_ms(6000000, 1000000) == 0U);
	assert(l850gl_l850_scan_retry_after_ms(999999, 1000000) == 5000U);
	assert(l850gl_l850_scan_retry_after_ms(INT64_MAX,
		INT64_MAX - INT64_C(1000)) == 4999U);

	puts("L850 cell parser tests passed");
	return 0;
}
