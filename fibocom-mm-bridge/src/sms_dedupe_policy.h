/*
 * SPDX-FileCopyrightText: 2026 As Tsaqib
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef FIBOCOM_SMS_DEDUPE_POLICY_H
#define FIBOCOM_SMS_DEDUPE_POLICY_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define FIBOCOM_SMS_DEDUPE_SECONDS 300U
#define FIBOCOM_SMS_DEDUPE_MAX 64U

int64_t fibocom_sms_dedupe_expiry(int64_t now_us);
bool fibocom_sms_dedupe_is_expired(int64_t expires_at_us, int64_t now_us);
size_t fibocom_sms_dedupe_evictions_required(size_t current_length);

#endif /* FIBOCOM_SMS_DEDUPE_POLICY_H */
