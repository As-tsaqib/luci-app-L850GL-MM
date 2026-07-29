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
l850gl_radio_band_name_is_canonical(const char *name)
{
	size_t length;
	size_t index;

	if (!bounded_length(name, L850GL_RADIO_BAND_NAME_MAX, &length) ||
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
l850gl_radio_band_family(const char *name)
{
	if (!l850gl_radio_band_name_is_canonical(name) ||
	    strcmp(name, "any") == 0)
		return L850GL_RADIO_FAMILY_NONE;
	if (is_gsm_band(name))
		return L850GL_RADIO_FAMILY_2G;
	if (strncmp(name, "utran-", sizeof("utran-") - 1U) == 0)
		return L850GL_RADIO_FAMILY_3G;
	if (strncmp(name, "eutran-", sizeof("eutran-") - 1U) == 0)
		return L850GL_RADIO_FAMILY_4G;
	if (strncmp(name, "ngran-", sizeof("ngran-") - 1U) == 0)
		return L850GL_RADIO_FAMILY_5G;
	return L850GL_RADIO_FAMILY_NONE;
}

static const struct L850GLRadioBand *
find_supported(const struct L850GLRadioBand *supported,
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

enum L850GLRadioPolicyResult
l850gl_radio_expand_lte_selection(
	const char *const *requested, size_t requested_count,
	const struct L850GLRadioBand *supported, size_t supported_count,
	unsigned int allowed_families, const char **effective,
	size_t *effective_count)
{
	size_t count;
	size_t index;

	if (requested == NULL || requested_count == 0U ||
	    requested_count > L850GL_RADIO_REQUEST_BANDS_MAX ||
	    supported == NULL || supported_count == 0U ||
	    effective == NULL || effective_count == NULL)
		return L850GL_RADIO_POLICY_INVALID_ARGUMENT;
	if (requested_count == 1U && requested[0] != NULL &&
	    strcmp(requested[0], "any") == 0) {
		effective[0] = requested[0];
		*effective_count = 1U;
		return L850GL_RADIO_POLICY_OK;
	}
	if ((allowed_families & L850GL_RADIO_FAMILY_4G) == 0U)
		return L850GL_RADIO_POLICY_FAMILY_MISMATCH;
	for (index = 0U; index < requested_count; index++) {
		if (l850gl_radio_band_family(requested[index]) !=
		    L850GL_RADIO_FAMILY_4G)
			return L850GL_RADIO_POLICY_INVALID_ARGUMENT;
		effective[index] = requested[index];
	}
	count = requested_count;
	for (index = 0U; index < supported_count; index++) {
		if (supported[index].family == L850GL_RADIO_FAMILY_4G ||
		    supported[index].family == L850GL_RADIO_FAMILY_NONE ||
		    (supported[index].family & allowed_families) == 0U)
			continue;
		if (count >= L850GL_RADIO_REQUEST_BANDS_MAX)
			return L850GL_RADIO_POLICY_INVALID_ARGUMENT;
		effective[count++] = supported[index].name;
	}
	*effective_count = count;
	return L850GL_RADIO_POLICY_OK;
}

enum L850GLRadioPolicyResult
l850gl_radio_resolve_bands(const char *const *requested,
			    size_t requested_count,
			    const struct L850GLRadioBand *supported,
			    size_t supported_count,
			    bool current_modes_known,
			    unsigned int allowed_families,
			    unsigned int any_value,
			    unsigned int *resolved)
{
	unsigned int requested_families = L850GL_RADIO_FAMILY_NONE;
	size_t index;
	size_t previous;

	if (requested == NULL || resolved == NULL || requested_count == 0U ||
	    requested_count > L850GL_RADIO_REQUEST_BANDS_MAX ||
	    (supported == NULL && supported_count != 0U))
		return L850GL_RADIO_POLICY_INVALID_ARGUMENT;
	if (!current_modes_known || allowed_families == L850GL_RADIO_FAMILY_NONE)
		return L850GL_RADIO_POLICY_MODES_UNKNOWN;
	if (supported_count == 0U)
		return L850GL_RADIO_POLICY_UNSUPPORTED_BAND;

	if (requested_count == 1U && requested[0] != NULL &&
	    strcmp(requested[0], "any") == 0) {
		resolved[0] = any_value;
		return L850GL_RADIO_POLICY_OK;
	}

	for (index = 0U; index < requested_count; index++) {
		const struct L850GLRadioBand *match;

		if (!l850gl_radio_band_name_is_canonical(requested[index]) ||
		    strcmp(requested[index], "any") == 0)
			return L850GL_RADIO_POLICY_INVALID_ARGUMENT;
		for (previous = 0U; previous < index; previous++) {
			if (strcmp(requested[index], requested[previous]) == 0)
				return L850GL_RADIO_POLICY_INVALID_ARGUMENT;
		}
		match = find_supported(supported, supported_count,
				       requested[index]);
		if (match == NULL)
			return L850GL_RADIO_POLICY_UNSUPPORTED_BAND;
		if (match->family == L850GL_RADIO_FAMILY_NONE ||
		    (match->family & allowed_families) == 0U)
			return L850GL_RADIO_POLICY_FAMILY_MISMATCH;
		requested_families |= match->family;
		resolved[index] = match->value;
	}

	if (requested_families != allowed_families)
		return L850GL_RADIO_POLICY_FAMILY_MISMATCH;
	return L850GL_RADIO_POLICY_OK;
}
