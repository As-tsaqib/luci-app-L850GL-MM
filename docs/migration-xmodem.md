<!--
SPDX-FileCopyrightText: 2026 As Tsaqib
SPDX-License-Identifier: Apache-2.0
-->

# Migration from XModem to ModemManager companion

## Principle

The migration changes the connection owner:

```text
before: XModem owns detection/dial/AT/SMS
after:  ModemManager + netifd own detection/dial/AT/SMS
        luci-app-fibocom is a companion UI
```

Do not run XModem/QModem dial or AT polling against a modem already managed by
ModemManager.

## Current router result

The user's router already completed the connection-owner migration:

- the network interface uses `proto modemmanager`;
- ModemManager 1.24.0-r10 recognizes L850 MBIM;
- unplug/replug automatically returns the interface online;
- the L850 is connected without XModem runtime packages.

Therefore no custom bearer cutover is required for the new LuCI app.
The v0.2.0 companion itself has not yet been installed on the router; the
result above validates only the existing native ModemManager/netifd stack.

## Pre-migration backup

Keep a private backup of:

- current `/etc/config/network`;
- the known-good package list/image;
- APN/auth/PIN values;
- lpac/eSIM settings;
- XModem settings needed only for rollback.

Do not place credentials, identifiers, SMS, or eSIM secrets in this repository.

## Remove conflicting ownership

Before installing the companion runtime, verify:

- no XModem/QModem dialer or watchdog is running;
- no XModem SMS/AT polling daemon opens `ttyACM`;
- no custom `proto fibocom` section is active;
- the L850 connection is a standard `proto modemmanager` interface;
- exactly one ModemManager process owns the modem.

The old experimental packages to remove if installed:

```text
fibocomd
fibocom-netifd
luci-proto-fibocom
```

The archived shadow `fibocomd` did not dial, but keeping it installed adds
unnecessary duplicate discovery and an obsolete API.

## Connection configuration

Use Network → Interfaces and `luci-proto-modemmanager` to configure:

- device/physical modem selection;
- APN;
- authentication;
- PIN;
- IP family;
- allowed/preferred mode;
- roaming;
- PLMN and metric where needed.

Do not copy those values into `/etc/config/fibocom`.

## Install order

1. Confirm the current `proto modemmanager` interface reconnects after replug.
2. Install `fibocom-mm-bridge`.
3. Verify its summary/status maps to the same ModemManager object and exact
   read-only UCI binding. Dynamic netifd state/counters are not available from
   bridge schema 1.
4. Install `luci-app-fibocom`.
5. Verify Overview and Status before enabling write ACLs.
6. Test SMS through ModemManager, including signal-driven incoming-message
   refresh, the 30-second reconciliation fallback, and the LuCI 10-second poll.
7. Test standard Advanced operations one at a time.
8. Optionally install `luci-app-fibocom-esim` and configure lpac MBIM proxy.
9. Keep vendor PCI controls disabled until the expert build and hardware matrix
   are complete.

## eSIM migration

Use the user's clean `luci-app-lpac`; do not migrate the XModem TgBot or
duplicate eSIM frontend.

For the initial single-L850 MBIM setup:

```uci
config global 'global'
        option apdu_backend 'mbim'

config mbim 'mbim'
        option device '/dev/cdc-wdmN'
        option proxy '1'
        option skip_slot_mapping '1'
```

`N` is runtime-specific. Confirm it belongs to the L850 before saving. Stable
multi-modem WDM binding in `luci-app-lpac` is not yet implemented, so eSIM
device ambiguity must fail closed. This limitation does not describe the base
bridge, which uses per-object opaque identities for multiple ModemManager
objects.

Profile enable/disable may trigger SIM reprobe and a short WAN interruption.
Do not stop ModemManager; allow ModemManager/netifd to restore the bearer.

## Settings that are not imported

- runtime `ttyACM`, `cdc-wdm`, or `wwan` paths as permanent identity;
- XModem PID/cache/lock files;
- custom dial/watchdog/recovery ladders;
- raw AT commands;
- Telegram/TgBot config;
- board-specific USB controller resets;
- custom NCM RAW-IP state;
- IMEI/FCC/NVM modifications;
- eSIM activation/confirmation codes.

## NCM warning

Do not switch a remotely accessed production L850 from MBIM to NCM expecting
ModemManager 1.24 to dial it. NCM connectivity is unsupported and can remove
the active WAN until the modem is switched back or physically recovered.

## Rollback

Preferred rollback keeps ModemManager as the connection owner and removes only
the companion packages:

1. uninstall `luci-app-fibocom-esim` if present;
2. uninstall `luci-app-fibocom`;
3. uninstall `fibocom-mm-bridge`;
4. leave `proto modemmanager` unchanged;
5. verify WAN and route state.

Rolling back all the way to XModem is a separate ownership migration:

1. record and disable the ModemManager network interface;
2. ensure ModemManager releases the device;
3. start one XModem owner;
4. verify route/DNS and port ownership;
5. never keep both stacks active as a shortcut.
