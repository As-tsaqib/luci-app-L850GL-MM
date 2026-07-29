/*
 * SPDX-FileCopyrightText: 2026 As Tsaqib
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef L850GL_UBUS_REQUEST_H
#define L850GL_UBUS_REQUEST_H

#include <libubox/blobmsg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

bool l850gl_ubus_blob_attr_is_valid(const struct blob_attr *attribute,
				     size_t remaining,
				     bool name_required);
bool l850gl_ubus_blob_string_is_canonical(const struct blob_attr *attribute,
					    size_t remaining,
					    bool name_required);
bool l850gl_ubus_rpc_session_is_valid(const struct blob_attr *attribute,
				       size_t remaining);
bool l850gl_ubus_message_is_empty(struct blob_attr *message);
bool l850gl_ubus_parse_exact(struct blob_attr *message,
			     const struct blobmsg_policy *policy,
			     size_t policy_length, uint64_t required,
			     struct blob_attr **parsed);

#endif /* L850GL_UBUS_REQUEST_H */
