#!/usr/bin/env python3
"""
Create a FAT32 disk image for NumOS - FIXED VERSION
Properly handles cluster-to-file-position mapping
"""

import struct
import sys
import os

# Disk parameters
DISK_SIZE_MB = 32
BYTES_PER_SECTOR = 512
SECTORS_PER_CLUSTER = 8
RESERVED_SECTORS = 32
NUM_FATS = 2

# Bytes per cluster
BYTES_PER_CLUSTER = SECTORS_PER_CLUSTER * BYTES_PER_SECTOR

def create_boot_sector():
    """Create FAT32 boot sector"""
    boot = bytearray(512)
    
    # Jump instruction
    boot[0:3] = b'\xEB\x58\x90'
    
    # OEM name
    boot[3:11] = b'NUMOS1.0'
    
    # BPB (BIOS Parameter Block)
    struct.pack_into('<H', boot, 11, BYTES_PER_SECTOR)  # Bytes per sector
    struct.pack_into('<B', boot, 13, SECTORS_PER_CLUSTER)  # Sectors per cluster
    struct.pack_into('<H', boot, 14, RESERVED_SECTORS)  # Reserved sectors
    struct.pack_into('<B', boot, 16, NUM_FATS)  # Number of FATs
    struct.pack_into('<H', boot, 17, 0)  # Root entries (0 for FAT32)
    struct.pack_into('<H', boot, 19, 0)  # Total sectors 16-bit (0 for FAT32)
    struct.pack_into('<B', boot, 21, 0xF8)  # Media descriptor
    struct.pack_into('<H', boot, 22, 0)  # FAT size 16-bit (0 for FAT32)
    struct.pack_into('<H', boot, 24, 63)  # Sectors per track
    struct.pack_into('<H', boot, 26, 16)  # Number of heads
    struct.pack_into('<I', boot, 28, 0)  # Hidden sectors
    
    total_sectors = (DISK_SIZE_MB * 1024 * 1024) // BYTES_PER_SECTOR
    struct.pack_into('<I', boot, 32, total_sectors)  # Total sectors 32-bit
    
    # Calculate FAT size using simpler formula
    # For a 32MB disk: 65536 total sectors
    # Data sectors = 65536 - 32 reserved = 65504
    # With 128 clusters per FAT sector and 2 FATs:
    # FAT size needed = CEIL(total_clusters / 128)
    # We need to know total_clusters = (65504 / 8) = 8188 clusters
    # FAT size = CEIL(8188 / 128) = 64 sectors per FAT
    # But let's use a simpler approach - just reserve enough
    fat_size = 160  # Conservative: enough for 20480 clusters
    
    # FAT32 Extended BPB
    struct.pack_into('<I', boot, 36, fat_size)  # FAT size 32-bit
    struct.pack_into('<H', boot, 40, 0)  # Ext flags
    struct.pack_into('<H', boot, 42, 0)  # FS version
    struct.pack_into('<I', boot, 44, 2)  # Root cluster (usually 2)
    struct.pack_into('<H', boot, 48, 1)  # FSInfo sector
    struct.pack_into('<H', boot, 50, 6)  # Backup boot sector
    
    # Reserved bytes
    boot[52:64] = b'\x00' * 12
    
    # Extended boot record
    struct.pack_into('<B', boot, 64, 0x80)  # Drive number
    struct.pack_into('<B', boot, 65, 0)  # Reserved
    struct.pack_into('<B', boot, 66, 0x29)  # Boot signature
    struct.pack_into('<I', boot, 67, 0x12345678)  # Volume ID
    boot[71:82] = b'NUMOS DISK '  # Volume label (11 bytes)
    boot[82:90] = b'FAT32   '  # FS type (8 bytes)
    
    # Boot signature
    boot[510:512] = b'\x55\xAA'
    
    return bytes(boot)

def create_fsinfo():
    """Create FSInfo sector"""
    fsinfo = bytearray(512)
    
    struct.pack_into('<I', fsinfo, 0, 0x41615252)  # Lead signature
    # Reserved 480 bytes
    struct.pack_into('<I', fsinfo, 484, 0x61417272)  # Struct signature
    struct.pack_into('<I', fsinfo, 488, 0xFFFFFFFF)  # Free clusters (unknown)
    struct.pack_into('<I', fsinfo, 492, 2)  # Next free cluster
    # Reserved 12 bytes
    struct.pack_into('<I', fsinfo, 508, 0xAA550000)  # Trail signature
    
    return bytes(fsinfo)

