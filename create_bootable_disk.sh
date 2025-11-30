#!/bin/bash
# create_bootable_disk.sh - Creates a bootable disk image with shell binary

set -e  # Exit on error

echo "========================================"
echo "  NumOS Bootable Disk Image Creator"
echo "========================================"
echo ""

# Configuration
DISK_IMAGE="NumOS.img"
DISK_SIZE_MB=64
MOUNT_DIR="/tmp/numos_mount"
KERNEL_BIN="build/kernel.bin"
SHELL_BIN="build/shell.bin"

# Check if running as root
if [ "$EUID" -eq 0 ]; then 
    SUDO=""
else
    SUDO="sudo"
    echo "Note: This script requires sudo for mounting operations"
    echo ""
fi

# Step 1: Check if kernel and shell exist
echo "[1/8] Checking build artifacts..."
if [ ! -f "$KERNEL_BIN" ]; then
    echo "ERROR: Kernel binary not found: $KERNEL_BIN"
    echo "Please run 'make' first to build the kernel"
    exit 1
fi

if [ ! -f "$SHELL_BIN" ]; then
    echo "ERROR: Shell binary not found: $SHELL_BIN"
    echo "Please run 'make userspace' first to build the shell"
    exit 1
fi

KERNEL_SIZE=$(stat -f%z "$KERNEL_BIN" 2>/dev/null || stat -c%s "$KERNEL_BIN")
SHELL_SIZE=$(stat -f%z "$SHELL_BIN" 2>/dev/null || stat -c%s "$SHELL_BIN")

echo "  ✓ Kernel: $KERNEL_BIN ($KERNEL_SIZE bytes)"
echo "  ✓ Shell:  $SHELL_BIN ($SHELL_SIZE bytes)"
echo ""

# Step 2: Create disk image
echo "[2/8] Creating disk image ($DISK_SIZE_MB MB)..."
dd if=/dev/zero of="$DISK_IMAGE" bs=1M count=$DISK_SIZE_MB status=none
echo "  ✓ Created $DISK_IMAGE"
echo ""

# Step 3: Create partition table
echo "[3/8] Creating partition table..."
parted -s "$DISK_IMAGE" mklabel msdos
parted -s "$DISK_IMAGE" mkpart primary fat32 1MiB 63MiB
parted -s "$DISK_IMAGE" set 1 boot on
echo "  ✓ Partition table created"
echo ""

# Step 4: Setup loop device
echo "[4/8] Setting up loop device..."
LOOP_DEVICE=$($SUDO losetup -fP --show "$DISK_IMAGE")
echo "  ✓ Loop device: $LOOP_DEVICE"

# Wait for partition to appear
sleep 1

# Verify partition exists
if [ ! -b "${LOOP_DEVICE}p1" ]; then
    echo "ERROR: Partition ${LOOP_DEVICE}p1 not found"
    $SUDO losetup -d "$LOOP_DEVICE"
    exit 1
fi
echo ""

# Step 5: Format partition
echo "[5/8] Formatting partition as FAT32..."
$SUDO mkfs.fat -F 32 -n "NUMOS" "${LOOP_DEVICE}p1" > /dev/null
echo "  ✓ FAT32 filesystem created"
echo ""

# Step 6: Mount partition
echo "[6/8] Mounting partition..."
$SUDO mkdir -p "$MOUNT_DIR"
$SUDO mount "${LOOP_DEVICE}p1" "$MOUNT_DIR"
echo "  ✓ Mounted at $MOUNT_DIR"
echo ""

# Step 7: Install GRUB and files
echo "[7/8] Installing GRUB bootloader..."
$SUDO mkdir -p "$MOUNT_DIR/boot/grub"

# Copy kernel
echo "  - Copying kernel..."
$SUDO cp "$KERNEL_BIN" "$MOUNT_DIR/boot/kernel.bin"

# Copy GRUB config
echo "  - Copying GRUB config..."
$SUDO cp preboot/grub/grub.cfg "$MOUNT_DIR/boot/grub/"

# Install GRUB
echo "  - Installing GRUB to MBR..."
$SUDO grub-install --target=i386-pc --boot-directory="$MOUNT_DIR/boot" "$LOOP_DEVICE" 2>&1 | grep -v "WARNING" || true

echo "  ✓ GRUB installed"
echo ""

# Step 8: Copy shell to root directory
echo "[8/8] Copying shell binary to disk..."
echo "  - Source: $SHELL_BIN"
echo "  - Destination: $MOUNT_DIR/SHELL (FAT32 root)"

# Copy with the exact name the kernel looks for
$SUDO cp "$SHELL_BIN" "$MOUNT_DIR/SHELL"

# Verify the copy
if [ -f "$MOUNT_DIR/SHELL" ]; then
    COPIED_SIZE=$($SUDO stat -f%z "$MOUNT_DIR/SHELL" 2>/dev/null || $SUDO stat -c%s "$MOUNT_DIR/SHELL")
    echo "  ✓ Shell copied successfully ($COPIED_SIZE bytes)"
    
    # List files to verify
    echo ""
    echo "Files on disk:"
    $SUDO ls -lh "$MOUNT_DIR/" | tail -n +2
else
    echo "  ✗ ERROR: Shell file not found after copy!"
    $SUDO umount "$MOUNT_DIR"
    $SUDO losetup -d "$LOOP_DEVICE"
    exit 1
fi
echo ""

# Step 9: Sync and unmount
echo "[9/9] Finalizing disk image..."
$SUDO sync
$SUDO umount "$MOUNT_DIR"
$SUDO losetup -d "$LOOP_DEVICE"
$SUDO rmdir "$MOUNT_DIR" 2>/dev/null || true
echo "  ✓ Disk image finalized"
echo ""

echo "========================================"
echo "  ✓ Bootable disk created successfully!"
echo "========================================"
echo ""
echo "Disk image: $DISK_IMAGE"
echo "Size: $DISK_SIZE_MB MB"
echo ""
echo "Contents:"
echo "  - GRUB bootloader in MBR"
echo "  - Kernel at /boot/kernel.bin"
echo "  - Shell at /SHELL"
echo ""
echo "To test with QEMU:"
echo "  qemu-system-x86_64 -drive file=$DISK_IMAGE,format=raw -m 128M"
echo ""
echo "To run:"
echo "  1. Make script executable: chmod +x create_bootable_disk.sh"
echo "  2. Run: ./create_bootable_disk.sh"
echo ""