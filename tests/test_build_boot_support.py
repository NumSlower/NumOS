"""Regression tests for the GRUB boot-support builder."""

import importlib.util
import pathlib
import tempfile
import unittest


REPO_ROOT = pathlib.Path(__file__).resolve().parents[1]
MODULE_PATH = REPO_ROOT / "tools" / "build_boot_support.py"

spec = importlib.util.spec_from_file_location("build_boot_support", MODULE_PATH)
build_boot_support = importlib.util.module_from_spec(spec)
spec.loader.exec_module(build_boot_support)


class BuildBootSupportTest(unittest.TestCase):
    """Keep the embedded GRUB script and module set aligned."""

    def test_grub_modules_cover_runtime_script_commands(self):
        # The normal-mode boot script still uses GRUB scripting, source, tests,
        # and echo. Missing any of these turns script lines into
        # "Unknown command" errors during boot because NumOS does not ship
        # external GRUB modules beside the core image.
        required_modules = {"normal", "configfile", "test", "echo", "loadenv"}

        self.assertTrue(required_modules.issubset(set(build_boot_support.GRUB_MODULES)))

    def test_embedded_config_loads_runtime_grub_cfg(self):
        with tempfile.TemporaryDirectory() as tmp:
            cfg_path = pathlib.Path(tmp) / "embedded.cfg"
            build_boot_support.write_embedded_config(str(cfg_path))
            script = cfg_path.read_text(encoding="ascii")

        self.assertIn("set root=hd0,msdos1", script)
        self.assertIn("set prefix=($root)/boot/grub", script)
        self.assertIn("configfile $prefix/grub.cfg", script)
        self.assertNotIn("source /boot/boot.cfg", script)
        self.assertNotIn("multiboot2", script)

    def test_runtime_grub_cfg_uses_source_and_echo_flow(self):
        # Keep the real boot script shape stable so the module guard above
        # stays honest.
        with tempfile.TemporaryDirectory() as tmp:
            cfg_path = pathlib.Path(tmp) / "grub.cfg"
            build_boot_support.write_runtime_grub_cfg(str(cfg_path))
            script = cfg_path.read_text(encoding="ascii")

        self.assertIn("source /boot/boot.cfg", script)
        self.assertIn("source /boot/status.cfg", script)
        self.assertIn('echo "NumOS: primary kernel failed, trying fallback"', script)
        self.assertIn('echo "NumOS: no bootable kernel found"', script)


if __name__ == "__main__":
    unittest.main()
