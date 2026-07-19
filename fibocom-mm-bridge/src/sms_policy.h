/*
 * SPDX-FileCopyrightText: 2026 As Tsaqib
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef FIBOCOM_SMS_POLICY_H
#define FIBOCOM_SMS_POLICY_H

#include <glib.h>

#define FIBOCOM_SMS_RECIPIENT_DIGITS_MAX 20U
#define FIBOCOM_SMS_OUTBOUND_CHARS_MAX 1600U
#define FIBOCOM_SMS_OUTBOUND_BYTES_MAX 6400U
#define FIBOCOM_SMS_SHA256_DIGEST_LEN 32U

gboolean fibocom_sms_recipient_is_valid(const gchar *recipient);
gboolean fibocom_sms_outbound_text_is_valid(const gchar *text);
gboolean fibocom_sms_request_digest(
	const gchar *recipient, const gchar *text,
	guint8 digest[FIBOCOM_SMS_SHA256_DIGEST_LEN]);
gboolean fibocom_sms_digest_equal(
	const guint8 left[FIBOCOM_SMS_SHA256_DIGEST_LEN],
	const guint8 right[FIBOCOM_SMS_SHA256_DIGEST_LEN]);

#endif /* FIBOCOM_SMS_POLICY_H */
