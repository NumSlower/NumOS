#!/bin/bash
# Complete build and deployment script for NumOS with userspace shell

set -e  # Exit on error

echo "==================================="
echo "  NumOS Build & Deploy System"
echo "==================================="

# Colors
GREEN='\033[0;32m'
BLUE='\033[0;34m'
RED='\033[0;31m'
NC='\033[0m' # No Color

# Configuration
DISK_IMAGE="NumOS.img"
MOUNT_POINT="/tmp/numos_mount"
LOOP_DEVICE=""

# Cleanup function
cleanup() {
    if [ -n "$LOOP_DEVICE" ]; then
        echo -e "${BLUE}Cleaning up loop device...${NC}"
        sudo umount "$MOUNT_POINT" 2>/dev/null || true
        sudo losetup -d "$LOOP_DEVICE" 2>/dev/null || true
    fi
}

trap cleanup EXIT

# Step 1: Build kernel
echo -e "\n${BLUE}[1/6] Building kernel...${NC}"
make clean
make kernel

if [ ! -f "build/kernel.bin" ]; then
    echo -e "${RED}ERROR: Kernel build failed!${NC}"
    exit 1
fi
echo -e "${GREEN}✓ Kernel built successfully${NC}"

# Step 2: Build userspace shell
echo -e "\n${BLUE}[2/6] Building userspace shell...${NC}"
make userspace

if [ ! -f "build/shell.bin" ]; then
    echo -e "${RED}ERROR: Shell build failed!${NC}"
    exit 1
fi

# Verify it's a valid ELF
if ! file build/shell.bin | grep -q "ELF 64-bit"; then
    echo -e "${RED}ERROR: shell.bin is not a valid 64-bit ELF!${NC}"
    exit 1
fi
echo -e "${GREEN}✓ Shell built successfully ($(stat -c%s build/shell.bin) bytes)${NC}"

# Step 3: Create disk image
echo -e "\n${BLUE}[3/6] Creating disk image...${NC}"
if [ -f "$DISK_IMAGE" ]; then
    rm "$DISK_IMAGE"
fi

dd if=/dev/zero of="$DISK_IMAGE" bs=1M count=64 status=none
parted -s "$DISK_IMAGE" mklabel msdos
parted -s "$DISK_IMAGE" mkpart primary fat32 1MiB 63MiB
parted -s "$DISK_IMAGE" set 1 boot on
echo -e "${GREEN}✓ Disk image created${NC}"

# Step 4: Setup loop device and format
echo -e "\n${BLUE}[4/6] Setting up filesystem...${NC}"
LOOP_DEVICE=$(sudo losetup -fP --show "$DISK_IMAGE")
echo "Loop device: $LOOP_DEVICE"

# Wait for partition device
sleep 1
if [ ! -e "${LOOP_DEVICE}p1" ]; then
    echo -e "${RED}ERROR: Partition device not found!${NC}"
    exit 1
fi

# Format partition
sudo mkfs.fat -F 32 "${LOOP_DEVICE}p1" > /dev/null
echo -e "${GREEN}✓ FAT32 filesystem created${NC}"

# Step 5: Install GRUB and kernel
echo -e "\n${BLUE}[5/6] Installing GRUB and kernel...${NC}"
sudo mkdir -p "$MOUNT_POINT"
sudo mount "${LOOP_DEVICE}p1" "$MOUNT_POINT"

sudo mkdir -p "$MOUNT_POINT/boot/grub"
sudo cp build/kernel.bin "$MOUNT_POINT/boot/kernel.bin"
sudo cp preboot/grub/grub.cfg "$MOUNT_POINT/boot/grub/"

sudo grub-install --target=i386-pc --boot-directory="$MOUNT_POINT/boot" "$LOOP_DEVICE" 2>&1 | grep -v "warning: "
echo -e "${GREEN}✓ GRUB and kernel installed${NC}"

# Step 6: Deploy userspace shell
echo -e "\n${BLUE}[6/6] Deploying userspace shell...${NC}"

# Copy shell binary with correct FAT32 name
sudo cp build/shell.bin "$MOUNT_POINT/SHELL"
sudo sync

# Verify file was written
if [ ! -f "$MOUNT_POINT/SHELL" ]; then
    echo -e "${RED}ERROR: Shell file not found on disk!${NC}"
    exit 1
fi

SHELL_SIZE=$(stat -c%s "$MOUNT_POINT/SHELL")
echo "Shell file: $SHELL_SIZE bytes"

# List files
echo -e "\n${BLUE}Files on disk:${NC}"
ls -lh "$MOUNT_POINT/"
ls -lh "$MOUNT_POINT/boot/"

# Unmount and cleanup
sudo umount "$MOUNT_POINT"
sudo losetup -d "$LOOP_DEVICE"
LOOP_DEVICE=""

echo -e "\n${GREEN}==================================="
echo "  Build Complete!"
echo "===================================${NC}"
echo ""
echo "To run NumOS:"
echo "  qemu-system-x86_64 -m 128M -drive file=$DISK_IMAGE,format=raw -serial stdio"
echo ""