#!/usr/bin/env python3
"""
Create a FAT32 disk image for NumOS - FIXED VERSION
Properly handles cluster-to-file-position mapping
"""

import struct
import sys
import os

# Disk parameters
DEFAULT_DISK_SIZE_MB = 32
DISK_SIZE_MB = DEFAULT_DISK_SIZE_MB
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

def create_fat(total_sectors, fat_size, init_size=0, init_label="INIT",
               syscalls_size=0, bin_programs=None, run_files=None,
               home_files=None):
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

    # Cluster 6: /home directory
    struct.pack_into('<I', fat, 24, 0x0FFFFFFF)  # EOC

    # Cluster 7: /include directory
    struct.pack_into('<I', fat, 28, 0x0FFFFFFF)  # EOC
    
    # Clusters 8+: file chains
    next_cluster = 8

    if init_size > 0:
        clusters_needed = (init_size + BYTES_PER_CLUSTER - 1) // BYTES_PER_CLUSTER
        print(f"[FAT] {init_label} file: {init_size} bytes = {clusters_needed} clusters")
        for i in range(clusters_needed):
            cluster_idx = next_cluster + i
            if i == clusters_needed - 1:
                struct.pack_into('<I', fat, cluster_idx * 4, 0x0FFFFFFF)
                print(f"[FAT] Cluster {cluster_idx}: EOC (last)")
            else:
                struct.pack_into('<I', fat, cluster_idx * 4, cluster_idx + 1)
                print(f"[FAT] Cluster {cluster_idx}: -> {cluster_idx + 1}")
        next_cluster += clusters_needed

    if syscalls_size > 0:
        clusters_needed = (syscalls_size + BYTES_PER_CLUSTER - 1) // BYTES_PER_CLUSTER
        print(f"[FAT] SYSCALLS.H file: {syscalls_size} bytes = {clusters_needed} clusters")
        for i in range(clusters_needed):
            cluster_idx = next_cluster + i
            if i == clusters_needed - 1:
                struct.pack_into('<I', fat, cluster_idx * 4, 0x0FFFFFFF)
                print(f"[FAT] Cluster {cluster_idx}: EOC (last)")
            else:
                struct.pack_into('<I', fat, cluster_idx * 4, cluster_idx + 1)
                print(f"[FAT] Cluster {cluster_idx}: -> {cluster_idx + 1}")
        next_cluster += clusters_needed

    if bin_programs:
        for prog in bin_programs:
            size = prog["size"]
            if size <= 0:
                continue
            clusters_needed = (size + BYTES_PER_CLUSTER - 1) // BYTES_PER_CLUSTER
            print(f"[FAT] {prog['name']} file: {size} bytes = {clusters_needed} clusters")
            for i in range(clusters_needed):
                cluster_idx = next_cluster + i
                if i == clusters_needed - 1:
                    struct.pack_into('<I', fat, cluster_idx * 4, 0x0FFFFFFF)
                    print(f"[FAT] Cluster {cluster_idx}: EOC (last)")
                else:
                    struct.pack_into('<I', fat, cluster_idx * 4, cluster_idx + 1)
                    print(f"[FAT] Cluster {cluster_idx}: -> {cluster_idx + 1}")
            next_cluster += clusters_needed

    if run_files:
        for item in run_files:
            size = item["size"]
            if size <= 0:
                continue
            clusters_needed = (size + BYTES_PER_CLUSTER - 1) // BYTES_PER_CLUSTER
            print(f"[FAT] {item['name']} file: {size} bytes = {clusters_needed} clusters")
            for i in range(clusters_needed):
                cluster_idx = next_cluster + i
                if i == clusters_needed - 1:
                    struct.pack_into('<I', fat, cluster_idx * 4, 0x0FFFFFFF)
                    print(f"[FAT] Cluster {cluster_idx}: EOC (last)")
                else:
                    struct.pack_into('<I', fat, cluster_idx * 4, cluster_idx + 1)
                    print(f"[FAT] Cluster {cluster_idx}: -> {cluster_idx + 1}")
            next_cluster += clusters_needed

    if home_files:
        for item in home_files:
            size = item["size"]
            if size <= 0:
                continue
            clusters_needed = (size + BYTES_PER_CLUSTER - 1) // BYTES_PER_CLUSTER
            print(f"[FAT] {item['name']} file: {size} bytes = {clusters_needed} clusters")
            for i in range(clusters_needed):
                cluster_idx = next_cluster + i
                if i == clusters_needed - 1:
                    struct.pack_into('<I', fat, cluster_idx * 4, 0x0FFFFFFF)
                    print(f"[FAT] Cluster {cluster_idx}: EOC (last)")
                else:
                    struct.pack_into('<I', fat, cluster_idx * 4, cluster_idx + 1)
                    print(f"[FAT] Cluster {cluster_idx}: -> {cluster_idx + 1}")
            next_cluster += clusters_needed
    
    return bytes(fat)

