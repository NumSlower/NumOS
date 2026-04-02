"""Regression tests for the FAT32 image builder helper."""

import importlib.util
import pathlib
import struct
import tempfile
import unittest


REPO_ROOT = pathlib.Path(__file__).resolve().parents[1]
MODULE_PATH = REPO_ROOT / "tools" / "create_disk.py"

spec = importlib.util.spec_from_file_location("create_disk", MODULE_PATH)
create_disk = importlib.util.module_from_spec(spec)
spec.loader.exec_module(create_disk)


class CreateDiskTest(unittest.TestCase):
    """Keep the image builder behavior stable while the implementation evolves."""

    def test_load_staged_files_keeps_fat_compatible_entries(self):
        # The builder only supports FAT 8.3 names, so non-compliant files
        # should be skipped without disturbing the deterministic sort order.
        with tempfile.TemporaryDirectory() as tmp:
            stage_dir = pathlib.Path(tmp)
            (stage_dir / "test.ocl").write_text(
                "func int main() {\n"
                "    Let x:Int = 1 + 2;\n"
                "    return x;\n"
                "}\n",
                encoding="utf-8",
            )
            (stage_dir / "readme.txt").write_text("hello\n", encoding="utf-8")
            (stage_dir / "too-long-name.ocl").write_text("skip\n", encoding="utf-8")
            (stage_dir / "nested").mkdir()

            files = create_disk.load_staged_files(str(stage_dir))

        self.assertEqual([item["name"] for item in files], ["readme.txt", "test.ocl"])
        self.assertEqual([item["short_name"] for item in files], ["README  TXT", "TEST    OCL"])
        self.assertEqual([item["nt_reserved"] for item in files], [0x18, 0x18])
        self.assertEqual(files[1]["data"], b"func int main() {\n    Let x:Int = 1 + 2;\n    return x;\n}\n")

    def test_create_home_directory_writes_staged_file_entry(self):
        # Home directory entries need to preserve FAT case flags so names
        # render the same way after boot.
        home_files = [{
            "name": "test.ocl",
            "short_name": "TEST    OCL",
            "nt_reserved": 0x18,
            "size": 42,
            "data": b"",
            "cluster": 7,
            "clusters": 1,
        }]

        cluster = create_disk.create_home_directory(home_files)

        self.assertIn(b"TEST    OCL", cluster)
        self.assertEqual(cluster[64 + 12], 0x18)

    def test_fat_short_name_case_flags_preserves_uppercase_when_requested(self):
        # Lower-case-only names should set the NT reserved flags, while mixed
        # or fully upper-case names stay unchanged.
        self.assertEqual(create_disk.fat_short_name_case_flags("os.txt"), 0x18)
        self.assertEqual(create_disk.fat_short_name_case_flags("OS.TXT"), 0x00)
        self.assertEqual(create_disk.fat_short_name_case_flags("Readme.TXT"), 0x00)

    def test_create_fsinfo_stores_known_free_cluster_values(self):
        # FSInfo is a compact metadata block, so a direct offset check catches
        # accidental field drift.
        fsinfo = create_disk.create_fsinfo(1234, 56)

        self.assertEqual(struct.unpack_from('<I', fsinfo, 488)[0], 1234)
        self.assertEqual(struct.unpack_from('<I', fsinfo, 492)[0], 56)

    def test_load_staged_files_from_dirs_deduplicates_by_name(self):
        # Earlier stage directories should win so user-provided overrides stay
        # stable across build hosts.
        with tempfile.TemporaryDirectory() as first, tempfile.TemporaryDirectory() as second:
            pathlib.Path(first, "net.elf").write_bytes(b"first")
            pathlib.Path(second, "net.elf").write_bytes(b"second")
            pathlib.Path(second, "usb.elf").write_bytes(b"usb")

            files = create_disk.load_staged_files_from_dirs(
                [first, second],
                allowed_names={"net.elf", "usb.elf"},
            )

        self.assertEqual([item["name"] for item in files], ["net.elf", "usb.elf"])
        self.assertEqual(files[0]["data"], b"first")
        self.assertEqual(files[1]["data"], b"usb")

    def test_preinstalled_bin_names_include_connect(self):
        # The shell help advertises connect as a bundled tool, so the disk
        # image must stage the ELF into /bin.
        self.assertIn("connect.elf", create_disk.PREINSTALLED_BIN_NAMES)


if __name__ == "__main__":
    unittest.main()
