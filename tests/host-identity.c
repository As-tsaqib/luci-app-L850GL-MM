/*
 * SPDX-FileCopyrightText: 2026 As Tsaqib
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "identity.h"

#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static bool
fill_sequence(uint8_t *buffer, size_t length, void *user_data)
{
	uint8_t start = *(uint8_t *)user_data;
	size_t i;

	for (i = 0; i < length; i++)
		buffer[i] = (uint8_t)(start + i);
	return true;
}

static bool
fill_failure(uint8_t *buffer, size_t length, void *user_data)
{
	(void)buffer;
	(void)length;
	(void)user_data;
	return false;
}

int
main(void)
{
	char ids[128][FIBOCOM_ID_BUFSIZE];
	char deterministic[FIBOCOM_ID_BUFSIZE];
	char invalid[FIBOCOM_ID_BUFSIZE];
	uint8_t start = 0;
	size_t i;
	size_t j;

	assert(fibocom_identity_generate_with(deterministic, fill_sequence, &start));
	assert(strcmp(deterministic,
		      "fibocom-000102030405060708090a0b0c0d0e0f") == 0);
	assert(fibocom_identity_is_valid(deterministic));

	strcpy(invalid, deterministic);
	invalid[8] = 'A';
	assert(!fibocom_identity_is_valid(invalid));
	assert(!fibocom_identity_is_valid("fibocom-short"));
	assert(!fibocom_identity_is_valid(NULL));

	memset(deterministic, 'x', sizeof(deterministic));
	assert(!fibocom_identity_generate_with(deterministic, fill_failure, NULL));
	assert(deterministic[0] == '\0');
	assert(!fibocom_identity_generate_with(deterministic, NULL, NULL));

	for (i = 0; i < sizeof(ids) / sizeof(ids[0]); i++) {
		assert(fibocom_identity_generate(ids[i]));
		assert(fibocom_identity_is_valid(ids[i]));
		for (j = 0; j < i; j++)
			assert(strcmp(ids[i], ids[j]) != 0);
	}

	puts("identity tests passed");
	return 0;
}
