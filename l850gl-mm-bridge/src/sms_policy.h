/*
 * SPDX-FileCopyrightText: 2026 As Tsaqib
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef L850GL_SMS_POLICY_H
#define L850GL_SMS_POLICY_H

#include <glib.h>

#define L850GL_SMS_RECIPIENT_DIGITS_MAX 20U
#define L850GL_SMS_OUTBOUND_CHARS_MAX 1600U
#define L850GL_SMS_OUTBOUND_BYTES_MAX 6400U
#define L850GL_SMS_SHA256_DIGEST_LEN 32U

gboolean l850gl_sms_recipient_is_valid(const gchar *recipient);
gboolean l850gl_sms_outbound_text_is_valid(const gchar *text);
gboolean l850gl_sms_request_digest(
	const gchar *recipient, const gchar *text,
	guint8 digest[L850GL_SMS_SHA256_DIGEST_LEN]);
gboolean l850gl_sms_digest_equal(
	const guint8 left[L850GL_SMS_SHA256_DIGEST_LEN],
	const guint8 right[L850GL_SMS_SHA256_DIGEST_LEN]);

#endif /* L850GL_SMS_POLICY_H */
