/*
 * SPDX-FileCopyrightText: 2026 As Tsaqib
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef FIBOCOM_RADIO_POLICY_H
#define FIBOCOM_RADIO_POLICY_H

#include <stdbool.h>
#include <stddef.h>

#define FIBOCOM_RADIO_BAND_NAME_MAX 48U
#define FIBOCOM_RADIO_REQUEST_BANDS_MAX 64U

enum FibocomRadioFamily {
	FIBOCOM_RADIO_FAMILY_NONE = 0U,
	FIBOCOM_RADIO_FAMILY_2G = 1U << 0U,
	FIBOCOM_RADIO_FAMILY_3G = 1U << 1U,
	FIBOCOM_RADIO_FAMILY_4G = 1U << 2U,
	FIBOCOM_RADIO_FAMILY_5G = 1U << 3U,
};

enum FibocomRadioPolicyResult {
	FIBOCOM_RADIO_POLICY_OK = 0,
	FIBOCOM_RADIO_POLICY_INVALID_ARGUMENT,
	FIBOCOM_RADIO_POLICY_UNSUPPORTED_BAND,
	FIBOCOM_RADIO_POLICY_MODES_UNKNOWN,
	FIBOCOM_RADIO_POLICY_FAMILY_MISMATCH,
};

struct FibocomRadioBand {
	const char *name;
	unsigned int value;
	unsigned int family;
};

bool fibocom_radio_band_name_is_canonical(const char *name);
unsigned int fibocom_radio_band_family(const char *name);

/*
 * Schema 3 exposes LTE band choices only. Preserve every advertised band from
 * the other currently allowed families so the later full-family validator and
 * XMM plugin still receive a complete request.
 */
enum FibocomRadioPolicyResult fibocom_radio_expand_lte_selection(
	const char *const *requested, size_t requested_count,
	const struct FibocomRadioBand *supported, size_t supported_count,
	unsigned int allowed_families, const char **effective,
	size_t *effective_count);

/*
 * Resolve an RPC band selection only through the current supported set.
 * The sole special request "any" resolves to any_value and is intentionally
 * not required to appear in the advertised set: SetCurrentBands(ANY) is the
 * standard ModemManager operation for restoring automatic selection.
 *
 * Explicit selections must contain exactly the same cellular families as the
 * current allowed-mode mask. This mirrors the XMM plugin's requirement that
 * every currently allowed family receives at least one band and that no band
 * is supplied for a currently disabled family.
 */
enum FibocomRadioPolicyResult fibocom_radio_resolve_bands(
	const char *const *requested, size_t requested_count,
	const struct FibocomRadioBand *supported, size_t supported_count,
	bool current_modes_known, unsigned int allowed_families,
	unsigned int any_value, unsigned int *resolved);

#endif /* FIBOCOM_RADIO_POLICY_H */
