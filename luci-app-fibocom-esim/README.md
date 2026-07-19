<!--
SPDX-FileCopyrightText: 2026 As Tsaqib
SPDX-License-Identifier: Apache-2.0
-->

# luci-app-fibocom-esim

Optional menu integration for the user's `luci-app-lpac` package. It adds an
eSIM entry below the Fibocom application and redirects to the existing lpac
views and typed backend.

This package contains no lpac fork, eSIM operation, iframe, Telegram/TgBot
component, or modem-port access. The base `luci-app-fibocom` package does not
depend on lpac.

The first supported deployment is a single L850-GL in MBIM mode with lpac's
MBIM backend and proxy enabled. Device ambiguity must fail closed in
`luci-app-lpac`; this menu package does not select a WDM path.
