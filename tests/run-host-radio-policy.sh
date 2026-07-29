#!/bin/sh
# SPDX-FileCopyrightText: 2026 As Tsaqib
# SPDX-License-Identifier: GPL-2.0-or-later

set -eu

repo_root=$(
	unset CDPATH
	cd -- "$(dirname -- "$0")/.."
	pwd
)
source_dir="$repo_root/l850gl-mm-bridge/src"
build_dir=$(mktemp -d "${TMPDIR:-/tmp}/l850gl-radio-policy-test.XXXXXX")

cleanup() {
	rm -rf "$build_dir"
}
trap cleanup EXIT HUP INT TERM

${CC:-cc} \
	-std=c11 -Wall -Wextra -Werror -Wpedantic \
	-I"$source_dir" \
	"$repo_root/tests/host-radio-policy.c" \
	"$source_dir/radio_policy.c" \
	-o "$build_dir/host-radio-policy"

"$build_dir/host-radio-policy"
