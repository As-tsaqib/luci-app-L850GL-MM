<!--
SPDX-FileCopyrightText: 2026 As Tsaqib
SPDX-License-Identifier: Apache-2.0
-->

# luci-app-L850GL-MM

LuCI 1.0.0-alpha frontend for the schema-4 `l850gl.mm` companion API. It installs
exactly Overview, Lock, and SMS below Modem / L850GL MM.

The browser uses only the shared typed RPC module. It never calls D-Bus,
`mmcli`, a shell, UCI, the filesystem, or a modem device. Every response must
have `schema: 4`, a valid envelope, and the expected object identity;
otherwise the UI shows a compatibility/malformed-response error and disables
mutations.

All three views use the same native LuCI `cbi-*` structure and one scoped,
theme-neutral responsive stylesheet. Desktop and phone layouts reflow the
same DOM and retain the same status fields, controls, and actions; no
viewport-specific JavaScript or hidden mobile/desktop copy is used. Focused
Lock and SMS editors are preserved across the ten-second poll.

The base methods are exactly:

```text
list_modems, get_overview, get_lock_status, set_bands, set_modes,
list_sms, send_sms, delete_sms
```

Overview displays the schema-4 USB mode, modem voltage when available, full
IMEI and ICCID, and the SIM number only when supplied by the base ModemManager
snapshot under the concise `Modem info by ModemManager` description. IMSI
remains deliberately hidden. On an expert build it also calls
the typed `get_carrier_info` method
every ten seconds and displays active LTE bands, active-carrier count, and
bounded per-carrier band/EARFCN/PCI details plus aggregate DL/UL bandwidth.
The total includes every active downlink and only uplinks actually reported by
the backend, rather than inventing secondary uplink values. A base build
has no expert object, so the same rows fail closed as unavailable. The browser
never receives raw command output or cellular location fields.
One validated carrier topology is retained for at most 30 seconds across the
reviewed retryable carrier states, so a valid 3CA/2CA/1CA transition replaces
the display on the next poll without requiring a page reload. Malformed,
schema-incompatible, stale-identity, and non-retryable responses remain
fail-closed.

Allowed/preferred mode selection persists through the exact bound netifd
interface and requires confirmation because activation reloads the mobile
network. Band Lock shows only LTE choices, keeps allowed non-LTE families
unrestricted internally, uses standard ModemManager bands, and requires
confirmation. The PCI
section discovers the optional `l850gl.mm.l850` expert object through its
status call. It remains disabled on the base build. In an expert build,
standard cell scan is independently gated from mutation. The exact
live-validated firmware can use the fixed set/clear/reprobe/verification state
machine; all other firmware remains fail-closed. Expert scans are single-flight
per modem with a five-second cooldown beginning only after completion. The
expert carrier query is independently single-flight against scans and mutations
and uses the same five-second completion-based cooldown; it never accepts
command text from LuCI.
EARFCN/PCI input validation updates only the Apply button on each `input` event,
preserving focus and cursor position; an empty PCI and PCI zero are both valid.

SMS polls the backend cache every 10 seconds and exposes All, Inbox, Outbox,
Draft, and Unknown. Each backend page is at most 100 messages; Load more follows
the opaque `next_cursor` so messages beyond the first page remain visible.
Focused compose input is preserved across polling. Send uses a browser CSPRNG
token and delete requires a confirmation modal. Numeric tokens are copyable;
a tap opens an exact-number conversation and a long press selects cards.
Selected and folder-wide deletion reuse the single-message API sequentially
and stop on the first unconfirmed result.

The exact ACL groups are:

```text
luci-app-l850gl-mm-overview
luci-app-l850gl-mm-sms-read
luci-app-l850gl-mm-sms-write
luci-app-l850gl-mm-lock-band
luci-app-l850gl-mm-lock-pci-expert
```

Run the dependency-free frontend checks from the repository root with:

```sh
node luci-app-l850gl-mm/tests/static.js
```
