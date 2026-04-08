#!/usr/bin/env python3
"""
Download a NumOS package manifest and its staged payloads from a URL.

The native pkg tool installs manifests from /run and copies referenced payloads
from the same directory. This helper mirrors that flow on the host by
downloading the manifest and every referenced file into a staging directory.
"""

from __future__ import annotations

import argparse
import os
import pathlib
import shutil
import sys
import tempfile
import urllib.error
import urllib.parse
import urllib.request
from dataclasses import dataclass

TOOLS_DIR = os.path.dirname(__file__)
if TOOLS_DIR not in sys.path:
    sys.path.insert(0, TOOLS_DIR)

from numos_version import read_numos_version


@dataclass
class CopyEntry:
    """One copy directive from a .PKG manifest."""

    source: str
    destination: str


@dataclass
class PackageManifest:
    """Parsed fields from a NumOS package manifest."""

    name: str
    version: str
    copies: list[CopyEntry]


def is_space(ch: str) -> bool:
    """Match the native pkg parser's whitespace rules."""
    return ch in (" ", "\t", "\r", "\n")


def trim(text: str) -> str:
    """Remove leading and trailing manifest whitespace."""
    start = 0
    end = len(text)
    while start < end and is_space(text[start]):
        start += 1
    while end > start and is_space(text[end - 1]):
        end -= 1
    return text[start:end]


def split_word(text: str) -> tuple[str, str]:
    """Split one line into directive head and tail."""
    text = trim(text)
    pos = 0
    while pos < len(text) and not is_space(text[pos]):
        pos += 1
    if pos >= len(text):
        return text, ""
    return trim(text[:pos]), trim(text[pos + 1 :])


def fat_8dot3_name(name: str) -> str | None:
    """Return an uppercase FAT 8.3 short name or None."""
    stem, ext = os.path.splitext(name)
    if ext.startswith("."):
        ext = ext[1:]
    if len(stem) == 0 or len(stem) > 8 or len(ext) > 3:
        return None
    return stem.upper().ljust(8) + ext.upper().ljust(3)


def validate_stage_name(name: str, label: str) -> None:
    """Reject names that the image builder would later skip."""
    if not name:
        raise ValueError(f"{label} is empty")
    if "/" in name or "\\" in name:
        raise ValueError(f"{label} must stay in one /run entry: {name}")
    if fat_8dot3_name(name) is None:
        raise ValueError(f"{label} is not FAT 8.3 compatible: {name}")


def parse_manifest(text: str) -> PackageManifest:
    """Parse a NumOS .PKG manifest."""
    manifest = PackageManifest(name="", version="", copies=[])

    for raw_line in text.splitlines():
        line = trim(raw_line)
        if not line or line.startswith("#"):
            continue

        head, tail = split_word(line)
        if head == "name":
            manifest.name = tail
            continue
        if head == "version":
            manifest.version = tail
            continue
        if head == "copy":
            source, destination = split_word(tail)
            if not source or not destination:
                raise ValueError(f"Malformed copy directive: {raw_line}")
            manifest.copies.append(CopyEntry(source=source, destination=destination))
            continue
        raise ValueError(f"Unknown manifest directive: {head}")

    return manifest


def download_bytes(url: str, timeout: int) -> bytes:
    """Fetch one URL into memory."""
    with urllib.request.urlopen(url, timeout=timeout) as response:
        return response.read()


def derive_manifest_filename(package_url: str, manifest: PackageManifest) -> str:
    """Choose the staged .PKG filename."""
    if manifest.name:
        filename = manifest.name
        if not filename.upper().endswith(".PKG"):
            filename = f"{filename}.PKG"
        filename = filename.upper()
    else:
        parsed = urllib.parse.urlparse(package_url)
        filename = pathlib.PurePosixPath(parsed.path).name.upper()
        if not filename:
            raise ValueError("Package URL must end with a .PKG filename when the manifest has no name")
    validate_stage_name(filename, "package manifest name")
    if not filename.upper().endswith(".PKG"):
        raise ValueError(f"package manifest name must end with .PKG: {filename}")
    return filename


