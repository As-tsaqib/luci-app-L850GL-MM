/*
 * SPDX-FileCopyrightText: 2026 As Tsaqib
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "sms_dedupe_policy.h"

#include <limits.h>

#define MICROSECONDS_PER_SECOND INT64_C(1000000)

int64_t
l850gl_sms_dedupe_expiry(int64_t now_us)
{
	const int64_t window =
		(int64_t)L850GL_SMS_DEDUPE_SECONDS * MICROSECONDS_PER_SECOND;

	if (now_us < 0)
		return 0;
	if (now_us > INT64_MAX - window)
		return INT64_MAX;
	return now_us + window;
}

bool
l850gl_sms_dedupe_is_expired(int64_t expires_at_us, int64_t now_us)
{
	return expires_at_us <= now_us;
}

size_t
l850gl_sms_dedupe_evictions_required(size_t current_length)
{
	if (current_length < L850GL_SMS_DEDUPE_MAX)
		return 0U;
	return current_length - L850GL_SMS_DEDUPE_MAX + 1U;
}
