/*
 * SPDX-FileCopyrightText: 2026 As Tsaqib
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "sms_policy.h"

#include <string.h>

gboolean
fibocom_sms_recipient_is_valid(const gchar *recipient)
{
	gsize length;
	gsize index = 0U;
	gsize digits = 0U;

	if (recipient == NULL)
		return FALSE;
	length = strlen(recipient);
	if (length == 0U ||
	    length > FIBOCOM_SMS_RECIPIENT_DIGITS_MAX + 1U)
		return FALSE;
	if (recipient[0] == '+')
		index++;
	for (; index < length; index++) {
		if (!g_ascii_isdigit(recipient[index]))
			return FALSE;
		digits++;
	}
	return digits >= 1U && digits <= FIBOCOM_SMS_RECIPIENT_DIGITS_MAX;
}

gboolean
fibocom_sms_outbound_text_is_valid(const gchar *value)
{
	const gchar *cursor;
	gsize bytes;
	glong characters;

	if (value == NULL)
		return FALSE;
	bytes = strlen(value);
	if (bytes == 0U || bytes > FIBOCOM_SMS_OUTBOUND_BYTES_MAX ||
	    !g_utf8_validate(value, -1, NULL))
		return FALSE;
	characters = g_utf8_strlen(value, -1);
	if (characters <= 0 ||
	    (gsize)characters > FIBOCOM_SMS_OUTBOUND_CHARS_MAX)
		return FALSE;
	for (cursor = value; *cursor != '\0'; cursor = g_utf8_next_char(cursor)) {
		gunichar character = g_utf8_get_char(cursor);

		if (g_unichar_iscntrl(character) && character != '\n' &&
		    character != '\r' && character != '\t')
			return FALSE;
	}
	return TRUE;
}

static void
checksum_add_field(GChecksum *checksum, const gchar *value)
{
	guint8 encoded_length[8];
	guint64 length = (guint64)strlen(value);
	guint index;

	for (index = 0U; index < G_N_ELEMENTS(encoded_length); index++)
		encoded_length[G_N_ELEMENTS(encoded_length) - 1U - index] =
			(guint8)(length >> (index * 8U));
	g_checksum_update(checksum, encoded_length, sizeof(encoded_length));
	g_checksum_update(checksum, (const guchar *)value, (gssize)length);
}

gboolean
fibocom_sms_request_digest(
	const gchar *recipient, const gchar *text,
	guint8 digest[FIBOCOM_SMS_SHA256_DIGEST_LEN])
{
	static const gchar domain[] = "fibocom-mm-send-sms-v1";
	g_autoptr(GChecksum) checksum = NULL;
	gsize digest_length = FIBOCOM_SMS_SHA256_DIGEST_LEN;

	if (recipient == NULL || text == NULL || digest == NULL)
		return FALSE;
	checksum = g_checksum_new(G_CHECKSUM_SHA256);
	if (checksum == NULL)
		return FALSE;
	g_checksum_update(checksum, (const guchar *)domain, sizeof(domain));
	checksum_add_field(checksum, recipient);
	checksum_add_field(checksum, text);
	g_checksum_get_digest(checksum, digest, &digest_length);
	return digest_length == FIBOCOM_SMS_SHA256_DIGEST_LEN;
}

gboolean
fibocom_sms_digest_equal(
	const guint8 left[FIBOCOM_SMS_SHA256_DIGEST_LEN],
	const guint8 right[FIBOCOM_SMS_SHA256_DIGEST_LEN])
{
	return left != NULL && right != NULL &&
		memcmp(left, right, FIBOCOM_SMS_SHA256_DIGEST_LEN) == 0;
}
