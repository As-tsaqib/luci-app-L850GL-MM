<!--
SPDX-FileCopyrightText: 2026 As Tsaqib
SPDX-License-Identifier: Apache-2.0
-->

# Live router validation

> This page separates schema-1 v0.2 testing from 2026-07-19 and the approved
> L850 firmware command/recovery matrix from 2026-07-27. The latter validates
> the exact PCI grammar and hardware behavior; the installed package-level
> schema-2 acceptance follows below. No live SMS mutation was run.

## Scope

Validation was performed on 2026-07-19. Identifiers and secrets were not
recorded. The evidence proves the MBIM/ModemManager lifecycle and the explicitly
listed v0.2 companion-app reads/inbound-SMS observation on this hardware; it
does not claim every L850 firmware or OpenWrt release. SMS send/delete and
radio/band/SIM-slot mutations were not exercised. The historical eSIM probe is
retained only as provenance for a package retired in 0.3.

## Environment

| Item | Observed value |
|---|---|
| Router | Linksys EA6350v3 |
| CPU / OpenWrt target | ARMv7 / `ipq40xx/generic` |
| OpenWrt | 25.12.5, `r33051-f5dae5ece4` |
| Kernel | 6.12.94 |
| ModemManager | 1.24.0-r10 |
| LuCI protocol | `luci-proto-modemmanager` installed |
| Network protocol | `proto modemmanager` |
| Modem | Fibocom L850-GL |
| Firmware | `18500.5001.00.05.27.30` |
| Plugin | `fibocom` |
| Composition | MBIM, `2cb7:0007` |

## Approved PCI command/recovery matrix, 2026-07-27

The user provided alternate hotspot access and explicitly approved scan, lock,
clear, reset, and reprobe testing. Every modem command went through
ModemManager's serialized queue; no TTY, MBIM device, sysfs path, or direct
port was opened by the test. Stock ModemManager rejected `Command` at INFO
with `Core.Unauthorized`, so debug mode was enabled temporarily for discovery
and restored afterward. The production expert artifact instead rebuilds
ModemManager with its reviewed AT-via-D-Bus option.

Firmware help returned the typed signature:

```text
freq_lock(sim_id, rat, band, inter_frequency_lock_enable, frequency, psc_pci)
```

The exact target-firmware grammar proven live is:

```text
set exact:   AT@SIC:FREQ_LOCK(0,3,<logical-band>,1,<earfcn>,<pci>)
set EARFCN:  AT@SIC:FREQ_LOCK(0,3,<logical-band>,1,<earfcn>,65535)
clear:       AT@SIC:FREQ_LOCK(0,3,255,0,65535,65535)
state query: AT@NVM:DYN_CPS.NAS_ASM.FREQ_LOCK_PARAMS.*??
apply/reset: AT+CFUN=15
scan:        AT+XMCI=1
```

Set and clear returned exactly
`Frequency Lock Configuration Success CPS_MSG_TYPE_ASM_EM_CTRL_CNF`.
Locked NVM state used `rat=3`, `band_info=0`,
`inter_freq_lock_support=1`, the requested frequency, and the requested PCI or
`65535`. Clear state used frequency/PCI `65535` and
`inter_freq_lock_support=0`. The observed `band_info` is `0`, not `255`.

The sanitized result matrix was:

| Test | Result |
|---|---|
| Fixed XMCI scan | Type-4 serving and type-5 neighbors returned; blank lines, quoted hex, signed/sentinel RSSNR, and discarded-field sentinels observed |
| Clear + reset | NVM clear state survived reprobe; registration and bearer recovered |
| Exact current cell | NVM and post-reset serving EARFCN/PCI matched |
| Exact visible neighbor | Serving cell changed to the requested band-1 EARFCN/PCI |
| EARFCN-only | NVM PCI wildcard `65535`; modem registered on the requested EARFCN |
| Final restoration | Clear/reset restored automatic serving selection and connected bearer |

`CFUN=15` returned the expected post-dispatch cancellation, removed the old
ModemManager object, and produced a replacement in roughly 14--15 seconds.
The exact internal hardware slot, Fibocom plugin, model, revision, MBIM
composition, and `2cb7:0007` attestation were stable. Raw TAC/cell ID,
subscriber identifiers, addresses, and logs were not retained.

Not exercised in this matrix: unavailable-cell registration timeout, physical
unplug during the operation, ModemManager restart mid-state-machine, and full
router-reboot persistence. At handoff the modem was clear/automatic,
registered with a connected bearer, ModemManager was back at INFO, and all
temporary debug files were removed.

