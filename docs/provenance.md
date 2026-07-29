<!--
SPDX-FileCopyrightText: 2026 As Tsaqib
SPDX-License-Identifier: Apache-2.0
-->

# Provenance

This repository is a clean-room companion built from public ModemManager,
OpenWrt, libubox, and LuCI APIs plus sanitized hardware observations. Reference
projects provide protocol facts, failure lessons, and UX ideas; their source is
not copied.

## Authoritative implementation sources

- documented libmm-glib/ModemManager D-Bus APIs;
- OpenWrt ubus/rpcd, netifd, package, and LuCI public interfaces;
- sanitized L850 observations and bounded synthetic fixtures;
- original implementation and tests authored for this repository.

The base project links OpenWrt's native unmodified ModemManager package. The
expert CI artifact rebuilds and ships that same upstream recipe with its
existing `MODEMMANAGER_WITH_AT_COMMAND_VIA_DBUS` option enabled; it carries no
ModemManager source patch or fork. netifd and `luci-proto-modemmanager` remain
upstream packages and retain connection ownership.

## Audit snapshot ledger

Commit IDs are evidence anchors, not source-import points.

| Reference | Snapshot/review | Used for |
|---|---|---|
| [FUjr/QModem](https://github.com/FUjr/QModem) | `3bd54c1334a66461587ca89b22ee2f71129fcad3` | lifecycle/ownership failure lessons |
| [As-tsaqib/L850GL-XModem](https://github.com/As-tsaqib/L850GL-XModem) | `eca5d92d31555777c8984ac3fe5b294eec0c9b39` | L850 USB/MBIM/NCM evidence |
| [nyawitniorang/XModem](https://github.com/nyawitniorang/XModem) | `12cd90055525ab9f97644190a659645a47c5e244` | UX ideas, GTCAINFO field hints, and parser/security risks |
| [ofmodemsandmen/RooterSource](https://github.com/ofmodemsandmen/RooterSource) | `ef9f007abe22345f1f646add0d812fd7ade652a5` (`fibocomdata350.sh`) | independent GTCAINFO 14-field primary/10-field secondary grammar |
| [ModemManager](https://gitlab.freedesktop.org/mobile-broadband/ModemManager) | `3568fb91a856d5e8de15dc7b2c2b80eecb46eb8e` plus 1.24 public API | Fibocom/XMM plugin and typed bands/SMS/cell-info behavior |
| [OpenWrt packages](https://github.com/openwrt/packages) | `9f76dfc43c63392621b44951a0a17f8d75245751` | ModemManager packaging/netifd integration |
| [OpenWrt LuCI](https://github.com/openwrt/luci) | `112388301e8b920a7532065c498700131990dd13` | view/menu/ACL/i18n conventions |
| [OpenWrt UCI](https://git.openwrt.org/project/uci.git) | `74f6277aabffc943d026f406df57c22595134c42` | pinned host helper tests only |
| [OpenWrt libubox](https://git.openwrt.org/project/libubox.git) | `e7608b69283d919d031d13cc8e21692503f5dbea` | pinned malformed-blob behavioral tests |
| [Fibocom L860/L8 manual mirror](https://www.manualslib.com/manual/1655076/Fibocom-L860-Gl.html?page=167) | pages 167-169, reviewed 2026-07-19 | XMCI field schema and cell types |
| [Fibocom 850/860 community wiki](https://wiki.vps-server.ru/doku.php/wiki:openwrt:fibocom-850) | reviewed 2026-07-19 | independent lock-signature reports |
| [Artnet Fibocom discussion](https://forum.artnet.biz/threads/fibocom-l860-gl-16-lte-obsuzhdeniye-proshivka.444/) | reviewed 2026-07-19 | firmware-dependent apply reports |
| [Drive2 L860 carrier-lock trace](https://www.drive2.ru/b/706927153861633966/) | reviewed 2026-07-19 | independent sentinel/lock examples |
| [4PDA Fibocom L8x0-GL discussion](https://4pda.to/forum/index.php?showtopic=1066668) | direct posts below, reviewed 2026-07-27 | community-only lock grammar, sentinels, and failure evidence |
| [4IceG/luci-app-modemband](https://github.com/4IceG/luci-app-modemband) | `9d2477269726` | band-control UX/research only |
| [4IceG/luci-app-3ginfo-lite](https://github.com/4IceG/luci-app-3ginfo-lite) | `cbc144e527b0` | independent L850 measurement evidence |
| [obsy/modemband](https://github.com/obsy/modemband) | `b49538773fcc` | independent XACT mapping evidence |
| [lastik9/openwrt-fibocom-l850](https://github.com/lastik9/openwrt-fibocom-l850) | `f5b900674fe0` | independent recovery warning |
| [mrhaav/openwrt-packages](https://github.com/mrhaav/openwrt-packages) | `392fe64ed2ba` | independent L850 NCM RAW-IP evidence |
| [lutfailham96/xmm-modem](https://github.com/lutfailham96/xmm-modem) | `b914028dafa8` | Intel XMM NCM comparison |

### 4PDA post ledger

- [121801248](https://4pda.to/forum/index.php?showtopic=1066668&view=findpost&p=121801248):
  six-field help signature plus conflicting band/apply suggestions, without an
  exact firmware.
- [126464498](https://4pda.to/forum/index.php?showtopic=1066668&view=findpost&p=126464498)
  and [126477912](https://4pda.to/forum/index.php?showtopic=1066668&view=findpost&p=126477912):
  L850 NVM state, candidate clear/wildcard sentinels, persistence, and
  frequency-only behavior; no target-firmware recovery matrix.
- [129670484](https://4pda.to/forum/index.php?showtopic=1066668&view=findpost&p=129670484),
  [129670766](https://4pda.to/forum/index.php?showtopic=1066668&view=findpost&p=129670766),
  and [129673899](https://4pda.to/forum/index.php?showtopic=1066668&view=findpost&p=129673899):
  registration-cycle failure, confounded recovery, and a candidate clear
  response.
- [139046345](https://4pda.to/forum/index.php?showtopic=1066668&view=findpost&p=139046345):
  two-stage exact-PCI then wildcard report without firmware or recovery proof.
- [142119412](https://4pda.to/forum/index.php?showtopic=1066668&view=findpost&p=142119412),
  [142133341](https://4pda.to/forum/index.php?showtopic=1066668&view=findpost&p=142133341),
  [143570066](https://4pda.to/forum/index.php?showtopic=1066668&view=findpost&p=143570066),
  and [143656918](https://4pda.to/forum/index.php?showtopic=1066668&view=findpost&p=143656918):
  target-firmware reports that are temporary, unsuccessful, or unresolved
  rather than a complete live-verified set/clear result.

## Facts retained

- L850 MBIM is USB `2cb7:0007`; NCM is `8087:095a`.
- ModemManager Fibocom rules and XMM plugin own port grouping and internal XACT
  handling; the companion must not reimplement it.
- MBIM is the supported production composition. L850 NCM bearer support is not
  established in ModemManager and is not a companion feature.
- ModemManager provides typed asynchronous bands, Messaging/Sms, and
  GetCellInfo APIs used by 0.6.
- The schema-4 base Overview obtains USB composition, equipment identifier,
  OwnNumbers, IMSI, and ICCID only from normalized libmm-glib properties. The
  explicit product-owner identifier disclosure does not authorize logging or
  fixture/evidence capture.
- Carrier parsing independently validates the reported band's DL and UL EARFCN
  ranges. No active B29/B32 target-firmware capture exists, so their
  downlink-only uplink/sentinel form is not inferred from XModem, RooterSource,
  or a generic band table and remains fail-closed.
- The XMCI LTE field schema and community lock-command family were candidate
  evidence only. The 2026-07-27 local target-firmware matrix independently
  proved logical band encoding, PCI wildcard `65535`, clear tuple, NVM state,
  `CFUN=15`, reprobe/registration, and serving-cell postconditions.
- Stock OpenWrt keeps generic AT-over-D-Bus disabled. The base 0.6 build must
  keep it disabled and omit the expert object.
- Firmware `18500.5001.00.05.27.30` is the sole PCI mutation allowlist entry,
  based on the dated local matrix rather than a public post.

A USB ID, manual, community trace, synthetic fixture, or successful parser test
does not establish hardware mutation support. Every future allowlist claim must
name exact model, firmware, composition, fixture, date, command/clear/reset
matrix, recovery result, and serving-cell postcondition.

## Excluded source and patterns

No source is copied from QModem, L850GL-XModem, XModem, Quectel-CM variants,
third-party modem LuCI applications, or shell modem tools. Specifically
excluded are:

- raw-AT/shell backends, custom dialers, SMS databases/PDU stacks, bots,
  watchdogs, and reset ladders;
- broad ACL or command consoles;
- hard-coded TTY/WDM/netdev, ModemManager index/path, sysfs path, or board USB
  controller path;
- undocumented fallback chains and guessed firmware tuples.

XModem's inconsistent license notices reinforce the clean-room boundary. At
snapshot `12cd900`, its carrier implementation uses shell/direct-TTY command
paths and counts an inactive secondary sentinel as a carrier. Neither its
implementation nor that behavior was copied. Only user-visible concepts and
independently corroborated protocol facts informed the new typed parser; the
independent RooterSource 14/10-field grammar and local sanitized modem response
were separate checks.

## Implementation record

- 2026-07-19: the original P0/P1 shadow core introduced sysfs discovery and a
  custom-owner architecture. It is preserved at annotated tag
  `archive/shadow-p0-p1-d2430f8` and removed from the active tree after the
  ownership decision changed to ModemManager/netifd.
- 2026-07-19: schema-1 v0.2 added the first libmm-glib bridge, opaque identities,
  native SMS, standard band/radio/reset/SIM-slot experiments, and broader LuCI
  pages. Historical CI runs and staged router details remain in
  `live-router-validation.md`.
- 2026-07-19: staged v0.2.0-r3 live testing validated authenticated schema-1
  reads, the rpcd session-field fix, one incoming SMS cache update, and the
  30-second reconciliation cadence. It did not run SMS send/delete or radio,
  band, reset, SIM-slot, or PCI mutations.
- 2026-07-27: version 0.3.0 replaces the public contract with schema 2 and
  exactly Overview, Lock, and SMS. The base object is reduced to seven methods;
  Status, Settings, old Advanced, radio/reset/SIM-slot API, and the eSIM addon
  are retired.
- 2026-07-27: 0.3 adds strict structural blob validation/tests, honest bounded
  SMS dedupe eviction/expiry, UI pagination, compact Overview, Band Lock, and a
  separately gated `fibocom.mm.l850` API with cell parser fixtures and
  asynchronous standard GetCellInfo.
- 2026-07-27: direct 4PDA posts were re-reviewed. They refined the candidate
  matrix but their incompatible tuple/apply claims were not copied as proof.
- 2026-07-27: an approved local matrix on the exact L850-GL MBIM firmware proved
  XMCI, exact and EARFCN-only set, clear, `CFUN=15`, NVM state, object
  replacement, registration/bearer recovery, and serving-cell postconditions.
  The fixed typed grammar and one-entry allowlist were then implemented.
- 2026-07-27: the expert SDK artifact was changed to rebuild/package the
  unmodified OpenWrt ModemManager recipe with its reviewed command-transport
  option; the base artifact keeps that option disabled.
- 2026-07-27: SDK run `30261750513` pinned the router-matched OpenWrt
  ModemManager 1.24.0-r10 recipe commit
  `d011c4fb8af70795928937ad5195479cc4ff6de9`. The checksum-verified expert
  artifact was installed and schema-2 XMCI, exact set, replacement identity,
  stale-identity rejection, clear, NVM/registration/serving postconditions,
  least-privilege HTTP `/ubus`, and final connected-bearer recovery passed.
- 2026-07-28: version 0.4.0 advances the base contract to schema 3 and eight
  methods. `set_modes` persists only `allowedmode`/`preferredmode` on the
  uniquely internally resolved netifd interface and activates through an
  asynchronous network reload. Lock now exposes LTE band choices only while
  retaining allowed non-LTE families. Overview gains a generation-bound,
  freshness-limited serving-cell cache without automatic XMCI fallback.
- 2026-07-28: one authorized pre-install expert scan on the target router
  reconfirmed that standard CellInfo is unsupported and the installed schema-2
  expert path selects `l850-xmci`; no mode, band, PCI, or SMS mutation was run.
- 2026-07-28: static run `30314503929` and SDK run `30314503962` passed for
  `06eb8df`. Checksum-verified v0.4 expert artifacts were installed. Schema-3
  combined/4G-only mode persistence and restoration, LTE-only Band Lock with
  UTRAN preservation, validated Serving Cell cache, and SMS read metadata were
  exercised with netifd/bearer recovery. The existing PCI configuration was
  observed but not mutated; no SMS write was run.
- 2026-07-28: static run `30348512717` and OpenWrt 25.12.5
  `ipq40xx/generic` SDK run `30348512557` passed for
  `10fa6a6868bd9ee423ad3473a897107194f8481e`. Checksum-verified bridge and
  LuCI 0.4.0-r2 packages were installed; the identical existing expert
  ModemManager 1.24.0-r10 was not reinstalled. A 50 ms overlap batch returned
  exactly one `scan_ready` and one retryable `busy` without `retry_after_ms`.
  The five-second completion cooldown decreased from 4933 ms to 3888 ms across
  a measured 1040 ms interval without rejection extending it, then three
  sequential post-cooldown XMCI scans returned bounded 4/5/5-cell results with
  the modem connected. No PCI set/clear/reset, Band Lock, mode, or SMS mutation
  was run. The release also includes separately requested LuCI/SMS-view changes;
  this live matrix validates only the expert backend scan delta.
- 2026-07-28: an earlier r2 harness attempt used an unsupported fractional
  BusyBox `sleep` and abandoned its client after dispatch. The modem later
  entered disabled/low power with a netifd `Invalid` loop; a ModemManager
  service restart, bounded netifd settle cycles, and one ModemManager power-on
  attempt did not recover it, while a physical replug did. The clean acceptance
  followed the replug. The temporal sequence is retained as a harness/recovery
  caveat and is not evidence that the scan caused the state change.
- 2026-07-28: version 0.5.0 advances the contract to schema 4 while retaining
  the same eight base methods. At the product owner's explicit direction,
  Overview adds normalized USB mode plus full bounded IMEI, SIM number, IMSI,
  and ICCID through the authenticated Overview ACL. The expert object grows to
  five methods with typed read-only `get_carrier_info`; the base build still
  contains no expert object or command path.
- 2026-07-28: before implementation, an approved read-only target-modem query
  returned a 14-field index-1 B3 primary at EARFCN 1325 / PCI 381 with 20 MHz
  bandwidth and an exact inactive index-2 sentinel, so the normalized active
  carrier count was one. `GTUSBMODE` value 7 and ModemManager composition both
  identified MBIM. No identifier or raw response is retained in repository
  evidence. This establishes the target grammar/input evidence only.
- 2026-07-28: static run `30365531206` and SDK run `30365531207` passed for
  schema-4 commit `56dbc0e`. Checksummed 0.5.0-r1 packages were installed and
  the sanitized UI/API, CA admission/cooldown, ownership, privacy, and
  served-asset checks passed. This installed evidence remains distinct from
  the earlier command-level capture.
- 2026-07-29: version 0.6.0 renames the active repository/application to
  `luci-app-L850GL-MM`, packages to `luci-app-l850gl-mm` and
  `l850gl-mm-bridge`, and ubus objects to `l850gl.mm` / `l850gl.mm.l850`.
  The retired package, service, ACL, and ubus names remain only as migration
  and historical evidence and are prohibited from coexisting with 0.6.
- 2026-07-29: version 0.6 adds a strict bounded parser and expert-build
  asynchronous fixed `AT+CBC` query for nullable Overview voltage in
  millivolts. The provided response shape and synthetic valid/invalid fixtures
  establish parser input coverage only; SDK installation and router acceptance
  remain separate evidence until recorded after they occur.

Historical v0.2 schema-1 results remain explicitly labeled and are not
rewritten as schema-2 package evidence. Firmware command-level live evidence
and installed schema-2 ubus/LuCI evidence are also reported separately.
