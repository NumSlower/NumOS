#!/usr/bin/env python3
"""
Partition helper for NumOS images and block devices.

The script defaults to dry run mode. Add --apply when you want the printed
commands to execute.
"""

import argparse
import os
import shlex
import subprocess
import sys
from typing import List, Optional

TOOLS_DIR = os.path.dirname(__file__)
if TOOLS_DIR not in sys.path:
    sys.path.insert(0, TOOLS_DIR)

from numos_version import read_numos_version


SUPPORTED_TABLES = ("gpt", "msdos")
SUPPORTED_FILESYSTEMS = ("fat32", "ext4", "none")


def run_command(cmd: List[str], apply: bool) -> None:
    """Print a shell-safe command and execute it only in apply mode."""
    text = " ".join(shlex.quote(part) for part in cmd)
    print(f"$ {text}")
    if apply:
        subprocess.run(cmd, check=True)


def command_exists(name: str) -> bool:
    """Check whether a required host tool is available."""
    return subprocess.call(
        ["bash", "-lc", f"command -v {shlex.quote(name)} >/dev/null 2>&1"]
    ) == 0


def assert_tooling(fs: str, apply: bool, _target_is_file: bool) -> None:
    """Validate the host tools needed for the requested operation."""
    required_tools = ["parted"]
    if fs == "fat32":
        required_tools.append("mkfs.vfat")
    elif fs == "ext4":
        required_tools.append("mkfs.ext4")

    missing = [tool_name for tool_name in required_tools if not command_exists(tool_name)]
    if missing:
        print(f"Missing required tools: {', '.join(missing)}")
        if apply:
            sys.exit(1)


def build_parted_commands(
    target: str, table: str, fs: str, start: str, end: str, name: Optional[str]
) -> List[List[str]]:
    """Build the parted commands for a one-partition NumOS layout."""
    commands: List[List[str]] = [["parted", "-s", target, "mklabel", table]]

    create_partition_cmd = ["parted", "-s", target, "mkpart", "primary"]
    if fs != "none":
        create_partition_cmd.append(fs)
    create_partition_cmd.extend([start, end])
    commands.append(create_partition_cmd)

    if table == "gpt" and name:
        commands.append(["parted", "-s", target, "name", "1", name])

    return commands


def build_mkfs_command(partition_path: str, fs: str) -> Optional[List[str]]:
    """Build the mkfs command for a block-device partition."""
    if fs == "fat32":
        return ["mkfs.vfat", "-F", "32", partition_path]
    if fs == "ext4":
        return ["mkfs.ext4", "-F", partition_path]
    return None


def build_mkfs_file_command(target: str, fs: str, start_sector: int) -> Optional[List[str]]:
    """Build the mkfs command for an image file that contains a partition."""
    if fs == "fat32":
        return ["mkfs.vfat", "-F", "32", f"--offset={start_sector}", target]
    if fs == "ext4":
        return ["mkfs.ext4", "-F", "-E", f"offset={start_sector * 512}", target]
    return None


def build_create_disk_command(target: str) -> List[str]:
    """Build the command that populates a FAT32 target with NumOS files."""
    repo_root = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
    create_disk_script = os.path.join(repo_root, "tools", "create_disk.py")
    return ["python3", create_disk_script, target]


def get_partition_path_for_device(target: str, index: int) -> str:
    """Return /dev path syntax for partition N on common Linux devices."""
    base_name = os.path.basename(target)
    suffix = f"p{index}" if base_name[-1].isdigit() else str(index)
    return f"{target}{suffix}"


def get_partition_start_sector(target: str, index: int) -> Optional[int]:
    """Read the partition start sector from parted machine-readable output."""
    result = subprocess.run(
        ["parted", "-m", "-s", target, "unit", "s", "print"],
        capture_output=True,
        text=True,
        check=False,
    )
    if result.returncode != 0:
        return None

    for raw_line in result.stdout.splitlines():
        line = raw_line.strip()
        if not line or not line[0].isdigit():
            continue

        columns = line.split(":")
        if not columns or int(columns[0]) != index or len(columns) < 3:
            continue

        start_text = columns[1].strip().removesuffix("s")
        try:
            return int(start_text)
        except ValueError:
            return None

    return None


def get_partition_geometry_sectors(target: str, index: int) -> Optional[tuple[int, int, int]]:
    """Read start, end, and size in sectors for one partition."""
    result = subprocess.run(
        ["parted", "-m", "-s", target, "unit", "s", "print"],
        capture_output=True,
        text=True,
        check=False,
    )
    if result.returncode != 0:
        return None

    for raw_line in result.stdout.splitlines():
        line = raw_line.strip()
        if not line or not line[0].isdigit():
            continue

        columns = line.split(":")
        if not columns or int(columns[0]) != index or len(columns) < 4:
            continue

        start_text = columns[1].strip().removesuffix("s")
        end_text = columns[2].strip().removesuffix("s")
        size_text = columns[3].strip().removesuffix("s")
        try:
            return int(start_text), int(end_text), int(size_text)
        except ValueError:
            return None

    return None