## Version 0.4 pre-install serving-cell probe, 2026-07-28

With the installed schema-2 expert package still active, one explicitly
authorized read-only `cell_scan` reconfirmed `method = l850-xmci`, exactly one
validated type-4 serving LTE cell, and bounded type-5 neighbors. This proves
that the live modem still lacks standard CellInfo and supplies the source
evidence for the schema-3 serving cache. It does not validate the 0.4 package,
does not authorize automatic XMCI polling, and made no band, mode, PCI, or SMS
mutation.

## Installed schema-3 package validation, 2026-07-28

Static run `30314503929` and OpenWrt SDK run `30314503962` passed for source
commit `06eb8df`. The SDK built and verified separate base/expert bridge
artifacts, rebuilt the pinned router-matched ModemManager 1.24.0-r10 expert
package, proved the base binary omitted `fibocom.mm.l850`, and proved the expert
binary contained it. Artifact checksums passed locally and again on the router.

The expert APK transaction reinstalled ModemManager 1.24.0-r10 and upgraded
both companion packages to 0.4.0-r1. ModemManager and the bridge were explicitly
restarted. Runtime tables were exactly eight base methods and four expert
methods:

```text
fibocom.mm:
  list_modems, get_overview, get_lock_status, set_bands, set_modes,
  list_sms, send_sms, delete_sms

fibocom.mm.l850:
  cell_scan, cell_lock_status, set_cell_lock, clear_cell_lock
```

The sanitized schema-3 result matrix was:

| Test | Result |
|---|---|
| Package/API | bridge 0.4.0; matching LuCI 0.4.0-r1; schema 3; exact method tables |
| Persistent combined mode | `3g|4g`, preferred `4g`; committed/read back; `activation = reloaded`; connected bearer retained |
| Persistent 4G-only mode | `4g`, preference `none`; live ModemManager modes matched; connected/home bearer retained |
| Restoration | returned to persisted/live `3g|4g`, preferred `4g`; connected/home bearer |
| Fail closed | stale generation returned non-retryable `stale_generation`; inconsistent `4g` + preferred `4g` returned `invalid_argument` before write |
| LTE-only Band Lock | all 24 supported LTE bands submitted; backend preserved 5 hidden UTRAN bands; 29-band live result and connected bearer |
| Serving Cell | standard CellInfo reported unavailable; explicit schema-3 XMCI scan populated validated Overview cache with one serving cell |
| PCI expert read | exact firmware capability available; existing `configured_earfcn` state observed and left untouched |
| SMS read | schema-3 cache `ready`, not truncated, dedupe 64/300; no number/body recorded |
| Ownership/recovery | netifd up/available/not pending, proto modemmanager, one ModemManager daemon, one bridge, INFO logging |
| Installed UI/ACL | packaged minified schema-3 API/widgets/mode/serving code present; menu and five exact ACL files matched source hashes |

No SMS send/delete or PCI set/clear/reset was run. The existing PCI lock was not
changed. The bridge logged zero warning/error entries during acceptance. A
private network-config backup and pre-upgrade binaries were retained only in a
mode-0700 router `/tmp` directory; no credential or subscriber value entered
this repository or the test output.

## Installed schema-2 package validation, 2026-07-27

Static run `30261750255` and OpenWrt SDK run `30261750513` passed for source
commit `307fcde`. Artifact `SHA256SUMS` passed locally and again on the router.
The base bridge binary was verified to omit `fibocom.mm.l850`; the expert
artifact contained the expert bridge, matching LuCI package, and a rebuilt
OpenWrt ModemManager 1.24.0-r10. Its recipe was pinned to OpenWrt packages
commit `d011c4fb8af70795928937ad5195479cc4ff6de9`, matching the installed router
package release instead of downgrading to the older SDK feed recipe.

The three expert packages installed in one APK transaction. ModemManager was
explicitly restarted so the new binary, not the pre-upgrade process, handled
the test. It remained at INFO log level. Runtime method tables were exactly:

```text
fibocom.mm:
  list_modems, get_overview, get_lock_status, set_bands,
  list_sms, send_sms, delete_sms

fibocom.mm.l850:
  cell_scan, cell_lock_status, set_cell_lock, clear_cell_lock
```

The sanitized installed-package result matrix was:

