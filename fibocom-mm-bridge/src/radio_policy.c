/*
 * SPDX-FileCopyrightText: 2026 As Tsaqib
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "radio_policy.h"

#include <string.h>

static bool
bounded_length(const char *value, size_t maximum, size_t *length)
{
	size_t index;

	if (value == NULL || length == NULL)
		return false;
	for (index = 0U; index <= maximum; index++) {
		if (value[index] == '\0') {
			*length = index;
			return true;
		}
	}
	return false;
}

bool
fibocom_radio_band_name_is_canonical(const char *name)
{
	size_t length;
	size_t index;

	if (!bounded_length(name, FIBOCOM_RADIO_BAND_NAME_MAX, &length) ||
	    length == 0U)
		return false;
	for (index = 0U; index < length; index++) {
		const unsigned char value = (unsigned char)name[index];

		if (!((value >= (unsigned char)'a' &&
		       value <= (unsigned char)'z') ||
		      (value >= (unsigned char)'0' &&
		       value <= (unsigned char)'9') ||
		      (index > 0U && value == (unsigned char)'-')))
			return false;
	}
	return name[length - 1U] != '-';
}

static bool
is_gsm_band(const char *name)
{
	static const char *const bands[] = {
		"egsm", "dcs", "pcs", "g850", "g450", "g480", "g750",
		"g380", "g410", "g710", "g810",
	};
	size_t index;

	for (index = 0U; index < sizeof(bands) / sizeof(bands[0]); index++) {
		if (strcmp(name, bands[index]) == 0)
			return true;
	}
	return false;
}

unsigned int
fibocom_radio_band_family(const char *name)
{
	if (!fibocom_radio_band_name_is_canonical(name) ||
	    strcmp(name, "any") == 0)
		return FIBOCOM_RADIO_FAMILY_NONE;
	if (is_gsm_band(name))
		return FIBOCOM_RADIO_FAMILY_2G;
	if (strncmp(name, "utran-", sizeof("utran-") - 1U) == 0)
		return FIBOCOM_RADIO_FAMILY_3G;
	if (strncmp(name, "eutran-", sizeof("eutran-") - 1U) == 0)
		return FIBOCOM_RADIO_FAMILY_4G;
	if (strncmp(name, "ngran-", sizeof("ngran-") - 1U) == 0)
		return FIBOCOM_RADIO_FAMILY_5G;
	return FIBOCOM_RADIO_FAMILY_NONE;
}

static const struct FibocomRadioBand *
find_supported(const struct FibocomRadioBand *supported,
	       size_t supported_count, const char *name)
{
	size_t index;

	for (index = 0U; index < supported_count; index++) {
		if (supported[index].name != NULL &&
		    strcmp(supported[index].name, name) == 0)
			return &supported[index];
	}
	return NULL;
}

enum FibocomRadioPolicyResult
fibocom_radio_resolve_bands(const char *const *requested,
			    size_t requested_count,
			    const struct FibocomRadioBand *supported,
			    size_t supported_count,
			    bool current_modes_known,
			    unsigned int allowed_families,
			    unsigned int any_value,
			    unsigned int *resolved)
{
	unsigned int requested_families = FIBOCOM_RADIO_FAMILY_NONE;
	size_t index;
	size_t previous;

	if (requested == NULL || resolved == NULL || requested_count == 0U ||
	    requested_count > FIBOCOM_RADIO_REQUEST_BANDS_MAX ||
	    (supported == NULL && supported_count != 0U))
		return FIBOCOM_RADIO_POLICY_INVALID_ARGUMENT;
	if (!current_modes_known || allowed_families == FIBOCOM_RADIO_FAMILY_NONE)
		return FIBOCOM_RADIO_POLICY_MODES_UNKNOWN;
	if (supported_count == 0U)
		return FIBOCOM_RADIO_POLICY_UNSUPPORTED_BAND;

	if (requested_count == 1U && requested[0] != NULL &&
	    strcmp(requested[0], "any") == 0) {
		resolved[0] = any_value;
		return FIBOCOM_RADIO_POLICY_OK;
	}

	for (index = 0U; index < requested_count; index++) {
		const struct FibocomRadioBand *match;

		if (!fibocom_radio_band_name_is_canonical(requested[index]) ||
		    strcmp(requested[index], "any") == 0)
			return FIBOCOM_RADIO_POLICY_INVALID_ARGUMENT;
		for (previous = 0U; previous < index; previous++) {
			if (strcmp(requested[index], requested[previous]) == 0)
				return FIBOCOM_RADIO_POLICY_INVALID_ARGUMENT;
		}
		match = find_supported(supported, supported_count,
				       requested[index]);
		if (match == NULL)
			return FIBOCOM_RADIO_POLICY_UNSUPPORTED_BAND;
		if (match->family == FIBOCOM_RADIO_FAMILY_NONE ||
		    (match->family & allowed_families) == 0U)
			return FIBOCOM_RADIO_POLICY_FAMILY_MISMATCH;
		requested_families |= match->family;
		resolved[index] = match->value;
	}

	if (requested_families != allowed_families)
		return FIBOCOM_RADIO_POLICY_FAMILY_MISMATCH;
	return FIBOCOM_RADIO_POLICY_OK;
}