FAT32_NTRES_LOWER_BASE = 0x08
FAT32_NTRES_LOWER_EXT = 0x10


def create_directory_entry(name, attr, cluster, size=0, nt_reserved=0):
    """Create a FAT32 directory entry (32 bytes)"""
    entry = bytearray(32)
    
    # Name (8.3 format, padded with spaces)
    entry[0:11] = name.ljust(11).encode('ascii')[:11]
    
    # Attributes
    entry[11] = attr
    
    # Reserved
    entry[12] = nt_reserved
    
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

def fat_format_name(filename):
    name, ext = os.path.splitext(filename)
    if ext.startswith("."):
        ext = ext[1:]
    if len(name) == 0 or len(name) > 8 or len(ext) > 3:
        return None
    return name.upper().ljust(8) + ext.upper().ljust(3)


def fat_short_name_case_flags(filename):
    name, ext = os.path.splitext(filename)
    if ext.startswith("."):
        ext = ext[1:]

    flags = 0
    if name and any(ch.islower() for ch in name) and not any(ch.isupper() for ch in name):
        flags |= FAT32_NTRES_LOWER_BASE
    if ext and any(ch.islower() for ch in ext) and not any(ch.isupper() for ch in ext):
        flags |= FAT32_NTRES_LOWER_EXT
    return flags

def load_staged_files(stage_dir):
    files = []

    if not os.path.isdir(stage_dir):
        return files

    print(f"Loading staged files from: {stage_dir}")
    for entry in sorted(os.listdir(stage_dir)):
        path = os.path.join(stage_dir, entry)
        if not os.path.isfile(path):
            continue

        short_name = fat_format_name(entry)
        if not short_name:
            print(f"  [SKIP] {entry}: name is not FAT 8.3 compatible")
            continue

        with open(path, 'rb') as f:
            data = f.read()

        size = len(data)
        files.append({
            'name': entry,
            'short_name': short_name,
            'nt_reserved': fat_short_name_case_flags(entry),
            'size': size,
            'data': data,
            'cluster': 0,
            'clusters': 0,
        })
        print(f"  [OK] {entry}: {size} bytes")

    return files

def create_root_directory():
    """Create root directory cluster with entries for /init, /bin, /run, /home, /include"""
    cluster = bytearray(BYTES_PER_CLUSTER)
    
    DIR_ATTR = 0x10  # Directory attribute
    RO_ATTR  = 0x01  # Read-only attribute
    
    # . entry (current directory - cluster 2)
    cluster[0:32] = create_directory_entry('.          ', DIR_ATTR, 2)
    
    # .. entry (parent directory - cluster 0 for root)
    cluster[32:64] = create_directory_entry('..         ', DIR_ATTR, 0)
    
    # /init directory (cluster 3), mark read-only
    cluster[64:96] = create_directory_entry('INIT       ', DIR_ATTR | RO_ATTR, 3)
    
    # /bin directory (cluster 4)
    cluster[96:128] = create_directory_entry('BIN        ', DIR_ATTR, 4)
    
    # /run directory (cluster 5)
    cluster[128:160] = create_directory_entry('RUN        ', DIR_ATTR, 5)

    # /home directory (cluster 6)
    cluster[160:192] = create_directory_entry('HOME       ', DIR_ATTR, 6)

    # /include directory (cluster 7)
    cluster[192:224] = create_directory_entry('INCLUDE    ', DIR_ATTR, 7)
    
    return bytes(cluster)

def create_init_directory():
    """Create empty /init directory"""
    cluster = bytearray(BYTES_PER_CLUSTER)
    
    DIR_ATTR = 0x10  # Directory attribute
    FILE_ATTR = 0x20  # File attribute
    
    # . entry (current directory - cluster 3)
    cluster[0:32] = create_directory_entry('.          ', DIR_ATTR, 3)
    
    # .. entry (parent directory - cluster 2, root)
    cluster[32:64] = create_directory_entry('..         ', DIR_ATTR, 2)

    return bytes(cluster)

