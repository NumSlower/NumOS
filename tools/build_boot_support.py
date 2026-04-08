#!/usr/bin/env python3
"""
Generate BIOS boot support files for NumOS ATA installs.

Outputs under the stage root:
- run/GRUBBOOT.BIN: GRUB stage 1 boot sector template
- run/GRUBCORE.BIN: embedded GRUB core image with the NumOS boot script
- boot/kern0001.bin: initial immutable kernel artifact
- boot/boot.cfg: default and fallback kernel selection
- boot/status.cfg: pending or success boot state
- boot/grub/grub.cfg: the normal-mode GRUB boot script
- boot/grub/grubenv: GRUB environment block for one-shot pending boots
"""

from __future__ import annotations

import argparse
import math
import os
import shutil
import subprocess
import sys
import tempfile

TOOLS_DIR = os.path.dirname(__file__)
if TOOLS_DIR not in sys.path:
    sys.path.insert(0, TOOLS_DIR)

from numos_version import read_numos_version


DEFAULT_BOOT_IMG = "/usr/lib/grub/i386-pc/boot.img"
DEFAULT_PARTITION_START_LBA = 2048
DEFAULT_KERNEL_BASENAME = "kern0001.bin"
DEFAULT_BOOT_CFG_NAME = "boot.cfg"
DEFAULT_STATUS_CFG_NAME = "status.cfg"
DEFAULT_GRUB_CFG_NAME = "grub.cfg"
DEFAULT_GRUBENV_NAME = "grubenv"
DEFAULT_INIT_PATH = "/bin/shell.elf"
DEFAULT_GFX_MODE = "vesa"

GRUB_MODULES = [
    "biosdisk",
    "part_msdos",
    "fat",
    # NumOS keeps the real boot logic in /boot/grub/grub.cfg, so every
    # command used there must live in the core image.
    "normal",
    "configfile",
    "test",
    "echo",
    "loadenv",
    "multiboot2",
    "all_video",
    "vbe",
    "vga",
    "gfxterm",
    "font",
]


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "-v",
        "--version",
        action="version",
        version=f"{os.path.basename(__file__)} {read_numos_version(__file__)}",
    )
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


def write_text_file(path: str, text: str) -> None:
    with open(path, "w", encoding="ascii", newline="\n") as handle:
        handle.write(text)


def write_embedded_config(path: str) -> None:
    script = """set timeout=0
set default=0
set root=hd0,msdos1
set prefix=($root)/boot/grub
terminal_output console
configfile $prefix/grub.cfg
"""
    write_text_file(path, script)


def write_runtime_grub_cfg(path: str) -> None:
    script = """set timeout=0
set default=0
terminal_output console

set numos_default_kernel="/boot/kern0001.bin"
set numos_fallback_kernel="/boot/kern0001.bin"
set numos_default_gfx="vesa"
set numos_fallback_gfx="vesa"
set numos_init_path="/bin/shell.elf"
set numos_boot_status="success"
set numos_last_pending_kernel=""

if [ -f /boot/boot.cfg ]; then
    source /boot/boot.cfg
fi

if [ -f /boot/status.cfg ]; then
    source /boot/status.cfg
fi

if [ -f /boot/grub/grubenv ]; then
    load_env -f /boot/grub/grubenv numos_last_pending_kernel
fi

set numos_kernel_path="$numos_default_kernel"
set numos_kernel_gfx="$numos_default_gfx"

if [ "$numos_boot_status" = "failed" ]; then
    set numos_kernel_path="$numos_fallback_kernel"
    set numos_kernel_gfx="$numos_fallback_gfx"
    set numos_last_pending_kernel=""
    if [ -f /boot/grub/grubenv ]; then
        save_env -f /boot/grub/grubenv numos_last_pending_kernel
    fi
elif [ "$numos_boot_status" = "pending" ]; then
    if [ "$numos_last_pending_kernel" = "$numos_default_kernel" ]; then
        set numos_kernel_path="$numos_fallback_kernel"
        set numos_kernel_gfx="$numos_fallback_gfx"
    else
        set numos_last_pending_kernel="$numos_default_kernel"
        if [ -f /boot/grub/grubenv ]; then
            save_env -f /boot/grub/grubenv numos_last_pending_kernel
        fi
    fi
else
    set numos_last_pending_kernel=""
    if [ -f /boot/grub/grubenv ]; then
        save_env -f /boot/grub/grubenv numos_last_pending_kernel
    fi
fi

if [ ! -f "$numos_kernel_path" -a -f "$numos_fallback_kernel" ]; then
    set numos_kernel_path="$numos_fallback_kernel"
    set numos_kernel_gfx="$numos_fallback_gfx"
fi

if [ "$numos_kernel_gfx" = "vesa" ]; then
    insmod all_video
    insmod vbe
    insmod vga
    insmod gfxterm
    insmod font
    set gfxpayload=keep
fi

if multiboot2 $numos_kernel_path init=$numos_init_path gfx=$numos_kernel_gfx; then
    boot
fi

if [ "$numos_kernel_path" != "$numos_fallback_kernel" -a -f "$numos_fallback_kernel" ]; then
    echo "NumOS: primary kernel failed, trying fallback"
    if [ "$numos_fallback_gfx" = "vesa" ]; then
        insmod all_video
        insmod vbe
        insmod vga
        insmod gfxterm
        insmod font
        set gfxpayload=keep
    fi
    if multiboot2 $numos_fallback_kernel init=$numos_init_path gfx=$numos_fallback_gfx; then
        boot
    fi
fi

echo "NumOS: no bootable kernel found"
"""
    write_text_file(path, script)