def create_fat(total_sectors, fat_size, file_size=0):
    """Create FAT table with proper cluster chains"""
    fat = bytearray(fat_size * BYTES_PER_SECTOR)
    
    # First two entries are reserved
    struct.pack_into('<I', fat, 0, 0x0FFFFFF8)  # Media descriptor
    struct.pack_into('<I', fat, 4, 0x0FFFFFFF)  # End of chain
    
    # Cluster 2: Root directory
    struct.pack_into('<I', fat, 8, 0x0FFFFFFF)  # EOC
    
    # Cluster 3: /init directory
    struct.pack_into('<I', fat, 12, 0x0FFFFFFF)  # EOC
    
    # Cluster 4: /bin directory
    struct.pack_into('<I', fat, 16, 0x0FFFFFFF)  # EOC
    
    # Cluster 5: /run directory
    struct.pack_into('<I', fat, 20, 0x0FFFFFFF)  # EOC
    
    # Clusters 6+: ELF file chain
    if file_size > 0:
        clusters_needed = (file_size + BYTES_PER_CLUSTER - 1) // BYTES_PER_CLUSTER
        print(f"[FAT] ELF file: {file_size} bytes = {clusters_needed} clusters")
        for i in range(clusters_needed):
            cluster_idx = 6 + i
            if i == clusters_needed - 1:
                # Last cluster
                struct.pack_into('<I', fat, cluster_idx * 4, 0x0FFFFFFF)
                print(f"[FAT] Cluster {cluster_idx}: EOC (last)")
            else:
                # Point to next cluster
                struct.pack_into('<I', fat, cluster_idx * 4, cluster_idx + 1)
                print(f"[FAT] Cluster {cluster_idx}: -> {cluster_idx + 1}")
    
    return bytes(fat)

def create_directory_entry(name, attr, cluster, size=0):
    """Create a FAT32 directory entry (32 bytes)"""
    entry = bytearray(32)
    
    # Name (8.3 format, padded with spaces)
    entry[0:11] = name.ljust(11).encode('ascii')[:11]
    
    # Attributes
    entry[11] = attr
    
    # Reserved
    entry[12] = 0
    
    # Creation time centiseconds
    entry[13] = 0
    # Creation time
    entry[14:16] = b'\x00\x00'
    # Creation date
    entry[16:18] = b'\x21\x00'
    
    # Last access date
    entry[18:20] = b'\x21\x00'
    
    # High word of cluster
    struct.pack_into('<H', entry, 20, (cluster >> 16) & 0xFFFF)
    
    # Last write time
    entry[22:24] = b'\x00\x00'
    # Last write date
    entry[24:26] = b'\x21\x00'
    
    # Low word of cluster
    struct.pack_into('<H', entry, 26, cluster & 0xFFFF)
    
    # File size
    struct.pack_into('<I', entry, 28, size)
    
    return bytes(entry)

def create_root_directory():
    """Create root directory cluster with entries for /init, /bin, /run"""
    cluster = bytearray(BYTES_PER_CLUSTER)
    
    DIR_ATTR = 0x10  # Directory attribute
    
    # . entry (current directory - cluster 2)
    cluster[0:32] = create_directory_entry('.          ', DIR_ATTR, 2)
    
    # .. entry (parent directory - cluster 0 for root)
    cluster[32:64] = create_directory_entry('..         ', DIR_ATTR, 0)
    
    # /init directory (cluster 3)
    cluster[64:96] = create_directory_entry('INIT       ', DIR_ATTR, 3)
    
    # /bin directory (cluster 4)
    cluster[96:128] = create_directory_entry('BIN        ', DIR_ATTR, 4)
    
    # /run directory (cluster 5)
    cluster[128:160] = create_directory_entry('RUN        ', DIR_ATTR, 5)
    
    return bytes(cluster)

def create_init_directory(elf_size=0):
    """Create /init directory with optional SHELL file entry"""
    cluster = bytearray(BYTES_PER_CLUSTER)
    
    DIR_ATTR = 0x10  # Directory attribute
    FILE_ATTR = 0x20  # File attribute
    
    # . entry (current directory - cluster 3)
    cluster[0:32] = create_directory_entry('.          ', DIR_ATTR, 3)
    
    # .. entry (parent directory - cluster 2, root)
    cluster[32:64] = create_directory_entry('..         ', DIR_ATTR, 2)
    
    # SHELL file entry (cluster 6, if ELF provided)
    if elf_size > 0:
        cluster[64:96] = create_directory_entry('SHELL      ', FILE_ATTR, 6, elf_size)
        print(f"  [DEBUG] Writing SHELL entry: cluster=6, size={elf_size}")
    
    return bytes(cluster)

def create_empty_directory(cluster_num):
    """Create an empty directory with just . and .. entries"""
    cluster = bytearray(BYTES_PER_CLUSTER)
    
    DIR_ATTR = 0x10  # Directory attribute
    
    # . entry (current directory)
    cluster[0:32] = create_directory_entry('.          ', DIR_ATTR, cluster_num)
    
    # .. entry (parent directory - cluster 2, root)
    cluster[32:64] = create_directory_entry('..         ', DIR_ATTR, 2)
    
    return bytes(cluster)

