<!--
SPDX-FileCopyrightText: 2026 As Tsaqib
SPDX-License-Identifier: Apache-2.0
-->

# Live router validation

> This page separates schema-1 v0.2 testing from 2026-07-19, the approved L850
> firmware command/recovery matrix from 2026-07-27, and the dated schema-2 and
> schema-3/schema-4 package acceptances that followed, and the renamed v0.6
> acceptance boundary. No live SMS mutation was run.

## Scope

The initial lifecycle validation was performed on 2026-07-19; later work is
reported only in its explicitly dated sections. Identifiers and secrets were
not recorded. The evidence proves the MBIM/ModemManager lifecycle and only the
listed companion behavior on this hardware; it does not claim every L850
firmware or OpenWrt release. The historical eSIM probe is retained only as
provenance for a package retired in 0.3.

## Installed 0.6.0-r6 PCI persistence acceptance, 2026-07-29

Static run `30457585212` and OpenWrt SDK run `30457585130` passed for source
`6ce14aaef45da9094a7413bd89401296dfb8d634`. Static CI compiled and ran the
new pure NVM policy matrix together with the complete host/static suite. SDK
CI built and binary-checked separate base and expert OpenWrt 25.12.5
`ipq40xx/generic` variants. The base binary omitted the expert object and
fixed command grammar; the expert binary retained the exact reviewed surface.

| Installed expert artifact | SHA-256 |
|---|---|
| `l850gl-mm-bridge-0.6.0-r6.apk` | `6f8fd90f63aab8d01913bbd6d69304b19fa2c3e71c06220c17e6e5e7d894d484` |
| `luci-app-l850gl-mm-0.6.0-r6.apk` | `69a31b3ded34ecabbfa55d3d93a04196ac0efdc9a898527f48bd6434b5e78ed8` |
| `modemmanager-1.24.0-r10.apk` | `9b46039b963f5766a0a13d767ff77e978f590d2d50226183bd609f462112e60c` |

All downloaded and router-staged checksums matched. An APK simulation admitted
exactly the r5-to-r6 bridge/LuCI upgrade. The expert ModemManager artifact was
byte-identical to the installed package, so it was not reinstalled and the
daemon process was preserved.

The preceding r5 live cycle exposed the failure this release addresses. The
first clear received the exact firmware acknowledgement, completed reset,
reprobe, and registration, but its single immediate post-reset NVM read still
reported the previous exact lock and terminated as `verification_mismatch`.
An identical second clear with fresh identity succeeded. Release r6 therefore
does not resend a mutation. It requires two consecutive matching NVM reads one
second apart before reset, then uses a ten-second observation deadline while
repeating only the read-only NVM query after registration.

An isolated comparator changed only the fixed reset literal to `AT+CFUN=1,1`.
Static run `30456402024` and SDK run `30456402102` passed for comparator source
`c8d7d57e73e983be2b0b5ed2f0644e9aff0911d2`; its expert bridge SHA-256 was
`52235e2e24286bb67b1b27d54b7a04dcdf9092a9a38e8be056244d97ef4f477e`.
Only that bridge was temporarily installed. ModemManager and LuCI were not
replaced.

| Live cycle | Set durations (ms) | First clear durations (ms) | Result |
|---|---:|---:|---|
| r5 comparator, `CFUN=1,1`, no NVM barrier | 16554, 16728, 17179 | 16940, 16478, 16992 | 3/3 `applied_verified`; 3/3 first-attempt `cleared_verified` |
| r6 production, `CFUN=15`, NVM barrier | 17597, 17636, 17345 | 17808, 17376, 17792 | 3/3 `applied_verified`; 3/3 first-attempt `cleared_verified` |

Every operation produced a new attested ModemManager object/generation while
preserving the ModemManager daemon and router uptime. Registration, connected
state, exact NVM, and set serving-cell postconditions passed; netifd recovered
to up/available/not-pending after each reset. The comparator establishes that
`CFUN=1,1` also performs the required modem replacement on this firmware. It
does not establish a reason to replace the already reviewed production reset:
the r6 timings include the intentional one-second persistence barrier and the
observed first-clear mismatch did not recur in any of the three r6 cycles.
Production therefore retains one fixed `CFUN=15` and has no reset fallback or
ladder.

Final installed acceptance established:

- schema 4 with exactly eight base/five expert methods, exact five ACL names,
  and exactly Overview, Lock, and SMS menu children;
- invalid PCI 504 rejected as `invalid_argument` and a stale generation
  rejected as `stale_generation`, both before mutation dispatch;
