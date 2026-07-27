/*
 * SPDX-FileCopyrightText: 2026 As Tsaqib
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "radio_policy.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static const struct FibocomRadioBand supported[] = {
	{ "egsm", 1U, FIBOCOM_RADIO_FAMILY_2G },
	{ "utran-1", 5U, FIBOCOM_RADIO_FAMILY_3G },
	{ "utran-8", 10U, FIBOCOM_RADIO_FAMILY_3G },
	{ "eutran-1", 31U, FIBOCOM_RADIO_FAMILY_4G },
	{ "eutran-3", 33U, FIBOCOM_RADIO_FAMILY_4G },
};

static void
test_names(void)
{
	assert(fibocom_radio_band_name_is_canonical("eutran-3"));
	assert(fibocom_radio_band_name_is_canonical("egsm"));
	assert(!fibocom_radio_band_name_is_canonical(""));
	assert(!fibocom_radio_band_name_is_canonical("EUTRAN-3"));
	assert(!fibocom_radio_band_name_is_canonical("-eutran-3"));
	assert(!fibocom_radio_band_name_is_canonical("eutran-3-"));
	assert(!fibocom_radio_band_name_is_canonical("eutran 3"));
	assert(fibocom_radio_band_family("egsm") ==
	       FIBOCOM_RADIO_FAMILY_2G);
	assert(fibocom_radio_band_family("utran-8") ==
	       FIBOCOM_RADIO_FAMILY_3G);
	assert(fibocom_radio_band_family("eutran-3") ==
	       FIBOCOM_RADIO_FAMILY_4G);
	assert(fibocom_radio_band_family("ngran-78") ==
	       FIBOCOM_RADIO_FAMILY_5G);
	assert(fibocom_radio_band_family("cdma-bc0") ==
	       FIBOCOM_RADIO_FAMILY_NONE);
}

static void
test_resolution(void)
{
	static const char *const automatic[] = { "any" };
	static const char *const valid[] = {
		"utran-8", "eutran-1", "eutran-3",
	};
	static const char *const missing_family[] = { "eutran-3" };
	static const char *const disabled_family[] = {
		"egsm", "utran-8", "eutran-3",
	};
	static const char *const duplicate[] = {
		"utran-8", "eutran-3", "eutran-3",
	};
	static const char *const unsupported[] = {
		"utran-8", "eutran-8",
	};
	static const char *const mixed_any[] = { "any", "eutran-3" };
	unsigned int resolved[FIBOCOM_RADIO_REQUEST_BANDS_MAX] = { 0U };
	const unsigned int modes = FIBOCOM_RADIO_FAMILY_3G |
		FIBOCOM_RADIO_FAMILY_4G;

	assert(fibocom_radio_resolve_bands(
		automatic, 1U, supported,
		sizeof(supported) / sizeof(supported[0]), true, modes, 256U,
		resolved) == FIBOCOM_RADIO_POLICY_OK);
	assert(resolved[0] == 256U);
	assert(fibocom_radio_resolve_bands(
		automatic, 1U, NULL, 0U, true, modes, 256U, resolved) ==
	       FIBOCOM_RADIO_POLICY_UNSUPPORTED_BAND);
	assert(fibocom_radio_resolve_bands(
		valid, 3U, supported, sizeof(supported) / sizeof(supported[0]),
		true, modes, 256U, resolved) == FIBOCOM_RADIO_POLICY_OK);
	assert(resolved[0] == 10U && resolved[1] == 31U &&
	       resolved[2] == 33U);
	assert(fibocom_radio_resolve_bands(
		missing_family, 1U, supported,
		sizeof(supported) / sizeof(supported[0]), true, modes, 256U,
		resolved) == FIBOCOM_RADIO_POLICY_FAMILY_MISMATCH);
	assert(fibocom_radio_resolve_bands(
		disabled_family, 3U, supported,
		sizeof(supported) / sizeof(supported[0]), true, modes, 256U,
		resolved) == FIBOCOM_RADIO_POLICY_FAMILY_MISMATCH);
	assert(fibocom_radio_resolve_bands(
		duplicate, 3U, supported,
		sizeof(supported) / sizeof(supported[0]), true, modes, 256U,
		resolved) == FIBOCOM_RADIO_POLICY_INVALID_ARGUMENT);
	assert(fibocom_radio_resolve_bands(
		unsupported, 2U, supported,
		sizeof(supported) / sizeof(supported[0]), true, modes, 256U,
		resolved) == FIBOCOM_RADIO_POLICY_UNSUPPORTED_BAND);
	assert(fibocom_radio_resolve_bands(
		mixed_any, 2U, supported,
		sizeof(supported) / sizeof(supported[0]), true, modes, 256U,
		resolved) == FIBOCOM_RADIO_POLICY_INVALID_ARGUMENT);
	assert(fibocom_radio_resolve_bands(
		valid, 3U, supported, sizeof(supported) / sizeof(supported[0]),
		false, modes, 256U, resolved) ==
	       FIBOCOM_RADIO_POLICY_MODES_UNKNOWN);
}

static void
test_lte_only_expansion(void)
{
	static const char *const automatic[] = { "any" };
	static const char *const lte[] = { "eutran-3" };
	static const char *const non_lte[] = { "utran-8" };
	const char *effective[FIBOCOM_RADIO_REQUEST_BANDS_MAX] = { NULL };
	unsigned int resolved[FIBOCOM_RADIO_REQUEST_BANDS_MAX] = { 0U };
	size_t count = 0U;
	const unsigned int combined = FIBOCOM_RADIO_FAMILY_3G |
		FIBOCOM_RADIO_FAMILY_4G;

	assert(fibocom_radio_expand_lte_selection(
		lte, 1U, supported, sizeof(supported) / sizeof(supported[0]),
		combined, effective, &count) == FIBOCOM_RADIO_POLICY_OK);
	assert(count == 3U);
	assert(strcmp(effective[0], "eutran-3") == 0);
	assert(strcmp(effective[1], "utran-1") == 0);
	assert(strcmp(effective[2], "utran-8") == 0);
	assert(fibocom_radio_resolve_bands(effective, count, supported,
		sizeof(supported) / sizeof(supported[0]), true, combined, 256U,
		resolved) == FIBOCOM_RADIO_POLICY_OK);

	assert(fibocom_radio_expand_lte_selection(
		lte, 1U, supported, sizeof(supported) / sizeof(supported[0]),
		FIBOCOM_RADIO_FAMILY_4G, effective, &count) ==
		FIBOCOM_RADIO_POLICY_OK);
	assert(count == 1U && strcmp(effective[0], "eutran-3") == 0);
	assert(fibocom_radio_expand_lte_selection(
		automatic, 1U, supported,
		sizeof(supported) / sizeof(supported[0]), combined,
		effective, &count) == FIBOCOM_RADIO_POLICY_OK);
	assert(count == 1U && strcmp(effective[0], "any") == 0);
	assert(fibocom_radio_expand_lte_selection(
		non_lte, 1U, supported,
		sizeof(supported) / sizeof(supported[0]), combined,
		effective, &count) == FIBOCOM_RADIO_POLICY_INVALID_ARGUMENT);
	assert(fibocom_radio_expand_lte_selection(
		lte, 1U, supported, sizeof(supported) / sizeof(supported[0]),
		FIBOCOM_RADIO_FAMILY_3G, effective, &count) ==
		FIBOCOM_RADIO_POLICY_FAMILY_MISMATCH);
}

int
main(void)
{
	test_names();
	test_resolution();
	test_lte_only_expansion();
	puts("radio policy tests passed");
	return 0;
}
