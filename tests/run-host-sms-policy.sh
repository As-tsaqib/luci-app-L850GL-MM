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
build_dir=$(mktemp -d "${TMPDIR:-/tmp}/l850gl-sms-policy-test.XXXXXX")
pkg_config=${PKG_CONFIG:-pkg-config}
glib_cflags=$($pkg_config --cflags glib-2.0)
glib_libs=$($pkg_config --libs glib-2.0)

cleanup() {
	rm -rf "$build_dir"
}
trap cleanup EXIT HUP INT TERM

# shellcheck disable=SC2086
${CC:-cc} \
	-std=c11 -Wall -Wextra -Werror -Wpedantic \
	$glib_cflags \
	-I"$source_dir" \
	"$repo_root/tests/host-sms-policy.c" \
	"$source_dir/sms_policy.c" \
	$glib_libs \
	-o "$build_dir/host-sms-policy"

"$build_dir/host-sms-policy"
