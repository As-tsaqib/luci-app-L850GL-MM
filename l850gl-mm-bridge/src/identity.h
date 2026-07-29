/*
 * SPDX-FileCopyrightText: 2026 As Tsaqib
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef L850GL_MM_IDENTITY_H
#define L850GL_MM_IDENTITY_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define L850GL_ID_PREFIX "l850gl-"
#define L850GL_ID_RANDOM_LEN 16U
#define L850GL_ID_HEX_LEN (L850GL_ID_RANDOM_LEN * 2U)
#define L850GL_ID_BUFSIZE \
	((sizeof(L850GL_ID_PREFIX) - 1U) + L850GL_ID_HEX_LEN + 1U)

#define L850GL_SMS_ID_PREFIX "sms-"
#define L850GL_SMS_ID_BUFSIZE \
	((sizeof(L850GL_SMS_ID_PREFIX) - 1U) + L850GL_ID_HEX_LEN + 1U)

#define L850GL_SMS_OP_PREFIX "smsop-"
#define L850GL_SMS_OP_BUFSIZE \
	((sizeof(L850GL_SMS_OP_PREFIX) - 1U) + L850GL_ID_HEX_LEN + 1U)

typedef bool (*L850GLIdentityRandomFill)(uint8_t *buffer, size_t length,
					  void *user_data);

bool l850gl_identity_generate(char output[L850GL_ID_BUFSIZE]);
bool l850gl_identity_generate_with(char output[L850GL_ID_BUFSIZE],
				    L850GLIdentityRandomFill random_fill,
				    void *user_data);
bool l850gl_identity_is_valid(const char *modem_id);

bool l850gl_sms_identity_generate(char output[L850GL_SMS_ID_BUFSIZE]);
bool l850gl_sms_identity_generate_with(char output[L850GL_SMS_ID_BUFSIZE],
					L850GLIdentityRandomFill random_fill,
					void *user_data);
bool l850gl_sms_identity_is_valid(const char *sms_id);
bool l850gl_sms_operation_token_is_valid(const char *token);

#endif /* L850GL_MM_IDENTITY_H */
