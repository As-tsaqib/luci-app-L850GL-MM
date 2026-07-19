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

The application will provide:

- Overview
- Status
- SMS through ModemManager Messaging
- Advanced controls through typed ModemManager APIs
- Settings backed by `/etc/config/network` and `luci-proto-modemmanager`
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

The working tree is being pivoted in reviewable commits. Until
`fibocom-mm-bridge` and the new LuCI views are implemented and target-built,
the repository must not be advertised as a finished package.

Read [PRD.md](PRD.md) for the product contract and [memory.md](memory.md) for
the persistent audit/checkpoint record.

## Planned package layout

```text
fibocom-mm-bridge         typed libmm-glib/GDBus ↔ ubus adapter
luci-app-fibocom         Overview/Status/SMS/Advanced/Settings
luci-app-fibocom-esim    optional luci-app-lpac integration
```

Expected base runtime dependencies include ModemManager,
`luci-proto-modemmanager`, GLib/libmm-glib, libubus/libubox,
`kmod-usb-acm`, `kmod-usb-wdm`, and
`kmod-usb-net-cdc-mbim`.

## Development

```sh
make check
```

The existing test suite belongs to the archived shadow implementation and will
be replaced alongside the pivot. A release requires OpenWrt SDK/buildroot
builds, typed API fixtures, ACL/privacy tests, malformed input tests, and
real-device MBIM replug validation.

## Licensing

- backend and system integration: GPL-2.0-or-later;
- LuCI JavaScript and documentation: Apache-2.0;
- no QModem/XModem source is copied.

Every source file must carry SPDX metadata and pass REUSE validation.
