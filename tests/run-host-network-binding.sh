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
build_dir=$(mktemp -d "${TMPDIR:-/tmp}/fibocom-network-binding-test.XXXXXX")

cleanup() {
	rm -rf "$build_dir"
}
trap cleanup EXIT HUP INT TERM

uci_cflags=
uci_libs=
if ${PKG_CONFIG:-pkg-config} --exists uci 2>/dev/null; then
	uci_cflags=$(${PKG_CONFIG:-pkg-config} --cflags uci)
	uci_libs=$(${PKG_CONFIG:-pkg-config} --libs uci)
elif [ -n "${LIBUCI_SOURCE_DIR:-}" ] &&
	[ -f "$LIBUCI_SOURCE_DIR/uci.h" ] &&
	[ -f "$LIBUCI_SOURCE_DIR/libuci.c" ]; then
	printf '%s\n' '/* generated for the host-only libuci test */' \
		>"$build_dir/uci_config.h"
	for source in libuci.c file.c util.c delta.c parse.c; do
		${CC:-cc} -std=gnu11 -Wall -Wextra \
			-Wno-unused-parameter \
			-I"$build_dir" -I"$LIBUCI_SOURCE_DIR" \
			-c "$LIBUCI_SOURCE_DIR/$source" \
			-o "$build_dir/${source%.c}.o"
	done
	${AR:-ar} rcs "$build_dir/libuci.a" \
		"$build_dir/libuci.o" \
		"$build_dir/file.o" \
		"$build_dir/util.o" \
		"$build_dir/delta.o" \
		"$build_dir/parse.o"
	uci_cflags="-I$build_dir -I$LIBUCI_SOURCE_DIR"
	uci_libs="-L$build_dir -luci -ldl"
else
	echo "network binding tests skipped: libuci host files unavailable"
	echo "set LIBUCI_SOURCE_DIR to an OpenWrt libuci source checkout"
	if [ "${CI:-}" = true ]; then
		echo "CI requires the pinned libuci behavioral test" >&2
		exit 1
	fi
	exit 0
fi

# Word splitting is intentional for compiler and pkg-config flag lists.
# shellcheck disable=SC2086
${CC:-cc} \
	-std=gnu11 -D_GNU_SOURCE -DFIBOCOM_NETWORK_BINDING_TESTING \
	-Wall -Wextra -Werror -Wpedantic \
	-I"$source_dir" $uci_cflags \
	"$repo_root/tests/host-network-binding.c" \
	"$source_dir/network_binding.c" \
	$uci_libs \
	-o "$build_dir/host-network-binding"

"$build_dir/host-network-binding"
