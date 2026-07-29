#!/bin/sh
# SPDX-FileCopyrightText: 2026 As Tsaqib
# SPDX-License-Identifier: Apache-2.0

set -eu

: "${LIBUBOX_SOURCE_DIR:?Set LIBUBOX_SOURCE_DIR to the pinned libubox source tree}"
ROOT=$(CDPATH='' cd -- "$(dirname "$0")/.." && pwd)
BUILD_DIR=$(mktemp -d)
trap 'rm -rf "$BUILD_DIR"' EXIT HUP INT TERM
mkdir -p "$BUILD_DIR/include"
ln -s "$LIBUBOX_SOURCE_DIR" "$BUILD_DIR/include/libubox"

${CC:-cc} -std=gnu11 -Wall -Wextra -Werror \
	-I"$ROOT/l850gl-mm-bridge/src" -I"$BUILD_DIR/include" \
	-c "$ROOT/tests/host-ubus-blob.c" -o "$BUILD_DIR/host-ubus-blob.o"
${CC:-cc} -std=gnu11 -Wall -Wextra -Werror \
	-I"$ROOT/l850gl-mm-bridge/src" -I"$BUILD_DIR/include" \
	-c "$ROOT/l850gl-mm-bridge/src/ubus_request.c" \
	-o "$BUILD_DIR/ubus-request.o"
${CC:-cc} -std=gnu11 -I"$BUILD_DIR/include" \
	-c "$LIBUBOX_SOURCE_DIR/blob.c" -o "$BUILD_DIR/blob.o"
${CC:-cc} -std=gnu11 -I"$BUILD_DIR/include" \
	-c "$LIBUBOX_SOURCE_DIR/blobmsg.c" -o "$BUILD_DIR/blobmsg.o"
${CC:-cc} "$BUILD_DIR/host-ubus-blob.o" "$BUILD_DIR/ubus-request.o" \
	"$BUILD_DIR/blob.o" "$BUILD_DIR/blobmsg.o" \
	-o "$BUILD_DIR/host-ubus-blob"
"$BUILD_DIR/host-ubus-blob"
