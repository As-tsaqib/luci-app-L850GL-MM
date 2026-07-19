<!--
SPDX-FileCopyrightText: 2026 As Tsaqib
SPDX-License-Identifier: Apache-2.0
-->

# Architecture

## Implemented P0/P1 data flow

```text
procd: fibocomd --foreground --shadow
                       │
USB/TTY/net hotplug ───┼── typed fibocom.rescan hints
periodic reconcile ────┘
                       ▼
              GLib debounce / scheduler
                       │
                       ▼
          bounded worker-thread sysfs scan
                       │
                       ▼
        main-context generation reconciliation
                       │
              cached inventory only
                 ┌─────┴────────┐
                 ▼              ▼
          read-only ubus       LuCI polls ubus
          list/status/...      and requests rescan
```

The worker reads sysfs metadata only. It does not open a TTY, WDM node, or
netdev. Scan results are reconciled on the GLib main context before the cache is
visible through ubus. One scan runs at a time; hotplug hints received while a
scan runs collapse into a pending follow-up scan.

The current tree also contains a netifd protocol and LuCI protocol form, but the
protocol always fails closed with `SHADOW_MODE`. They do not participate in the
P0/P1 data path.

## Shadow safety boundary

P0/P1 performs only:

1. strict loading of the reviewed L850 data profile;
2. cold, periodic, and debounced hint-driven scans;
3. grouping below an exact canonical physical USB ancestor;
4. matching exact L850 USB IDs;
5. role assignment from lowercase hexadecimal USB interface numbers;
6. topology classification and generation-scoped cached inventory;
7. sanitized read-only ubus serialization.

It must not open device nodes, issue AT/MBIM commands, change UCI, claim a
bearer, reset a modem, or alter a network interface. Diagnostics reports
`ownership: not-probed` and `fibocomd_claims_device: false`: shadow mode does not
claim the modem, but it also does not pretend to know which external manager
owns it.

## Matching and topology

Runtime matching currently uses only these exact USB IDs:

| Composition | VID:PID | Complete-topology requirements |
|---|---|---|
| MBIM | `2cb7:0007` | `cdc_acm` for `02`/`04`/`06`, plus exactly one same-object WDM/netdev pair on interface `00` driven by `cdc_mbim` |
| NCM | `8087:095a` | `cdc_acm` for `00`/`02`/`04`, no WDM, and one `cdc_ncm` netdev on each candidate `06`, `08`, and `0a` |

The NCM rule inventories candidates; it does not prove which netdev carries
`/USBHS/NCM/0`, select an active candidate, or dial CID 0. A `complete` topology
therefore means only “exact match to the P0 sysfs profile”. Model and firmware
are not probed, so ubus reports `model_confidence: usb_id_only` and diagnostics
reports `hardware_validated: false`.

Absolute canonical sysfs paths are kept private. The API exposes the bounded USB
slot label as `physical_path`, plus sanitized node names, interface numbers,
drivers, and roles. No RPC accepts a physical path as a device selector.

## Device identity

For a usable USB `serial` attribute, identity material is
`fibocom-l850:v1:serial:<serial>` and the public identifier is
`l850-<sha256>`. The raw serial is never serialized; the response reports
`identity_scope: usb-serial-hash`.

If the serial is missing, malformed, all-zero, or a known placeholder, the
material becomes `fibocom-l850:v1:path:<usb-slot>` and the response reports
`identity_scope: path-scoped`. This fallback can change when physical topology
changes and must not be treated as a hardware identity.

If two simultaneously present devices produce the same identity, each ID gets
a short slot-hash suffix and both topologies become `ambiguous` with reason
`duplicate-usb-identity`. This prevents one identifier from selecting two
physical devices.

## Generation and reconciliation

The topology fingerprint includes VID/PID, interface number, driver, node kind,
and sanitized node name. An unchanged device at the same USB slot keeps its
generation. Add, topology/identity change, removal, and later replug advance the
monotonic generation counter. Removed devices leave the current cache rather
than remaining as historical `present: false` records.

Discovery scans are serialized, cancellable on shutdown, and applied only on
the main context. If a newer request is queued while a worker is running, the
superseded result is discarded before it can replace cached state. The P0 host
test verifies stable generations for unchanged fixtures, an increment for a
changed node, removal, and a higher generation on replug. It also verifies that
incorrect MBIM/NCM drivers remain `partial` with reason `driver-mismatch`, split
MBIM control/data parents are `ambiguous`, and a pair on the wrong interface is
`partial`.

## Event loop and ubus

GLib `GMainLoop` is the only event loop. The libubus socket is integrated using
its external-loop contract:

1. register `ctx->sock.fd` as a GLib Unix-fd source;
2. call `ubus_handle_event(ctx)` when readable;
3. on connection loss, remove the old source;
4. reconnect with exponential bounded backoff and jitter;
5. install a source for the reconnected fd.

The daemon does not run uloop in another thread, nest a main loop, or poll
`g_main_context_iteration()`. Source review confirms this shape, but live ubus
disconnect/reconnect has not yet been exercised on an OpenWrt target.

If target testing rejects the single-process adapter, the future production
fallback is a GLib/libmbim worker plus a small uloop/libubus bridge over Unix
`SOCK_SEQPACKET`. Parsing CLI output is not a production fallback.

## Dependencies by phase

P0/P1 compiles against GLib/GIO, json-c, libubus, and libubox. The package also
selects ubus/jshn and the USB ACM, WDM, CDC-MBIM, and CDC-NCM kernel packages.
It does not link libmbim and does not run `mbimcli`, ModemManager, or Quectel-CM.

Direct `libmbim` plus `mbim-proxy` is a P2 dependency, when the MBIM Basic
Connect state machine and real netifd lease/IP contract are implemented.

## Target ownership after P2

The following is the intended architecture, not current functionality:

```text
LuCI / netifd client ───── typed ubus ───── fibocomd
                                                │
                                   direct libmbim + proxy
                                                │
                                         MBIM Basic Connect

official lpac ───── coordinated maintenance lease ───── eUICC/APDU
```

- fibocomd will own modem bearer lifecycle, radio, and serialized control;
- netifd will exclusively own addresses, routes, DNS, MTU, metrics, and
  firewall integration;
- official lpac will be a delegated UICC client, not a Basic Connect owner;
- exactly one bearer manager may own a physical modem.

Connection intent, including canonical `device_id`, belongs in
`/etc/config/network`. A future `proto fibocom` will acquire a
generation-scoped session lease and apply the typed result through netifd. The
current protocol does none of these operations and fails closed.