| Test | Result |
|---|---|
| Initial expert status | `available`, mutable, exact NVM clear state |
| Typed cell scan | `scan_ready` via `l850-xmci`; one type-4 serving cell and bounded type-5 neighbors |
| Typed exact current-cell set | `applied_verified`; replacement opaque identity plus registration, NVM, serving EARFCN/PCI, and band postconditions |
| Stale pre-reset identity | Rejected as non-retryable `stale_identity` before dispatch |
| Cooldown | New identity reported `busy`, non-mutable, with bounded `retry_after_ms` |
| Typed clear | `cleared_verified`; second replacement identity plus registration and exact NVM clear postcondition |
| HTTP rpcd path | A least-privilege `/ubus` session returned schema 2 for `list_modems` and expert clear status; rpcd-injected session metadata was accepted |
| UI/ACL package layout | Only Overview, Lock, SMS views; five exact ACL grants; no legacy view or wildcard grant |
| Final restoration | NVM clear, modem connected/home, one bearer, power on, ModemManager INFO |

The system log contained no raw XMCI, FREQ_LOCK, or NVM command text and no
bridge warning/failure from the operation. No opaque identities, subscriber
identifiers, addresses, or raw scan output are retained in this document.
Interactive browser rendering, Band Lock mutation, live SMS send/delete, and
the fault/persistence cases listed above were not exercised by this package
acceptance run.

## Staged v0.2 application validation

The v0.2 base and optional eSIM menu packages installed successfully. The
bridge registered `fibocom.mm` and reported version 0.2.0. Typed read-only
`list_modems`, `get_overview`, `get_status`, and `get_capabilities` calls
returned successful schema-1 responses for the admitted L850-GL. The results
confirmed the Fibocom plugin, MBIM composition, connected bearer, and exact
netifd ownership binding without retaining a stable hardware identifier,
credential, subscriber identifier, phone number, or assigned address.

The r2 staged smoke test used source commit `a803c8c`: static run `29692009885`
and OpenWrt SDK run `29692009880` both passed. Downloaded `SHA256SUMS` passed
for the two-package base and five-package optional eSIM artifacts. The router
was upgraded to `fibocom-mm-bridge` 0.2.0-r2 and the matching noarch LuCI base
and eSIM alias. Its native ModemManager, lpac, and `luci-app-lpac` packages were
not replaced. After restarting only the companion bridge, one ModemManager
daemon remained and the netifd interface stayed up.

An authenticated browser test then exposed a transport-contract bug that the
direct local ubus smoke test could not reveal. LuCI appends a canonical
`ubus_rpc_session` field before forwarding each JSON-RPC call; r2's strict
parser treated that transport field as an unknown product argument. All tabs
therefore showed `list_modems does not accept arguments` even though direct
local calls succeeded.

A temporary rpcd ucode proxy restored only the five read methods while the
permanent fix built. Source commit `5fc095d` accepts and ignores exactly one
canonical session transport field in every parser while still rejecting
malformed, duplicate, and unknown fields. Static run `29694605507` and the
base-only SDK run `29694615964` passed; every optional eSIM build step was
skipped. The downloaded base checksums passed and only
`fibocom-mm-bridge` 0.2.0-r3 was installed.

Before removing the proxy, direct authenticated HTTP tests passed for list,
overview, status, capabilities, and SMS. A deliberately invalid send request
reached product validation and was rejected before any ModemManager operation,
proving that the write parser also accepted the transport metadata without
sending an SMS. The temporary proxy, ACL, files, and APK were then removed,
rpcd was restarted, and the same five HTTP reads passed directly against
`fibocom.mm`. The original LuCI API module was restored, the stored inbound SMS
remained visible, and netifd stayed up.

The SMS cache was initially `ready` with count zero. After one externally
delivered SMS, it updated automatically to count one and classified the entry
as inbound/inbox. Cache revision advanced from 4 to 12; a revision delta is not
a message count because Added, property changes, list completion, and periodic
reconciliation may each increment it. No SMS number, body, opaque ID, or D-Bus
path was recorded.

With the count held at one, revision-only updates were observed approximately
29–30 seconds apart. This proves that the configured 30-second reconciliation
runs live. Signals were not deliberately suppressed, so this does not by
itself prove repair of a missed signal. An open LuCI SMS view polls the bridge
cache every 10 seconds; that browser behavior is source/static-test evidence,
not a measured browser-render latency in this router session.

The optional Fibocom eSIM menu alias resolved to the already installed
`luci-app-lpac` UI. A typed read-only chip-information request succeeded and
returned an EID with the required 32-decimal-digit shape; the value was
discarded and was not recorded. The MBIM proxy remained enabled.

