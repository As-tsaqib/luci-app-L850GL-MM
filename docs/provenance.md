<!--
SPDX-FileCopyrightText: 2026 As Tsaqib
SPDX-License-Identifier: Apache-2.0
-->

# Provenance

The target project is a clean-room companion for public ModemManager and
OpenWrt APIs. Reference repositories provide protocol facts, hardware evidence,
failure lessons, and UX ideas; their source is not copied.

## Target implementation sources

- libmm-glib and documented ModemManager D-Bus APIs;
- OpenWrt ubus/rpcd, netifd, package, and LuCI public interfaces;
- the user's clean-room `luci-app-lpac` menu/RPC contract;
- sanitized observations from real L850 hardware;
- original code authored for this repository.

ModemManager is now a runtime dependency and linked API provider for the
planned native bridge. It is not forked, and its internal source structure is
not copied. netifd and `luci-proto-modemmanager` remain their upstream packages.

## Audit snapshot ledger

Commit IDs are evidence anchors, not source-import points.

| Reference | Snapshot/review | Used for |
|---|---|---|
| [FUjr/QModem](https://github.com/FUjr/QModem) | `3bd54c1334a66461587ca89b22ee2f71129fcad3` | upstream feasibility and lifecycle lessons |
| [As-tsaqib/L850GL-XModem](https://github.com/As-tsaqib/L850GL-XModem) | `eca5d92d31555777c8984ac3fe5b294eec0c9b39` | L850 USB/mode/MBIM/NCM evidence |
| [As-tsaqib/XModem](https://github.com/As-tsaqib/XModem) | `12cd90055525ab9f97644190a659645a47c5e244` | UX ideas, hardware behavior, and migration/security risks |
| [ModemManager](https://gitlab.freedesktop.org/mobile-broadband/ModemManager) | `3568fb91a856d5e8de15dc7b2c2b80eecb46eb8e` | authoritative Fibocom/XMM ports and standard API behavior |
| [OpenWrt packages](https://github.com/openwrt/packages) | audit snapshot `9f76dfc43c63392621b44951a0a17f8d75245751`; local lpac branch `9df1dcf49` | ModemManager/lpac packaging and netifd integration |
| [OpenWrt LuCI](https://github.com/openwrt/luci) | `112388301e8b920a7532065c498700131990dd13` | modern views, menu, ACL, i18n, protocol conventions |
| user `luci-app-lpac` | `19f31e7` | optional eSIM package/menu/RPC contract |
| [Fibocom L860/L8 AT manual mirror](https://www.manualslib.com/manual/1655076/Fibocom-L860-Gl.html?page=167) | pages 167–169, reviewed 2026-07-19 | authoritative XMCI response schema and cell-type meanings |
| [Fibocom 850/860 community wiki](https://wiki.vps-server.ru/doku.php/wiki:openwrt:fibocom-850) | reviewed 2026-07-19 | independent FREQ_LOCK signature and operational reports |
| [Artnet Fibocom discussion](https://forum.artnet.biz/threads/fibocom-l860-gl-16-lte-obsuzhdeniye-proshivka.444/) | reviewed 2026-07-19 | command introspection text and firmware-dependent apply behavior |
| [Drive2 L860 carrier-lock trace](https://www.drive2.ru/b/706927153861633966/) | reviewed 2026-07-19 | independent band/PCI sentinel and lock/unlock examples |
| [4IceG/luci-app-modemband](https://github.com/4IceG/luci-app-modemband) | `9d2477269726` | band-control UX and command research only |
| [4IceG/luci-app-3ginfo-lite](https://github.com/4IceG/luci-app-3ginfo-lite) | `cbc144e527b0` | independent L850 XMCI/RSRP parsing evidence |
| [obsy/modemband](https://github.com/obsy/modemband) | `b49538773fcc` | independent L850 XACT band mapping |
| [lastik9/openwrt-fibocom-l850](https://github.com/lastik9/openwrt-fibocom-l850) | `f5b900674fe0` | independent L850 recovery warning |
| [mrhaav/openwrt-packages](https://github.com/mrhaav/openwrt-packages) | `392fe64ed2ba` | independent L850 NCM RAW-IP evidence |
| [lutfailham96/xmm-modem](https://github.com/lutfailham96/xmm-modem) | `b914028dafa8` | Intel XMM NCM comparison |

The complete findings and unresolved claims are retained in `memory.md`.

## Exact facts retained

- L850 MBIM USB ID `2cb7:0007`.
- L850 NCM USB ID `8087:095a`.
- ModemManager Fibocom rules map MBIM AT interfaces `02/04/06` and NCM AT
  interfaces `00/02/04`.
- MBIM is the production/eSIM target.
- The audited ModemManager tree exposes standard modes/bands/SMS/reset APIs and
  has Fibocom/XMM support.
- Exact L850 NCM RAW-IP dialing is not established in that tree; therefore the
  product does not claim NCM data support.
- XMM `XACT` behavior is handled inside ModemManager; the new app does not
  implement the AT parser/builder.
- The XMCI schema and FREQ_LOCK function signature are independently
  corroborated. Exact sentinels, NVM status path, reset/apply sequence, and
  persistence remain firmware-specific and require live fixtures before the
  optional expert feature is enabled.

Every new hardware claim must name its firmware, fixture, test matrix, and
date. VID:PID or a synthetic test alone is not a hardware acceptance result.

## Excluded source and patterns

No source is copied from QModem, L850GL-XModem, XModem, Quectel-CM variants,
4IceG applications, or third-party modem shell scripts.

Specifically excluded:

- XModem raw AT and shell backends, SMS database/PDU implementation, TgBot,
  watchdog/reset ladders, and broad ACL;
- QModem/Quectel-CM binary/source integration;
- hard-coded `ttyACM`, `cdc-wdm`, `wwan`, ModemManager index, or board USB
  controller path;
- undocumented command fallback chains.

XModem's inconsistent license notices make clean-room reimplementation
especially important. Only user-observable UX concepts and independently
verified protocol facts may inform new code.

## Legacy implementation record

- 2026-07-19: original P0/P1 clean-room shadow core added at `187a9b0`, with
  sysfs discovery, strict L850 profile, typed read-only ubus, fail-closed custom
  proto scaffolding, and read-only LuCI.
- 2026-07-19: docs checkpoint `d2430f8` recorded that implementation.
- 2026-07-19: PRD 3.1 superseded the custom-owner architecture in favor of
  ModemManager/netifd ownership. The legacy source is preserved through
  annotated Git tag `archive/shadow-p0-p1-d2430f8` and was then removed from
  the active tree.

## New implementation record

- 2026-07-19: P0 added a read-only `fibocom-mm-bridge` using libmm-glib/GDBus,
  a random per-object identity, four typed ubus read methods, and
  Overview/Status/Settings LuCI views.
- 2026-07-19: sanitized live fixture
  `tests/fixtures/live/l850-mbim-connected.json` records the tested MBIM state
  without subscriber identifiers, credentials, addresses, or message content.
- 2026-07-19: optional `luci-app-fibocom-esim` adds only an alias/dependency to
  the user's `luci-app-lpac`; it does not copy or wrap lpac operations.

The P0 source has passed host/static and ModemManager 1.24 header syntax checks,
but not an OpenWrt SDK cross-build or live installation. Add a dated record for
each later API, hardware quirk, fixture source, or dependency patch.
