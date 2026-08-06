<!--
SPDX-FileCopyrightText: 2026 As Tsaqib
SPDX-License-Identifier: Apache-2.0
-->

# Migration to luci-app-L850GL-MM

## Principle

The migration establishes one modem owner and one connection owner:

```text
ModemManager: modem, ports, SIM, SMS, radio, bearer
netifd:       APN, connection intent, route, DNS
L850GL MM 1.0: Overview, persistent Mode/Band Lock, gated PCI/CA UI, SMS
```

Do not run XModem/QModem polling, direct-AT helpers, custom dialers, SMS tools,
or watchdog/reset scripts alongside ModemManager. A filesystem lock cannot
serialize them with ModemManager's internal command queue.

## Before changing the router

Record a sanitized rollback inventory:

- router model, OpenWrt version/target, and installed package versions;
- modem model, firmware, USB ID/composition, and ModemManager plugin;
- the name of the netifd `proto modemmanager` interface without APN,
  credentials, PIN, addresses, gateway, or DNS;
- a verified alternate management path or physical access.

Back up private configuration outside this repository. Never commit subscriber
identifiers, phone numbers, SMS, credentials, or activation codes.

## Remove conflicting ownership

1. Disable custom modem daemons, hotplug scripts, direct TTY/WDM pollers,
   custom netifd protocols, external dialers, SMS tools, and reset watchdogs.
2. Confirm exactly one ModemManager daemon owns the modem.
3. Configure connection intent through OpenWrt Network / Interfaces using
   `proto modemmanager`.
4. Replug once and verify that ModemManager creates the object and netifd
   reconnects without a saved ModemManager index or runtime device path.

## Remove the retired companion

The renamed service and packages must never coexist with the retired pair.
Before installing package version 1.0.0-r3, stop and disable the old service, then remove all
old companion packages with the router's package manager:

```text
/etc/init.d/fibocom-mm-bridge stop
/etc/init.d/fibocom-mm-bridge disable

retired packages to remove:
  luci-app-fibocom-esim
  luci-app-fibocom
  fibocom-mm-bridge
```

Use either `apk del` or `opkg remove`, according to the active OpenWrt release,
and verify that the old init script, old `fibocom.mm` ubus object, and all three
retired packages are absent. Do not continue while any old companion process
or package remains. This sequence intentionally removes the old LuCI package
before the renamed pair is unpacked, so files and ACLs cannot overlap.

## Install 1.0.0

Install the matching checksum-verified 1.0.0-r3 expert bundle only after
the retirement checks above pass:

```text
modemmanager-l850gl-expert
l850gl-mm-bridge
luci-app-l850gl-mm
```

Follow the `INSTALL.txt` in that exact target bundle; APK and OPKG replacement
steps are intentionally target-specific. Stage all three local packages and a
matching stock rollback package before removing ownership, because replacing
ModemManager may immediately interrupt a modem-only WAN. In particular, an APK
direct-add cannot replace stock `modemmanager` while it remains a pinned world
constraint.

Do not reinstall the retired eSIM addon. The base ModemManager
build must keep generic AT-over-D-Bus disabled. Use an expert image only when
its broader ModemManager capability and separate ACL have been explicitly
reviewed. The expert artifact includes a matching upstream OpenWrt
`modemmanager-l850gl-expert` package rebuilt with that capability; install it together with
the expert bridge rather than mixing the bridge with the stock package. The
v1.0.0 GitHub release contains expert bundles only; the base build is a CI
verification artifact and is not a release installation option.

After installation:

1. Verify `l850gl-mm-bridge --version` reports 1.0.0.
2. Verify `l850gl.mm` exposes exactly the eight schema-4 methods, including
   `set_modes`.
3. Verify the retired `fibocom.mm` object and service remain absent. On a base
   build, also verify `l850gl.mm.l850` is absent.
4. Open LuCI and verify the menu is exactly Overview, Lock, and SMS.
5. Confirm Overview returns schema 4 with normalized USB mode and only the
   approved bounded IMEI/SIM-number/IMSI/ICCID identifiers; confirm paths, IP
   data, credentials, and raw modem output remain absent.
6. Confirm Network / Interfaces remains the only connection settings UI.
7. Exercise read-only SMS listing and pagination before enabling write ACLs.
8. On an expert build, verify `l850gl.mm.l850` has exactly five methods,
   including read-only `get_carrier_info`, then verify its normalized carrier
   result and nullable Overview `modem.voltage_mv` without recording
   subscriber/cellular identity or raw output.

The preceding 0.5.0-r1 package completed the equivalent pre-rename checklist
on 2026-07-28 through static run
`30365531206`, SDK run `30365531207`, checksum verification on both host and
router, and the sanitized post-install checks in `live-router-validation.md`.

## Mutation staging

Writes are separate maintenance actions, not migration prerequisites.

- SMS send/delete requires explicit permission because it changes external
  state and may expose private content.
- Band Lock requires confirmation, alternate access, and one reviewed change
  at a time because an invalid subset can remove WAN.
- Allowed/preferred mode changes are persistent netifd mutations and reload the
  network. Verify the unique ModemManager interface binding and alternate
  management path before applying one reviewed change at a time.
- Do not run live cell scan, PCI lock, clear, or reset without explicit user
  permission. The expert path requires exact L850-GL hardware attestation and
  a successful runtime NVM protocol probe; unrecognized command grammars remain
  `unsupported_protocol`.

After a stale-generation or `outcome_unknown` response, refresh the live modem
and do not retry until its state makes a new request safe.

## Settings that are intentionally not imported

Version 1.0 does not import or reproduce:

- custom APN/auth/PIN, route, DNS, or firewall settings;
- raw AT macros, port paths, ModemManager indexes, sysfs paths, or USB
  controller reset paths;
- radio-enable intent, generic reset ladders, or SIM-slot policy;
- XModem SMS database/PDU state;
- eSIM activation or confirmation data;
- PCI band encodings, wildcard values, NVM paths, unlock tuples, or reset
  sequences.

Persistent network settings must be recreated or retained in the existing
netifd section. Version 1.0 may update only that section's `allowedmode` and
`preferredmode` after resolving it internally; it does not import XModem mode
state or expose a section selector. SMS inventory is read from ModemManager/SIM
storage, not migrated from an application database.

## NCM warning

The L850 NCM composition `8087:095a` is not a supported companion data path.
Do not keep a custom NCM dialer as a hidden fallback. Use MBIM `2cb7:0007`, or
develop and validate NCM bearer support in ModemManager as a separate project.

## Rollback

If the 1.0 expert deployment fails, follow the exact package-manager recovery
commands in that bundle's `INSTALL.txt`. Stop `l850gl-mm-bridge`, remove the
LuCI package, bridge, and `modemmanager-l850gl-expert` together, then install
the matching stock ModemManager package staged before the upgrade and restart
its service. Verify ModemManager ownership, modem reprobe, and netifd recovery
before restoring any prior companion package. Reinstalling the retired pair is
a separate explicit rollback decision; never run old and new services, mix an
expert bridge with stock ModemManager, or publish old and new ubus objects
together.

If a separately approved band test disrupts WAN, use the prepared alternate
management path and restore automatic `["any"]` only after the modem object and
generation have been refreshed. For a runtime-validated expert protocol, PCI
rollback is the typed `clear_cell_lock` action followed by its reset/reprobe and
exact NVM-clear verification. Never issue a copied raw clear tuple, and do not
blindly retry an `outcome_unknown` result.