The existing ModemManager/netifd connection remained up before and after
package installation, incoming-SMS observation, and the read-only eUICC probe.
This does not establish connection behavior for SMS, Advanced, or eSIM
mutations.

Observed ModemManager port grouping:

```text
cdc-wdm0  mbim
ttyACM0   at
ttyACM1   ignored
ttyACM2   at
wwan0     net
```

The runtime names above are observations, not stable configuration keys.

## Replug timeline

Sanitized log timeline:

| Router time (UTC) | Event |
|---|---|
| 06:56:47 | `cdc_mbim` unregisters `wwan0`; WDM/net ports are released |
| 06:56:48 | AT ports are removed; netifd brings `modem` down |
| 06:58:18 | kernel registers `cdc-wdm0` and `wwan0` again |
| 06:58:18–19 | OpenWrt hotplug reports WDM, net, and three AT ports |
| 06:58:20 | ModemManager probes the MBIM control port |
| 06:58:23 | a new ModemManager object is created with the expected grouping |
| 06:58:25 | `ModemManager-monitor` marks network interface `modem` available |
| 06:58:26 | netifd successfully enables the modem |
| 06:58:28 | netifd registers and connects a bearer |
| 06:58:29 | netifd applies IP configuration and reports `modem` up |

Elapsed time from first add event to interface up was approximately eleven
seconds.

## Later automatic bearer recovery

A second read-only check at 09:11:50 UTC found the same modem connected and
both IPv4 and IPv6 online. The latest log sequence was:

| Router time (UTC) | Event |
|---|---|
| 08:58:22 | a disconnected event makes netifd lower and clean the logical interfaces |
| 08:58:23 | the existing modem object is available and enabled again |
| 08:58:25 | network registration and allowed-mode setup complete |
| 08:58:26 | a new data bearer connects on `wwan0`; IPv4 is up |
| 08:58:27 | the dynamic DHCPv6 interface is up |

This recovery took about five seconds. It proves automatic bearer/netifd
recovery, but it was not a USB remove/add event and must not be presented as a
second physical-replug trace.

## Final state

Allowlisted status fields showed:

```text
ModemManager state       connected
power                    on
access technology        LTE
registration             home
packet service           attached
bearer connected         yes
bearer interface         wwan0
IPv4 default route       present
IPv6 default route       present
netifd up                true
netifd available         true
netifd pending           false
netifd protocol          modemmanager
connect attempts         1
failed attempts          none
```

Messaging reported storage `mt`, so the native ModemManager SMS interface is
available on the tested combination. The later check found zero stored SMS;
no message path or content was recorded. This proves API availability only,
not the companion bridge's automatic SMS synchronization or send/delete paths.

The recorded netifd state came from direct read-only router inspection. The
historical v0.2 bridge did not export dynamic logical-interface up/down state,
uptime, or traffic counters; schema 1 reported those fields as unavailable.
Schema 2 intentionally omits that diagnostic/network-status surface.

## Capability probes

Two read-only probes establish the boundary for cell control:

```text
mmcli -m any --get-cell-info
→ Core.Unsupported: base stations info is not supported

mmcli -m any --command="AT@SIC:FREQ_LOCK()??"
→ Core.Unauthorized: operation only allowed in debug mode
```

The second command is command introspection, not a lock request. The rejection
confirms that the live OpenWrt build uses the safe default where generic AT over
D-Bus is unavailable.

## Source correlation

The live sequence matches the OpenWrt and ModemManager source:

1. hotplug sends `ReportKernelEvent`;
2. ModemManager probes and exports the modem object;
3. `ModemManager-monitor` maps the physical device to a configured netifd
   interface and calls `proto_set_available`;
4. netifd runs `proto_modemmanager_setup`;
5. setup enables/registers and requests `Simple.Connect`;
6. netifd applies the returned IP settings;
7. the connection hook cycles the logical interface after a bearer-down event.

Therefore “automatic detection and dial” is correct shorthand, while the exact
owner model is:

```text
ModemManager: device and bearer
OpenWrt monitor/netifd: automatic orchestration and network state
```

## Privacy procedure

The following were deliberately excluded from stored evidence:

- IMEI and device identifiers;
- IMSI, ICCID, EID, and phone numbers;
- SMS paths/content;
- APN, username, password, and PIN;
- assigned IP, gateway, and DNS values;
- eSIM activation or confirmation secrets.

Future support bundles must use the same allowlist approach instead of dumping
raw `mmcli -K`, `uci show network`, or unredacted logs.
