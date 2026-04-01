#!/usr/bin/env python3
"""
Generate BIOS boot support files for NumOS ATA installs.

Outputs:
- GRUBBOOT.BIN: GRUB stage 1 boot sector template
- GRUBCORE.BIN: embedded GRUB core image with a fixed NumOS boot script
- KERN.BIN: kernel image loaded by the embedded GRUB script
"""

from __future__ import annotations

import argparse
import math
import os
import shutil
import subprocess
import sys
import tempfile


DEFAULT_BOOT_IMG = "/usr/lib/grub/i386-pc/boot.img"
DEFAULT_PARTITION_START_LBA = 2048
GRUB_MODULES = [
    "biosdisk",
    "part_msdos",
    "fat",
    "multiboot2",
]


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("--output-dir", required=True)
    parser.add_argument("--kernel", required=True)
    parser.add_argument("--boot-img", default=DEFAULT_BOOT_IMG)
    parser.add_argument("--partition-start-lba", type=int, default=DEFAULT_PARTITION_START_LBA)
    return parser.parse_args()


def ensure_file(path: str, label: str) -> None:
    if os.path.isfile(path):
        return
    print(f"Missing {label}: {path}", file=sys.stderr)
    sys.exit(1)


def run_command(cmd: list[str]) -> None:
    subprocess.run(cmd, check=True)


def write_embedded_config(path: str) -> None:
    script = """set timeout=0
set default=0
set root=(hd0,msdos1)
terminal_output console
multiboot2 /run/KERN.BIN init=/bin/shell.elf gfx=vesa
boot
"""
    with open(path, "w", encoding="ascii", newline="\n") as handle:
        handle.write(script)


def main() -> int:
    args = parse_args()

    ensure_file(args.kernel, "kernel image")
    ensure_file(args.boot_img, "GRUB boot image")

    if shutil.which("grub-mkimage") is None:
        print("Missing required tool: grub-mkimage", file=sys.stderr)
        return 1

    os.makedirs(args.output_dir, exist_ok=True)

    grub_boot_out = os.path.join(args.output_dir, "GRUBBOOT.BIN")
    grub_core_out = os.path.join(args.output_dir, "GRUBCORE.BIN")
    kernel_out = os.path.join(args.output_dir, "KERN.BIN")

    shutil.copyfile(args.boot_img, grub_boot_out)
    shutil.copyfile(args.kernel, kernel_out)

    with tempfile.TemporaryDirectory(prefix="numos-grub-") as tmpdir:
        cfg_path = os.path.join(tmpdir, "embedded.cfg")
        write_embedded_config(cfg_path)
        run_command(
            [
                "grub-mkimage",
                "-O",
                "i386-pc",
                "-p",
                "/boot/grub",
                "-c",
                cfg_path,
                "-o",
                grub_core_out,
                *GRUB_MODULES,
            ]
        )

    core_size = os.path.getsize(grub_core_out)
    core_sectors = math.ceil(core_size / 512)
    embed_limit = args.partition_start_lba - 1
    if core_sectors > embed_limit:
        print(
            f"GRUB core image is too large for the embedding area: "
            f"{core_sectors} sectors, limit {embed_limit}",
            file=sys.stderr,
        )
        return 1

    print(f"[BOOT] GRUBBOOT.BIN: {os.path.getsize(grub_boot_out)} bytes")
    print(f"[BOOT] GRUBCORE.BIN: {core_size} bytes, {core_sectors} sectors")
    print(f"[BOOT] KERN.BIN: {os.path.getsize(kernel_out)} bytes")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
