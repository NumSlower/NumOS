#!/usr/bin/env python3
"""
Partition helper for NumOS images and physical drives.

Defaults to dry run. Use --apply to execute commands.
"""

import argparse
import os
import shlex
import subprocess
import sys
from typing import List, Optional


SUPPORTED_TABLES = ("gpt", "msdos")
SUPPORTED_FILESYSTEMS = ("fat32", "ext4", "none")


def run_command(cmd: List[str], apply: bool) -> None:
    text = " ".join(shlex.quote(part) for part in cmd)
    print(f"$ {text}")
    if not apply:
        return
    subprocess.run(cmd, check=True)


def command_exists(name: str) -> bool:
    return subprocess.call(
        ["bash", "-lc", f"command -v {shlex.quote(name)} >/dev/null 2>&1"]
    ) == 0


def assert_tooling(fs: str, apply: bool, target_is_file: bool) -> None:
    required = ["parted"]
    if fs == "fat32":
        required.append("mkfs.vfat")
    elif fs == "ext4":
        required.append("mkfs.ext4")

    missing = [name for name in required if not command_exists(name)]
    if missing:
        print(f"Missing required tools: {', '.join(missing)}")
        if apply:
            sys.exit(1)


def build_parted_commands(
    target: str, table: str, fs: str, start: str, end: str, name: Optional[str]
) -> List[List[str]]:
    cmds: List[List[str]] = []
    cmds.append(["parted", "-s", target, "mklabel", table])

    mkpart = ["parted", "-s", target, "mkpart", "primary"]
    if fs != "none":
        mkpart.append(fs)
    mkpart.extend([start, end])
    cmds.append(mkpart)

    if table == "gpt" and name:
        cmds.append(["parted", "-s", target, "name", "1", name])

    return cmds


def build_mkfs_command(partition_path: str, fs: str) -> Optional[List[str]]:
    if fs == "fat32":
        return ["mkfs.vfat", "-F", "32", partition_path]
    if fs == "ext4":
        return ["mkfs.ext4", "-F", partition_path]
    return None


def build_mkfs_file_command(target: str, fs: str, start_sector: int) -> Optional[List[str]]:
    if fs == "fat32":
        return ["mkfs.vfat", "-F", "32", f"--offset={start_sector}", target]
    if fs == "ext4":
        return ["mkfs.ext4", "-F", "-E", f"offset={start_sector * 512}", target]
    return None


def get_partition_path_for_device(target: str, index: int) -> str:
    base = os.path.basename(target)
    suffix = f"p{index}" if base[-1].isdigit() else str(index)
    return f"{target}{suffix}"


def get_partition_start_sector(target: str, index: int) -> Optional[int]:
    result = subprocess.run(
        ["parted", "-m", "-s", target, "unit", "s", "print"],
        capture_output=True,
        text=True,
        check=False,
    )
    if result.returncode != 0:
        return None

    for raw in result.stdout.splitlines():
        line = raw.strip()
        if not line or not line[0].isdigit():
            continue
        cols = line.split(":")
        if not cols or int(cols[0]) != index or len(cols) < 3:
            continue
        start = cols[1].strip()
        if start.endswith("s"):
            start = start[:-1]
        try:
            return int(start)
        except ValueError:
            return None
    return None


def get_partition_geometry_sectors(target: str, index: int) -> Optional[tuple[int, int, int]]:
    result = subprocess.run(
        ["parted", "-m", "-s", target, "unit", "s", "print"],
        capture_output=True,
        text=True,
        check=False,
    )
    if result.returncode != 0:
        return None

    for raw in result.stdout.splitlines():
        line = raw.strip()
        if not line or not line[0].isdigit():
            continue
        cols = line.split(":")
        if not cols or int(cols[0]) != index or len(cols) < 4:
            continue
        start_text = cols[1].strip().removesuffix("s")
        end_text = cols[2].strip().removesuffix("s")
        size_text = cols[3].strip().removesuffix("s")
        try:
            start = int(start_text)
            end = int(end_text)
            size = int(size_text)
        except ValueError:
            return None
        return (start, end, size)
    return None


def populate_numos_partition_image(target: str, start_sector: int, size_sectors: int, apply: bool) -> int:
    size_mb = (size_sectors * 512) // (1024 * 1024)
    if size_mb < 8:
        print(f"Partition is too small for NumOS payload: {size_mb} MB")
        return 1

    root_dir = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
    create_disk = os.path.join(root_dir, "tools", "create_disk.py")
    env = os.environ.copy()
    env["NUMOS_DISK_SIZE_MB"] = str(size_mb)
    env["NUMOS_DISK_OFFSET_BYTES"] = str(start_sector * 512)
    env["NUMOS_DISK_PRESERVE_PREFIX"] = "1"

    cmd = ["python3", create_disk, target]
    text = " ".join(shlex.quote(part) for part in cmd)
    print(f"$ NUMOS_DISK_SIZE_MB={size_mb} NUMOS_DISK_OFFSET_BYTES={start_sector * 512} NUMOS_DISK_PRESERVE_PREFIX=1 {text}")
    if not apply:
        return 0

    subprocess.run(cmd, check=True, env=env)
    return 0