def create_bin_directory(init_short_name=None, init_cluster=0, init_size=0,
                         init_label="INIT", init_nt_reserved=0,
                         bin_programs=None):
    """Create /bin directory with init and program entries"""
    cluster = bytearray(BYTES_PER_CLUSTER)
    
    DIR_ATTR  = 0x10  # Directory attribute
    FILE_ATTR = 0x20  # File attribute

    # . entry (current directory - cluster 4)
    cluster[0:32] = create_directory_entry('.          ', DIR_ATTR, 4)

    # .. entry (parent directory - cluster 2, root)
    cluster[32:64] = create_directory_entry('..         ', DIR_ATTR, 2)

    offset = 64
    if init_size > 0 and init_cluster >= 8 and init_short_name:
        if offset + 32 <= len(cluster):
            cluster[offset:offset + 32] = create_directory_entry(
                init_short_name, FILE_ATTR, init_cluster, init_size,
                init_nt_reserved
            )
            print(f"  [DEBUG] Writing {init_label} entry (bin): cluster={init_cluster}, size={init_size}")
            offset += 32
    if bin_programs:
        for prog in bin_programs:
            if prog["cluster"] <= 0:
                continue
            if offset + 32 > len(cluster):
                break
            cluster[offset:offset + 32] = create_directory_entry(
                prog["short_name"], FILE_ATTR, prog["cluster"], prog["size"],
                prog.get("nt_reserved", 0)
            )
            print(f"  [DEBUG] Writing {prog['name']} entry (bin): cluster={prog['cluster']}, size={prog['size']}")
            offset += 32

    return bytes(cluster)

def create_run_directory(run_files):
    """Create /run directory with staged runtime file entries"""
    cluster = bytearray(BYTES_PER_CLUSTER)

    DIR_ATTR  = 0x10  # Directory attribute
    FILE_ATTR = 0x20  # File attribute

    # . entry (current directory - cluster 5)
    cluster[0:32] = create_directory_entry('.          ', DIR_ATTR, 5)

    # .. entry (parent directory - cluster 2, root)
    cluster[32:64] = create_directory_entry('..         ', DIR_ATTR, 2)

    offset = 64
    if run_files:
        for item in run_files:
            if item["cluster"] <= 0:
                continue
            if offset + 32 > len(cluster):
                break
            cluster[offset:offset + 32] = create_directory_entry(
                item["short_name"], FILE_ATTR, item["cluster"], item["size"],
                item.get("nt_reserved", 0)
            )
            print(f"  [DEBUG] Writing {item['name']} entry: cluster={item['cluster']}, size={item['size']}")
            offset += 32

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

def create_home_directory(home_files):
    """Create /home directory with file entries"""
    cluster = bytearray(BYTES_PER_CLUSTER)

    DIR_ATTR  = 0x10
    FILE_ATTR = 0x20

    cluster[0:32] = create_directory_entry('.          ', DIR_ATTR, 6)
    cluster[32:64] = create_directory_entry('..         ', DIR_ATTR, 2)

    offset = 64
    if home_files:
        for item in home_files:
            if item["cluster"] <= 0:
                continue
            if offset + 32 > len(cluster):
                break
            cluster[offset:offset + 32] = create_directory_entry(
                item["short_name"], FILE_ATTR, item["cluster"], item["size"],
                item.get("nt_reserved", 0)
            )
            print(f"  [DEBUG] Writing {item['name']} entry (home): cluster={item['cluster']}, size={item['size']}")
            offset += 32

    return bytes(cluster)

def create_include_directory(syscalls_size=0, syscalls_cluster=0):
    """Create /include directory with header entries"""
    cluster = bytearray(BYTES_PER_CLUSTER)

    DIR_ATTR  = 0x10
    FILE_ATTR = 0x20

    cluster[0:32] = create_directory_entry('.          ', DIR_ATTR, 7)
    cluster[32:64] = create_directory_entry('..         ', DIR_ATTR, 2)

    if syscalls_size > 0 and syscalls_cluster >= 8:
        cluster[64:96] = create_directory_entry('SYSCALLSH  ', FILE_ATTR,
                                                syscalls_cluster, syscalls_size,
                                                fat_short_name_case_flags('syscalls.h'))
        print(f"  [DEBUG] Writing SYSCALLS.H entry (include): cluster={syscalls_cluster}, size={syscalls_size}")

    return bytes(cluster)

