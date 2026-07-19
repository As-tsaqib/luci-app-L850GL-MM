<!--
SPDX-FileCopyrightText: 2026 As Tsaqib
SPDX-License-Identifier: Apache-2.0
-->

# luci-app-fibocom

LuCI companion for Fibocom modems managed by ModemManager, initially scoped to
the Fibocom L850-GL in USB MBIM mode.

## Current direction

ModemManager and OpenWrt netifd own the modem connection:

```text
hotplug → ModemManager → ModemManager-monitor → netifd proto modemmanager
                                                   │
                                             MBIM bearer/IP

LuCI → typed fibocom-mm-bridge → libmm-glib → ModemManager
```

The v0.2.0 beta base application provides:

- Overview
- Status
- SMS through ModemManager Messaging
- Advanced controls through typed ModemManager APIs
- Settings with read-only `/etc/config/network` correlation and a link to
  `luci-proto-modemmanager`
- optional eSIM integration through the user's `luci-app-lpac`

It does not dial, create bearers, scan sysfs, or open modem device nodes.
QModem, XModem runtime, Quectel-CM, `sms-tool`, and custom netifd protocols are
not dependencies.

## Verified hardware baseline

Live testing on OpenWrt 25.12.5 with ModemManager 1.24.0-r10 and an L850-GL
proved automatic unplug/replug recovery:

- ModemManager detected and grouped MBIM, AT, and network ports;
- the OpenWrt monitor marked the configured network interface available;
- netifd automatically registered and connected the modem;
- the MBIM interface returned online in about eleven seconds.

The router is a Linksys EA6350 v3: ARMv7, OpenWrt target
`ipq40xx/generic`, package architecture `arm_cortex-a7_neon-vfpv4`.

Production connectivity scope:

| Composition | USB ID | Status |
|---|---|---|
| MBIM | `2cb7:0007` | supported through ModemManager |
| NCM | `8087:095a` | connectivity unsupported by ModemManager |

NCM requires a bearer implementation in ModemManager. This LuCI project will
not hide a second custom dialer behind the UI.

## Advanced controls

Band lock uses standard ModemManager `SetCurrentBands`; the XMM plugin handles
`XACT` internally. Radio, reset, and SIM slot selection are also standard
capability-gated ModemManager operations. Current/supported modes are displayed
read-only; persistent allowed/preferred modes are changed through the existing
`proto modemmanager` network section so netifd remains the single owner.

The bridge correlates a modem to `/etc/config/network` with a read-only,
exact-match libuci lookup. It does not return APN, PIN, credentials, or device
paths. Direct radio enable/disable is unavailable when an exact
`proto modemmanager` binding exists, because netifd owns the persistent intent
and could otherwise immediately reverse the change. Use **Network ->
Interfaces** for a managed modem.

L850 PCI/EARFCN controls need an optional expert build. Neighbor scan and cell
lock must run through ModemManager's internal AT queue, never by opening
`ttyACM` concurrently. OpenWrt disables AT commands over D-Bus by default, so
the feature remains unavailable unless the image deliberately enables and
secures that capability. The base bridge does not compile or expose the expert
ubus object.

The XModem implementation is useful evidence but contains parser, sentinel,
validation, reset, and post-lock verification gaps. See
[docs/pci-cell-lock.md](docs/pci-cell-lock.md).

## SMS

SMS uses ModemManager's Messaging and Sms D-Bus interfaces. There is no
`sms-tool` fallback because a second AT/SMS owner can race ModemManager.
ModemManager 1.24 also contains an L850-specific MBIM multipart-index
workaround.

Incoming and changed messages synchronize automatically: the bridge consumes
ModemManager `Added`, `Deleted`, and SMS property-change signals, with a
30-second inventory reconciliation fallback. The SMS view polls the bridge's
cache every 10 seconds, so no manual refresh is normally required. A poll does
not replace a focused compose editor; its cached update is rendered as soon as
focus leaves the editor. If the safety cache is truncated, entries are sorted
newest-first before the 1,024-message bound is applied. Delivery is still
bounded by ModemManager, modem, SIM/storage, and browser polling latency.