- final PCI NVM state `clear`, modem connected/home, power on, connected
  bearer, and netifd up/available/not-pending;
- all 29 supported bands remained current, with allowed `3g|4g` and preferred
  `4g`; no Band/Mode mutation was needed;
- typed voltage recovered to `3550 mV`; SMS cache was `ready` without exposing
  a number or body; no live SMS send/delete was run;
- one 3-carrier B5+B1+B3 result, an immediate accurate `rate_limited` response
  with `retry_after_ms=4734`, and successful recovery after that deadline;
- installed and loopback-served hashes matched for API, widgets, CSS, Overview,
  Lock, and SMS assets;
- one bridge and one ModemManager process, no retired object, no picocom
  process, and zero recent bridge warning/error or ModemManager error line.

The final mutation target was the validated current serving B3 EARFCN
1325/PCI 381. Raw command responses, cell/subscriber identifiers, addresses,
and SMS content were not retained.

## Installed 0.6.0-r4 final acceptance, 2026-07-29

Static run `30434665005` and OpenWrt SDK run `30434665450` passed for source
`955a3d0e14001e63ac5b002001c6f8a967fae82b`. Static CI compiled and passed
the complete host suite, including malformed ubus blobs, the CA parser's
derived two-carrier fixture, coherent 3CA/2CA/1CA LuCI transitions, bounded
retryable-cache behavior, and immediate EARFCN/PCI input validation. SDK CI
built and binary-checked base and expert OpenWrt 25.12.5 `ipq40xx/generic`
variants. The base artifact omitted the expert object and fixed AT commands;
the expert artifact retained exactly the reviewed command paths.

| Installed expert artifact | SHA-256 |
|---|---|
| `l850gl-mm-bridge-0.6.0-r4.apk` | `51929cdce4b1f3477d767cbfecb5eb46ca3c571b0e24119fa9141036f08d15e5` |
| `luci-app-l850gl-mm-0.6.0-r4.apk` | `1ef556f8902834302f914a8c3723fba757752e3bbf230d19dd201d048fd61f39` |
| `modemmanager-1.24.0-r10.apk` | `9b46039b963f5766a0a13d767ff77e978f590d2d50226183bd609f462112e60c` |

Both downloaded `SHA256SUMS` files and both `BUILD_INFO.txt` manifests passed
locally. The manifests identify schema 4, release `0.6.0-r4`, the exact source
commit, and the absent/present expert-object split. Router-staged bridge and
LuCI hashes matched again. An APK simulation admitted exactly the r3-to-r4
bridge/LuCI upgrades. The ModemManager artifact was byte-identical to the
already installed expert package, so it was not reinstalled and its PID was
preserved; the bridge PID was replaced.

Installed acceptance established:

- one ModemManager and one bridge process, exact eight base/five expert method
  tables, schema-4 Overview/Lock success, power on, and a connected bearer;
- no retired ubus object and no recent bridge warning/error line;
- installed and loopback-served Overview hash
  `3824d6725e2a406018ae4599f3ac4e184ff75389eb897f9488d37a86b7284e6b`
  and Lock hash
  `869404aa4abc5eebdf7e1cc308a67febd0c772f7ebec873bbdbebe978db738af`;
- served Overview contained the concise `Modem info by ModemManager`
  description and reviewed retryable CA states; served Lock contained the
  input-only Apply-button updater and the `0..70545` EARFCN bound;
- one carrier success returned live 3CA B5+B3+B1, its immediate retry returned
  accurate `rate_limited` with `retry_after_ms=4992`, and an inventory
  replacement rejected the old opaque identity as `stale_identity`;
- eight further queries, each resolving fresh identity/generation and spaced
  six seconds apart, all returned `available` with the live 3CA topology and
  no malformed, busy, or unavailable result.

No cell scan, SMS operation, band/mode mutation, PCI mutation, reset, direct
TTY access, or bearer operation was run during r4 acceptance.

## Installed 0.6.0-r3 CA acceptance, 2026-07-29

Static run `30423022209` and OpenWrt SDK run `30423022228` passed for source
`a1047fcbdf248692fc76ed891e574d6150154ebe`. Static CI compiled and passed the
complete host suite, including live active-secondary SINR `127`, primary `127`
rejection, secondary `128`/`255` rejection, and `127` combined with an invalid
PCI. SDK CI built and verified base/expert OpenWrt 25.12.5
`ipq40xx/generic` variants. The base binary omitted the expert object and fixed
AT commands; the expert binary contained only the reviewed expert paths.

