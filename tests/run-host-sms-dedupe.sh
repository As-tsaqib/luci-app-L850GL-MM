#!/bin/sh
# SPDX-FileCopyrightText: 2026 As Tsaqib
# SPDX-License-Identifier: Apache-2.0

set -eu

ROOT=$(CDPATH='' cd -- "$(dirname "$0")/.." && pwd)
BUILD_DIR=$(mktemp -d)
trap 'rm -rf "$BUILD_DIR"' EXIT HUP INT TERM

${CC:-cc} -std=c11 -Wall -Wextra -Werror \
	-I"$ROOT/fibocom-mm-bridge/src" \
	"$ROOT/tests/host-sms-dedupe.c" \
	"$ROOT/fibocom-mm-bridge/src/sms_dedupe_policy.c" \
	-o "$BUILD_DIR/host-sms-dedupe"
"$BUILD_DIR/host-sms-dedupe"
