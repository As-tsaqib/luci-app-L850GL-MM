# SPDX-FileCopyrightText: 2026 As Tsaqib
# SPDX-License-Identifier: Apache-2.0

"""Add the reviewed L850-GL expert binary package to OpenWrt's pinned recipe."""

from pathlib import Path
import re
import sys


SUPPORTED_RECIPES = {
    ("1.22.0", "20"),
    ("1.24.0", "10"),
}


EXPERT_DEFINITION_TEMPLATE = r'''
define Package/modemmanager-l850gl-expert
  $(Package/modemmanager)
  TITLE:=ModemManager expert transport for the L850-GL companion
  PROVIDES:=modemmanager
  CONFLICTS:=modemmanager
endef

define Package/modemmanager-l850gl-expert/description
 The upstream OpenWrt ModemManager {recipe_version} package rebuilt with the
 reviewed AT-over-D-Bus transport required by the firmware-gated L850-GL
 expert bridge. It provides and conflicts with the stock modemmanager package.
endef
'''

EXPERT_INSTALL = r'''
define Package/modemmanager-l850gl-expert/install
  $(Package/modemmanager/install)
endef
'''


def insert_once(source: str, marker: str, insertion: str) -> str:
    if source.count(marker) != 1:
        raise ValueError(f"expected one recipe marker, found {source.count(marker)}: {marker!r}")
    return source.replace(marker, insertion + "\n" + marker, 1)


def recipe_assignment(source: str, name: str) -> str:
    matches = re.findall(rf"^{re.escape(name)}:=(\S+)\s*$", source, re.MULTILINE)
    if len(matches) != 1:
        raise ValueError(f"expected exactly one {name} assignment, found {len(matches)}")
    return matches[0]


def main() -> int:
    if len(sys.argv) != 2:
        print(f"usage: {Path(sys.argv[0]).name} OPENWRT_MODEMMANAGER_MAKEFILE", file=sys.stderr)
        return 2

    makefile = Path(sys.argv[1])
    source = makefile.read_text(encoding="utf-8").replace("\r\n", "\n")
    version = recipe_assignment(source, "PKG_VERSION")
    release = recipe_assignment(source, "PKG_RELEASE")
    if (version, release) not in SUPPORTED_RECIPES:
        raise ValueError(
            f"unsupported ModemManager recipe {version}-r{release}; "
            "expected exactly 1.22.0-r20 or 1.24.0-r10"
        )

    for required in (
        "PKG_NAME:=modemmanager",
        "-Dat_command_via_dbus=$(if $(CONFIG_MODEMMANAGER_WITH_AT_COMMAND_VIA_DBUS),true,false)",
        "define Package/modemmanager/install",
    ):
        if required not in source:
            raise ValueError(f"pinned ModemManager recipe is missing {required!r}")
    if "Package/modemmanager-l850gl-expert" in source:
        raise ValueError("expert package definition already exists")

    expert_definition = EXPERT_DEFINITION_TEMPLATE.format(
        recipe_version=f"{version}-r{release}"
    )
    source = insert_once(source, "MESON_ARGS += \\\n", expert_definition)
    source = insert_once(
        source,
        "define Package/modemmanager-rpcd/install\n",
        EXPERT_INSTALL,
    )
    source = source.replace(
        "$(eval $(call BuildPackage,modemmanager))\n",
        "$(eval $(call BuildPackage,modemmanager))\n"
        "$(eval $(call BuildPackage,modemmanager-l850gl-expert))\n",
        1,
    )
    if source.count("$(eval $(call BuildPackage,modemmanager-l850gl-expert))") != 1:
        raise ValueError("failed to register exactly one expert package")

    makefile.write_text(source, encoding="utf-8", newline="\n")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
