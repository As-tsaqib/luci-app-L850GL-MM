<!--
SPDX-FileCopyrightText: 2026 As Tsaqib
SPDX-License-Identifier: Apache-2.0
-->

# Architecture

## System boundary

`luci-app-fibocom` is a ModemManager companion. It does not implement a modem
or network connection state machine.

```text
                         ┌─────────────────────────┐
                         │ LuCI                    │
                         │ Overview Status SMS     │
                         │ Advanced Settings       │
                         └────────────┬────────────┘
                                      │ typed ubus
                         ┌────────────▼────────────┐
                         │ fibocom-mm-bridge       │
                         │ cache + normalization   │
                         │ validation + actions    │
                         └────────────┬────────────┘
                                      │ libmm-glib / GDBus async
                         ┌────────────▼────────────┐
                         │ ModemManager            │
                         │ ports/SIM/SMS/radio/    │
                         │ registration/bearer     │
                         └───────┬─────────┬───────┘
                                 │         │
                           USB MBIM/AT     │ OpenWrt monitor
                                           │
 /etc/config/network ───────── netifd proto modemmanager
                                           │
                                  address/route/DNS

 Optional eSIM:
 LuCI menu → luci-app-lpac → lpac → mbim-proxy → L850 eUICC
```

## Why no custom discovery or dialer

The tested OpenWrt stack already:

- forwards kernel events to ModemManager;
- groups the L850 WDM, net, and AT ports;
- recreates the modem after unplug/replug;
- maps it to a configured netifd interface;
- automatically enables, registers, and connects a bearer;
- applies IP configuration and recovers the logical interface.

A second sysfs scanner or bearer daemon would duplicate identity, lifecycle,
retry, and port ownership. The bridge observes ModemManager objects instead.

## Component responsibilities

### ModemManager

- kernel-event consumption and device probing;
- physical-device and port association;
- AT and MBIM port ownership/serialization;
- SIM and registration state;
- signal, modes, bands, power, reset, and SIM slots;
- Messaging/Sms store and send lifecycle;
- bearer creation, connect, disconnect, and cleanup.

### OpenWrt monitor and netifd

- mark configured interfaces available when the physical modem appears;
- execute `proto modemmanager` setup/teardown;
- retain connection intent from `/etc/config/network`;
- install address, route, DNS, MTU, and metric;
- expose logical interface status and firewall membership;
- trigger reconnect after a bearer-down event.

### fibocom-mm-bridge

- create one async `MMManager` client on the system D-Bus;
- subscribe to object-added/object-removed and relevant property/signals;
- normalize allowlisted fields into a stable ubus schema;
- generate an opaque random public `modem_id` for each admitted object;
- cache read-mostly snapshots for LuCI polling;
- perform typed SMS and Advanced operations;
- validate identity and capability again immediately before every mutation;
- serialize application-originated mutations per modem;
- enforce timeouts, cooldowns, privacy, and stale-object failure.

It never calls:

- `Simple.Connect`;
- bearer Create/Connect/Disconnect/Delete;
- shell `mmcli`;
- `mbimcli`;
- direct `/dev/ttyACM*` or `/dev/cdc-wdm*`;
- custom hotplug or netifd scripts.

### LuCI

- renders normalized data;
- gathers typed input;
- requires explicit confirmation for disruptive actions;
- provides no shell, raw AT, path, object-path, or D-Bus input;
- links Settings to the standard `luci-proto-modemmanager` network editor.

### luci-app-lpac

- owns eUICC profile and notification UI/API;
- serializes its own lpac calls;
- uses MBIM through `mbim-proxy`;
- does not own Basic Connect or netifd state.

## Process model

`fibocom-mm-bridge` is a procd-managed daemon written in C.

- GLib `GMainLoop` is the single main loop.
- `MMManager` is a typed `GDBusObjectManagerClient`.
- The libubus socket is attached to the GLib main context using its external
  loop API.
- All D-Bus work is asynchronous.
- Long operations do not block ubus dispatch.
- Object removal cancels or invalidates operation contexts.

The archived shadow daemon already proved the GLib/libubus external-loop shape,
but the new bridge must have fresh target/runtime tests because its object model
and dependencies differ.

