"""Regression tests for the NumOS package download helper."""

import importlib.util
import io
import pathlib
import sys
import tempfile
import unittest
from unittest import mock


REPO_ROOT = pathlib.Path(__file__).resolve().parents[1]
MODULE_PATH = REPO_ROOT / "tools" / "download_pkg.py"

spec = importlib.util.spec_from_file_location("download_pkg", MODULE_PATH)
download_pkg = importlib.util.module_from_spec(spec)
sys.modules[spec.name] = download_pkg
spec.loader.exec_module(download_pkg)


class FakeResponse(io.BytesIO):
    """Minimal urlopen response object for local tests."""

    def __enter__(self):
        return self

    def __exit__(self, exc_type, exc, traceback):
        self.close()
        return False


class DownloadPkgTest(unittest.TestCase):
    """Keep the package staging helper stable as website flows evolve."""

    def test_parse_manifest_reads_copy_entries(self):
        manifest = download_pkg.parse_manifest(
            "# sample\n"
            "name OCLDEV\n"
            "version 1.2.3\n"
            "copy OCL.ELF /bin/OCL.ELF\n"
            "copy README.TXT /home/README.TXT\n"
        )

        self.assertEqual(manifest.name, "OCLDEV")
        self.assertEqual(manifest.version, "1.2.3")
        self.assertEqual(
            [(entry.source, entry.destination) for entry in manifest.copies],
            [("OCL.ELF", "/bin/OCL.ELF"), ("README.TXT", "/home/README.TXT")],
        )

    def test_download_package_stages_manifest_and_payloads(self):
        responses = {
            "https://packages.example.com/OCLDEV.PKG": (
                "name OCLDEV\n"
                "version 1.2.3\n"
                "copy OCL.ELF /bin/OCL.ELF\n"
                "copy README.TXT /home/README.TXT\n"
            ).encode("utf-8"),
            "https://packages.example.com/OCL.ELF": b"elf-bytes",
            "https://packages.example.com/README.TXT": b"readme-bytes",
        }

        def fake_urlopen(url, timeout=0):
            self.assertEqual(timeout, 9)
            return FakeResponse(responses[url])

        with tempfile.TemporaryDirectory() as tmp, mock.patch.object(
            download_pkg.urllib.request, "urlopen", side_effect=fake_urlopen
        ):
            manifest, written = download_pkg.download_package(
                package_url="https://packages.example.com/OCLDEV.PKG",
                staging_dir=tmp,
                timeout=9,
            )

            self.assertEqual(manifest.name, "OCLDEV")
            self.assertEqual(sorted(path.name for path in written), ["OCL.ELF", "OCLDEV.PKG", "README.TXT"])
            self.assertEqual(pathlib.Path(tmp, "OCLDEV.PKG").read_text(encoding="utf-8"), responses[
                "https://packages.example.com/OCLDEV.PKG"
            ].decode("utf-8"))
            self.assertEqual(pathlib.Path(tmp, "OCL.ELF").read_bytes(), b"elf-bytes")
            self.assertEqual(pathlib.Path(tmp, "README.TXT").read_bytes(), b"readme-bytes")

    def test_download_package_uses_override_base_url(self):
        responses = {
            "https://example.com/pkg/OCLDEV.PKG": b"name OCLDEV\ncopy OCL.ELF /bin/OCL.ELF\n",
            "https://cdn.example.com/assets/OCL.ELF": b"elf-bytes",
        }

        def fake_urlopen(url, timeout=0):
            return FakeResponse(responses[url])

        with tempfile.TemporaryDirectory() as tmp, mock.patch.object(
            download_pkg.urllib.request, "urlopen", side_effect=fake_urlopen
        ):
            download_pkg.download_package(
                package_url="https://example.com/pkg/OCLDEV.PKG",
                staging_dir=tmp,
                asset_base_url="https://cdn.example.com/assets/",
            )

            self.assertEqual(pathlib.Path(tmp, "OCL.ELF").read_bytes(), b"elf-bytes")

    def test_download_package_rejects_non_fat_source_name(self):
        with tempfile.TemporaryDirectory() as tmp, mock.patch.object(
            download_pkg, "download_bytes", return_value=b"name OCLDEV\ncopy TOOLONG99.BIN /bin/TOOLONG99.BIN\n"
        ):
            with self.assertRaisesRegex(ValueError, "FAT 8.3"):
                download_pkg.download_package(
                    package_url="https://packages.example.com/OCLDEV.PKG",
                    staging_dir=tmp,
                )


if __name__ == "__main__":
    unittest.main()
