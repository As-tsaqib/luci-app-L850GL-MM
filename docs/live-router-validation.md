<!--
SPDX-FileCopyrightText: 2026 As Tsaqib
SPDX-License-Identifier: Apache-2.0
-->

# Live router validation

## Scope

Read-only validation performed on 2026-07-19. Identifiers and secrets were not
recorded. The evidence proves the MBIM/ModemManager lifecycle on this hardware;
it does not claim every L850 firmware or OpenWrt release. It predates the
v0.2.0 SMS/Advanced implementation: no current `fibocom-mm-bridge` package was
installed, and no app SMS or Advanced mutation was exercised.

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
current bridge does not yet export dynamic logical-interface up/down state,
uptime, or traffic counters; schema 1 reports those fields as unavailable.

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
