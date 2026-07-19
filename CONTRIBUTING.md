<!--
SPDX-FileCopyrightText: 2026 As Tsaqib
SPDX-License-Identifier: Apache-2.0
-->

# Contributing

## Keep ownership unambiguous

This project is a ModemManager companion. ModemManager remains the only modem,
port, SIM, SMS, radio, and bearer owner; netifd remains the connection-intent
and L3 owner. Contributions must not add a second dialer, custom netifd
protocol, hotplug scanner, direct TTY/WDM access, or bearer lifecycle method.

Connection settings, including persistent allowed/preferred modes, belong to
the existing `proto modemmanager` network section. The application may display
them or navigate to the standard editor; it must not keep a duplicate profile.

## Evidence for hardware claims

A supported-device or capability contribution must include:

1. exact manufacturer, model, firmware, hardware IDs, composition, and
   ModemManager plugin;
2. sanitized ModemManager and OpenWrt lifecycle observations;
3. connect, reconnect, replug, and data-plane results appropriate to the claim;
4. capability-specific success, failure, unplug, and recovery cases;
5. provenance for every protocol fact;
6. fixtures with identifiers, credentials, addresses, and SMS content removed.

Do not infer support from a shared vendor ID, similar model name, or an AT
string found in another project. NCM connectivity is unsupported until its
bearer backend exists in ModemManager and passes hardware testing.

## Standard and expert changes

Prefer typed libmm-glib APIs for status, SMS, bands, power, reset, and SIM
slots. Persistent mode changes go through network UCI/netifd.

L850 PCI/EARFCN support is an expert-only variant. It requires an exact
firmware allowlist, fixed integer-only grammar, parser fixtures, system D-Bus
policy review, reset/reprobe verification, and a separately confirmed rollback
path. The base build must keep generic AT-via-D-Bus disabled and must not
compile or expose the expert object.

## Identity and asynchronous safety

- Generate a random 128-bit `modem_id` for each admitted ModemManager object.
- Never derive a selector from `DeviceIdentifier`, equipment identifiers,
  physical paths, D-Bus paths, indexes, or runtime ports.
- Every mutation must carry and revalidate exact `{modem_id, generation}`.
- Object removal invalidates operations and SMS IDs immediately.
- A callback may not retarget a replacement object; follow-up writes require a
  new ID, generation, and confirmation.
- Use bounded asynchronous D-Bus work; do not block ubus dispatch with `mmcli`
  or a nested main loop.

## Security and privacy

- No raw AT, arbitrary D-Bus, shell, `cgi-io`, or path RPC.
- No global WDM/TTY/netdev fallback and no direct port lock.
- No SMS text/number, IMEI, IMSI, ICCID, EID, APN credential, PIN, activation
  code, assigned address, or raw diagnostic dump in logs or fixtures.
- Reject unknown fields, invalid types, oversized data, stale generations, and
  unsupported capabilities.
- Keep status, SMS-read, SMS-write, radio, expert, and lpac ACLs separate.
- Destructive operations require confirmation, serialization, timeout, and
  an explicit recovery result.

## Clean-room policy

QModem and the historical XModem repositories have incompatible or
non-standard license combinations. Do not copy their source, comments, shell
functions, or frontend. Reimplement behavior from public specifications,
ModemManager/OpenWrt APIs, sanitized hardware observations, and independently
written tests. Record sources in `docs/provenance.md`.

## Development flow

1. Update `PRD.md` and `memory.md` for ownership, capability, or hardware-command
   changes.
2. Add a sanitized failing fixture/test before changing behavior.
3. Run `make check` and inspect `git diff --check`.
4. Build/install in a current OpenWrt SDK or buildroot; host tests do not prove
   libmm-glib/libubus target compatibility.
5. Run read-only live regression first. Announce any reset, radio, SIM, band,
   cell-lock, or eSIM test that may interrupt WAN.
6. Keep commits focused and add SPDX metadata to every new file.
