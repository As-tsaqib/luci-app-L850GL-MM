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

bool
fibocom_identity_generate_with(char output[FIBOCOM_ID_BUFSIZE],
			       FibocomIdentityRandomFill random_fill,
			       void *user_data)
{
	static const char hex[] = "0123456789abcdef";
	uint8_t random_bytes[FIBOCOM_ID_RANDOM_LEN];
	size_t prefix_length = sizeof(FIBOCOM_ID_PREFIX) - 1U;
	size_t i;

	if (output == NULL || random_fill == NULL)
		return false;
	output[0] = '\0';
	if (!random_fill(random_bytes, sizeof(random_bytes), user_data))
		return false;
	memcpy(output, FIBOCOM_ID_PREFIX, prefix_length);
	for (i = 0; i < sizeof(random_bytes); i++) {
		output[prefix_length + (i * 2U)] = hex[random_bytes[i] >> 4U];
		output[prefix_length + (i * 2U) + 1U] =
			hex[random_bytes[i] & 0x0fU];
	}
	output[FIBOCOM_ID_BUFSIZE - 1U] = '\0';
	return true;
}

bool
fibocom_identity_generate(char output[FIBOCOM_ID_BUFSIZE])
{
	return fibocom_identity_generate_with(output, system_random_fill, NULL);
}

bool
fibocom_identity_is_valid(const char *modem_id)
{
	size_t prefix_length = sizeof(FIBOCOM_ID_PREFIX) - 1U;
	size_t i;

	if (modem_id == NULL || strlen(modem_id) != FIBOCOM_ID_BUFSIZE - 1U ||
	    strncmp(modem_id, FIBOCOM_ID_PREFIX, prefix_length) != 0)
		return false;
	for (i = prefix_length; modem_id[i] != '\0'; i++) {
		if (!((modem_id[i] >= '0' && modem_id[i] <= '9') ||
		      (modem_id[i] >= 'a' && modem_id[i] <= 'f')))
			return false;
	}
	return true;
}