| Artifact | SHA-256 |
|---|---|
| `l850gl-mm-bridge-0.6.0-r3.apk` | `c0bb5db429583c6a3deb050106b81de82e2651abe0c8c344d2fb4db60198a2a8` |
| `luci-app-l850gl-mm-0.6.0-r3.apk` | `60121616dad9979a157e13a311c508324888fbb8e4ce613ae25e12821b29e251` |
| `modemmanager-1.24.0-r10.apk` | `9b46039b963f5766a0a13d767ff77e978f590d2d50226183bd609f462112e60c` |

The hashes matched locally and again in router staging. An APK simulation
admitted exactly the r2-to-r3 bridge/LuCI upgrades. The byte-identical
ModemManager package was not reinstalled and its PID remained unchanged. The
bridge restarted on the r3 binary and republished the exact eight base/five
expert schema-4 methods. Old ubus objects remained absent; one bridge and one
ModemManager process ran, no picocom process existed, and recent bridge logs
contained no warning/error line.

Read-only runtime acceptance established:

- 20 carrier queries at six-second intervals all returned `available`, with
  real 3-carrier to 1-carrier to 3-carrier transitions and zero malformed,
  busy, or rate-limited sample;
- three-carrier output was B5 primary plus B3/B1 secondaries, with both
  secondary UL bandwidths explicitly null; one-carrier output omitted the
  inactive secondaries;
- one success followed by an immediate retry returned accurate
  `rate_limited` (`retry_after_ms=4717`), then recovered to `available` just
  after that deadline;
- five carrier-first Overview/Lock cycles kept all three typed snapshots valid,
  the bearer connected, and voltage available as `3550 mV` after its initial
  asynchronous refresh;
- standard CellInfo remained unavailable, but the actual LuCI validator
  accepted a sanitized live response and the actual renderer displayed
  B5+B3+B1, `10/10`, `20/—`, `15/—`, and an effective available serving
  EARFCN/PCI fallback instead of any unavailable CA row;
- r3 APK-manifest hashes for Overview/widgets matched installed files and
  loopback-served bodies; served code contained explicit-null validation,
  carrier-first polling, and em-dash rendering.

Restarting rpcd invalidated previous authenticated sessions, so authenticated
HTTP RPC was not fabricated or rerun; only the anonymous session remained.
The installed exact ACL and local ubus path were verified, and the previous r1
authenticated HTTP acceptance remains scoped evidence for the unchanged RPC
route. No scan, SMS mutation, Band/Mode mutation, PCI mutation, reset, or
bearer operation was run during r3 acceptance.

## Installed 0.6.0-r2 CA diagnostic, 2026-07-29 (not accepted)

Static run `30420544115` and OpenWrt SDK run `30420570825` passed for source
`f5cfa3e99ff4fbaf28af5a07d992c5b3d4a1b968`. The expert bundle hashes were:

| Artifact | SHA-256 |
|---|---|
| `l850gl-mm-bridge-0.6.0-r2.apk` | `97f8bc72ddbdf4938c6f4a58d64b8796dea4236b84792d1cde9b9d9ccfa3f97a` |
| `luci-app-l850gl-mm-0.6.0-r2.apk` | `2ade043d2b607911a875f3d87c52213271e90b70fac971306a3ad280c8b4b680` |
| `modemmanager-1.24.0-r10.apk` | `9b46039b963f5766a0a13d767ff77e978f590d2d50226183bd609f462112e60c` |

The bridge/LuCI pair upgraded from r1 to r2 after an exact APK simulation;
ModemManager was byte-identical and its PID was preserved. The bridge PID was
replaced and the exact 8/5 schema-4 method tables republished. The first live
typed query returned three carriers (B5 primary plus B3/B1 secondaries) and
both secondary UL bandwidths as explicit null. However, only seven of eight
queries spaced six seconds apart succeeded; one failed closed as
`malformed_response`. Ten additional sanitized read-only command observations
showed that active secondaries intermittently report SINR code `127` while
their band, PCI, DL EARFCN/bandwidth, copied primary UL EARFCN, and UL sentinel
remain valid. R2 is therefore diagnostic evidence, not final acceptance; r3
adds only this live-verified unavailable-SINR case. No scan, SMS mutation,
Band/Mode mutation, PCI mutation, reset, or bearer operation was run.

## Installed 0.6.0-r1 schema-4 acceptance, 2026-07-29

