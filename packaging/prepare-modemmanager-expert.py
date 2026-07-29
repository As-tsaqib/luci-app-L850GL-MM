# SPDX-FileCopyrightText: 2026 As Tsaqib
# SPDX-License-Identifier: Apache-2.0

"""Rename OpenWrt's pinned ModemManager binary package for the expert bundle."""

from pathlib import Path
import re
import sys


SUPPORTED_RECIPES = {
    ("1.22.0", "20"),
    ("1.24.0", "10"),
}


EXPERT_PACKAGE = "modemmanager-l850gl-expert"


def replace_once(source: str, marker: str, replacement: str) -> str:
    count = source.count(marker)
    if count != 1:
        raise ValueError(f"expected one recipe marker, found {count}: {marker!r}")
    return source.replace(marker, replacement, 1)


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
    config_in = makefile.with_name("Config.in")
    if not config_in.is_file():
        raise ValueError(f"missing sibling ModemManager Config.in: {config_in}")
    config_source = config_in.read_text(encoding="utf-8").replace("\r\n", "\n")

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
    if EXPERT_PACKAGE in source or f"PACKAGE_{EXPERT_PACKAGE}" in config_source:
        raise ValueError("expert package rename already exists")

    package_header = "define Package/modemmanager\n"
    source = replace_once(
        source,
        package_header,
        f"define Package/{EXPERT_PACKAGE}\n"
        "  PROVIDES:=modemmanager\n"
        "  CONFLICTS:=modemmanager\n",
    )
    for suffix in ("config", "description", "install"):
        source = replace_once(
            source,
            f"define Package/modemmanager/{suffix}\n",
            f"define Package/{EXPERT_PACKAGE}/{suffix}\n",
        )
    source = replace_once(
        source,
        "$(eval $(call BuildPackage,modemmanager))\n",
        f"$(eval $(call BuildPackage,{EXPERT_PACKAGE}))\n",
    )

    dependency = re.compile(
        r"^([ \t]*depends[ \t]+on[ \t]+)PACKAGE_modemmanager([ \t]*)$",
        re.MULTILINE,
    )
    dependency_matches = list(dependency.finditer(config_source))
    if len(dependency_matches) != 1:
        raise ValueError(
            "expected exactly one Config.in dependency on PACKAGE_modemmanager, "
            f"found {len(dependency_matches)}"
        )
    config_source = dependency.sub(
        lambda match: (
            f"{match.group(1)}PACKAGE_{EXPERT_PACKAGE}{match.group(2)}"
        ),
        config_source,
        count=1,
    )

    if "define Package/modemmanager\n" in source:
        raise ValueError("stock ModemManager binary package remains after rename")
    if "$(eval $(call BuildPackage,modemmanager))" in source:
        raise ValueError("stock ModemManager binary output remains registered")

    config_in.write_text(config_source, encoding="utf-8", newline="\n")
    makefile.write_text(source, encoding="utf-8", newline="\n")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
