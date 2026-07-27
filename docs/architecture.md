<!--
SPDX-FileCopyrightText: 2026 As Tsaqib
SPDX-License-Identifier: Apache-2.0
-->

# Architecture

## Ownership boundary

Version 0.4.0 is a companion, not a modem connection stack.

```text
LuCI: Overview / Lock / SMS
              |
              | typed ubus schema 3
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
network orchestration. The 0.4 UI has no Settings page and does not expose APN,
addresses, routes, gateways, DNS, or credentials. The narrow `set_modes` path
resolves the unique bound `proto modemmanager` interface from the modem's
internal Device value, writes only `allowedmode` and `preferredmode`, commits
once, verifies readback, and requests an asynchronous `network reload`.
Section names and device paths never cross the API boundary.

## Identity and generations

Raw ModemManager object paths never cross the ubus boundary. Each admitted
modem receives a random 128-bit opaque `modem_id` and a monotonically changing
`generation`. Each live SMS receives an independent opaque `sms_id` bound to
the modem generation and `messaging_generation`.

Removal immediately marks the old object non-live and cancels ordinary
object-bound operations. Replug creates a new identity/generation. A callback
must still refer to the same live proxy and generation before its result is
accepted. The PCI reset coordinator is the narrow exception in lifetime, not
authority: after an acknowledged reset command it may correlate and verify a
replacement at the same internally captured, re-attested hardware slot. It
never retargets a follow-up write, and returns the replacement's new opaque ID
and generation.

## Read flows

Overview reads only normalized properties already owned by ModemManager. SIM,
bearer, and SMS inventories are obtained asynchronously and retain explicit
cache states when incomplete. Unknown signal metrics are omitted rather than
invented. Overview starts only standard asynchronous `GetCellInfo`, with a
30-second retry backoff. A serving cell is cached only when exactly one LTE
serving record has PCI 0..503, EARFCN-to-supported-band mapping, finite optional
metrics, the current generation, and bounded freshness. Explicit expert scans
and verified PCI postconditions update the same cache. Overview never starts
the XMCI fallback; on the tested L850 firmware, where standard CellInfo is
unsupported, status stays honestly unavailable until an explicit expert result
exists.

SMS initialization uses `Messaging.List`. Added, Deleted, and Sms property
signals update the cache, and a 30-second asynchronous reconciliation repairs
event loss. The cache is sorted newest-first and bounded to 1,024 entries.
`list_sms` returns at most 100 entries per opaque-cursor page.

The expert scan uses asynchronous `GetCellInfo` first. On the one allowlisted
L850 firmware, `Core.Unsupported` may fall back to the compiled-in XMCI query
through ModemManager's queue. Only LTE cells are normalized; serving cells map
to type 4 and neighbors to type 5. PCI resolves to 0..503, including zero.
EARFCN must map to a band in the same modem's live SupportedBands. Non-finite
metrics, required sentinels, malformed objects, duplicate serving records, and
more than 64 LTE cells fail closed. Raw MCC/MNC/TAC/cell ID is discarded.

## Mutation flows

SMS, persistent Mode Lock, Band Lock, and PCI Lock share one per-modem mutation lock. Mutations require an
opaque ID, current generation, exact L850-GL MBIM `2cb7:0007` hardware
attestation, and operation-specific capability checks.

Mode Lock accepts only `3g`, `4g`, or canonical `3g|4g`, with `none`, `3g`, or
`4g` preference. A single allowed mode requires `none`. The UCI writer rejects
missing, anonymous, unsafe, or ambiguous bindings and never touches connection
secrets. Persistent readback is distinguished from netifd activation, so a
lost activation response is not blindly retried.

Band Lock validates the complete request before dispatching asynchronous
`SetCurrentBands`. `["any"]` is the sole automatic request. Schema 3 explicit
requests contain LTE bands only; the backend retains every live supported band
from other currently allowed families before full family validation. Every
selected LTE band remains canonical, unique, supported, and currently allowed.
A timeout or transport loss after dispatch is `outcome_unknown`; there is no
automatic retry. Completion applies a cooldown.

SMS send binds a CSPRNG client token to a SHA-256 request digest, then calls
Messaging.Create and Sms.Send. Delete resolves an opaque live SMS and calls
Messaging.Delete. The token cache retains at most 64 entries for up to 300
seconds after their most recent stored state; capacity eviction may shorten
that window. An uncertain send is cached as `outcome_unknown` to prevent an
immediate blind resend.

PCI mutation exists only for exact firmware `18500.5001.00.05.27.30`. Typed
integers build one reviewed set tuple or the fixed clear tuple; exact command
acknowledgement is followed by fixed `CFUN=15`. The coordinator keeps the
hardware-slot mutation exclusion across object replacement, waits for
registration, and parses a fixed NVM query. Set additionally requires a
matching XMCI serving EARFCN/PCI; clear requires the exact NVM clear sentinel.
Unexpected state and post-dispatch transport uncertainty fail closed without
automatic retry.

## Asynchronous lifetime

All D-Bus calls are asynchronous and carry a `GCancellable`. The bridge keeps
the ubus request deferred until callback completion, timeout, object removal,
or transport loss. The operation owns references to the original modem and
interface proxy. Cancellation does not by itself prove that a dispatched write
did not execute, which is why post-dispatch uncertainty is distinct from an
ordinary retryable failure. A reset-triggered `Core.Cancelled` is treated as
post-dispatch uncertainty and resolved only by replacement, registration, NVM,
and (for set) serving-cell postconditions.

Loss of the ubus socket cancels deferred operations and starts bounded reconnect
backoff. Loss of the ModemManager name clears live inventory and reconnects
without retaining object paths or indexes as identity.

## Build boundary

The base build publishes only `fibocom.mm` with eight methods and is built with
generic AT-over-D-Bus disabled. Preprocessing removes the expert object,
method table, scan operations, and PCI runtime state.

The explicit expert variant requires both:

```text
CONFIG_MODEMMANAGER_WITH_AT_COMMAND_VIA_DBUS=y
CONFIG_FIBOCOM_MM_BRIDGE_L850_EXPERT=y
```

That build publishes `fibocom.mm.l850` under a separate ACL and packages a
matching ModemManager rebuilt with reviewed command transport. Enabling the
build gate alone does not enable mutation: exact model/plugin/composition,
`2cb7:0007` attestation, live bands, generation, and the one-entry firmware
allowlist must all match. The base artifact retains the stock disabled
transport and has no expert object.

## Retired architecture

Version 0.4 has no Status or Settings page, old Advanced page, direct radio
toggle, generic reset, SIM-slot switch, eSIM addon, diagnostic UI, shadow
daemon, custom netifd protocol, or modem rescan action. The earlier shadow
implementation remains recoverable through Git history/tag; it is not part of
the active package.
