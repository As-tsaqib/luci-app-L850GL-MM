#!/bin/sh
# SPDX-FileCopyrightText: 2026 As Tsaqib
# SPDX-License-Identifier: GPL-2.0-or-later

set -eu

repo_root=$(
	unset CDPATH
	cd -- "$(dirname -- "$0")/.."
	pwd
)
source_dir="$repo_root/fibocomd/src"
profile="$repo_root/fibocomd/files/usr/share/fibocom/profiles/l850-gl.json"
build_dir=$(mktemp -d "${TMPDIR:-/tmp}/fibocom-host-test.XXXXXX")

cleanup() {
	rm -rf "$build_dir"
}
trap cleanup EXIT HUP INT TERM

if ! pkg-config --exists glib-2.0 gio-2.0 json-c; then
	echo "host discovery test requires glib-2.0, gio-2.0, and json-c" >&2
	exit 77
fi

cflags=$(pkg-config --cflags glib-2.0 gio-2.0 json-c)
libs=$(pkg-config --libs glib-2.0 gio-2.0 json-c)

# Intentional word splitting: pkg-config emits compiler argument lists.
# shellcheck disable=SC2086
${CC:-cc} \
	-std=c11 -D_GNU_SOURCE -Wall -Wextra -Werror -Wpedantic \
	-I"$source_dir" $cflags \
	"$repo_root/tests/host-discovery.c" \
	"$source_dir/types.c" \
	"$source_dir/profile.c" \
	"$source_dir/discovery.c" \
	$libs \
	-o "$build_dir/host-discovery"

G_DEBUG=fatal-warnings "$build_dir/host-discovery" "$profile"