Static run `30416321185` and OpenWrt SDK run `30416321209` passed for source
commit `47836330574cf69a09e9829a98ac40897caf9c59`. Static CI compiled and passed
identity, hardware attestation, network binding, radio, SMS policy/dedupe,
malformed ubus blob, cell, carrier, and voltage host tests. SDK CI built base
and expert OpenWrt 25.12.5 `ipq40xx/generic` variants. Binary checks proved the
base bridge omitted the expert object plus `AT+GTCAINFO?` and `AT+CBC`, while
the expert bridge contained only the reviewed fixed command paths.

The checksum-verified expert artifact contained:

| Artifact | SHA-256 |
|---|---|
| `l850gl-mm-bridge-0.6.0-r1.apk` | `a9841f04c477a2d3807cafa898b073ac16d314ef24aec493e1ef61857d03d22b` |
| `luci-app-l850gl-mm-0.6.0-r1.apk` | `f06bcb4440b41687640b3e2cba5133091330c0d06f28927a39b6a1c8197d4486` |
| `modemmanager-1.24.0-r10.apk` | `9b46039b963f5766a0a13d767ff77e978f590d2d50226183bd609f462112e60c` |

The ModemManager artifact was byte-identical to the already installed reviewed
expert package and was not reinstalled. A rollback pair and the current old-UI
snapshot were checksum-verified in `/tmp` before migration. The retired service
was stopped and disabled, its LuCI/bridge packages were removed, then the two
renamed packages were installed. The router exposed exactly the eight base and
five expert methods under `l850gl.mm` / `l850gl.mm.l850`; the old packages,
process, files, and `fibocom.mm` objects were absent. rpcd and uhttpd were
restarted after removing the exact LuCI index-cache file.

Read-only runtime validation established:

- modem state `connected`, power `on`, connected bearer, and live parsed
  voltage `3550 mV` from the nullable Overview field;
- three carrier queries spaced six seconds apart all returned `available`; an
  immediate fourth returned accurate retryable `rate_limited`, followed by
  `available` after the reported deadline;
- standard CellInfo remained unavailable on this firmware, while three full
  Overview/Lock/carrier cycles at ten-second intervals each produced an
  effective `available` Serving Cell through the validated carrier fallback;
- cell-lock status and one-message SMS-page metadata reads succeeded without
  running scan, SMS write/delete, Band/Mode Lock, or PCI mutation;
- the menu contained exactly L850GL MM / Overview / Lock / SMS and all five
  installed ACL groups retained exact non-wildcard permissions;
- all six served CSS/JS hashes matched the APK manifest, the bridge and
  ModemManager each had one process, picocom had none, and three bridge log
  lines contained zero errors and zero private-data labels.

The following sections are immutable historical records for the retired
pre-rename packages and objects.

## Installed 0.5.0-r1 schema-4 acceptance, 2026-07-28

Static run `30365531206` and OpenWrt SDK run `30365531207` passed for source
commit `56dbc0eb4f59c2686ef31001c8376c6091b7c281`. The SDK built OpenWrt
25.12.5 `ipq40xx/generic` base and expert variants. Its binary checks proved
that the base bridge omitted both `fibocom.mm.l850` and `AT+GTCAINFO?`, while
the expert bridge contained the object, `get_carrier_info`, and the fixed
command. Downloaded expert-package hashes, verified again on the router, were:

| Artifact | SHA-256 |
|---|---|
| `fibocom-mm-bridge-0.5.0-r1.apk` | `b38b228f1a2c119154c3de3fa9718c3535698bf9ff711f6b82d47496b1dc61fa` |
| `luci-app-fibocom-0.5.0-r1.apk` | `485d3101ed439ef7442fea421fa288cfc294c046c85a7b065150bf11e0571bbe` |
| `modemmanager-1.24.0-r10.apk` | `9b46039b963f5766a0a13d767ff77e978f590d2d50226183bd609f462112e60c` |

An APK simulation admitted only the bridge and LuCI upgrades. The installed
expert ModemManager package was byte-identical to the artifact and therefore
was not reinstalled. The transaction upgraded the companion packages to
0.5.0-r1, refreshed the LuCI/rpcd cache, and republished both ubus objects in
two seconds. Runtime tables were exactly:

```text
fibocom.mm:
  list_modems, get_overview, get_lock_status, set_bands, set_modes,
  list_sms, send_sms, delete_sms

fibocom.mm.l850:
  cell_scan, get_carrier_info, cell_lock_status,
  set_cell_lock, clear_cell_lock
```

The sanitized schema-4 acceptance matrix was:

| Test | Observed result |
|---|---|
| Overview envelope | schema 4; MBIM; modem connected/on; SIM present |
| Identifier contract | `list_modems` contained no private identifier keys; authenticated Overview returned IMEI/IMSI/ICCID lengths 15/15/19 without printing their values |
| SIM number | ModemManager OwnNumbers was empty; the always-present UI row therefore rendered the honest `Unavailable` state rather than inventing a number |
| Carrier state | one validated active carrier, primary B3 at EARFCN 1325 / PCI 381; no active secondary |
| Serving fallback | backend standard serving cache remained unavailable; LuCI's validated primary-carrier fallback supplied the displayed serving EARFCN/PCI without launching XMCI |
| Parallel carrier reads | exactly one `available` and one retryable `busy` |
| Completion cooldown | immediate retry was `rate_limited` with `retry_after_ms = 4981`; a retry after six seconds was `available` |
| Installed UI | three menu children; exact ACL; all requested Overview rows present; concise capability badges and responsive CSS present |
| Served assets | loopback uhttpd hashes for Overview JS and shared CSS matched their installed package files |
| Ownership/health | one bridge, one ModemManager at INFO, bearer connected, netifd up/available/not pending with `proto modemmanager` |
| Privacy/logs | identifier values were absent from process arguments and logs; zero bridge warning/error entries were observed |

No cell scan, SMS read/send/delete, Band/Mode mutation, PCI set/clear, reset, or
traffic-generating test was run for this acceptance. Raw ubus responses,
opaque modem identifiers, subscriber values, addresses, and credentials were
not retained.

## Installed 0.4.0-r2 scan-cooldown acceptance, 2026-07-28

Static CI run `30348512717` and OpenWrt SDK run `30348512557` passed for source
commit `10fa6a6868bd9ee423ad3473a897107194f8481e`. The SDK target was OpenWrt
25.12.5 `ipq40xx/generic`. Downloaded expert-artifact hashes were:

| Artifact | SHA-256 |
|---|---|
| `fibocom-mm-bridge` | `c73084340684b2e5894bfa416f498352a06f729920c19dfb153ae0aa0ba60c48` |
| `luci-app-fibocom` | `afb29d100f331a61ca8bedcb9a2016d212cdb190d44ea55467f45ac0b9da85af` |
| `modemmanager` | `9b46039b963f5766a0a13d767ff77e978f590d2d50226183bd609f462112e60c` |

The bridge and LuCI packages were installed as 0.4.0-r2. The router already had
the identical expert ModemManager 1.24.0-r10 package, so ModemManager was not
reinstalled. Release r2 also contains separately requested LuCI and SMS-view
changes; the backend delta exercised in this section is limited to expert scan
admission and cooldown behavior.

The sanitized scan matrix was:

| Test | Observed result |
|---|---|
| 50 ms overlap batch | Exactly one `scan_ready` and one retryable `busy`; the busy response omitted `retry_after_ms` and therefore did not invent a completion estimate |
| Dispatched scan | `method = l850-xmci`, 4 normalized cells, 718-byte bounded response; the two-request batch completed in 50 ms |
| Immediate cooldown | `rate_limited`, `retry_after_ms = 4933` |
| Cooldown after 1040 ms | `rate_limited`, `retry_after_ms = 3888`; the reported drop was 1045 ms, a 5 ms measurement difference, and neither rejection extended the original deadline |
| Three post-cooldown scans | Each began after the five-second post-completion interval and returned `scan_ready` through `l850-xmci`; cell counts were 4/5/5, bounded response sizes were 722/847/839 bytes, and each recorded duration was 50 ms |
| Health after each scan | Modem remained connected |
| Final state | Modem connected/on/home, bearer connected, netifd up/available/not pending with `proto modemmanager`, current bands unchanged, cell lock clear, and scan capability available |

The first harness attempt is excluded from those acceptance measurements.
BusyBox did not support the harness's fractional `sleep`; the client was
abandoned after a scan had already dispatched. The modem later reported
disabled/low power and netifd entered an `Invalid` transition loop. A
ModemManager service restart, bounded netifd settle cycles, and one
ModemManager power-on attempt did not recover it. A physical replug restored
the modem, after which the clean matrix above passed. This temporal sequence
does not establish that the scan caused the modem state change; it records a
harness/recovery caveat so the discarded attempt is not silently presented as
a clean result.

No PCI set/clear/reset, Band Lock, mode, or SMS mutation was run. The evidence
retains only normalized state, counts, sizes, and timing; it does not retain
opaque IDs, raw cells, subscriber data, or network addressing.

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