Send retry tokens are held only in bridge memory for five minutes. They reduce
accidental immediate duplicates, but do not promise exactly-once delivery
after that window or after a bridge restart.

## Optional eSIM

The base package has no lpac dependency. Selecting
`luci-app-fibocom-esim` will depend on `luci-app-lpac` and reuse its views
and typed backend. No TgBot/Telegram component is included.

The initial eSIM claim is single-L850 with MBIM proxy enabled. Stable
multi-modem WDM identity binding remains future work.

## Repository status

The previous shadow discovery/direct-dialer foundation is preserved at tag:

```text
archive/shadow-p0-p1-d2430f8
```

The active v0.2.0 beta source contains the typed `fibocom-mm-bridge`, LuCI
Overview/Status/SMS/Advanced/Settings views, separate least-privilege ACLs,
automatic SMS cache synchronization, standard ModemManager mutations, exact
L850 MBIM hardware attestation, and read-only UCI network binding. The old
`fibocomd`, custom netifd protocol, sysfs profiles, and rescan UI have been
removed from the active tree.

This is still a development checkpoint, not a finished package. SMS and
standard Advanced are implemented in the current worktree, but this version
has not yet been cross-built by GitHub Actions or installed/tested through live
ubus/libmm-glib on the router. The earlier successful SDK run built commit
`5dd9697`, before these v0.2.0 changes; it is evidence for the build pipeline,
not for the current beta. OpenWrt dynamic logical-interface state and traffic
counters are also not implemented: schema 1 reports that `openwrt` status as
unavailable and exposes only a separate, read-only UCI ownership correlation.

Read [the architecture](docs/architecture.md),
[ubus API contract](docs/ubus-api.md), and
[live-router validation](docs/live-router-validation.md) for the public design
and evidence record.

## Current package layout

```text
fibocom-mm-bridge         v0.2.0 typed libmm-glib/GDBus ↔ ubus adapter
luci-app-fibocom         Overview/Status/SMS/Advanced/Settings
luci-app-fibocom-esim    optional menu alias to luci-app-lpac
```

Expected base runtime dependencies include ModemManager,
`luci-proto-modemmanager`, GLib/libmm-glib, libubus/libubox, libuci,
`kmod-usb-acm`, `kmod-usb-wdm`, and
`kmod-usb-net-cdc-mbim`.

The project uses the native, unmodified ModemManager package supplied by
OpenWrt. It does not fork, patch, or ship ModemManager source; the bridge merely
links to its public libmm-glib API.

## Development

```sh
make check
```

Cross-compilation is defined in
[`.github/workflows/openwrt-sdk.yml`](.github/workflows/openwrt-sdk.yml). It
uses the official OpenWrt 25.12.5 `ipq40xx/generic` SDK and uploads a mandatory
base bundle plus an optional MBIM-only eSIM bundle. The SDK filename ends in
`.Linux-x86_64` because GitHub Actions is the build host; generated bridge
packages target the router's ARMv7 architecture.

Run `29684338522` successfully cross-built the older sanitized commit
`5dd9697` for OpenWrt 25.12.5 and produced the expected base and optional eSIM
artifacts. A fresh run for the v0.2.0 SMS/Advanced implementation is pending.
The static workflow pins OpenWrt libuci commit
`74f6277aabffc943d026f406df57c22595134c42` only to compile the host-side
network-binding tests; target packages use OpenWrt's native libuci.

The active suite covers the package/API boundary, LuCI ACL and views, CSPRNG
identity, hardware attestation, network-binding and band-policy helpers,
shell/JSON/JavaScript syntax, translation format, and the sanitized live
fixture. A release still requires a green current SDK build, target
ubus/libmm-glib tests, and staged live validation of read/SMS/Advanced behavior.

## Licensing

- backend and system integration: GPL-2.0-or-later;
- LuCI JavaScript and documentation: Apache-2.0;
- no QModem/XModem source is copied.

Every source file must carry SPDX metadata and pass REUSE validation.