def main():
    if len(sys.argv) < 2:
        print("Usage: python3 create_disk_fixed.py <output.img> [elf_file]")
        sys.exit(1)
    
    output_file = sys.argv[1]
    elf_file = sys.argv[2] if len(sys.argv) > 2 else None
    
    # Load ELF file if provided
    elf_data = None
    elf_size = 0
    if elf_file and os.path.isfile(elf_file):
        print(f"Loading ELF file: {elf_file}")
        with open(elf_file, 'rb') as f:
            elf_data = f.read()
        elf_size = len(elf_data)
        print(f"  ✓ ELF loaded: {elf_size} bytes")
        print(f"  ✓ Clusters needed: {(elf_size + BYTES_PER_CLUSTER - 1) // BYTES_PER_CLUSTER}")
        print(f"  ✓ Padded size: {((elf_size + BYTES_PER_CLUSTER - 1) // BYTES_PER_CLUSTER) * BYTES_PER_CLUSTER}")
    elif elf_file:
        print(f"✗ ERROR: ELF file not found: {elf_file}")
        print(f"  Expected at: {os.path.abspath(elf_file)}")
        sys.exit(1)
    
    print(f"\nCreating {DISK_SIZE_MB}MB FAT32 disk image: {output_file}")
    
    # Use consistent FAT size (must match what's in boot sector)
    fat_size = 160  # Conservative size for this disk
    total_sectors = (DISK_SIZE_MB * 1024 * 1024) // BYTES_PER_SECTOR
    
    data_start_sector = RESERVED_SECTORS + (NUM_FATS * fat_size)
    
    print(f"  Total sectors: {total_sectors}")
    print(f"  FAT size: {fat_size} sectors ({fat_size * BYTES_PER_SECTOR} bytes)")
    print(f"  Data area starts at sector: {data_start_sector}")
    print(f"  Bytes per cluster: {BYTES_PER_CLUSTER}")
    
    # Create the disk image
    with open(output_file, 'wb') as f:
        # Sector 0: Boot sector
        print("  Writing boot sector...")
        f.write(create_boot_sector())
        
        # Sector 1: FSInfo
        print("  Writing FSInfo...")
        f.write(create_fsinfo())
        
        # Sectors 2-31: Reserved area
        print("  Writing reserved sectors...")
        for i in range(2, RESERVED_SECTORS):
            if i == 6:
                # Sector 6 is backup boot sector
                f.write(create_boot_sector())
            else:
                f.write(b'\x00' * BYTES_PER_SECTOR)
        
        # FAT tables
        print(f"  Writing FAT tables ({NUM_FATS} copies)...")
        fat_data = create_fat(total_sectors, fat_size, elf_size)
        for _ in range(NUM_FATS):
            f.write(fat_data)
        
        # DATA AREA: Clusters start here
        # Cluster 2: Root directory
        print("  Writing cluster 2 (root directory)...")
        f.write(create_root_directory())
        
        # Cluster 3: /init directory
        print("  Writing cluster 3 (/init directory)...")
        f.write(create_init_directory(elf_size))
        
        # Cluster 4: /bin directory
        print("  Writing cluster 4 (/bin directory)...")
        f.write(create_empty_directory(4))
        
        # Cluster 5: /run directory
        print("  Writing cluster 5 (/run directory)...")
        f.write(create_empty_directory(5))
        
        # Clusters 6+: ELF file data
        if elf_data:
            clusters_needed = (elf_size + BYTES_PER_CLUSTER - 1) // BYTES_PER_CLUSTER
            print(f"  Writing clusters 6-{5 + clusters_needed} (ELF file: {elf_size} bytes)...")
            # Pad to cluster boundary
            padded_size = clusters_needed * BYTES_PER_CLUSTER
            elf_padded = elf_data + b'\x00' * (padded_size - elf_size)
            f.write(elf_padded)
        
        # Fill rest of disk with zeros
        current_pos = f.tell()
        total_size = total_sectors * BYTES_PER_SECTOR
        remaining = total_size - current_pos
        
        if remaining > 0:
            print(f"  Filling remaining space ({remaining // 1024}KB)...")
            chunk_size = 1024 * 1024  # 1MB chunks
            chunks = remaining // chunk_size
            last_chunk = remaining % chunk_size
            
            for _ in range(chunks):
                f.write(b'\x00' * chunk_size)
            if last_chunk > 0:
                f.write(b'\x00' * last_chunk)
    
    print(f"\nSuccessfully created {output_file}")
    print("\nDisk structure:")
    print(f"  Boot sector + reserved: sectors 0-{RESERVED_SECTORS-1}")
    print(f"  FAT copies: sectors {RESERVED_SECTORS}-{RESERVED_SECTORS + NUM_FATS * fat_size - 1}")
    print(f"  Data area: sectors {data_start_sector}+")
    print(f"    Cluster 2 (sector {data_start_sector}): Root directory")
    print(f"    Cluster 3 (sector {data_start_sector + SECTORS_PER_CLUSTER}): /init directory")
    print(f"    Cluster 4 (sector {data_start_sector + 2*SECTORS_PER_CLUSTER}): /bin directory")
    print(f"    Cluster 5 (sector {data_start_sector + 3*SECTORS_PER_CLUSTER}): /run directory")
    if elf_data:
        print(f"    Cluster 6+ (sector {data_start_sector + 4*SECTORS_PER_CLUSTER}): ELF file ({elf_size} bytes)")
    
    print("\nDisk contents:")
    print(f"  /init/SHELL - {elf_size} bytes" if elf_data else "  /init - Empty")
    print("  /bin - Empty")
    print("  /run - Empty")

if __name__ == '__main__':
    main()
