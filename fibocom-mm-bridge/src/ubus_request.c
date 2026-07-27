/*
 * SPDX-FileCopyrightText: 2026 As Tsaqib
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "ubus_request.h"

#include <string.h>

#define RPC_SESSION_NAME "ubus_rpc_session"
#define RPC_SESSION_HEX_LENGTH 32U
#define REQUEST_POLICY_MAX 63U

static bool
ascii_is_hex(char value)
{
	return (value >= '0' && value <= '9') ||
		(value >= 'a' && value <= 'f') ||
		(value >= 'A' && value <= 'F');
}

bool
fibocom_ubus_blob_attr_is_valid(const struct blob_attr *attribute,
				size_t remaining, bool name_required)
{
	if (attribute == NULL || remaining < sizeof(*attribute))
		return false;
	return blobmsg_check_attr_len(attribute, name_required, remaining);
}

bool
fibocom_ubus_blob_string_is_canonical(const struct blob_attr *attribute,
				      size_t remaining, bool name_required)
{
	const char *value;
	size_t length;

	if (!fibocom_ubus_blob_attr_is_valid(attribute, remaining,
		name_required) ||
	    blobmsg_type(attribute) != BLOBMSG_TYPE_STRING)
		return false;
	value = blobmsg_data(attribute);
	length = blobmsg_data_len(attribute);
	return length > 0U && value[length - 1U] == '\0' &&
		memchr(value, '\0', length - 1U) == NULL;
}

bool
fibocom_ubus_rpc_session_is_valid(const struct blob_attr *attribute,
				  size_t remaining)
{
	const char *value;
	size_t index;

	if (!fibocom_ubus_blob_attr_is_valid(attribute, remaining, true) ||
	    strcmp(blobmsg_name(attribute), RPC_SESSION_NAME) != 0 ||
	    blobmsg_type(attribute) != BLOBMSG_TYPE_STRING ||
	    !fibocom_ubus_blob_string_is_canonical(attribute, remaining, true))
		return false;
	value = blobmsg_data(attribute);
	if (strlen(value) != RPC_SESSION_HEX_LENGTH)
		return false;
	for (index = 0; index < RPC_SESSION_HEX_LENGTH; index++) {
		if (!ascii_is_hex(value[index]))
			return false;
	}
	return true;
}

bool
fibocom_ubus_message_is_empty(struct blob_attr *message)
{
	struct blob_attr *attribute;
	size_t remaining;
	bool session_seen = false;

	if (message == NULL)
		return true;
	remaining = blobmsg_data_len(message);
	attribute = (struct blob_attr *)blobmsg_data(message);
	while (remaining > 0U) {
		size_t padded;

		if (!fibocom_ubus_blob_attr_is_valid(attribute, remaining, true) ||
		    session_seen ||
		    !fibocom_ubus_rpc_session_is_valid(attribute, remaining))
			return false;
		session_seen = true;
		padded = blob_pad_len(attribute);
		if (padded > remaining)
			return false;
		remaining -= padded;
		attribute = (struct blob_attr *)((char *)attribute + padded);
	}
	return true;
}

bool
fibocom_ubus_parse_exact(struct blob_attr *message,
			const struct blobmsg_policy *policy,
			size_t policy_length, uint64_t required,
			struct blob_attr **parsed)
{
	struct blob_attr *attribute;
	size_t remaining;
	uint64_t seen = 0U;
	bool session_seen = false;

	if (message == NULL || policy == NULL || parsed == NULL ||
	    policy_length == 0U || policy_length > REQUEST_POLICY_MAX)
		return false;
	memset(parsed, 0, policy_length * sizeof(*parsed));
	remaining = blobmsg_data_len(message);
	attribute = (struct blob_attr *)blobmsg_data(message);
	while (remaining > 0U) {
		size_t index;
		size_t padded;

		if (!fibocom_ubus_blob_attr_is_valid(attribute, remaining, true))
			return false;
		if (strcmp(blobmsg_name(attribute), RPC_SESSION_NAME) == 0) {
			if (session_seen ||
			    !fibocom_ubus_rpc_session_is_valid(attribute, remaining))
				return false;
			session_seen = true;
			goto next;
		}
		for (index = 0; index < policy_length; index++) {
			if (policy[index].name != NULL &&
			    strcmp(blobmsg_name(attribute), policy[index].name) == 0)
				break;
		}
		if (index == policy_length ||
		    (seen & (UINT64_C(1) << index)) != 0U ||
		    blobmsg_type(attribute) != (int)policy[index].type)
			return false;
		if (policy[index].type == BLOBMSG_TYPE_STRING &&
		    !fibocom_ubus_blob_string_is_canonical(attribute, remaining,
			true))
			return false;
		seen |= UINT64_C(1) << index;
		parsed[index] = attribute;

next:
		padded = blob_pad_len(attribute);
		if (padded > remaining)
			return false;
		remaining -= padded;
		attribute = (struct blob_attr *)((char *)attribute + padded);
	}
	return (seen & required) == required;
}