## Identity and stale-object protection

ModemManager numeric paths such as `/Modem/8` change after replug and are never
public identity. `DeviceIdentifier` is not guaranteed unique, while physical
paths and runtime ports may be reused.

When an object is admitted, the bridge creates a random 128-bit `modem_id` and
a process-local integer `generation`. The ID is not derived from hardware or
D-Bus properties. It changes after object removal/re-addition and after a
bridge restart. Failure to obtain secure random bytes fails closed instead of
falling back to a predictable selector.

Each cached object and mutation context records:

- public `modem_id`;
- current D-Bus object path internally;
- generation;
- operation ID;
- expected capability/model.

Every mutation request supplies both `modem_id` and `generation`. Before
sending a D-Bus method, the bridge resolves and compares both again. A callback
from a removed generation cannot control the replacement object. LuCI refreshes
the modem list when a selector becomes stale; there is no cross-replug stable
browser identity.

## Read data flow

```text
MM property/signal
    → normalize allowlisted types
    → redact identifiers/secrets
    → replace immutable cached snapshot
    → ubus summary/status response
    → LuCI poll/render
```

No LuCI polling request spawns `mmcli`.

## SMS data flow

```text
Messaging.List / Added / Deleted
    → opaque sms_id cache
    → list metadata

LuCI send(number,text)
    → strict UTF-8/length validation
    → Messaging.Create
    → Sms.Send
    → normalized result
```

SMS text and number are not written to syslog, ubus events, argv, or support
diagnostics. Browser-provided paths are forbidden; delete/get resolves only an
opaque ID already associated with the current modem generation.

## Standard Advanced flow

- band: `SetCurrentBands`;
- mode: current/supported values are read-only here; persistent
  allowed/preferred mode is changed in Settings through the exact network UCI
  section and then applied by netifd;
- power: standard enable/power operations;
- reset: `Reset` with confirmation/cooldown;
- SIM: `SetPrimarySimSlot` only when multiple slots are advertised;
- cell info: `GetCellInfo` when supported.

The XMM plugin owns translation to XACT/CFUN commands.

## L850 vendor cell flow

Stock OpenWrt disables generic AT over D-Bus. Therefore this path is a separate
expert-build capability:

```text
LuCI typed cell request
    → fibocom-mm-bridge exact L850 validation
    → fixed command grammar
    → Modem.Command
    → ModemManager internal AT port queue
    → typed parser
```

There is no direct-port fallback. With AT-via-D-Bus disabled, capability is
`unavailable`.

Neighbor scan first tries `GetCellInfo`; only an exact L850 may fall back to
`AT+XMCI=1`. PCI lock uses a firmware-tested
`AT@SIC:FREQ_LOCK(...)` tuple. See `pci-cell-lock.md`.

## Configuration

Connection configuration remains in a standard network section:

```uci
config interface 'modem'
        option proto 'modemmanager'
        ...
```

The bridge may have display/feature policy such as polling intervals or whether
expert controls are visible, but it must not duplicate APN, authentication,
PIN, PDP/IP family, roaming, PLMN, mode intent, or metric.

## Package boundaries

```text
fibocom-mm-bridge
  depends: modemmanager, glib2/libmm-glib, libubus, libubox

luci-app-fibocom
  depends: luci-base, fibocom-mm-bridge,
           modemmanager, luci-proto-modemmanager, L850 MBIM kmods

luci-app-fibocom-esim
  depends: luci-app-fibocom, luci-app-lpac
```

The base package has no lpac dependency. Modular ModemManager builds must also
install the Fibocom/XMM plugin packages at image level. Official OpenWrt
25.12.5 uses builtin plugins and does not define those split packages, so the
LuCI package cannot name a portable conditional dependency for both layouts.

## Retired architecture

The following are retained only in Git history/tag
`archive/shadow-p0-p1-d2430f8`:

- `fibocomd` sysfs discovery/profile/lifecycle;
- direct libmbim design;
- custom XMM NCM dialer design;
- `fibocom-netifd`;
- `luci-proto-fibocom`;
- Fibocom hotplug scanner and `rescan` ubus method.

They must not remain as dormant runtime packages because their presence makes
ownership ambiguous.
