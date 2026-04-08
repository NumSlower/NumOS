#!/usr/bin/env python3
"""Audit host build dependencies for NumOS."""

from __future__ import annotations

import argparse
import json
import platform
import shutil
import subprocess
import sys
from dataclasses import asdict, dataclass
from pathlib import Path

from numos_version import read_numos_version


@dataclass
class DependencyResult:
    name: str
    commands: list[str]
    found: bool
    selected: str | None
    version: str | None
    required: bool
    note: str
    ok: bool


DEPENDENCIES = {
    "common": [
        {"name": "GNU make", "commands": ["make"], "required": True},
        {"name": "Python 3", "commands": ["python3"], "required": True},
        {"name": "Disk partitioning", "commands": ["parted"], "required": True},
        {"name": "FAT32 formatter", "commands": ["mkfs.vfat"], "required": True},
        {"name": "Debugger", "commands": ["gdb", "gdb-multiarch"], "required": True},
    ],
    "x86_64": [
        {
            "name": "x86 compiler",
            "commands": ["x86_64-elf-gcc", "gcc", "clang"],
            "required": True,
            "min_gcc": "13.0.0",
        },
        {"name": "x86 linker", "commands": ["x86_64-elf-ld", "ld", "ld.lld"], "required": True},
        {"name": "x86 assembler", "commands": ["nasm", "yasm"], "required": True},
        {"name": "QEMU x86_64", "commands": ["qemu-system-x86_64"], "required": True},
        {"name": "GRUB ISO builder", "commands": ["grub-mkrescue"], "required": True},
        {"name": "xorriso", "commands": ["xorriso"], "required": True},
        {"name": "mtools", "commands": ["mformat"], "required": True},
    ],
    "arm64": [
        {
            "name": "ARM64 compiler",
            "commands": ["aarch64-none-elf-gcc", "aarch64-linux-gnu-gcc"],
            "required": True,
            "min_gcc": "13.0.0",
        },
        {"name": "ARM64 linker", "commands": ["aarch64-none-elf-ld", "aarch64-linux-gnu-ld"], "required": True},
        {"name": "QEMU ARM64", "commands": ["qemu-system-aarch64"], "required": True},
        {"name": "Multiarch GDB", "commands": ["gdb-multiarch", "gdb"], "required": True},
    ],
    "toolchain": [
        {"name": "Downloader", "commands": ["curl", "wget"], "required": True},
        {"name": "Texinfo", "commands": ["makeinfo"], "required": True},
    ],
}


INSTALL_HINTS = {
    "Linux": [
        "sudo apt update",
        "sudo apt install make python3 nasm qemu-system-x86 qemu-system-arm grub-common grub-pc-bin xorriso mtools parted dosfstools gcc-aarch64-linux-gnu binutils-aarch64-linux-gnu gdb gdb-multiarch texinfo curl",
    ],
    "Darwin": [
        "brew install make nasm qemu xorriso mtools gdb texinfo",
        "brew install aarch64-elf-gcc",
        "brew install x86_64-elf-gcc",
    ],
    "Windows": [
        "Use WSL2 with Ubuntu, then run the Linux install commands inside WSL.",
        "MSYS2 also works for host tooling, but the documented path in this repo is WSL2.",
    ],
}


def parse_version(version_text: str) -> tuple[int, int, int]:
    digits: list[int] = []
    token = []
    for ch in version_text.strip():
        if ch.isdigit():
            token.append(ch)
            continue
        if token:
            digits.append(int("".join(token)))
            token = []
        if len(digits) >= 3:
            break
    if token and len(digits) < 3:
        digits.append(int("".join(token)))
    while len(digits) < 3:
        digits.append(0)
    return tuple(digits[:3])


def command_version(command_path: str) -> str | None:
    try:
        if command_path.endswith("gcc") or command_path == "gcc":
            proc = subprocess.run(
                [command_path, "-dumpfullversion"],
                check=False,
                capture_output=True,
                text=True,
            )
            version_text = proc.stdout.strip() or proc.stderr.strip()
            if version_text:
                return version_text
        proc = subprocess.run(
            [command_path, "--version"],
            check=False,
            capture_output=True,
            text=True,
        )
    except OSError:
        return None

    output = (proc.stdout or proc.stderr).strip()
    if not output:
        return None
    return output.splitlines()[0].strip()


def check_dependency(entry: dict[str, object]) -> DependencyResult:
    selected = None
    for command_name in entry["commands"]:
        command_path = shutil.which(command_name)
        if command_path:
            selected = command_path
            break

    if not selected:
        return DependencyResult(
            name=str(entry["name"]),
            commands=list(entry["commands"]),
            found=False,
            selected=None,
            version=None,
            required=bool(entry["required"]),
            note="missing",
            ok=not bool(entry["required"]),
        )

    version_text = command_version(selected)
    ok = True
    note = "found"
    min_gcc = entry.get("min_gcc")
    if min_gcc and selected.endswith("gcc"):
        if version_text is None:
            ok = False
            note = "failed to read GCC version"
        elif parse_version(version_text) < parse_version(str(min_gcc)):
            ok = False
            note = f"GCC {min_gcc}+ required"

    return DependencyResult(
        name=str(entry["name"]),
        commands=list(entry["commands"]),
        found=True,
        selected=selected,
        version=version_text,
        required=bool(entry["required"]),
        note=note,
        ok=ok,
    )


def selected_groups(arch: str) -> list[str]:
    groups = ["common", "toolchain"]
    if arch == "all":
        groups.extend(["x86_64", "arm64"])
    else:
        groups.append(arch)
    return groups


def main() -> int:
    parser = argparse.ArgumentParser(description="Audit host build dependencies for NumOS.")
    parser.add_argument(
        "--arch",
        choices=("all", "x86_64", "arm64"),
        default="all",
        help="Dependency profile to audit",
    )
    parser.add_argument("--json", action="store_true", help="Emit machine-readable JSON")
    parser.add_argument(
        "-v",
        "--version",
        action="version",
        version=f"{Path(__file__).name} {read_numos_version(__file__)}",
    )
    args = parser.parse_args()

    results: dict[str, list[DependencyResult]] = {}
    exit_code = 0
    for group_name in selected_groups(args.arch):
        group_results = [check_dependency(item) for item in DEPENDENCIES[group_name]]
        results[group_name] = group_results
        if any(item.required and not item.ok for item in group_results):
            exit_code = 1

    if args.json:
        payload = {
            "version": read_numos_version(__file__),
            "host_os": platform.system(),
            "host_release": platform.release(),
            "arch": args.arch,
            "results": {
                group_name: [asdict(item) for item in group_results]
                for group_name, group_results in results.items()
            },
            "install_hints": INSTALL_HINTS.get(platform.system(), []),
        }
        print(json.dumps(payload, indent=2))
        return exit_code

    print(f"NumOS host dependency audit {read_numos_version(__file__)}")
    print(f"Host: {platform.system()} {platform.release()}")
    print(f"Profile: {args.arch}")
    print("")

    for group_name, group_results in results.items():
        print(group_name)
        for item in group_results:
            status = "OK" if item.ok else "MISSING"
            print(f"  {status:<7} {item.name}")
            if item.selected:
                print(f"    command: {item.selected}")
            if item.version:
                print(f"    version: {item.version}")
            if item.note and item.note != "found":
                print(f"    note: {item.note}")
        print("")

    if exit_code != 0:
        print("Install hints")
        for line in INSTALL_HINTS.get(platform.system(), []):
            print(f"  {line}")

    return exit_code


if __name__ == "__main__":
    raise SystemExit(main())
