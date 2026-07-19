/*
 * SPDX-FileCopyrightText: 2026 As Tsaqib
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "sms_policy.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static gchar *
repeat_text(const gchar *unit, gsize count)
{
	gsize unit_length = strlen(unit);
	gchar *result = g_malloc(unit_length * count + 1U);
	gsize index;

	for (index = 0U; index < count; index++)
		memcpy(result + index * unit_length, unit, unit_length);
	result[unit_length * count] = '\0';
	return result;
}

static void
test_recipient_validation(void)
{
	gchar twenty_digits[FIBOCOM_SMS_RECIPIENT_DIGITS_MAX + 1U];
	gchar plus_twenty_digits[FIBOCOM_SMS_RECIPIENT_DIGITS_MAX + 2U];
	gchar twenty_one_digits[FIBOCOM_SMS_RECIPIENT_DIGITS_MAX + 2U];

	memset(twenty_digits, '7', sizeof(twenty_digits) - 1U);
	twenty_digits[sizeof(twenty_digits) - 1U] = '\0';
	plus_twenty_digits[0] = '+';
	memcpy(plus_twenty_digits + 1U, twenty_digits,
	       sizeof(twenty_digits));
	memset(twenty_one_digits, '8', sizeof(twenty_one_digits) - 1U);
	twenty_one_digits[sizeof(twenty_one_digits) - 1U] = '\0';

	assert(fibocom_sms_recipient_is_valid("1"));
	assert(fibocom_sms_recipient_is_valid("+628123456789"));
	assert(fibocom_sms_recipient_is_valid(twenty_digits));
	assert(fibocom_sms_recipient_is_valid(plus_twenty_digits));
	assert(!fibocom_sms_recipient_is_valid(NULL));
	assert(!fibocom_sms_recipient_is_valid(""));
	assert(!fibocom_sms_recipient_is_valid("+"));
	assert(!fibocom_sms_recipient_is_valid("++1"));
	assert(!fibocom_sms_recipient_is_valid("12 34"));
	assert(!fibocom_sms_recipient_is_valid("12-34"));
	assert(!fibocom_sms_recipient_is_valid("12a34"));
	assert(!fibocom_sms_recipient_is_valid("\xd9\xa1"));
	assert(!fibocom_sms_recipient_is_valid(twenty_one_digits));
}

static void
test_outbound_text_validation(void)
{
	static const gchar invalid_utf8[] = { (gchar)0xc3, '(', '\0' };
	g_autofree gchar *ascii_limit = NULL;
	g_autofree gchar *ascii_too_long = NULL;
	g_autofree gchar *four_byte_limit = NULL;
	g_autofree gchar *four_byte_too_long = NULL;

	ascii_limit = repeat_text("a", FIBOCOM_SMS_OUTBOUND_CHARS_MAX);
	ascii_too_long = repeat_text("a",
		FIBOCOM_SMS_OUTBOUND_CHARS_MAX + 1U);
	four_byte_limit = repeat_text("\xf0\x9f\x98\x80",
		FIBOCOM_SMS_OUTBOUND_CHARS_MAX);
	four_byte_too_long = repeat_text("\xf0\x9f\x98\x80",
		FIBOCOM_SMS_OUTBOUND_CHARS_MAX + 1U);

	assert(fibocom_sms_outbound_text_is_valid("hello"));
	assert(fibocom_sms_outbound_text_is_valid("line 1\nline 2\r\t"));
	assert(fibocom_sms_outbound_text_is_valid("\xe4\xbd\xa0\xe5\xa5\xbd"));
	assert(fibocom_sms_outbound_text_is_valid(ascii_limit));
	assert(fibocom_sms_outbound_text_is_valid(four_byte_limit));
	assert(strlen(four_byte_limit) == FIBOCOM_SMS_OUTBOUND_BYTES_MAX);
	assert(!fibocom_sms_outbound_text_is_valid(NULL));
	assert(!fibocom_sms_outbound_text_is_valid(""));
	assert(!fibocom_sms_outbound_text_is_valid("bad\x01text"));
	assert(!fibocom_sms_outbound_text_is_valid("bad\x7ftext"));
	assert(!fibocom_sms_outbound_text_is_valid(invalid_utf8));
	assert(!fibocom_sms_outbound_text_is_valid(ascii_too_long));
	assert(!fibocom_sms_outbound_text_is_valid(four_byte_too_long));
}

static void
test_request_digest(void)
{
	static const guint8 expected[FIBOCOM_SMS_SHA256_DIGEST_LEN] = {
		0x76U, 0x20U, 0x8cU, 0xabU, 0x24U, 0x09U, 0xffU, 0x19U,
		0xd7U, 0x06U, 0x6aU, 0x38U, 0xaeU, 0xb6U, 0xbeU, 0x15U,
		0xc7U, 0x26U, 0x48U, 0xf3U, 0xbcU, 0xadU, 0xfeU, 0x1eU,
		0x47U, 0x10U, 0x72U, 0x14U, 0x72U, 0xa4U, 0xdfU, 0xccU,
	};
	guint8 digest[FIBOCOM_SMS_SHA256_DIGEST_LEN];
	guint8 duplicate[FIBOCOM_SMS_SHA256_DIGEST_LEN];
	guint8 changed_recipient[FIBOCOM_SMS_SHA256_DIGEST_LEN];
	guint8 changed_text[FIBOCOM_SMS_SHA256_DIGEST_LEN];
	guint8 split_a[FIBOCOM_SMS_SHA256_DIGEST_LEN];
	guint8 split_b[FIBOCOM_SMS_SHA256_DIGEST_LEN];

	assert(fibocom_sms_request_digest("+628123456789", "hello", digest));
	assert(fibocom_sms_request_digest("+628123456789", "hello",
					  duplicate));
	assert(fibocom_sms_digest_equal(digest, expected));
	assert(fibocom_sms_digest_equal(digest, duplicate));
	assert(fibocom_sms_request_digest("+628123456780", "hello",
					  changed_recipient));
	assert(fibocom_sms_request_digest("+628123456789", "Hello",
					  changed_text));
	assert(!fibocom_sms_digest_equal(digest, changed_recipient));
	assert(!fibocom_sms_digest_equal(digest, changed_text));

	assert(fibocom_sms_request_digest("1", "23", split_a));
	assert(fibocom_sms_request_digest("12", "3", split_b));
	assert(!fibocom_sms_digest_equal(split_a, split_b));
	assert(!fibocom_sms_request_digest(NULL, "hello", digest));
	assert(!fibocom_sms_request_digest("+1", NULL, digest));
	assert(!fibocom_sms_request_digest("+1", "hello", NULL));
	assert(!fibocom_sms_digest_equal(NULL, digest));
	assert(!fibocom_sms_digest_equal(digest, NULL));
}

int
main(void)
{
	test_recipient_validation();
	test_outbound_text_validation();
	test_request_digest();
	puts("SMS policy tests passed");
	return 0;
}
