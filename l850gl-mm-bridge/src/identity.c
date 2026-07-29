/*
 * SPDX-FileCopyrightText: 2026 As Tsaqib
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include "identity.h"

#include <errno.h>
#include <string.h>
#include <sys/syscall.h>
#include <unistd.h>

static bool
system_random_fill(uint8_t *buffer, size_t length, void *user_data)
{
	size_t offset = 0;

	(void)user_data;
#ifndef SYS_getrandom
	(void)buffer;
	(void)length;
	return false;
#else
	while (offset < length) {
		ssize_t count = syscall(SYS_getrandom, buffer + offset,
				length - offset, 0);

		if (count > 0) {
			offset += (size_t)count;
			continue;
		}
		if (count < 0 && errno == EINTR)
			continue;
		return false;
	}
	return true;
#endif
}

static bool
token_generate_with(char *output, size_t output_size, const char *prefix,
		    L850GLIdentityRandomFill random_fill, void *user_data)
{
	static const char hex[] = "0123456789abcdef";
	uint8_t random_bytes[L850GL_ID_RANDOM_LEN];
	size_t prefix_length;
	size_t expected_size;
	size_t i;

	if (output == NULL || output_size == 0U || prefix == NULL ||
	    random_fill == NULL)
		return false;
	output[0] = '\0';
	prefix_length = strlen(prefix);
	expected_size = prefix_length + L850GL_ID_HEX_LEN + 1U;
	if (output_size != expected_size ||
	    !random_fill(random_bytes, sizeof(random_bytes), user_data))
		return false;
	memcpy(output, prefix, prefix_length);
	for (i = 0; i < sizeof(random_bytes); i++) {
		output[prefix_length + (i * 2U)] = hex[random_bytes[i] >> 4U];
		output[prefix_length + (i * 2U) + 1U] =
			hex[random_bytes[i] & 0x0fU];
	}
	output[output_size - 1U] = '\0';
	return true;
}

static bool
token_is_valid(const char *value, const char *prefix, size_t expected_size)
{
	size_t prefix_length;
	size_t i;

	if (value == NULL || prefix == NULL)
		return false;
	prefix_length = strlen(prefix);
	if (strlen(value) != expected_size - 1U ||
	    strncmp(value, prefix, prefix_length) != 0)
		return false;
	for (i = prefix_length; value[i] != '\0'; i++) {
		if (!((value[i] >= '0' && value[i] <= '9') ||
		      (value[i] >= 'a' && value[i] <= 'f')))
			return false;
	}
	return true;
}

bool
l850gl_identity_generate_with(char output[L850GL_ID_BUFSIZE],
			       L850GLIdentityRandomFill random_fill,
			       void *user_data)
{
	return token_generate_with(output, L850GL_ID_BUFSIZE,
		L850GL_ID_PREFIX, random_fill, user_data);
}

bool
l850gl_identity_generate(char output[L850GL_ID_BUFSIZE])
{
	return l850gl_identity_generate_with(output, system_random_fill, NULL);
}

bool
l850gl_identity_is_valid(const char *modem_id)
{
	return token_is_valid(modem_id, L850GL_ID_PREFIX,
		L850GL_ID_BUFSIZE);
}

bool
l850gl_sms_identity_generate_with(char output[L850GL_SMS_ID_BUFSIZE],
				   L850GLIdentityRandomFill random_fill,
				   void *user_data)
{
	return token_generate_with(output, L850GL_SMS_ID_BUFSIZE,
		L850GL_SMS_ID_PREFIX, random_fill, user_data);
}

bool
l850gl_sms_identity_generate(char output[L850GL_SMS_ID_BUFSIZE])
{
	return l850gl_sms_identity_generate_with(output, system_random_fill, NULL);
}

bool
l850gl_sms_identity_is_valid(const char *sms_id)
{
	return token_is_valid(sms_id, L850GL_SMS_ID_PREFIX,
		L850GL_SMS_ID_BUFSIZE);
}

bool
l850gl_sms_operation_token_is_valid(const char *token)
{
	return token_is_valid(token, L850GL_SMS_OP_PREFIX,
		L850GL_SMS_OP_BUFSIZE);
}