def build_asset_url(package_url: str, asset_base_url: str | None, source_name: str) -> str:
    """Resolve one staged payload URL."""
    base_url = asset_base_url if asset_base_url else package_url
    return urllib.parse.urljoin(base_url, urllib.parse.quote(source_name))


def write_bytes(path: pathlib.Path, data: bytes) -> None:
    """Write one file and create parent directories."""
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(data)


def stage_downloads(staging_dir: pathlib.Path, files: dict[str, bytes]) -> list[pathlib.Path]:
    """Write a download set into the staging directory."""
    staging_dir.mkdir(parents=True, exist_ok=True)

    with tempfile.TemporaryDirectory(prefix="numos-pkg-", dir=staging_dir.parent) as temp_dir:
        temp_path = pathlib.Path(temp_dir)
        written_paths: list[pathlib.Path] = []

        for name, data in files.items():
            write_bytes(temp_path / name, data)

        for name in sorted(files):
            final_path = staging_dir / name
            shutil.copyfile(temp_path / name, final_path)
            written_paths.append(final_path)

    return written_paths


def download_package(
    package_url: str,
    staging_dir: str,
    asset_base_url: str | None = None,
    timeout: int = 15,
) -> tuple[PackageManifest, list[pathlib.Path]]:
    """Download one .PKG manifest and every staged file it references."""
    manifest_bytes = download_bytes(package_url, timeout)
    manifest_text = manifest_bytes.decode("utf-8")
    manifest = parse_manifest(manifest_text)

    files_to_stage: dict[str, bytes] = {}
    manifest_name = derive_manifest_filename(package_url, manifest)
    files_to_stage[manifest_name] = manifest_bytes

    downloaded_sources: set[str] = set()
    for entry in manifest.copies:
        validate_stage_name(entry.source, "copy source")
        if entry.source in downloaded_sources:
            continue
        downloaded_sources.add(entry.source)
        asset_url = build_asset_url(package_url, asset_base_url, entry.source)
        files_to_stage[entry.source] = download_bytes(asset_url, timeout)

    written = stage_downloads(pathlib.Path(staging_dir), files_to_stage)
    return manifest, written


def build_parser() -> argparse.ArgumentParser:
    """Build the command-line interface."""
    repo_root = pathlib.Path(__file__).resolve().parents[1]
    default_stage_dir = repo_root / "build" / "stage" / "run"

    parser = argparse.ArgumentParser(
        description="Download a NumOS package manifest and stage it into /run payload files."
    )
    parser.add_argument(
        "-v",
        "--version",
        action="version",
        version=f"{pathlib.Path(__file__).name} {read_numos_version(__file__)}",
    )
    parser.add_argument("url", help="URL to a .PKG manifest on your website")
    parser.add_argument(
        "--stage-dir",
        default=str(default_stage_dir),
        help="Directory to receive staged /run files",
    )
    parser.add_argument(
        "--base-url",
        default=None,
        help="Base URL for payload files referenced by copy directives",
    )
    parser.add_argument(
        "--timeout",
        type=int,
        default=15,
        help="HTTP timeout in seconds",
    )
    return parser


def main() -> int:
    """Parse CLI arguments and download the requested package."""
    parser = build_parser()
    args = parser.parse_args()

    try:
        manifest, written = download_package(
            package_url=args.url,
            staging_dir=args.stage_dir,
            asset_base_url=args.base_url,
            timeout=args.timeout,
        )
    except (OSError, UnicodeDecodeError, ValueError, urllib.error.URLError) as exc:
        print(f"pkg download failed: {exc}")
        return 1

    print(f"Downloaded package into: {args.stage_dir}")
    if manifest.name:
        print(f"  name: {manifest.name}")
    if manifest.version:
        print(f"  version: {manifest.version}")
    for path in written:
        print(f"  staged: {path.name}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