def main():
    if len(sys.argv) < 2:
        print("Usage: python3 create_disk.py <output.img> [init_elf] [init_name]")
        sys.exit(1)

    syscalls_data = None
    syscalls_size = 0
    bin_programs = []
    run_files = []
    home_files = []
    root_dir = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
    build_user_dir = os.path.join(root_dir, "build", "user")
    init_name_lower = None
    syscalls_path = os.path.join(root_dir, "user", "include", "syscalls.h")
    if os.path.isfile(syscalls_path):
        print(f"Loading SYSCALLS.H: {syscalls_path}")
        with open(syscalls_path, 'rb') as f:
            syscalls_data = f.read()
        syscalls_size = len(syscalls_data)
        print(f"  [OK] SYSCALLS.H loaded: {syscalls_size} bytes")

    if os.path.isdir(build_user_dir):
        for entry in sorted(os.listdir(build_user_dir)):
            if not entry.lower().endswith('.elf'):
                continue
            if init_name_lower and entry.lower() == init_name_lower:
                continue
            short_name = fat_format_name(entry)
            if not short_name:
                continue
            path_elf = os.path.join(build_user_dir, entry)
            with open(path_elf, 'rb') as f:
                data = f.read()
            size = len(data)
            bin_programs.append({
                'name': entry,
                'short_name': short_name,
                'nt_reserved': fat_short_name_case_flags(entry),
                'size': size,
                'data': data,
                'cluster': 0,
                'clusters': 0,
            })

    run_stage_dir = os.path.join(root_dir, "user", "files", "run")
    home_stage_dir = os.path.join(root_dir, "user", "files", "home")
    run_files.extend(load_staged_files(run_stage_dir))
    home_files.extend(load_staged_files(home_stage_dir))

    global DISK_SIZE_MB
    output_file = sys.argv[1]
    disk_size_override = os.environ.get("NUMOS_DISK_SIZE_MB")
    if disk_size_override:
        try:
            DISK_SIZE_MB = int(disk_size_override)
        except ValueError:
            print(f"ERROR: invalid NUMOS_DISK_SIZE_MB value: {disk_size_override}")
            sys.exit(1)
    else:
        DISK_SIZE_MB = DEFAULT_DISK_SIZE_MB

    offset_bytes_env = os.environ.get("NUMOS_DISK_OFFSET_BYTES", "0")
    try:
        offset_bytes = int(offset_bytes_env)
    except ValueError:
        print(f"ERROR: invalid NUMOS_DISK_OFFSET_BYTES value: {offset_bytes_env}")
        sys.exit(1)
    if offset_bytes < 0:
        print("ERROR: NUMOS_DISK_OFFSET_BYTES must be >= 0")
        sys.exit(1)

    preserve_prefix = os.environ.get("NUMOS_DISK_PRESERVE_PREFIX", "0") == "1"
    init_elf_file = sys.argv[2] if len(sys.argv) > 2 else None
    init_name_arg = sys.argv[3] if len(sys.argv) > 3 else None

    init_data = None
    init_size = 0
    init_name = None
    init_short_name = None
    init_label = "INIT"
    init_nt_reserved = 0
    if init_elf_file and os.path.isfile(init_elf_file):
        print(f"Loading init ELF: {init_elf_file}")
        with open(init_elf_file, 'rb') as f:
            init_data = f.read()
        init_size = len(init_data)
        init_name = init_name_arg if init_name_arg else os.path.basename(init_elf_file)
        init_short_name = fat_format_name(init_name)
        if not init_short_name:
            print(f"ERROR: init name not 8.3 compatible: {init_name}")
            sys.exit(1)
        init_label = init_name
        init_nt_reserved = fat_short_name_case_flags(init_name)
        init_name_lower = init_name.lower()
        print(f"  [OK] Init ELF loaded: {init_size} bytes")
        print(f"  [OK] Clusters needed: {(init_size + BYTES_PER_CLUSTER - 1) // BYTES_PER_CLUSTER}")
        print(f"  [OK] Padded size: {((init_size + BYTES_PER_CLUSTER - 1) // BYTES_PER_CLUSTER) * BYTES_PER_CLUSTER}")
    elif init_elf_file:
        print(f"ERROR: init ELF not found: {init_elf_file}")
        print(f"  Expected at: {os.path.abspath(init_elf_file)}")
        sys.exit(1)

    if init_name_lower:
        bin_programs = [p for p in bin_programs if p["name"].lower() != init_name_lower]

    print(f"\nCreating {DISK_SIZE_MB}MB FAT32 disk image: {output_file}")
    
    # Use consistent FAT size (must match what's in boot sector)
    fat_size = 160  # Conservative size for this disk
    total_sectors = (DISK_SIZE_MB * 1024 * 1024) // BYTES_PER_SECTOR
    
    data_start_sector = RESERVED_SECTORS + (NUM_FATS * fat_size)
    
    print(f"  Total sectors: {total_sectors}")
    print(f"  FAT size: {fat_size} sectors ({fat_size * BYTES_PER_SECTOR} bytes)")
    print(f"  Data area starts at sector: {data_start_sector}")
    print(f"  Bytes per cluster: {BYTES_PER_CLUSTER}")

    def clusters_for(size):
        return (size + BYTES_PER_CLUSTER - 1) // BYTES_PER_CLUSTER if size > 0 else 0

    init_clusters = clusters_for(init_size)
    syscalls_clusters = clusters_for(syscalls_size)
    for prog in bin_programs:
        prog["clusters"] = clusters_for(prog["size"])
    for item in run_files:
        item["clusters"] = clusters_for(item["size"])
    for item in home_files:
        item["clusters"] = clusters_for(item["size"])

    next_cluster = 8
    init_cluster = next_cluster if init_clusters > 0 else 0
    next_cluster += init_clusters
    syscalls_cluster = next_cluster if syscalls_clusters > 0 else 0
    next_cluster += syscalls_clusters
    for prog in bin_programs:
        if prog["clusters"] > 0:
            prog["cluster"] = next_cluster
            next_cluster += prog["clusters"]
    for item in run_files:
        if item["clusters"] > 0:
            item["cluster"] = next_cluster
            next_cluster += item["clusters"]
    for item in home_files:
        if item["clusters"] > 0:
            item["cluster"] = next_cluster
            next_cluster += item["clusters"]
    
    # Create the disk image
    file_mode = 'r+b' if preserve_prefix else 'wb'
    if preserve_prefix and not os.path.exists(output_file):
        print(f"ERROR: preserve mode requires existing file: {output_file}")
        sys.exit(1)

    with open(output_file, file_mode) as f:
        if offset_bytes:
            f.seek(offset_bytes)
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
        fat_data = create_fat(total_sectors, fat_size, init_size, init_label,
                              syscalls_size, bin_programs, run_files,
                              home_files)
        for _ in range(NUM_FATS):
            f.write(fat_data)
        
        # DATA AREA: Clusters start here
        # Cluster 2: Root directory
        print("  Writing cluster 2 (root directory)...")
        f.write(create_root_directory())
        
        # Cluster 3: /init directory
        print("  Writing cluster 3 (/init directory)...")
        f.write(create_init_directory())
        
        # Cluster 4: /bin directory
        print("  Writing cluster 4 (/bin directory)...")
        f.write(create_bin_directory(init_short_name, init_cluster, init_size,
                                     init_label, init_nt_reserved,
                                     bin_programs))
        
        # Cluster 5: /run directory
        print("  Writing cluster 5 (/run directory)...")
        f.write(create_run_directory(run_files))

        # Cluster 6: /home directory
        print("  Writing cluster 6 (/home directory)...")
        f.write(create_home_directory(home_files))

        # Cluster 7: /include directory
        print("  Writing cluster 7 (/include directory)...")
        f.write(create_include_directory(syscalls_size, syscalls_cluster))
        
        # Clusters 7+: file data
        if init_data:
            print(f"  Writing clusters {init_cluster}-{init_cluster + init_clusters - 1} ({init_label}: {init_size} bytes)...")
            padded_size = init_clusters * BYTES_PER_CLUSTER
            init_padded = init_data + b'\x00' * (padded_size - init_size)
            f.write(init_padded)

        if syscalls_data:
            print(f"  Writing clusters {syscalls_cluster}-{syscalls_cluster + syscalls_clusters - 1} (SYSCALLS.H: {syscalls_size} bytes)...")
            padded_size = syscalls_clusters * BYTES_PER_CLUSTER
            syscalls_padded = syscalls_data + b'\x00' * (padded_size - syscalls_size)
            f.write(syscalls_padded)

        if bin_programs:
            for prog in bin_programs:
                if prog["clusters"] <= 0:
                    continue
                print(f"  Writing clusters {prog['cluster']}-{prog['cluster'] + prog['clusters'] - 1} ({prog['name']}: {prog['size']} bytes)...")
                padded_size = prog["clusters"] * BYTES_PER_CLUSTER
                padded = prog["data"] + b'\x00' * (padded_size - prog["size"])
                f.write(padded)

        if run_files:
            for item in run_files:
                if item["clusters"] <= 0:
                    continue
                print(f"  Writing clusters {item['cluster']}-{item['cluster'] + item['clusters'] - 1} ({item['name']}: {item['size']} bytes)...")
                padded_size = item["clusters"] * BYTES_PER_CLUSTER
                padded = item["data"] + b'\x00' * (padded_size - item["size"])
                f.write(padded)

        if home_files:
            for item in home_files:
                if item["clusters"] <= 0:
                    continue
                print(f"  Writing clusters {item['cluster']}-{item['cluster'] + item['clusters'] - 1} ({item['name']}: {item['size']} bytes)...")
                padded_size = item["clusters"] * BYTES_PER_CLUSTER
                padded = item["data"] + b'\x00' * (padded_size - item["size"])
                f.write(padded)
        
        # Fill rest of disk with zeros
        current_pos = f.tell()
        total_size = total_sectors * BYTES_PER_SECTOR
        target_end_pos = offset_bytes + total_size
        remaining = target_end_pos - current_pos
        
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
    print(f"    Cluster 6 (sector {data_start_sector + 4*SECTORS_PER_CLUSTER}): /home directory")
    print(f"    Cluster 7 (sector {data_start_sector + 5*SECTORS_PER_CLUSTER}): /include directory")
    if init_clusters > 0:
        init_sector = data_start_sector + (init_cluster - 2) * SECTORS_PER_CLUSTER
        print(f"    Cluster {init_cluster}-{init_cluster + init_clusters - 1} (sector {init_sector}): {init_label} file ({init_size} bytes)")
    if syscalls_clusters > 0:
        syscalls_sector = data_start_sector + (syscalls_cluster - 2) * SECTORS_PER_CLUSTER
        print(f"    Cluster {syscalls_cluster}-{syscalls_cluster + syscalls_clusters - 1} (sector {syscalls_sector}): SYSCALLS.H file ({syscalls_size} bytes)")
    if bin_programs:
        for prog in bin_programs:
            if prog["clusters"] <= 0:
                continue
            sector = data_start_sector + (prog["cluster"] - 2) * SECTORS_PER_CLUSTER
            print(f"    Cluster {prog['cluster']}-{prog['cluster'] + prog['clusters'] - 1} (sector {sector}): {prog['name']} file ({prog['size']} bytes)")
    if run_files:
        for item in run_files:
            if item["clusters"] <= 0:
                continue
            sector = data_start_sector + (item["cluster"] - 2) * SECTORS_PER_CLUSTER
            print(f"    Cluster {item['cluster']}-{item['cluster'] + item['clusters'] - 1} (sector {sector}): {item['name']} file ({item['size']} bytes)")
    if home_files:
        for item in home_files:
            if item["clusters"] <= 0:
                continue
            sector = data_start_sector + (item["cluster"] - 2) * SECTORS_PER_CLUSTER
            print(f"    Cluster {item['cluster']}-{item['cluster'] + item['clusters'] - 1} (sector {sector}): {item['name']} file ({item['size']} bytes)")
    
    print("\nDisk contents:")
    print("  /init - Empty")
    if init_clusters > 0:
        if init_name:
            print(f"  /bin/{init_name} - {init_size} bytes")
        else:
            print(f"  /bin/{init_label} - {init_size} bytes")
    if bin_programs:
        for prog in bin_programs:
            if prog["clusters"] <= 0:
                continue
            print(f"  /bin/{prog['name']} - {prog['size']} bytes")
    if syscalls_clusters > 0:
        print(f"  /include/SYSCALLS.H - {syscalls_size} bytes")
    if syscalls_clusters == 0 and init_clusters == 0 and (not bin_programs):
        print("  /bin - Empty")
    if run_files:
        for item in run_files:
            if item["clusters"] <= 0:
                continue
            print(f"  /run/{item['name']} - {item['size']} bytes")
    if not run_files:
        print("  /run - Empty")
    if home_files:
        for item in home_files:
            if item["clusters"] <= 0:
                continue
            print(f"  /home/{item['name']} - {item['size']} bytes")
    else:
        print("  /home - Empty")

if __name__ == '__main__':
    main()
