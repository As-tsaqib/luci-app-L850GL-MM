# SPDX-FileCopyrightText: 2026 As Tsaqib
# SPDX-License-Identifier: Apache-2.0

"""Behavioral tests for the allowlisted expert ModemManager rename."""

from pathlib import Path
import subprocess
import sys
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]
TRANSFORMER = ROOT / "packaging" / "prepare-modemmanager-expert.py"


def recipe(version: str, release: str) -> str:
    return f"""PKG_NAME:=modemmanager
PKG_VERSION:={version}
PKG_RELEASE:={release}

define Package/modemmanager/config
  source "$(SOURCE)/Config.in"
endef

define Package/modemmanager
  TITLE:=Control utility for any kind of mobile broadband modem
endef

define Package/modemmanager/description
  Stock package description.
endef

define Package/modemmanager-rpcd
  DEPENDS:=modemmanager
endef

-Dat_command_via_dbus=$(if $(CONFIG_MODEMMANAGER_WITH_AT_COMMAND_VIA_DBUS),true,false)

MESON_ARGS += \\

define Package/modemmanager/install
endef

define Package/modemmanager-rpcd/install
endef

$(eval $(call BuildPackage,modemmanager))
$(eval $(call BuildPackage,modemmanager-rpcd))
"""


CONFIG_IN = """menu "Configuration"
\tdepends on PACKAGE_modemmanager

config MODEMMANAGER_WITH_AT_COMMAND_VIA_DBUS
\tbool "Allow AT commands via D-Bus"

endmenu
"""


class TransformerTest(unittest.TestCase):
    def transform(
        self,
        source: str,
        config_source: str = CONFIG_IN,
        *,
        create_config: bool = True,
    ) -> tuple[subprocess.CompletedProcess[str], str, str | None]:
        with tempfile.TemporaryDirectory() as directory:
            makefile = Path(directory) / "Makefile"
            config_in = Path(directory) / "Config.in"
            makefile.write_text(source, encoding="utf-8")
            if create_config:
                config_in.write_text(config_source, encoding="utf-8")
            result = subprocess.run(
                [sys.executable, str(TRANSFORMER), str(makefile)],
                check=False,
                capture_output=True,
                text=True,
            )
            transformed_config = (
                config_in.read_text(encoding="utf-8") if config_in.exists() else None
            )
            return result, makefile.read_text(encoding="utf-8"), transformed_config

    def test_supported_release_recipes(self) -> None:
        for version, release in (("1.22.0", "20"), ("1.24.0", "10")):
            with self.subTest(version=version, release=release):
                result, transformed, transformed_config = self.transform(
                    recipe(version, release)
                )
                self.assertEqual(result.returncode, 0, result.stderr)
                self.assertIn("PKG_NAME:=modemmanager\n", transformed)
                self.assertIn(
                    "define Package/modemmanager-l850gl-expert\n", transformed
                )
                self.assertIn("  PROVIDES:=modemmanager\n", transformed)
                self.assertIn("  CONFLICTS:=modemmanager\n", transformed)
                for suffix in ("config", "description", "install"):
                    self.assertEqual(
                        transformed.count(
                            f"define Package/modemmanager-l850gl-expert/{suffix}\n"
                        ),
                        1,
                    )
                self.assertNotIn("define Package/modemmanager\n", transformed)
                self.assertNotIn(
                    "$(eval $(call BuildPackage,modemmanager))", transformed
                )
                self.assertEqual(
                    transformed.count(
                        "$(eval $(call BuildPackage,modemmanager-l850gl-expert))"
                    ),
                    1,
                )
                self.assertIn(
                    "$(eval $(call BuildPackage,modemmanager-rpcd))", transformed
                )
                self.assertIsNotNone(transformed_config)
                self.assertIn(
                    "depends on PACKAGE_modemmanager-l850gl-expert",
                    transformed_config,
                )
                self.assertNotIn(
                    "depends on PACKAGE_modemmanager\n", transformed_config
                )

    def test_unknown_recipe_fails_without_rewrite(self) -> None:
        source = recipe("1.24.0", "9")
        result, transformed, transformed_config = self.transform(source)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("unsupported ModemManager recipe", result.stderr)
        self.assertEqual(transformed, source)
        self.assertEqual(transformed_config, CONFIG_IN)

    def test_missing_config_in_fails_without_rewrite(self) -> None:
        source = recipe("1.24.0", "10")
        result, transformed, transformed_config = self.transform(
            source, create_config=False
        )
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("missing sibling ModemManager Config.in", result.stderr)
        self.assertEqual(transformed, source)
        self.assertIsNone(transformed_config)

    def test_ambiguous_config_dependency_fails_without_rewrite(self) -> None:
        source = recipe("1.22.0", "20")
        ambiguous = CONFIG_IN.replace(
            "endmenu\n", "\tdepends on PACKAGE_modemmanager\nendmenu\n"
        )
        result, transformed, transformed_config = self.transform(source, ambiguous)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("expected exactly one Config.in dependency", result.stderr)
        self.assertEqual(transformed, source)
        self.assertEqual(transformed_config, ambiguous)


if __name__ == "__main__":
    unittest.main()