def write_boot_cfg(path: str) -> None:
    script = f"""set numos_default_kernel="/boot/{DEFAULT_KERNEL_BASENAME}"
set numos_fallback_kernel="/boot/{DEFAULT_KERNEL_BASENAME}"
set numos_default_gfx="{DEFAULT_GFX_MODE}"
set numos_fallback_gfx="{DEFAULT_GFX_MODE}"
set numos_init_path="{DEFAULT_INIT_PATH}"
"""
    write_text_file(path, script)


def write_status_cfg(path: str) -> None:
    write_text_file(path, 'set numos_boot_status="success"\n')


def create_grubenv(path: str) -> None:
    run_command(["grub-editenv", path, "create"])


def main() -> int:
    args = parse_args()

    ensure_file(args.kernel, "kernel image")
    ensure_file(args.boot_img, "GRUB boot image")

    if shutil.which("grub-mkimage") is None:
        print("Missing required tool: grub-mkimage", file=sys.stderr)
        return 1
    if shutil.which("grub-editenv") is None:
        print("Missing required tool: grub-editenv", file=sys.stderr)
        return 1

    os.makedirs(args.output_dir, exist_ok=True)
    run_dir = os.path.join(args.output_dir, "run")
    boot_dir = os.path.join(args.output_dir, "boot")
    grub_dir = os.path.join(boot_dir, "grub")
    os.makedirs(run_dir, exist_ok=True)
    os.makedirs(grub_dir, exist_ok=True)

    grub_boot_out = os.path.join(run_dir, "GRUBBOOT.BIN")
    grub_core_out = os.path.join(run_dir, "GRUBCORE.BIN")
    kernel_out = os.path.join(boot_dir, DEFAULT_KERNEL_BASENAME)
    boot_cfg_out = os.path.join(boot_dir, DEFAULT_BOOT_CFG_NAME)
    status_cfg_out = os.path.join(boot_dir, DEFAULT_STATUS_CFG_NAME)
    grub_cfg_out = os.path.join(grub_dir, DEFAULT_GRUB_CFG_NAME)
    grubenv_out = os.path.join(grub_dir, DEFAULT_GRUBENV_NAME)

    shutil.copyfile(args.boot_img, grub_boot_out)
    shutil.copyfile(args.kernel, kernel_out)
    write_boot_cfg(boot_cfg_out)
    write_status_cfg(status_cfg_out)
    write_runtime_grub_cfg(grub_cfg_out)
    create_grubenv(grubenv_out)

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
    print(f"[BOOT] {DEFAULT_KERNEL_BASENAME}: {os.path.getsize(kernel_out)} bytes")
    print(f"[BOOT] {DEFAULT_BOOT_CFG_NAME}: {os.path.getsize(boot_cfg_out)} bytes")
    print(f"[BOOT] {DEFAULT_STATUS_CFG_NAME}: {os.path.getsize(status_cfg_out)} bytes")
    print(f"[BOOT] {DEFAULT_GRUB_CFG_NAME}: {os.path.getsize(grub_cfg_out)} bytes")
    print(f"[BOOT] {DEFAULT_GRUBENV_NAME}: {os.path.getsize(grubenv_out)} bytes")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
