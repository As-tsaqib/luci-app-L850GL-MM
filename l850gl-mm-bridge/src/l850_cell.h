/*
 * SPDX-FileCopyrightText: 2026 As Tsaqib
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef L850GL_L850_CELL_H
#define L850GL_L850_CELL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define L850GL_L850_CELL_MAX_RESPONSE 16384U
#define L850GL_L850_CELL_MAX_RESULTS 64U
#define L850GL_L850_CELL_SCAN_COOLDOWN_SECONDS 5U
#define L850GL_L850_NVM_MAX_RESPONSE 512U
#define L850GL_L850_COMMAND_MAX 96U
#define L850GL_L850_PCI_WILDCARD UINT16_MAX
#define L850GL_L850_CLEAR_FREQUENCY 65535U

enum L850GLL850CellParseResult {
	L850GL_L850_CELL_PARSE_OK = 0,
	L850GL_L850_CELL_PARSE_EMPTY,
	L850GL_L850_CELL_PARSE_OVERSIZED,
	L850GL_L850_CELL_PARSE_MALFORMED,
	L850GL_L850_CELL_PARSE_UNSUPPORTED_TYPE,
	L850GL_L850_CELL_PARSE_SENTINEL,
	L850GL_L850_CELL_PARSE_RANGE,
	L850GL_L850_CELL_PARSE_TOO_MANY,
};

enum L850GLL850State {
	L850GL_L850_STATE_AVAILABLE = 0,
	L850GL_L850_STATE_UNSUPPORTED_BUILD,
	L850GL_L850_STATE_UNSUPPORTED_PROTOCOL,
	L850GL_L850_STATE_SCAN_READY,
	L850GL_L850_STATE_LOCK_APPLIED_RESET_REQUIRED,
	L850GL_L850_STATE_RESETTING,
	L850GL_L850_STATE_APPLIED_VERIFIED,
	L850GL_L850_STATE_CLEARED_VERIFIED,
	L850GL_L850_STATE_REPROBE_TIMEOUT,
	L850GL_L850_STATE_REGISTRATION_TIMEOUT,
	L850GL_L850_STATE_VERIFICATION_MISMATCH,
	L850GL_L850_STATE_OUTCOME_UNKNOWN,
};

struct L850GLL850Cell {
	uint8_t type;
	bool serving;
	uint32_t earfcn;
	uint16_t pci;
	uint16_t band;
	int16_t rsrp_dbm;
	int16_t rsrq_tenths_db;
};

struct L850GLL850CellScan {
	struct L850GLL850Cell cells[L850GL_L850_CELL_MAX_RESULTS];
	size_t length;
};

struct L850GLL850LockState {
	bool enabled;
	bool has_pci;
	uint32_t earfcn;
	uint16_t pci;
	uint16_t band;
};

enum L850GLL850CellParseResult l850gl_l850_cell_parse(
	const char *response, size_t response_length,
	struct L850GLL850CellScan *scan);
enum L850GLL850CellParseResult l850gl_l850_nvm_parse(
	const char *response, size_t response_length,
	struct L850GLL850LockState *state);
const char *l850gl_l850_cell_parse_result_name(
	enum L850GLL850CellParseResult result);
const char *l850gl_l850_state_name(enum L850GLL850State state);
bool l850gl_l850_state_transition_is_valid(enum L850GLL850State from,
					    enum L850GLL850State to);
bool l850gl_l850_earfcn_to_band(uint32_t earfcn, uint16_t *band);
bool l850gl_l850_band_is_supported(uint16_t band,
				    const char *const *supported_bands,
				    size_t supported_band_count);
bool l850gl_l850_build_set_command(uint32_t earfcn, bool has_pci,
				    uint16_t pci, char *command,
				    size_t command_size);
const char *l850gl_l850_clear_command(void);
const char *l850gl_l850_reset_command(void);
const char *l850gl_l850_scan_command(void);
const char *l850gl_l850_nvm_query_command(void);
bool l850gl_l850_set_response_is_success(const char *response,
					  size_t response_length);
bool l850gl_l850_lock_state_matches(const struct L850GLL850LockState *state,
				     bool clear, uint32_t earfcn,
				     bool has_pci, uint16_t pci);
uint32_t l850gl_l850_scan_retry_after_ms(int64_t now_us,
					 int64_t last_scan_completed_us);

#endif /* L850GL_L850_CELL_H */
