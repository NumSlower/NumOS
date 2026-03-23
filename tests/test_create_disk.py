import importlib.util
import pathlib
import tempfile
import unittest


REPO_ROOT = pathlib.Path(__file__).resolve().parents[1]
MODULE_PATH = REPO_ROOT / "tools" / "create_disk.py"

spec = importlib.util.spec_from_file_location("create_disk", MODULE_PATH)
create_disk = importlib.util.module_from_spec(spec)
spec.loader.exec_module(create_disk)


class CreateDiskTest(unittest.TestCase):
    def test_load_staged_files_keeps_fat_compatible_entries(self):
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
        self.assertEqual(create_disk.fat_short_name_case_flags("os.txt"), 0x18)
        self.assertEqual(create_disk.fat_short_name_case_flags("OS.TXT"), 0x00)
        self.assertEqual(create_disk.fat_short_name_case_flags("Readme.TXT"), 0x00)


if __name__ == "__main__":
    unittest.main()
