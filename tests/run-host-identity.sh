#!/bin/sh
# SPDX-FileCopyrightText: 2026 As Tsaqib
# SPDX-License-Identifier: GPL-2.0-or-later

set -eu

repo_root=$(
	unset CDPATH
	cd -- "$(dirname -- "$0")/.."
	pwd
)
source_dir="$repo_root/fibocom-mm-bridge/src"
build_dir=$(mktemp -d "${TMPDIR:-/tmp}/fibocom-identity-test.XXXXXX")

cleanup() {
	rm -rf "$build_dir"
}
trap cleanup EXIT HUP INT TERM

${CC:-cc} \
	-std=c11 -D_GNU_SOURCE -Wall -Wextra -Werror -Wpedantic \
	-I"$source_dir" \
	"$repo_root/tests/host-identity.c" \
	"$source_dir/identity.c" \
	-o "$build_dir/host-identity"

"$build_dir/host-identity"
