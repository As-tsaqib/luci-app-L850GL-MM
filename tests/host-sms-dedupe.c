/*
 * SPDX-FileCopyrightText: 2026 As Tsaqib
 * SPDX-License-Identifier: Apache-2.0
 */

#include "sms_dedupe_policy.h"

#include <assert.h>
#include <limits.h>
#include <stdio.h>

int
main(void)
{
	const int64_t now = INT64_C(1000000);
	const int64_t expiry = fibocom_sms_dedupe_expiry(now);

	assert(FIBOCOM_SMS_DEDUPE_SECONDS == 300U);
	assert(FIBOCOM_SMS_DEDUPE_MAX == 64U);
	assert(expiry == INT64_C(301000000));
	assert(!fibocom_sms_dedupe_is_expired(expiry, expiry - 1));
	assert(fibocom_sms_dedupe_is_expired(expiry, expiry));
	assert(fibocom_sms_dedupe_is_expired(expiry, expiry + 1));
	assert(fibocom_sms_dedupe_expiry(-1) == 0);
	assert(fibocom_sms_dedupe_expiry(INT64_MAX) == INT64_MAX);

	assert(fibocom_sms_dedupe_evictions_required(0U) == 0U);
	assert(fibocom_sms_dedupe_evictions_required(63U) == 0U);
	assert(fibocom_sms_dedupe_evictions_required(64U) == 1U);
	assert(fibocom_sms_dedupe_evictions_required(65U) == 2U);
	assert(fibocom_sms_dedupe_evictions_required(1024U) == 961U);

	puts("SMS dedupe eviction and expiry tests passed");
	return 0;
}
