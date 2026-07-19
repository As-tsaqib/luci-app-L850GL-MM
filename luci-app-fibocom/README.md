<!--
SPDX-FileCopyrightText: 2026 As Tsaqib
SPDX-License-Identifier: Apache-2.0
-->

# luci-app-fibocom

> **Legacy implementation notice:** this directory still describes the P0/P1
> shadow frontend at commit `d2430f8`. PRD 3.1 supersedes its `fibocomd` API,
> menu layout, and custom discovery ownership. It is not the target
> ModemManager companion UI and must be rewritten rather than extended.

Modern LuCI JavaScript frontend for the cached, typed `fibocomd` ubus API.
The P0/P1 frontend is intentionally limited to shadow-mode inventory, status,
capabilities, sanitized diagnostics, and a manually requested rescan.

Security boundaries:

- no Lua controller or CBI model;
- no shell, `cgi-io`, filesystem, raw AT, MBIM utility, or lpac access;
- no direct UCI access;
- the read ACL contains only cached `fibocom` methods;
- the write ACL contains only the typed `fibocom.rescan` method.

Connection settings remain under **Network -> Interfaces**. The Settings page
in this package is explanatory and read-only until the network protocol and
connection lifecycle are enabled after shadow-mode validation.

Run the dependency-free static checks with:

```sh
node tests/static.js
```
