<!--
SPDX-FileCopyrightText: 2026 As Tsaqib
SPDX-License-Identifier: Apache-2.0
-->

# luci-app-fibocom

Read-only LuCI companion for Fibocom modems owned by ModemManager. The P0 UI
contains **Overview**, **Status**, and **Settings** under **Modem -> Fibocom
Modem**.

The browser talks only to the typed `fibocom.mm` ubus facade supplied by
`fibocom-mm-bridge`:

- `list_modems`;
- `get_overview`;
- `get_status`;
- `get_capabilities`.

It does not call D-Bus, `mmcli`, AT tools, MBIM tools, files, shell commands, or
UCI directly. Its rpcd ACL grants the four read methods above and has no write
section. ModemManager remains responsible for discovery and modem objects;
netifd's ModemManager protocol remains responsible for automatic connection,
addresses, routes, and DNS.

Connection configuration is deliberately linked to **Network -> Interfaces**
instead of being duplicated. SMS, advanced controls, and optional eSIM
integration are future packages or milestones and are not represented as
working tabs in P0.

The package requires ModemManager built with MBIM and netifd support. Official
OpenWrt 25.12.5 builds the Fibocom plugin into ModemManager. A downstream build
that modularizes plugins must add its matching Fibocom plugin package at image
level; the package name is not portable across both layouts.

Run the dependency-free frontend checks with:

```sh
node tests/static.js
```
