/*
 * SPDX-FileCopyrightText: 2026 As Tsaqib
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef L850GL_SMS_DEDUPE_POLICY_H
#define L850GL_SMS_DEDUPE_POLICY_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define L850GL_SMS_DEDUPE_SECONDS 300U
#define L850GL_SMS_DEDUPE_MAX 64U

int64_t l850gl_sms_dedupe_expiry(int64_t now_us);
bool l850gl_sms_dedupe_is_expired(int64_t expires_at_us, int64_t now_us);
size_t l850gl_sms_dedupe_evictions_required(size_t current_length);

#endif /* L850GL_SMS_DEDUPE_POLICY_H */
