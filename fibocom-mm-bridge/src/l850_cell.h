/*
 * SPDX-FileCopyrightText: 2026 As Tsaqib
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef FIBOCOM_L850_CELL_H
#define FIBOCOM_L850_CELL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define FIBOCOM_L850_CELL_MAX_RESPONSE 16384U
#define FIBOCOM_L850_CELL_MAX_RESULTS 64U
#define FIBOCOM_L850_CELL_SCAN_INTERVAL_SECONDS 60U
#define FIBOCOM_L850_NVM_MAX_RESPONSE 512U
#define FIBOCOM_L850_COMMAND_MAX 96U
#define FIBOCOM_L850_PCI_WILDCARD UINT16_MAX
#define FIBOCOM_L850_CLEAR_FREQUENCY 65535U
#define FIBOCOM_L850_ALLOWED_FIRMWARE "18500.5001.00.05.27.30"

enum FibocomL850CellParseResult {
	FIBOCOM_L850_CELL_PARSE_OK = 0,
	FIBOCOM_L850_CELL_PARSE_EMPTY,
	FIBOCOM_L850_CELL_PARSE_OVERSIZED,
	FIBOCOM_L850_CELL_PARSE_MALFORMED,
	FIBOCOM_L850_CELL_PARSE_UNSUPPORTED_TYPE,
	FIBOCOM_L850_CELL_PARSE_SENTINEL,
	FIBOCOM_L850_CELL_PARSE_RANGE,
	FIBOCOM_L850_CELL_PARSE_TOO_MANY,
};

enum FibocomL850State {
	FIBOCOM_L850_STATE_AVAILABLE = 0,
	FIBOCOM_L850_STATE_UNSUPPORTED_BUILD,
	FIBOCOM_L850_STATE_UNSUPPORTED_FIRMWARE,
	FIBOCOM_L850_STATE_SCAN_READY,
	FIBOCOM_L850_STATE_LOCK_APPLIED_RESET_REQUIRED,
	FIBOCOM_L850_STATE_RESETTING,
	FIBOCOM_L850_STATE_APPLIED_VERIFIED,
	FIBOCOM_L850_STATE_CLEARED_VERIFIED,
	FIBOCOM_L850_STATE_REPROBE_TIMEOUT,
	FIBOCOM_L850_STATE_REGISTRATION_TIMEOUT,
	FIBOCOM_L850_STATE_VERIFICATION_MISMATCH,
	FIBOCOM_L850_STATE_OUTCOME_UNKNOWN,
};

struct FibocomL850Cell {
	uint8_t type;
	bool serving;
	uint32_t earfcn;
	uint16_t pci;
	uint16_t band;
	int16_t rsrp_dbm;
	int16_t rsrq_tenths_db;
};

struct FibocomL850CellScan {
	struct FibocomL850Cell cells[FIBOCOM_L850_CELL_MAX_RESULTS];
	size_t length;
};

struct FibocomL850LockState {
	bool enabled;
	bool has_pci;
	uint32_t earfcn;
	uint16_t pci;
	uint16_t band;
};

enum FibocomL850CellParseResult fibocom_l850_cell_parse(
	const char *response, size_t response_length,
	struct FibocomL850CellScan *scan);
enum FibocomL850CellParseResult fibocom_l850_nvm_parse(
	const char *response, size_t response_length,
	struct FibocomL850LockState *state);
const char *fibocom_l850_cell_parse_result_name(
	enum FibocomL850CellParseResult result);
const char *fibocom_l850_state_name(enum FibocomL850State state);
bool fibocom_l850_state_transition_is_valid(enum FibocomL850State from,
					    enum FibocomL850State to);
bool fibocom_l850_earfcn_to_band(uint32_t earfcn, uint16_t *band);
bool fibocom_l850_band_is_supported(uint16_t band,
				    const char *const *supported_bands,
				    size_t supported_band_count);
bool fibocom_l850_firmware_is_allowed(const char *revision);
bool fibocom_l850_build_set_command(uint32_t earfcn, bool has_pci,
				    uint16_t pci, char *command,
				    size_t command_size);
const char *fibocom_l850_clear_command(void);
const char *fibocom_l850_reset_command(void);
const char *fibocom_l850_scan_command(void);
const char *fibocom_l850_nvm_query_command(void);
bool fibocom_l850_set_response_is_success(const char *response,
					  size_t response_length);
bool fibocom_l850_lock_state_matches(const struct FibocomL850LockState *state,
				     bool clear, uint32_t earfcn,
				     bool has_pci, uint16_t pci);
uint32_t fibocom_l850_scan_retry_after_ms(int64_t now_us,
					 int64_t last_scan_us);

#endif /* FIBOCOM_L850_CELL_H */
