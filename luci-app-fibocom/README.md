<!--
SPDX-FileCopyrightText: 2026 As Tsaqib
SPDX-License-Identifier: Apache-2.0
-->

# luci-app-fibocom

LuCI 0.4.0 frontend for the schema-3 `fibocom.mm` companion API. It installs
exactly Overview, Lock, and SMS below Modem / Fibocom Modem.

The browser uses only the shared typed RPC module. It never calls D-Bus,
`mmcli`, a shell, UCI, the filesystem, or a modem device. Every response must
have `schema: 3`, a valid envelope, and the expected object identity;
otherwise the UI shows a compatibility/malformed-response error and disables
mutations.

The base methods are exactly:

```text
list_modems, get_overview, get_lock_status, set_bands, set_modes,
list_sms, send_sms, delete_sms
```

Allowed/preferred mode selection persists through the exact bound netifd
interface and requires confirmation because activation reloads the mobile
network. Band Lock shows only LTE choices, keeps allowed non-LTE families
unrestricted internally, uses standard ModemManager bands, and requires
confirmation. The PCI
section discovers the optional `fibocom.mm.l850` expert object through its
status call. It remains disabled on the base build. In an expert build,
standard cell scan is independently gated from mutation. The exact
live-validated firmware can use the fixed set/clear/reprobe/verification state
machine; all other firmware remains fail-closed.

SMS polls the backend cache every 10 seconds and exposes All, Inbox, Outbox,
Draft, and Unknown. Each backend page is at most 100 messages; Load more follows
the opaque `next_cursor` so messages beyond the first page remain visible.
Focused compose input is preserved across polling. Send uses a browser CSPRNG
token and delete requires a confirmation modal.

The exact ACL groups are:

```text
luci-app-fibocom-overview
luci-app-fibocom-sms-read
luci-app-fibocom-sms-write
luci-app-fibocom-lock-band
luci-app-fibocom-lock-pci-expert
```

Run the dependency-free frontend checks from the repository root with:

```sh
node luci-app-fibocom/tests/static.js
```
