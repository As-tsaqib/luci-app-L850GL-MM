<!--
SPDX-FileCopyrightText: 2026 As Tsaqib
SPDX-License-Identifier: Apache-2.0
-->

# Migration from XModem

Migration is a staged ownership transfer, not an in-place upgrade.

Only Phase 1 is possible with the current P0/P1 tree. The netifd protocol fails
closed with `SHADOW_MODE`; there is no bearer backend to cut over to yet.

## Phase 1: evidence and shadow mode

1. Export the existing XModem and network configuration to a private backup.
2. Remove secrets from any diagnostic copy.
3. Install fibocomd and verify procd invokes
   `/usr/sbin/fibocomd --foreground --shadow`.
4. Confirm that discovery reports the correct physical device, composition, and port roles.
5. Repeat boot and replug tests while XModem remains the only bearer owner.

Shadow mode must not open TTY/WDM, stop XModem, or change networking.
There is intentionally no `shadow_mode` UCI toggle: the init script hardcodes
`--shadow`, and this daemon build refuses to start without it. Diagnostics
reports external ownership as `not-probed`; verify the existing owner using
independent system evidence.

## Phase 2: ownership cutover

Only after the MBIM backend meets its acceptance tests:

1. record the current known-good package and rollback steps;
2. stop and disable XModem/QModem/ModemManager ownership of the L850;
3. verify no legacy monitor, dialer, or lpac wrapper holds the selected TTY/WDM;
4. configure one `proto fibocom` network interface using the canonical
   `device_id` returned by inventory, never a TTY/WDM/netdev path;
5. enable the fibocom bearer backend;
6. verify IP, route, DNS, traffic, disconnect, and reconnect;
7. keep the old configuration disabled but available for rollback until soak testing passes.

## Imported data

An optional one-shot importer may copy:

- enabled intent;
- APN;
- authentication type and credentials through a write-only path;
- route metric;
- preferred composition.

It must not import:

- runtime TTY/WDM/netdev paths;
- PID, lock, or state files;
- raw AT commands;
- watchdog/reset ladders;
- Telegram/bot configuration;
- eSIM activation or confirmation codes;
- global lpac device fallback;
- board-specific USB controller rules.

## Rollback

1. bring down the `proto fibocom` interface;
2. stop fibocomd and verify it released the session and ports;
3. restore the saved network/XModem configuration;
4. start exactly one legacy owner;
5. verify route and DNS state before declaring rollback complete.

Never run two bearer owners as a rollback shortcut.

Do not attempt this rollback/cutover procedure until P2 has a target-built,
hardware-validated MBIM backend. Installing the current protocol package cannot
provide connectivity.