def list_devices() -> int:
    cmd = [
        "lsblk",
        "-d",
        "-o",
        "NAME,PATH,SIZE,TYPE,TRAN,RM,MODEL",
    ]
    result = subprocess.run(cmd, capture_output=True, text=True, check=False)
    if result.returncode != 0:
        print("Failed to list devices with lsblk.")
        if result.stderr:
            print(result.stderr.strip())
        return result.returncode
    print(result.stdout.strip())
    return 0


def create_partition(args: argparse.Namespace) -> int:
    target = args.target
    target_is_file = os.path.isfile(target)
    target_is_block = os.path.exists(target) and not target_is_file

    if not os.path.exists(target):
        print(f"Target not found: {target}")
        return 1

    if not target_is_file and not target_is_block:
        print(f"Unsupported target type: {target}")
        return 1

    assert_tooling(args.fs, args.apply, target_is_file)

    print("Target info")
    print(f"  path: {target}")
    print(f"  type: {'image file' if target_is_file else 'block device'}")
    print(f"  table: {args.table}")
    print(f"  fs: {args.fs}")
    print(f"  range: {args.start} -> {args.end}")
    print(f"  apply: {'yes' if args.apply else 'no (dry run)'}")

    if not args.apply:
        print("Dry run only, no data changed.")

    parted_cmds = build_parted_commands(
        target=target,
        table=args.table,
        fs=args.fs,
        start=args.start,
        end=args.end,
        name=args.name,
    )
    for cmd in parted_cmds:
        run_command(cmd, args.apply)

    if not args.format:
        return 0

    mkfs_cmd: Optional[List[str]]
    if target_is_file:
        geometry = get_partition_geometry_sectors(target, 1)
        if geometry is None:
            print("Failed to read partition geometry from image.")
            return 1
        start_sector, _end_sector, size_sectors = geometry

        if args.populate_numos and args.fs == "fat32":
            return populate_numos_partition_image(
                target=target,
                start_sector=start_sector,
                size_sectors=size_sectors,
                apply=args.apply,
            )

        if args.apply:
            start_sector = get_partition_start_sector(target, 1)
            if start_sector is None:
                print("Failed to read partition start sector from image.")
                return 1
            mkfs_cmd = build_mkfs_file_command(target, args.fs, start_sector)
            if mkfs_cmd:
                run_command(mkfs_cmd, True)
        else:
            print("$ mkfs command runs on the image file using partition start offset")
    else:
        partition_path = get_partition_path_for_device(target, 1)
        if args.apply:
            subprocess.run(["partprobe", target], check=False)
        mkfs_cmd = build_mkfs_command(partition_path, args.fs)
        if mkfs_cmd:
            run_command(mkfs_cmd, args.apply)

    return 0


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description="Partition a block device or .img file for NumOS."
    )
    sub = parser.add_subparsers(dest="command", required=True)

    list_parser = sub.add_parser("list", help="List available block devices")
    list_parser.set_defaults(handler=lambda _args: list_devices())

    create = sub.add_parser("create", help="Create partition table and partition 1")
    create.add_argument("target", help="Target device path or image path")
    create.add_argument(
        "--table",
        choices=SUPPORTED_TABLES,
        default="gpt",
        help="Partition table type",
    )
    create.add_argument(
        "--fs",
        choices=SUPPORTED_FILESYSTEMS,
        default="fat32",
        help="Filesystem type for partition 1",
    )
    create.add_argument("--start", default="1MiB", help="Partition start offset")
    create.add_argument("--end", default="100%", help="Partition end offset")
    create.add_argument(
        "--name",
        default="NUMOS",
        help="GPT partition name for partition 1",
    )
    create.add_argument(
        "--format",
        action="store_true",
        help="Format partition 1 after creating it",
    )
    create.add_argument(
        "--populate-numos",
        action="store_true",
        help="Populate FAT32 partition 1 with NumOS files",
    )
    create.add_argument(
        "--no-populate-numos",
        action="store_true",
        help="Do not populate NumOS files even on FAT32 image targets",
    )
    create.add_argument(
        "--apply",
        action="store_true",
        help="Execute commands, default mode only prints commands",
    )
    create.set_defaults(handler=create_partition)

    return parser


def main() -> int:
    parser = build_parser()
    args = parser.parse_args()
    if args.command == "create":
        if args.no_populate_numos:
            args.populate_numos = False
        elif not args.populate_numos:
            args.populate_numos = True
    return args.handler(args)


if __name__ == "__main__":
    raise SystemExit(main())
