/*
 * SPDX-FileCopyrightText: 2026 As Tsaqib
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef FIBOCOM_MM_IDENTITY_H
#define FIBOCOM_MM_IDENTITY_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define FIBOCOM_ID_PREFIX "fibocom-"
#define FIBOCOM_ID_RANDOM_LEN 16U
#define FIBOCOM_ID_HEX_LEN (FIBOCOM_ID_RANDOM_LEN * 2U)
#define FIBOCOM_ID_BUFSIZE \
	((sizeof(FIBOCOM_ID_PREFIX) - 1U) + FIBOCOM_ID_HEX_LEN + 1U)

typedef bool (*FibocomIdentityRandomFill)(uint8_t *buffer, size_t length,
					  void *user_data);

bool fibocom_identity_generate(char output[FIBOCOM_ID_BUFSIZE]);
bool fibocom_identity_generate_with(char output[FIBOCOM_ID_BUFSIZE],
				    FibocomIdentityRandomFill random_fill,
				    void *user_data);
bool fibocom_identity_is_valid(const char *modem_id);

#endif /* FIBOCOM_MM_IDENTITY_H */
