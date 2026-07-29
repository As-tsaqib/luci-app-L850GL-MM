# SPDX-FileCopyrightText: 2026 As Tsaqib
# SPDX-License-Identifier: Apache-2.0

"""Behavioral tests for the allowlisted expert ModemManager transformer."""

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

-Dat_command_via_dbus=$(if $(CONFIG_MODEMMANAGER_WITH_AT_COMMAND_VIA_DBUS),true,false)

MESON_ARGS += \\

define Package/modemmanager/install
endef

define Package/modemmanager-rpcd/install
endef

$(eval $(call BuildPackage,modemmanager))
"""


class TransformerTest(unittest.TestCase):
    def transform(self, source: str) -> tuple[subprocess.CompletedProcess[str], str]:
        with tempfile.TemporaryDirectory() as directory:
            makefile = Path(directory) / "Makefile"
            makefile.write_text(source, encoding="utf-8")
            result = subprocess.run(
                [sys.executable, str(TRANSFORMER), str(makefile)],
                check=False,
                capture_output=True,
                text=True,
            )
            return result, makefile.read_text(encoding="utf-8")

    def test_supported_release_recipes(self) -> None:
        for version, release in (("1.22.0", "20"), ("1.24.0", "10")):
            with self.subTest(version=version, release=release):
                result, transformed = self.transform(recipe(version, release))
                self.assertEqual(result.returncode, 0, result.stderr)
                self.assertIn(
                    "define Package/modemmanager-l850gl-expert\n", transformed
                )
                self.assertIn(f"ModemManager {version}-r{release} package", transformed)
                self.assertNotIn(
                    "define Package/modemmanager-l850gl-expert/config", transformed
                )
                self.assertEqual(
                    transformed.count(
                        "$(eval $(call BuildPackage,modemmanager-l850gl-expert))"
                    ),
                    1,
                )

    def test_unknown_recipe_fails_without_rewrite(self) -> None:
        source = recipe("1.24.0", "9")
        result, transformed = self.transform(source)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("unsupported ModemManager recipe", result.stderr)
        self.assertEqual(transformed, source)


if __name__ == "__main__":
    unittest.main()
