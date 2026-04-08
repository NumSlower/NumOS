"""Regression tests for the local NumOS update publisher."""

import importlib.util
import pathlib
import sys
import tempfile
import unittest
from unittest import mock


REPO_ROOT = pathlib.Path(__file__).resolve().parents[1]
MODULE_PATH = REPO_ROOT / "New_Update" / "serve_update_site.py"

spec = importlib.util.spec_from_file_location("serve_update_site", MODULE_PATH)
serve_update_site = importlib.util.module_from_spec(spec)
sys.modules[spec.name] = serve_update_site
spec.loader.exec_module(serve_update_site)


class ServeUpdateSiteTest(unittest.TestCase):
    """Keep kernel and file update publishing aligned with build outputs."""

    def test_publish_site_writes_kernel_and_file_updates(self):
        with tempfile.TemporaryDirectory() as tmp:
            repo_root = pathlib.Path(tmp)
            build_dir = repo_root / "build"
            build_dir.mkdir()
            (build_dir / "kernel.bin").write_bytes(b"kernel-default")
            (build_dir / "kernel-vesa.bin").write_bytes(b"kernel-vesa")
            (build_dir / "user").mkdir()
            (build_dir / "user" / "shell.elf").write_bytes(b"fresh-shell")

            user_bin_dir = repo_root / "user" / "files" / "bin"
            user_bin_dir.mkdir(parents=True)
            (user_bin_dir / "shell.elf").write_bytes(b"installed-shell")

            home_dir = repo_root / "user" / "files" / "home"
            home_dir.mkdir(parents=True)
            (home_dir / "Test.txt").write_text("hello\n", encoding="utf-8")

            include_dir = repo_root / "user" / "include"
            include_dir.mkdir(parents=True)
            (include_dir / "syscalls.h").write_text("/* syscalls */\n", encoding="utf-8")

            site_root = repo_root / "New_Update" / "site"
            with mock.patch.object(serve_update_site, "detect_windows_host_ip", return_value=None):
                metadata = serve_update_site.publish_site(repo_root, site_root, 8080)

            downloads_dir = site_root / "downloads"
            self.assertEqual((downloads_dir / "latest-kernel.bin").read_bytes(), b"kernel-default")
            self.assertEqual((downloads_dir / "latest-kernel-vesa.bin").read_bytes(), b"kernel-vesa")
            self.assertEqual((downloads_dir / "shell.elf").read_bytes(), b"installed-shell")
            self.assertEqual((downloads_dir / "SYSCALLS.H").read_text(encoding="utf-8"), "/* syscalls */\n")
            self.assertEqual((downloads_dir / "Test.txt").read_text(encoding="utf-8"), "hello\n")

            manifest = (downloads_dir / "latest-files.pkg").read_text(encoding="utf-8")
            self.assertIn("name FILES", manifest)
            self.assertIn("copy shell.elf /bin/shell.elf", manifest)
            self.assertIn("copy Test.txt /home/Test.txt", manifest)
            self.assertIn("copy SYSCALLS.H /include/SYSCALLS.H", manifest)

            self.assertIn("kernel", metadata["guest_urls"])
            self.assertIn("files_package", metadata["guest_urls"])
            self.assertIn("fallback_kernel", metadata["guest_urls"])
            self.assertIn("fallback_files_package", metadata["guest_urls"])

            index_html = (site_root / "index.html").read_text(encoding="utf-8")
            self.assertIn("pkg kernel http://10.0.2.2:8080/downloads/latest-kernel.bin reboot", index_html)

    def test_publish_site_supports_file_updates_without_kernel_artifacts(self):
        with tempfile.TemporaryDirectory() as tmp:
            repo_root = pathlib.Path(tmp)
            build_user_dir = repo_root / "build" / "user"
            build_user_dir.mkdir(parents=True)
            (build_user_dir / "pkg.elf").write_bytes(b"pkg-bytes")

            site_root = repo_root / "New_Update" / "site"
            with mock.patch.object(serve_update_site, "detect_windows_host_ip", return_value=None):
                metadata = serve_update_site.publish_site(repo_root, site_root, 8080)

            self.assertNotIn("kernel", metadata["guest_urls"])
            self.assertIn("files_package", metadata["guest_urls"])
            self.assertEqual([artifact["kind"] for artifact in metadata["artifacts"]], ["package"])

            index_html = (site_root / "index.html").read_text(encoding="utf-8")
            self.assertIn("pkg http://10.0.2.2:8080/downloads/latest-files.pkg", index_html)


if __name__ == "__main__":
    unittest.main()
