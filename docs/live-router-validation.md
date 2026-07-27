<!--
SPDX-FileCopyrightText: 2026 As Tsaqib
SPDX-License-Identifier: Apache-2.0
-->

# Live router validation

> Historical record only: this page documents schema-1 v0.2 testing performed
> on 2026-07-19. It does not validate the schema-2 0.3.0 source or packages.
> No result below may be cited as live validation of current PCI, Band Lock, or
> SMS mutation behavior.

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
