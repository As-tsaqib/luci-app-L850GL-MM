<!--
SPDX-FileCopyrightText: 2026 As Tsaqib
SPDX-License-Identifier: Apache-2.0
-->

# Threat model

## Protected assets

- modem availability and firmware state;
- WAN connectivity and route ownership;
- AT and MBIM control channels;
- APN credentials and SIM PIN;
- IMEI, IMSI, ICCID, and EID;
- eSIM activation/confirmation codes and profiles;
- integrity of other attached modems.

## Primary threats

1. Browser input becomes a shell or AT command.
2. A global device fallback controls another modem.
3. A stale callback mutates a newly replugged device.
4. Two managers own the same bearer.
5. Recovery resets a modem during an eSIM transaction.
6. A profile supplies executable behavior.
7. Secrets leak through logs, argv, environment, `/proc`, or support bundles.
8. A destructive command is offered on an unverified firmware.

## Required controls

- typed ubus methods and least-privilege ACL;
- no arbitrary exec, file-read wildcard, raw AT, or user-supplied path;
- physical-parent association and fail-closed ambiguity;
- generation tokens on all asynchronous work;
- one mutating job and one bearer owner per device;
- maintenance lease for delegated UICC operations;
- data-only profiles and firmware-gated mutations;
- deterministic redaction;
- bounded operations, backoff, cooldown, and circuit breaker;
- online RSP secrets transported through stdin/pipe, memfd, or another upstream secret-safe API.

## P0 reduction

Shadow mode does not open device nodes or mutate state. Its remaining attack
surface is profile/sysfs parsing, identity/grouping, asynchronous reconciliation,
and ubus serialization.

Implemented controls include bounded sysfs/profile reads, `O_NOFOLLOW` for
attributes and the profile, canonical containment checks, exact physical-parent
grouping, strict component and RPC-input character sets, opaque hashed identity,
duplicate-identity ambiguity, a data-only exact L850 profile, and no RPC that
accepts a runtime path. Diagnostics explicitly says external ownership is
`not-probed` and `fibocomd_claims_device` is false.

The host fake-sysfs test currently covers exact two-device isolation including
a textual-prefix collision trap, serial-derived IDs, MBIM/NCM role grouping,
generation retention/change/removal/replug, an unsupported USB device, uppercase
interface rejection in the profile, embedded-NUL rejection, profile-symlink
rejection, and fail-closed classification for unexpected MBIM/NCM drivers,
split MBIM parents, and a pair on the wrong MBIM interface.

Still required before a runtime security claim are malformed/missing/oversized
sysfs fixtures, duplicate-identity and ambiguous-role fixtures, ubus fuzz and
authorization tests, live reconnect tests, OpenWrt target tests, and real-device
remove/replug races. The current LuCI static checks are useful guardrails, not a
substitute for those tests.
