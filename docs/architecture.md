<!--
SPDX-FileCopyrightText: 2026 As Tsaqib
SPDX-License-Identifier: Apache-2.0
-->

# Architecture

## Ownership boundary

Version 0.3.0 is a companion, not a modem connection stack.

```text
LuCI: Overview / Lock / SMS
              |
              | typed ubus schema 2
              v
      fibocom-mm-bridge
              |
              | asynchronous libmm-glib
              v
         ModemManager  <---- owns modem, ports, SIM, SMS, radio, bearer
              |
              v
 OpenWrt monitor + netifd <---- owns connect intent, APN, route, DNS
```

There is no companion dialer, bearer lifecycle API, hotplug scanner, custom
netifd protocol, direct TTY/WDM access, or external process execution. The
browser cannot submit an AT command, D-Bus path, sysfs path, or device path.

## Components

`fibocom-mm-bridge` observes ModemManager's object manager without auto-starting
it. It admits live Fibocom modem objects, allocates CSPRNG opaque IDs, caches
the small amount of state required by the UI, and publishes one base ubus
object. The optional expert object is compiled and registered only behind
`FIBOCOM_MM_L850_EXPERT`.

The LuCI package contains three views and one shared RPC/validation module.
Views never declare ad-hoc RPC calls. Every view polls its typed snapshot every
10 seconds; cell scan is the exception and runs only after a button press.

ModemManager remains authoritative for:

- modem identity, state, power, plugin, ports, and composition;
- SIM presence and lock state;
- Messaging and Sms objects;
- supported/current bands and `SetCurrentBands`;
- bearer connection state and data interface;
- standard `GetCellInfo` when the plugin supports it.

netifd remains authoritative for persistent connection settings and runtime
network orchestration. The 0.3 UI has no Settings page and does not expose APN,
addresses, routes, gateways, DNS, or credentials.

## Identity and generations

Raw ModemManager object paths never cross the ubus boundary. Each admitted
modem receives a random 128-bit opaque `modem_id` and a monotonically changing
`generation`. Each live SMS receives an independent opaque `sms_id` bound to
the modem generation and `messaging_generation`.

Removal immediately marks the old object non-live and cancels associated
operations. Replug creates a new identity/generation. A callback must still
refer to the same live proxy and generation before its result is accepted. A
replacement modem may be observed read-only, but an old operation is never
retargeted to it.

## Read flows

Overview reads only normalized properties already owned by ModemManager. SIM,
bearer, and SMS inventories are obtained asynchronously and retain explicit
cache states when incomplete. Unknown signal metrics are omitted rather than
invented. Serving EARFCN/PCI is represented as unavailable until a validated
source exists.

SMS initialization uses `Messaging.List`. Added, Deleted, and Sms property
signals update the cache, and a 30-second asynchronous reconciliation repairs
event loss. The cache is sorted newest-first and bounded to 1,024 entries.
`list_sms` returns at most 100 entries per opaque-cursor page.

The expert scan uses asynchronous `GetCellInfo` before considering any vendor
fallback. Only LTE cells are normalized; serving cells map to type 4 and
neighbors to type 5. PCI must be hexadecimal data from the typed ModemManager
object that resolves to 0..503. EARFCN must map to an LTE band present in the
same modem's live SupportedBands. Non-finite metrics, malformed objects,
duplicate serving records, and more than 64 LTE cells fail closed.

## Mutation flows

SMS and Band Lock share one per-modem mutation lock. Mutations require an
opaque ID, current generation, exact L850-GL MBIM `2cb7:0007` hardware
attestation, and operation-specific capability checks.

Band Lock validates the complete request before dispatching asynchronous
`SetCurrentBands`. `["any"]` is the sole automatic request. Explicit bands must
be canonical, unique, supported, and inside the currently allowed radio
families. A timeout or transport loss after dispatch is `outcome_unknown`;
there is no automatic retry. Completion applies a cooldown.

SMS send binds a CSPRNG client token to a SHA-256 request digest, then calls
Messaging.Create and Sms.Send. Delete resolves an opaque live SMS and calls
Messaging.Delete. The token cache retains at most 64 entries for up to 300
seconds after their most recent stored state; capacity eviction may shorten
that window. An uncertain send is cached as `outcome_unknown` to prevent an
immediate blind resend.

PCI set/clear inputs, parser policy, and state transitions exist in the expert
variant, but no mutation is dispatched. The firmware allowlist is empty until
one exact command/clear/reset/reprobe/registration/postcondition sequence has
been proven on hardware. This preserves the state-machine boundary without
claiming a fictional recovery path.

## Asynchronous lifetime

All D-Bus calls are asynchronous and carry a `GCancellable`. The bridge keeps
the ubus request deferred until callback completion, timeout, object removal,
or transport loss. The operation owns references to the original modem and
interface proxy. Cancellation does not by itself prove that a dispatched write
did not execute, which is why post-dispatch uncertainty is distinct from an
ordinary retryable failure.

Loss of the ubus socket cancels deferred operations and starts bounded reconnect
backoff. Loss of the ModemManager name clears live inventory and reconnects
without retaining object paths or indexes as identity.

## Build boundary

The base build publishes only `fibocom.mm` with seven methods and is built with
generic AT-over-D-Bus disabled. Preprocessing removes the expert object,
method table, scan operations, and PCI runtime state.

The explicit expert variant requires both:

```text
CONFIG_MODEMMANAGER_WITH_AT_COMMAND_VIA_DBUS=y
CONFIG_FIBOCOM_MM_BRIDGE_L850_EXPERT=y
```

That build publishes `fibocom.mm.l850` under a separate ACL. Enabling the build
gate does not enable mutation: firmware attestation and the empty allowlist
still fail closed.

## Retired architecture

Version 0.3 has no Status or Settings page, old Advanced page, direct radio
toggle, generic reset, SIM-slot switch, eSIM addon, diagnostic UI, shadow
daemon, custom netifd protocol, or modem rescan action. The earlier shadow
implementation remains recoverable through Git history/tag; it is not part of
the active package.
