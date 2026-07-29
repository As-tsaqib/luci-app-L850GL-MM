#!/bin/sh
# SPDX-FileCopyrightText: 2026 As Tsaqib
# SPDX-License-Identifier: Apache-2.0

set -eu

ROOT=$(CDPATH='' cd -- "$(dirname "$0")/.." && pwd)
BUILD_DIR=$(mktemp -d)
trap 'rm -rf "$BUILD_DIR"' EXIT HUP INT TERM

${CC:-cc} -std=c11 -Wall -Wextra -Werror \
	-I"$ROOT/l850gl-mm-bridge/src" \
	"$ROOT/tests/host-cell-mutation-policy.c" \
	"$ROOT/l850gl-mm-bridge/src/l850_mutation_policy.c" \
	-o "$BUILD_DIR/host-cell-mutation-policy"
"$BUILD_DIR/host-cell-mutation-policy"
