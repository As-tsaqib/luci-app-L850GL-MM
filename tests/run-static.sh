#!/bin/sh
# SPDX-FileCopyrightText: 2026 As Tsaqib
# SPDX-License-Identifier: Apache-2.0

set -eu

ROOT=$(CDPATH='' cd -- "$(dirname "$0")/.." && pwd)
cd "$ROOT"

json_files=$(find . -type f -name '*.json' -not -path './.git/*' | sort)
for file in $json_files; do
	node -e 'JSON.parse(require("fs").readFileSync(process.argv[1], "utf8"))' "$file"
done

js_files=$(find . -type f -name '*.js' -not -path './.git/*' | sort)
for file in $js_files; do
	node --check "$file" >/dev/null
done

shell_files=$(find . -type f \( \
	-path '*/etc/init.d/*' -o \
	-name '*.sh' \
\) -not -path './.git/*' | sort)
for file in $shell_files; do
	sh -n "$file"
done

node tests/validate-package-contract.js
node luci-app-l850gl-mm/tests/static.js
node tests/generate-pot.js --check
sh tests/run-host-identity.sh
sh tests/run-host-hardware.sh
sh tests/run-host-network-binding.sh
sh tests/run-host-radio-policy.sh
sh tests/run-host-sms-policy.sh
sh tests/run-host-sms-dedupe.sh
sh tests/run-host-ubus-blob.sh
sh tests/run-host-cell-parser.sh
sh tests/run-host-ca-parser.sh
sh tests/run-host-voltage-parser.sh

if command -v shellcheck >/dev/null 2>&1; then
	for file in $shell_files; do
		shellcheck -S error -e SC1091 "$file"
	done
fi

if command -v reuse >/dev/null 2>&1; then
	reuse lint
fi

if command -v msgfmt >/dev/null 2>&1; then
	msgfmt --check-format -o /dev/null \
		luci-app-l850gl-mm/po/templates/l850gl-mm.pot
fi

if rg -n \
	'fs\.(exec|exec_direct)|cgi-io|/dev/(cdc-wdm|ttyACM)[0-9]+|killall|(^|[^A-Za-z])eval[[:space:]]*\(' \
	luci-app-l850gl-mm/htdocs \
	luci-app-l850gl-mm/root 2>/dev/null; then
	echo "forbidden LuCI execution or global-device pattern found" >&2
	exit 1
fi

echo "static checks passed"
