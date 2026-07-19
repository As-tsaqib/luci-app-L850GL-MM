<!--
SPDX-FileCopyrightText: 2026 As Tsaqib
SPDX-License-Identifier: Apache-2.0
-->

# Threat model

## Protected assets

- WAN availability and routing;
- modem firmware, radio, SIM, and registration state;
- ModemManager AT/MBIM port ownership;
- SMS content and correspondent numbers;
- APN credentials and SIM PIN;
- IMEI, IMSI, ICCID, EID, and device identifiers;
- eSIM profiles, activation codes, and confirmation codes;
- other attached modems and network interfaces.

## Trust boundaries

```text
browser
  │ LuCI session + rpcd ACL
  ▼
typed ubus API
  │
  ▼
root fibocom-mm-bridge
  │ system D-Bus
  ▼
ModemManager
  │
  ▼
modem ports
```

`luci-app-lpac` is a separate privileged boundary for eUICC operations.
`/etc/config/network` is the network-intent boundary owned by netifd.

## Primary threats

1. Browser input becomes shell, D-Bus object path, or AT syntax.
2. Generic ModemManager AT access exposes destructive commands.
3. A stale ModemManager index controls a different modem after replug.
4. An application opens TTY concurrently and steals/corrupts MM responses.
5. A GUI method creates or tears down a bearer behind netifd.
6. A reset/cell lock leaves WAN down or camps on an unintended cell.
7. SMS text, phone numbers, identifiers, or credentials leak through logs,
   argv, events, diagnostics, or raw D-Bus dumps.
8. An opaque SMS ID is replayed against another modem/generation.
9. eSIM profile mutation races a bearer/SIM reprobe.
10. A global `cdc-wdm0` configuration targets the wrong eUICC after replug or
    in a multi-modem system.
11. Capability is inferred from a vendor name and a command is sent to an
    unsupported firmware.
12. Malformed D-Bus data, oversized SMS, or cell scan output consumes memory or
    blocks the event loop.

## Ownership controls

- ModemManager is the only owner of AT/MBIM ports and bearers.
- netifd is the only owner of logical interface and network configuration.
- bridge API has no connect/disconnect/create/delete bearer method.
- no direct TTY/WDM fallback exists.
- custom hotplug, sysfs discovery, and netifd proto Fibocom are retired.
- SMS is accessed only through ModemManager Messaging.
- lpac uses MBIM proxy and does not stop ModemManager.

## API controls

- exact typed methods and exact rpcd ACL lists;
- no `cgi-io`, `file.exec`, shell, raw D-Bus, raw AT, or path input;
- separate permissions for read, SMS mutation, Advanced mutation, and eSIM;
- opaque random `modem_id`, generation-scoped `sms_id`, and an explicit
  `generation` on every mutation;
- bounded strings, arrays, and response sizes;
- UTF-8 validation and normalized integer parsing;
- unknown fields rejected rather than ignored on mutating methods;
- operation timeout and one mutation per modem;
- reset confirmation and cooldown;
- cell scan is explicit and rate-limited, not polled.

## Identity and replug controls

- public ID is a random 128-bit token per admitted object, never a hash of
  `DeviceIdentifier`, an equipment identifier, or physical path;
- numeric D-Bus indexes and runtime node names are never selectors;
- every mutation supplies and revalidates exact `{modem_id, generation}`
  immediately before dispatch;
- operation context includes internal generation and operation ID;
- object removal invalidates pending work;
- replug/re-export and bridge restart issue new IDs;
- failure to obtain secure randomness fails closed;
- callbacks cannot retry against a replacement object;
- reset/cell-lock may observe a correlated replacement read-only, but any
  follow-up write requires a new confirmation and its new ID/generation;
- SMS cache entries are generation-scoped.

## Privacy controls

The normal status schema excludes:

- equipment/subscriber/card identifiers;
- own phone numbers;
- APN username/password and PIN;
- SMS body and recipient/sender except inside the explicit SMS view;
- eSIM activation/confirmation secrets;
- raw physical paths where not diagnostically required.

Logging rules:

- never log SMS text or number;
- never log user AT/D-Bus input because neither is accepted;
- redact identifier-like values before any support bundle;
- do not log raw `mmcli -K`, raw UCI network sections, or raw NVM output;
- D-Bus/ubus events report state changes without message content.

## Generic AT risk

OpenWrt defaults to:

```text
MODEMMANAGER_WITH_AT_COMMAND_VIA_DBUS=n
polkit=no
```

The base product retains this safe default. L850 cell controls require an
expert image that enables generic Command as a transport. Mitigations:

- do not run ModemManager with `--debug`;
- restrict system D-Bus control/Command callers as far as the platform allows;
- bridge exposes fixed typed methods only;
- command grammar is constructed internally from bounded integers;
- exact VID/PID, plugin, model, and firmware allowlist;
- raw response is parsed and discarded;
- no web/API method can pass arbitrary AT.

If the D-Bus policy cannot be hardened acceptably, vendor cell controls remain
unavailable.

## PCI lock safety

- `GetCellInfo` is attempted before vendor AT fallback;
- XMCI parser accepts LTE types 4 and 5 only;
- PCI 0–503 includes zero;
- sentinel and malformed values are rejected;
- EARFCN must map to a ModemManager-supported band;
- lock-state query failure prevents unlock guesses;
- command success and reset/reprobe/registration are separate states;
- serving cell is verified after reconnect;
- a mismatch is never reported as success;
- there is no chain of alternate CFUN commands;
- live lock/reset testing requires explicit user notice and a rollback path.

## SMS safety

- native Messaging Create/Send/Delete;
- phone and text never appear in process argv;
- maximum lengths and UTF-8 enforced;
- list/get/delete accept only cached opaque IDs;
- no raw object path;
- body is returned only to an authorized SMS call, never a general status call;
- multipart handling remains ModemManager's responsibility;
- no `sms-tool` race.

## eSIM safety

- eSIM code remains in `luci-app-lpac`;
- base package cannot invoke lpac;
- optional package contributes menu/dependency only;
- MBIM proxy required;
- initial claim limited to a single L850;
- global WDM path is preflighted and must fail closed on ambiguity;
- profile mutation may trigger SIM/bearer reprobe; MM/netifd recovers it;
- online RSP operations remain disabled while packaged lpac lacks TLS peer and
  hostname verification.

## OpenWrt configuration risk

Settings does not duplicate APN/auth/PIN. It links to or edits the existing
`proto modemmanager` section using standard LuCI/network permissions.

The bridge does not read credentials into its status cache. Bugs in the
upstream protocol form should be fixed in separate OpenWrt/LuCI patches rather
than worked around with a second configuration store.

## Required security tests

- ACL allow/deny matrix for every method;
- unknown key, type mismatch, oversized, invalid UTF-8, and integer-boundary
  tests;
- stale modem and stale SMS ID tests;
- unplug during every mutation;
- multiple modem identity isolation;
- no bearer method/API/static string regression;
- no raw AT/path input regression;
- SMS log/event privacy test;
- support-bundle redaction test;
- D-Bus outage and ModemManager restart;
- cell parser fixtures for types 4/5/6, PCI 0, sentinels, truncated and
  oversized output;
- cell reset/reprobe read-only verification and separately confirmed rollback;
- eSIM ambiguity and proxy preflight;
- fuzz normalized D-Bus dictionaries;
- target ASAN/UBSAN where feasible plus OpenWrt SDK builds.
