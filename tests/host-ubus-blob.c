/*
 * SPDX-FileCopyrightText: 2026 As Tsaqib
 * SPDX-License-Identifier: Apache-2.0
 */

#include "ubus_request.h"

#include <assert.h>
#include <libubox/blob.h>
#include <libubox/blobmsg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

enum {
	FIELD_MODEM_ID,
	FIELD_GENERATION,
	__FIELD_MAX,
};

static const struct blobmsg_policy policy[__FIELD_MAX] = {
	[FIELD_MODEM_ID] = { .name = "modem_id", .type = BLOBMSG_TYPE_STRING },
	[FIELD_GENERATION] = { .name = "generation", .type = BLOBMSG_TYPE_INT32 },
};

static const uint64_t required =
	(UINT64_C(1) << FIELD_MODEM_ID) | (UINT64_C(1) << FIELD_GENERATION);

static struct blob_attr *
first_attribute(struct blob_buf *buffer)
{
	return (struct blob_attr *)blobmsg_data(buffer->head);
}

static void
add_valid_fields(struct blob_buf *buffer)
{
	assert(blobmsg_add_string(buffer, "modem_id",
		"fibocom-000102030405060708090a0b0c0d0e0f") == 0);
	assert(blobmsg_add_u32(buffer, "generation", 7U) == 0);
}

int
main(void)
{
	struct blob_attr *parsed[__FIELD_MAX] = {};
	struct blob_attr *attribute;
	struct blobmsg_hdr *header;
	struct blob_buf buffer = {};
	static const char embedded[] = "fibocom-safe\0hidden";

	blob_buf_init(&buffer, 0);
	add_valid_fields(&buffer);
	assert(fibocom_ubus_parse_exact(buffer.head, policy, __FIELD_MAX,
		required, parsed));
	assert(strcmp(blobmsg_get_string(parsed[FIELD_MODEM_ID]),
		"fibocom-000102030405060708090a0b0c0d0e0f") == 0);
	assert(blobmsg_get_u32(parsed[FIELD_GENERATION]) == 7U);
	blob_buf_free(&buffer);

	blob_buf_init(&buffer, 0);
	assert(blobmsg_add_string(&buffer, "ubus_rpc_session",
		"0123456789abcdef0123456789ABCDEF") == 0);
	add_valid_fields(&buffer);
	assert(fibocom_ubus_parse_exact(buffer.head, policy, __FIELD_MAX,
		required, parsed));
	blob_buf_free(&buffer);

	blob_buf_init(&buffer, 0);
	assert(blobmsg_add_string(&buffer, "ubus_rpc_session", "short") == 0);
	add_valid_fields(&buffer);
	assert(!fibocom_ubus_parse_exact(buffer.head, policy, __FIELD_MAX,
		required, parsed));
	blob_buf_free(&buffer);

	blob_buf_init(&buffer, 0);
	add_valid_fields(&buffer);
	assert(blobmsg_add_string(&buffer, "modem_id", "duplicate") == 0);
	assert(!fibocom_ubus_parse_exact(buffer.head, policy, __FIELD_MAX,
		required, parsed));
	blob_buf_free(&buffer);

	blob_buf_init(&buffer, 0);
	add_valid_fields(&buffer);
	assert(blobmsg_add_u32(&buffer, "unknown", 1U) == 0);
	assert(!fibocom_ubus_parse_exact(buffer.head, policy, __FIELD_MAX,
		required, parsed));
	blob_buf_free(&buffer);

	blob_buf_init(&buffer, 0);
	assert(blobmsg_add_string(&buffer, "modem_id", "fibocom-valid") == 0);
	assert(blobmsg_add_string(&buffer, "generation", "7") == 0);
	assert(!fibocom_ubus_parse_exact(buffer.head, policy, __FIELD_MAX,
		required, parsed));
	blob_buf_free(&buffer);

	blob_buf_init(&buffer, 0);
	assert(blobmsg_add_field(&buffer, BLOBMSG_TYPE_STRING, "modem_id",
		embedded, sizeof(embedded)) == 0);
	assert(blobmsg_add_u32(&buffer, "generation", 7U) == 0);
	assert(!fibocom_ubus_parse_exact(buffer.head, policy, __FIELD_MAX,
		required, parsed));
	blob_buf_free(&buffer);

	blob_buf_init(&buffer, 0);
	add_valid_fields(&buffer);
	attribute = first_attribute(&buffer);
	header = (struct blobmsg_hdr *)blob_data(attribute);
	header->namelen = cpu_to_be16(4095U);
	assert(!fibocom_ubus_blob_attr_is_valid(attribute,
		blobmsg_data_len(buffer.head), true));
	assert(!fibocom_ubus_parse_exact(buffer.head, policy, __FIELD_MAX,
		required, parsed));
	blob_buf_free(&buffer);

	blob_buf_init(&buffer, 0);
	add_valid_fields(&buffer);
	attribute = first_attribute(&buffer);
	blob_set_raw_len(attribute, sizeof(*attribute) - 1U);
	assert(!fibocom_ubus_parse_exact(buffer.head, policy, __FIELD_MAX,
		required, parsed));
	blob_buf_free(&buffer);

	blob_buf_init(&buffer, 0);
	add_valid_fields(&buffer);
	attribute = first_attribute(&buffer);
	blob_set_raw_len(attribute, blob_raw_len(attribute) + 4096U);
	assert(!fibocom_ubus_parse_exact(buffer.head, policy, __FIELD_MAX,
		required, parsed));
	blob_buf_free(&buffer);

	/* A malformed trailing attribute must not be silently ignored after all
	 * required fields have already been accepted. */
	blob_buf_init(&buffer, 0);
	add_valid_fields(&buffer);
	assert(blobmsg_add_string(&buffer, "ubus_rpc_session",
		"0123456789abcdef0123456789abcdef") == 0);
	attribute = blob_next(blob_next(first_attribute(&buffer)));
	blob_set_raw_len(attribute, blob_raw_len(attribute) + 4096U);
	assert(!fibocom_ubus_parse_exact(buffer.head, policy, __FIELD_MAX,
		required, parsed));
	blob_buf_free(&buffer);

	blob_buf_init(&buffer, 0);
	assert(fibocom_ubus_message_is_empty(buffer.head));
	assert(blobmsg_add_string(&buffer, "ubus_rpc_session",
		"0123456789abcdef0123456789abcdef") == 0);
	assert(fibocom_ubus_message_is_empty(buffer.head));
	assert(blobmsg_add_string(&buffer, "ubus_rpc_session",
		"fedcba9876543210fedcba9876543210") == 0);
	assert(!fibocom_ubus_message_is_empty(buffer.head));
	blob_buf_free(&buffer);

	blob_buf_init(&buffer, 0);
	assert(blobmsg_add_string(&buffer, "ubus_rpc_session",
		"0123456789abcdef0123456789abcdef") == 0);
	assert(blobmsg_add_string(&buffer, "ignored-if-malformed", "value") == 0);
	attribute = blob_next(first_attribute(&buffer));
	blob_set_raw_len(attribute, blob_raw_len(attribute) + 4096U);
	assert(!fibocom_ubus_message_is_empty(buffer.head));
	blob_buf_free(&buffer);

	puts("malformed ubus blob tests passed");
	return 0;
}
