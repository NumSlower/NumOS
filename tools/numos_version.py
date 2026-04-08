#!/usr/bin/env python3
"""Shared version helpers for NumOS host-side scripts."""

from __future__ import annotations

from pathlib import Path

DEFAULT_NUMOS_VERSION = "v0.0.0"


def repo_root_from(path: str | Path) -> Path:
    return Path(path).resolve().parents[1]


def read_numos_version(path: str | Path) -> str:
    version_path = repo_root_from(path) / "VERSION"
    try:
        version_text = version_path.read_text(encoding="utf-8").strip()
    except OSError:
        return DEFAULT_NUMOS_VERSION
    return version_text or DEFAULT_NUMOS_VERSION

