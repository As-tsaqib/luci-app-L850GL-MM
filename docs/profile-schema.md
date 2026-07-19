<!--
SPDX-FileCopyrightText: 2026 As Tsaqib
SPDX-License-Identifier: Apache-2.0
-->

# Profile schema

> **Legacy notice:** PRD 3.1 supersedes this runtime profile system. The target
> architecture delegates discovery and port matching to ModemManager and does
> not install a Fibocom sysfs profile loader. This document and schema remain
> temporarily as evidence for the `d2430f8` baseline and must be removed from
> the active package tree together with the legacy source once the replacement
> bridge is buildable. Do not add new modem support here.

Profiles are data-only hardware declarations. The JSON Schema describes the
format, while the current P0/P1 C loader deliberately accepts only the reviewed
L850-GL contract. Passing schema validation alone is not enough for runtime
acceptance.

Schema version 1 has these top-level fields:

| Field | Type | Meaning |
|---|---|---|
| `schema` | integer | exact schema version |
| `id` | string | stable profile identifier |
| `display_name` | string | user-facing model name |
| `match` | object | declared manufacturer/model and exact USB facts |
| `management_backend` | string | reserved internal backend identifier; not evidence that the backend is active |
| `bearer_backends` | object | target composition-to-backend mapping; not an availability claim |
| `ports` | object | USB-interface roles per composition |
| `ncm` | object | profile-scoped NCM parameters |
| `capabilities` | object | reviewed roadmap states; runtime ubus capabilities remain authoritative |

Example:

```json
{
  "schema": 1,
  "id": "fibocom-l850-gl",
  "display_name": "Fibocom L850-GL",
  "match": {
    "models_exact": ["L850-GL"],
    "usb": [
      {"vid": "2cb7", "pid": "0007", "composition": "mbim"},
      {"vid": "8087", "pid": "095a", "composition": "ncm"}
    ]
  },
  "management_backend": "intel-xmm",
  "bearer_backends": {
    "mbim": "mbim-basic-connect",
    "ncm": "intel-xmm-ncm"
  },
  "ports": {
    "mbim": {
      "at_primary": "02",
      "ignored": ["04"],
      "at_secondary": "06"
    },
    "ncm": {
      "at_primary": "00",
      "ignored": ["02"],
      "at_secondary": "04",
      "data_candidates": ["06", "08", "0a"],
      "data_selector": "hardware-required"
    }
  },
  "ncm": {"session_cid": 0},
  "capabilities": {
    "shadow_inventory": "supported",
    "mbim": "planned",
    "ncm": "hardware-validation-required",
    "esim": "mbim-only-planned",
    "band_lock": "planned",
    "cell_lock": "experimental-disabled",
    "sim_switch": "probe-required"
  }
}
```

## Constraints

- VID/PID values are four lowercase hexadecimal digits.
- Interface numbers are two hexadecimal digits.
- Profile files cannot contain AT commands, shell, executable paths, environment names, or runtime device paths.
- A data candidate is not an active-data claim.
- Unknown capability values fail schema validation.
- Unsupported or unverified features remain unavailable instead of falling back.

## P0/P1 runtime exactness

The daemon opens the profile read-only with `O_NOFOLLOW`, requires a regular
file no larger than 64 KiB, rejects trailing non-whitespace JSON, rejects every
unknown field and embedded-NUL strings, and requires schema exactly `1`.

The current loader additionally requires:

- ID `fibocom-l850-gl` and exactly one model declaration, `L850-GL`;
- exactly two USB matches: `2cb7:0007` as MBIM and `8087:095a` as NCM;
- management identifier `intel-xmm`;
- target backend identifiers `mbim-basic-connect` and `intel-xmm-ncm`;
- MBIM roles `02` primary, `04` ignored, and `06` secondary;
- NCM roles `00` primary, `02` ignored, `04` secondary, and exactly the
  lowercase candidates `06`, `08`, and `0a` with selector
  `hardware-required`;
- NCM `session_cid` exactly `0`;
- the seven reviewed capability keys and their exact states shown above.

This strictness prevents an installed data file from enabling new commands or
hardware behavior. Generalizing profiles for a second modem requires a reviewed
loader change, not only dropping another JSON file into the directory.

P1 also requires `cdc_acm` on reviewed AT/ignored interfaces, one exact
same-object `cdc_mbim` WDM/net pair on interface `00`, and `cdc_ncm` on NCM
candidates. That driver contract is
currently compiled into discovery rather than supplied by profile JSON; it must
move to typed schema data before supporting a second model.

## What P0 actually matches

Discovery matches only the exact USB VID/PID pair. It does not open AT to check
the declared manufacturer/model, firmware, CID behavior, or NCM traffic path.
Therefore responses explicitly report `model_confidence: usb_id_only`, and
diagnostics reports `hardware_validated: false`.

The profile's backend and capability strings are roadmap declarations. P0/P1
does not compile or invoke an MBIM/NCM bearer, eSIM operation, AT management
backend, band lock, cell lock, or SIM switch.
