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
	gchar twenty_digits[L850GL_SMS_RECIPIENT_DIGITS_MAX + 1U];
	gchar plus_twenty_digits[L850GL_SMS_RECIPIENT_DIGITS_MAX + 2U];
	gchar twenty_one_digits[L850GL_SMS_RECIPIENT_DIGITS_MAX + 2U];

	memset(twenty_digits, '7', sizeof(twenty_digits) - 1U);
	twenty_digits[sizeof(twenty_digits) - 1U] = '\0';
	plus_twenty_digits[0] = '+';
	memcpy(plus_twenty_digits + 1U, twenty_digits,
	       sizeof(twenty_digits));
	memset(twenty_one_digits, '8', sizeof(twenty_one_digits) - 1U);
	twenty_one_digits[sizeof(twenty_one_digits) - 1U] = '\0';

	assert(l850gl_sms_recipient_is_valid("1"));
	assert(l850gl_sms_recipient_is_valid("+628123456789"));
	assert(l850gl_sms_recipient_is_valid(twenty_digits));
	assert(l850gl_sms_recipient_is_valid(plus_twenty_digits));
	assert(!l850gl_sms_recipient_is_valid(NULL));
	assert(!l850gl_sms_recipient_is_valid(""));
	assert(!l850gl_sms_recipient_is_valid("+"));
	assert(!l850gl_sms_recipient_is_valid("++1"));
	assert(!l850gl_sms_recipient_is_valid("12 34"));
	assert(!l850gl_sms_recipient_is_valid("12-34"));
	assert(!l850gl_sms_recipient_is_valid("12a34"));
	assert(!l850gl_sms_recipient_is_valid("\xd9\xa1"));
	assert(!l850gl_sms_recipient_is_valid(twenty_one_digits));
}

static void
test_outbound_text_validation(void)
{
	static const gchar invalid_utf8[] = { (gchar)0xc3, '(', '\0' };
	g_autofree gchar *ascii_limit = NULL;
	g_autofree gchar *ascii_too_long = NULL;
	g_autofree gchar *four_byte_limit = NULL;
	g_autofree gchar *four_byte_too_long = NULL;

	ascii_limit = repeat_text("a", L850GL_SMS_OUTBOUND_CHARS_MAX);
	ascii_too_long = repeat_text("a",
		L850GL_SMS_OUTBOUND_CHARS_MAX + 1U);
	four_byte_limit = repeat_text("\xf0\x9f\x98\x80",
		L850GL_SMS_OUTBOUND_CHARS_MAX);
	four_byte_too_long = repeat_text("\xf0\x9f\x98\x80",
		L850GL_SMS_OUTBOUND_CHARS_MAX + 1U);

	assert(l850gl_sms_outbound_text_is_valid("hello"));
	assert(l850gl_sms_outbound_text_is_valid("line 1\nline 2\r\t"));
	assert(l850gl_sms_outbound_text_is_valid("\xe4\xbd\xa0\xe5\xa5\xbd"));
	assert(l850gl_sms_outbound_text_is_valid(ascii_limit));
	assert(l850gl_sms_outbound_text_is_valid(four_byte_limit));
	assert(strlen(four_byte_limit) == L850GL_SMS_OUTBOUND_BYTES_MAX);
	assert(!l850gl_sms_outbound_text_is_valid(NULL));
	assert(!l850gl_sms_outbound_text_is_valid(""));
	assert(!l850gl_sms_outbound_text_is_valid("bad\x01text"));
	assert(!l850gl_sms_outbound_text_is_valid("bad\x7ftext"));
	assert(!l850gl_sms_outbound_text_is_valid(invalid_utf8));
	assert(!l850gl_sms_outbound_text_is_valid(ascii_too_long));
	assert(!l850gl_sms_outbound_text_is_valid(four_byte_too_long));
}

static void
test_request_digest(void)
{
	static const guint8 expected[L850GL_SMS_SHA256_DIGEST_LEN] = {
		0x9aU, 0x33U, 0x54U, 0x04U, 0x93U, 0x55U, 0x23U, 0xcbU,
		0x4bU, 0x9dU, 0x51U, 0x57U, 0xf9U, 0x1cU, 0x56U, 0x23U,
		0x0dU, 0x54U, 0xceU, 0x69U, 0x13U, 0x34U, 0x64U, 0x64U,
		0x71U, 0x91U, 0xfaU, 0x0cU, 0xf0U, 0xddU, 0xecU, 0xe2U,
	};
	guint8 digest[L850GL_SMS_SHA256_DIGEST_LEN];
	guint8 duplicate[L850GL_SMS_SHA256_DIGEST_LEN];
	guint8 changed_recipient[L850GL_SMS_SHA256_DIGEST_LEN];
	guint8 changed_text[L850GL_SMS_SHA256_DIGEST_LEN];
	guint8 split_a[L850GL_SMS_SHA256_DIGEST_LEN];
	guint8 split_b[L850GL_SMS_SHA256_DIGEST_LEN];

	assert(l850gl_sms_request_digest("+628123456789", "hello", digest));
	assert(l850gl_sms_request_digest("+628123456789", "hello",
					  duplicate));
	assert(l850gl_sms_digest_equal(digest, expected));
	assert(l850gl_sms_digest_equal(digest, duplicate));
	assert(l850gl_sms_request_digest("+628123456780", "hello",
					  changed_recipient));
	assert(l850gl_sms_request_digest("+628123456789", "Hello",
					  changed_text));
	assert(!l850gl_sms_digest_equal(digest, changed_recipient));
	assert(!l850gl_sms_digest_equal(digest, changed_text));

	assert(l850gl_sms_request_digest("1", "23", split_a));
	assert(l850gl_sms_request_digest("12", "3", split_b));
	assert(!l850gl_sms_digest_equal(split_a, split_b));
	assert(!l850gl_sms_request_digest(NULL, "hello", digest));
	assert(!l850gl_sms_request_digest("+1", NULL, digest));
	assert(!l850gl_sms_request_digest("+1", "hello", NULL));
	assert(!l850gl_sms_digest_equal(NULL, digest));
	assert(!l850gl_sms_digest_equal(digest, NULL));
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