def populate_numos_partition_target(
    target: str,
    size_sectors: int,
    apply: bool,
    offset_bytes: int = 0,
    preserve_prefix: bool = False,
) -> int:
    """
    Populate a FAT32 target with NumOS files.

    The create_disk.py script expects a size in MiB. This helper converts the
    final partition geometry into the environment variables that script uses.
    """
    size_mb = (size_sectors * 512) // (1024 * 1024)
    if size_mb < 8:
        print(f"Partition is too small for NumOS payload: {size_mb} MB")
        return 1

    env = os.environ.copy()
    env["NUMOS_DISK_SIZE_MB"] = str(size_mb)
    env_parts = [f"NUMOS_DISK_SIZE_MB={size_mb}"]

    if offset_bytes > 0:
        env["NUMOS_DISK_OFFSET_BYTES"] = str(offset_bytes)
        env_parts.append(f"NUMOS_DISK_OFFSET_BYTES={offset_bytes}")
    if preserve_prefix:
        env["NUMOS_DISK_PRESERVE_PREFIX"] = "1"
        env_parts.append("NUMOS_DISK_PRESERVE_PREFIX=1")

    command = build_create_disk_command(target)
    text = " ".join(shlex.quote(part) for part in env_parts + command)
    print(f"$ {text}")

    if apply:
        subprocess.run(command, check=True, env=env)
    return 0


def populate_numos_partition_image(target: str, start_sector: int, size_sectors: int, apply: bool) -> int:
    """Populate partition 1 inside an image file at a byte offset."""
    return populate_numos_partition_target(
        target=target,
        size_sectors=size_sectors,
        apply=apply,
        offset_bytes=start_sector * 512,
        preserve_prefix=True,
    )


def list_devices() -> int:
    """List host block devices in a compact format."""
    result = subprocess.run(
        ["lsblk", "-d", "-o", "NAME,PATH,SIZE,TYPE,TRAN,RM,MODEL"],
        capture_output=True,
        text=True,
        check=False,
    )
    if result.returncode != 0:
        print("Failed to list devices with lsblk.")
        if result.stderr:
            print(result.stderr.strip())
        return result.returncode

    print(result.stdout.strip())
    return 0


def create_partition(args: argparse.Namespace) -> int:
    """Create, format, and optionally populate partition 1."""
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

    for command in build_parted_commands(
        target=target,
        table=args.table,
        fs=args.fs,
        start=args.start,
        end=args.end,
        name=args.name,
    ):
        run_command(command, args.apply)

    if not args.format:
        return 0

    if target_is_file:
        if not args.apply:
            if args.populate_numos and args.fs == "fat32":
                command = build_create_disk_command(target)
                print(f"$ {' '.join(shlex.quote(part) for part in command)}")
                print("Populate uses partition 1 after apply and sizes the payload from the final partition geometry.")
            else:
                print("$ mkfs command runs on the image file using partition start offset")
            return 0

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

        mkfs_command = build_mkfs_file_command(target, args.fs, start_sector)
        if mkfs_command:
            run_command(mkfs_command, True)
        return 0

    partition_path = get_partition_path_for_device(target, 1)
    run_command(["partprobe", target], args.apply)

    if args.populate_numos and args.fs == "fat32":
        if not args.apply:
            command = build_create_disk_command(partition_path)
            print(f"$ {' '.join(shlex.quote(part) for part in command)}")
            print("Populate uses partition 1 after apply and sizes the payload from the final partition geometry.")
            return 0

        geometry = get_partition_geometry_sectors(target, 1)
        if geometry is None:
            print("Failed to read partition geometry from device.")
            return 1

        _start_sector, _end_sector, size_sectors = geometry
        return populate_numos_partition_target(
            target=partition_path,
            size_sectors=size_sectors,
            apply=True,
        )

    mkfs_command = build_mkfs_command(partition_path, args.fs)
    if mkfs_command:
        run_command(mkfs_command, args.apply)
    return 0


def build_parser() -> argparse.ArgumentParser:
    """Build the command-line interface."""
    parser = argparse.ArgumentParser(
        description="Partition a block device or .img file for NumOS."
    )
    parser.add_argument(
        "-v",
        "--version",
        action="version",
        version=f"{os.path.basename(__file__)} {read_numos_version(__file__)}",
    )
    subcommands = parser.add_subparsers(dest="command", required=True)

    list_parser = subcommands.add_parser("list", help="List available block devices")
    list_parser.set_defaults(handler=lambda _args: list_devices())

    create_parser = subcommands.add_parser(
        "create", help="Create partition table and partition 1"
    )
    create_parser.add_argument("target", help="Target device path or image path")
    create_parser.add_argument(
        "--table",
        choices=SUPPORTED_TABLES,
        default="gpt",
        help="Partition table type",
    )
    create_parser.add_argument(
        "--fs",
        choices=SUPPORTED_FILESYSTEMS,
        default="fat32",
        help="Filesystem type for partition 1",
    )
    create_parser.add_argument("--start", default="1MiB", help="Partition start offset")
    create_parser.add_argument("--end", default="100%", help="Partition end offset")
    create_parser.add_argument(
        "--name",
        default="NUMOS",
        help="GPT partition name for partition 1",
    )
    create_parser.add_argument(
        "--format",
        action="store_true",
        help="Format partition 1 after creating it",
    )
    create_parser.add_argument(
        "--populate-numos",
        action="store_true",
        help="Populate FAT32 partition 1 with NumOS files",
    )
    create_parser.add_argument(
        "--no-populate-numos",
        action="store_true",
        help="Do not populate NumOS files even on FAT32 image targets",
    )
    create_parser.add_argument(
        "--apply",
        action="store_true",
        help="Execute commands, default mode only prints commands",
    )
    create_parser.set_defaults(handler=create_partition)

    return parser


def main() -> int:
    """Parse CLI arguments and dispatch to the selected subcommand."""
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
