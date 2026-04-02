#!/usr/bin/env python3
"""
Build a small FAT32 image with the files NumOS expects at boot.

The image layout is intentionally fixed and simple. That keeps the script easy
to audit and makes the generated images deterministic across build hosts.
"""

import os
import struct
import sys

TOOLS_DIR = os.path.dirname(__file__)
if TOOLS_DIR not in sys.path:
    sys.path.insert(0, TOOLS_DIR)

import numloss_codec


# FAT32 image geometry. The current boot flow depends on this fixed layout.
DEFAULT_DISK_SIZE_MB = 32
DISK_SIZE_MB = DEFAULT_DISK_SIZE_MB
BYTES_PER_SECTOR = 512
SECTORS_PER_CLUSTER = 8
RESERVED_SECTORS = 32
NUM_FATS = 2
FAT_SIZE_SECTORS = 160
BYTES_PER_CLUSTER = SECTORS_PER_CLUSTER * BYTES_PER_SECTOR

# Fixed directory clusters inside the image.
ROOT_DIRECTORY_CLUSTER = 2
INIT_DIRECTORY_CLUSTER = 3
BIN_DIRECTORY_CLUSTER = 4
RUN_DIRECTORY_CLUSTER = 5
HOME_DIRECTORY_CLUSTER = 6
INCLUDE_DIRECTORY_CLUSTER = 7
FIRST_FILE_CLUSTER = 8

# FAT32 directory attributes and entry values.
DIRECTORY_ATTR = 0x10
FILE_ATTR = 0x20
READ_ONLY_ATTR = 0x01
FAT_EOC = 0x0FFFFFFF

FAT32_NTRES_LOWER_BASE = 0x08
FAT32_NTRES_LOWER_EXT = 0x10

PREINSTALLED_BIN_NAMES = {
    "connect.elf",
    "date.elf",
    "empty.elf",
    "edit.elf",
    "install.elf",
    "mk.elf",
    "net.elf",
    "numloss.elf",
    "ocl.elf",
    "pkg.elf",
    "proc.elf",
    "see.elf",
    "shell.elf",
    "tcp.elf",
    "thread.elf",
    "usb.elf",
}


def cluster_count_for_size(size):
    """Return how many clusters are needed for a payload of size bytes."""
    if size <= 0:
        return 0
    return (size + BYTES_PER_CLUSTER - 1) // BYTES_PER_CLUSTER


def create_boot_sector():
    """Create the FAT32 boot sector for the current image size."""
    boot_sector = bytearray(BYTES_PER_SECTOR)

    boot_sector[0:3] = b"\xEB\x58\x90"
    boot_sector[3:11] = b"NUMOS1.0"

    struct.pack_into("<H", boot_sector, 11, BYTES_PER_SECTOR)
    struct.pack_into("<B", boot_sector, 13, SECTORS_PER_CLUSTER)
    struct.pack_into("<H", boot_sector, 14, RESERVED_SECTORS)
    struct.pack_into("<B", boot_sector, 16, NUM_FATS)
    struct.pack_into("<H", boot_sector, 17, 0)
    struct.pack_into("<H", boot_sector, 19, 0)
    struct.pack_into("<B", boot_sector, 21, 0xF8)
    struct.pack_into("<H", boot_sector, 22, 0)
    struct.pack_into("<H", boot_sector, 24, 63)
    struct.pack_into("<H", boot_sector, 26, 16)
    struct.pack_into("<I", boot_sector, 28, 0)

    total_sectors = (DISK_SIZE_MB * 1024 * 1024) // BYTES_PER_SECTOR
    struct.pack_into("<I", boot_sector, 32, total_sectors)

    struct.pack_into("<I", boot_sector, 36, FAT_SIZE_SECTORS)
    struct.pack_into("<H", boot_sector, 40, 0)
    struct.pack_into("<H", boot_sector, 42, 0)
    struct.pack_into("<I", boot_sector, 44, ROOT_DIRECTORY_CLUSTER)
    struct.pack_into("<H", boot_sector, 48, 1)
    struct.pack_into("<H", boot_sector, 50, 6)

    boot_sector[52:64] = b"\x00" * 12

    struct.pack_into("<B", boot_sector, 64, 0x80)
    struct.pack_into("<B", boot_sector, 65, 0)
    struct.pack_into("<B", boot_sector, 66, 0x29)
    struct.pack_into("<I", boot_sector, 67, 0x12345678)
    boot_sector[71:82] = b"NUMOS DISK "
    boot_sector[82:90] = b"FAT32   "
    boot_sector[510:512] = b"\x55\xAA"

    return bytes(boot_sector)


def create_fsinfo(free_clusters, next_free_cluster):
    """Create the FAT32 FSInfo sector."""
    fsinfo_sector = bytearray(BYTES_PER_SECTOR)
    struct.pack_into("<I", fsinfo_sector, 0, 0x41615252)
    struct.pack_into("<I", fsinfo_sector, 484, 0x61417272)
    struct.pack_into("<I", fsinfo_sector, 488, free_clusters)
    struct.pack_into("<I", fsinfo_sector, 492, next_free_cluster)
    struct.pack_into("<I", fsinfo_sector, 508, 0xAA550000)
    return bytes(fsinfo_sector)


def write_fat_entry(fat_table, cluster, value):
    """Write one FAT32 cluster entry."""
    struct.pack_into("<I", fat_table, cluster * 4, value)


def append_cluster_chain(fat_table, next_cluster, record):
    """
    Append a cluster chain for one staged record and print the allocation.

    The caller keeps ownership of the actual file data. This function only
    writes the cluster links into the FAT.
    """
    clusters = record["clusters"]
    if clusters <= 0:
        return next_cluster

    print(
        f"[FAT] {record['name']} file: {record['size']} bytes = "
        f"{clusters} clusters"
    )
    for offset in range(clusters):
        cluster = next_cluster + offset
        next_value = FAT_EOC if offset == clusters - 1 else cluster + 1
        write_fat_entry(fat_table, cluster, next_value)
        if next_value == FAT_EOC:
            print(f"[FAT] Cluster {cluster}: EOC (last)")
        else:
            print(f"[FAT] Cluster {cluster}: -> {next_value}")

    return next_cluster + clusters


def create_fat(
    _total_sectors,
    fat_size,
    init_record=None,
    syscalls_record=None,
    bin_programs=None,
    run_files=None,
    home_files=None,
):
    """Create the FAT table for the image."""
    fat_table = bytearray(fat_size * BYTES_PER_SECTOR)

    write_fat_entry(fat_table, 0, 0x0FFFFFF8)
    write_fat_entry(fat_table, 1, FAT_EOC)

    for cluster in (
        ROOT_DIRECTORY_CLUSTER,
        INIT_DIRECTORY_CLUSTER,
        BIN_DIRECTORY_CLUSTER,
        RUN_DIRECTORY_CLUSTER,
        HOME_DIRECTORY_CLUSTER,
        INCLUDE_DIRECTORY_CLUSTER,
    ):
        write_fat_entry(fat_table, cluster, FAT_EOC)

    next_cluster = FIRST_FILE_CLUSTER
    for record in (
        [init_record, syscalls_record]
        + list(bin_programs or [])
        + list(run_files or [])
        + list(home_files or [])
    ):
        if record:
            next_cluster = append_cluster_chain(fat_table, next_cluster, record)

    return bytes(fat_table)


def create_directory_entry(name, attr, cluster, size=0, nt_reserved=0):
    """Create one 32 byte FAT32 directory entry."""
    entry = bytearray(32)
    entry[0:11] = name.ljust(11).encode("ascii")[:11]
    entry[11] = attr
    entry[12] = nt_reserved
    entry[13] = 0
    entry[14:16] = b"\x00\x00"
    entry[16:18] = b"\x21\x00"
    entry[18:20] = b"\x21\x00"
    struct.pack_into("<H", entry, 20, (cluster >> 16) & 0xFFFF)
    entry[22:24] = b"\x00\x00"
    entry[24:26] = b"\x21\x00"
    struct.pack_into("<H", entry, 26, cluster & 0xFFFF)
    struct.pack_into("<I", entry, 28, size)
    return bytes(entry)


def fat_format_name(filename):
    """Convert a host filename into an uppercase FAT 8.3 short name."""
    name, ext = os.path.splitext(filename)
    if ext.startswith("."):
        ext = ext[1:]
    if len(name) == 0 or len(name) > 8 or len(ext) > 3:
        return None
    return name.upper().ljust(8) + ext.upper().ljust(3)


def fat_short_name_case_flags(filename):
    """Return FAT32 case flags so names keep lower-case display when possible."""
    name, ext = os.path.splitext(filename)
    if ext.startswith("."):
        ext = ext[1:]

    flags = 0
    if name and any(ch.islower() for ch in name) and not any(ch.isupper() for ch in name):
        flags |= FAT32_NTRES_LOWER_BASE
    if ext and any(ch.islower() for ch in ext) and not any(ch.isupper() for ch in ext):
        flags |= FAT32_NTRES_LOWER_EXT
    return flags


def load_staged_files(stage_dir, allowed_names=None):
    """
    Load regular files from a staging directory.

    Files must fit FAT 8.3 naming rules because this image builder only emits
    short directory entries.
    """
    staged_files = []

    if not os.path.isdir(stage_dir):
        return staged_files

    print(f"Loading staged files from: {stage_dir}")
    for entry_name in sorted(os.listdir(stage_dir)):
        if allowed_names is not None and entry_name.lower() not in allowed_names:
            continue

        path = os.path.join(stage_dir, entry_name)
        if not os.path.isfile(path):
            continue

        short_name = fat_format_name(entry_name)
        if not short_name:
            print(f"  [SKIP] {entry_name}: name is not FAT 8.3 compatible")
            continue

        with open(path, "rb") as stage_file:
            data = stage_file.read()

        staged_files.append(
            {
                "name": entry_name,
                "short_name": short_name,
                "nt_reserved": fat_short_name_case_flags(entry_name),
                "size": len(data),
                "data": data,
                "cluster": 0,
                "clusters": 0,
            }
        )
        print(f"  [OK] {entry_name}: {len(data)} bytes")

    return staged_files


def load_staged_files_from_dirs(stage_dirs, allowed_names=None):
    """Load staged files from several directories without duplicate names."""
    staged_files = []
    seen_names = set()

    for stage_dir in stage_dirs:
        for record in load_staged_files(stage_dir, allowed_names):
            record_key = record["name"].lower()
            if record_key in seen_names:
                continue
            seen_names.add(record_key)
            staged_files.append(record)

    return staged_files


def create_base_directory(current_cluster, parent_cluster=ROOT_DIRECTORY_CLUSTER):
    """Create a directory cluster populated with only . and .. entries."""
    cluster = bytearray(BYTES_PER_CLUSTER)
    cluster[0:32] = create_directory_entry(".          ", DIRECTORY_ATTR, current_cluster)
    cluster[32:64] = create_directory_entry("..         ", DIRECTORY_ATTR, parent_cluster)
    return cluster


def append_directory_file_entries(cluster, start_offset, items, debug_label):
    """Append file entries to a directory cluster and log what was written."""
    offset = start_offset
    for item in items or []:
        if item["cluster"] <= 0:
            continue
        if offset + 32 > len(cluster):
            break
        cluster[offset:offset + 32] = create_directory_entry(
            item["short_name"],
            FILE_ATTR,
            item["cluster"],
            item["size"],
            item.get("nt_reserved", 0),
        )
        print(
            f"  [DEBUG] Writing {item['name']} entry ({debug_label}): "
            f"cluster={item['cluster']}, size={item['size']}"
        )
        offset += 32
    return offset


def create_root_directory():
    """Create the root directory with the fixed top-level folders."""
    cluster = create_base_directory(ROOT_DIRECTORY_CLUSTER, 0)
    cluster[64:96] = create_directory_entry(
        "INIT       ", DIRECTORY_ATTR | READ_ONLY_ATTR, INIT_DIRECTORY_CLUSTER
    )
    cluster[96:128] = create_directory_entry(
        "BIN        ", DIRECTORY_ATTR, BIN_DIRECTORY_CLUSTER
    )
    cluster[128:160] = create_directory_entry(
        "RUN        ", DIRECTORY_ATTR, RUN_DIRECTORY_CLUSTER
    )
    cluster[160:192] = create_directory_entry(
        "HOME       ", DIRECTORY_ATTR, HOME_DIRECTORY_CLUSTER
    )
    cluster[192:224] = create_directory_entry(
        "INCLUDE    ", DIRECTORY_ATTR, INCLUDE_DIRECTORY_CLUSTER
    )
    return bytes(cluster)


def create_init_directory():
    """Create the empty /init directory."""
    return bytes(create_base_directory(INIT_DIRECTORY_CLUSTER))


def create_bin_directory(init_record=None, bin_programs=None):
    """Create the /bin directory with the init ELF and user programs."""
    cluster = create_base_directory(BIN_DIRECTORY_CLUSTER)
    next_offset = 64

    if init_record and init_record["clusters"] > 0:
        cluster[next_offset:next_offset + 32] = create_directory_entry(
            init_record["short_name"],
            FILE_ATTR,
            init_record["cluster"],
            init_record["size"],
            init_record.get("nt_reserved", 0),
        )
        print(
            f"  [DEBUG] Writing {init_record['name']} entry (bin): "
            f"cluster={init_record['cluster']}, size={init_record['size']}"
        )
        next_offset += 32

    append_directory_file_entries(cluster, next_offset, bin_programs, "bin")
    return bytes(cluster)


def create_run_directory(run_files):
    """Create the /run directory with staged runtime files."""
    cluster = create_base_directory(RUN_DIRECTORY_CLUSTER)
    append_directory_file_entries(cluster, 64, run_files, "run")
    return bytes(cluster)


def create_empty_directory(cluster_num):
    """Backward compatible helper for callers that need an empty directory."""
    return bytes(create_base_directory(cluster_num))


def create_home_directory(home_files):
    """Create the /home directory with staged user files."""
    cluster = create_base_directory(HOME_DIRECTORY_CLUSTER)
    append_directory_file_entries(cluster, 64, home_files, "home")
    return bytes(cluster)


def create_include_directory(syscalls_record=None):
    """Create the /include directory with exported headers."""
    cluster = create_base_directory(INCLUDE_DIRECTORY_CLUSTER)
    append_directory_file_entries(cluster, 64, [syscalls_record] if syscalls_record else [], "include")
    return bytes(cluster)


def load_binary_file(path, label):
    """Read a required binary payload from disk and print a short status line."""
    print(f"Loading {label}: {path}")
    with open(path, "rb") as binary_file:
        data = binary_file.read()
    print(f"  [OK] {label} loaded: {len(data)} bytes")
    return data


def create_file_record(name, data):
    """Create the record shape used through staging, allocation, and writing."""
    short_name = fat_format_name(name)
    if not short_name:
        return None
    return {
        "name": name,
        "short_name": short_name,
        "nt_reserved": fat_short_name_case_flags(name),
        "size": len(data),
        "data": data,
        "cluster": 0,
        "clusters": cluster_count_for_size(len(data)),
    }


def should_pack_user_elf():
    """Return True when staged user ELFs should be packed with numloss."""
    return os.environ.get("NUMOS_PACK_USER_ELF", "1") == "1"


def maybe_pack_record(record):
    """Pack one staged ELF when numloss makes it smaller."""
    if not record:
        return record
    if not record["name"].lower().endswith(".elf"):
        return record
    if record["size"] == 0:
        return record
    if numloss_codec.is_archive(record["data"]):
        return record

    packed_data = numloss_codec.encode(record["data"])
    if len(packed_data) >= record["size"]:
        return record

    packed_record = dict(record)
    packed_record["data"] = packed_data
    packed_record["size"] = len(packed_data)
    packed_record["clusters"] = cluster_count_for_size(len(packed_data))
    packed_record["packed"] = True
    packed_record["source_size"] = record["size"]
    return packed_record


def maybe_pack_records(records, enabled):
    """Pack staged ELF records when the build enables the feature."""
    if not enabled:
        return records

    packed_records = []
    for record in records:
        packed = maybe_pack_record(record)
        if packed is not record and packed.get("packed"):
            print(
                f"  [PACK] {record['name']}: "
                f"{record['size']} -> {packed['size']} bytes"
            )
        packed_records.append(packed)
    return packed_records


def assign_record_clusters(records, next_cluster):
    """Assign clusters in order so on-disk layout matches directory order."""
    for record in records:
        if not record:
            continue
        record["clusters"] = cluster_count_for_size(record["size"])
        if record["clusters"] <= 0:
            record["cluster"] = 0
            continue
        record["cluster"] = next_cluster
        next_cluster += record["clusters"]
    return next_cluster


def write_reserved_area(image_file):
    """Write reserved sectors and the backup boot sector."""
    print("  Writing reserved sectors...")
    for sector in range(2, RESERVED_SECTORS):
        if sector == 6:
            image_file.write(create_boot_sector())
        else:
            image_file.write(b"\x00" * BYTES_PER_SECTOR)


def pad_record_data(record):
    """Pad one record to a whole-number cluster count."""
    padded_size = record["clusters"] * BYTES_PER_CLUSTER
    return record["data"] + b"\x00" * (padded_size - record["size"])


def write_record_payload(image_file, record):
    """Write one staged payload after the directory area."""
    if not record or record["clusters"] <= 0:
        return

    end_cluster = record["cluster"] + record["clusters"] - 1
    print(
        f"  Writing clusters {record['cluster']}-{end_cluster} "
        f"({record['name']}: {record['size']} bytes)..."
    )
    image_file.write(pad_record_data(record))


def print_record_layout(record, data_start_sector):
    """Print the on-disk cluster range for one staged record."""
    if not record or record["clusters"] <= 0:
        return

    start_sector = data_start_sector + (record["cluster"] - ROOT_DIRECTORY_CLUSTER) * SECTORS_PER_CLUSTER
    end_cluster = record["cluster"] + record["clusters"] - 1
    print(
        f"    Cluster {record['cluster']}-{end_cluster} "
        f"(sector {start_sector}): {record['name']} file ({record['size']} bytes)"
    )


def print_record_contents(directory, record):
    """Print one user-facing file listing entry."""
    if not record or record["clusters"] <= 0:
        return
    print(f"  {directory}/{record['name']} - {record['size']} bytes")


def parse_disk_size_mb():
    """Read NUMOS_DISK_SIZE_MB and keep the default when unset."""
    disk_size_override = os.environ.get("NUMOS_DISK_SIZE_MB")
    if not disk_size_override:
        return DEFAULT_DISK_SIZE_MB
    try:
        return int(disk_size_override)
    except ValueError:
        print(f"ERROR: invalid NUMOS_DISK_SIZE_MB value: {disk_size_override}")
        raise SystemExit(1)


def parse_offset_bytes():
    """Read NUMOS_DISK_OFFSET_BYTES and reject negative offsets."""
    offset_text = os.environ.get("NUMOS_DISK_OFFSET_BYTES", "0")
    try:
        offset_bytes = int(offset_text)
    except ValueError:
        print(f"ERROR: invalid NUMOS_DISK_OFFSET_BYTES value: {offset_text}")
        raise SystemExit(1)
    if offset_bytes < 0:
        print("ERROR: NUMOS_DISK_OFFSET_BYTES must be >= 0")
        raise SystemExit(1)
    return offset_bytes


def load_init_record(init_elf_file, init_name_arg):
    """Load an optional replacement init ELF."""
    if not init_elf_file:
        return None
    if not os.path.isfile(init_elf_file):
        print(f"ERROR: init ELF not found: {init_elf_file}")
        print(f"  Expected at: {os.path.abspath(init_elf_file)}")
        raise SystemExit(1)

    init_data = load_binary_file(init_elf_file, "init ELF")
    init_name = init_name_arg if init_name_arg else os.path.basename(init_elf_file)
    init_record = create_file_record(init_name, init_data)
    if not init_record:
        print(f"ERROR: init name not 8.3 compatible: {init_name}")
        raise SystemExit(1)

    print(f"  [OK] Clusters needed: {init_record['clusters']}")
    print(f"  [OK] Padded size: {init_record['clusters'] * BYTES_PER_CLUSTER}")
    return init_record


def main():
    """Build the image and print a short layout summary."""
    if len(sys.argv) < 2:
        print("Usage: python3 create_disk.py <output.img> [init_elf] [init_name]")
        raise SystemExit(1)

    root_dir = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
    output_file = sys.argv[1]
    init_elf_file = sys.argv[2] if len(sys.argv) > 2 else None
    init_name_arg = sys.argv[3] if len(sys.argv) > 3 else None

    global DISK_SIZE_MB
    DISK_SIZE_MB = parse_disk_size_mb()
    offset_bytes = parse_offset_bytes()
    preserve_prefix = os.environ.get("NUMOS_DISK_PRESERVE_PREFIX", "0") == "1"
    pack_user_elf = should_pack_user_elf()

    syscalls_record = None
    syscalls_path = os.path.join(root_dir, "user", "include", "syscalls.h")
    if os.path.isfile(syscalls_path):
        syscalls_record = create_file_record(
            "SYSCALLS.H", load_binary_file(syscalls_path, "SYSCALLS.H")
        )

    bin_programs = load_staged_files_from_dirs(
        [
            os.path.join(root_dir, "user", "files", "bin"),
            os.path.join(root_dir, "build", "user"),
        ],
        PREINSTALLED_BIN_NAMES,
    )
    run_files = load_staged_files_from_dirs(
        [
            os.path.join(root_dir, "user", "files", "run"),
            os.path.join(root_dir, "build", "stage", "run"),
        ]
    )
    home_files = load_staged_files_from_dirs(
        [os.path.join(root_dir, "user", "files", "home")]
    )

    init_record = load_init_record(init_elf_file, init_name_arg)
    if init_record:
        init_name_lower = init_record["name"].lower()
        bin_programs = [
            program for program in bin_programs if program["name"].lower() != init_name_lower
        ]

    bin_programs = maybe_pack_records(bin_programs, pack_user_elf)
    run_files = maybe_pack_records(run_files, pack_user_elf)

    total_sectors = (DISK_SIZE_MB * 1024 * 1024) // BYTES_PER_SECTOR
    data_start_sector = RESERVED_SECTORS + (NUM_FATS * FAT_SIZE_SECTORS)

    print(f"\nCreating {DISK_SIZE_MB}MB FAT32 disk image: {output_file}")
    print(f"  Total sectors: {total_sectors}")
    print(f"  FAT size: {FAT_SIZE_SECTORS} sectors ({FAT_SIZE_SECTORS * BYTES_PER_SECTOR} bytes)")
    print(f"  Data area starts at sector: {data_start_sector}")
    print(f"  Bytes per cluster: {BYTES_PER_CLUSTER}")

    next_cluster = FIRST_FILE_CLUSTER
    next_cluster = assign_record_clusters([init_record, syscalls_record], next_cluster)
    next_cluster = assign_record_clusters(bin_programs, next_cluster)
    next_cluster = assign_record_clusters(run_files, next_cluster)
    next_cluster = assign_record_clusters(home_files, next_cluster)

    file_mode = "r+b" if preserve_prefix else "wb"
    if preserve_prefix and not os.path.exists(output_file):
        print(f"ERROR: preserve mode requires existing file: {output_file}")
        raise SystemExit(1)

    with open(output_file, file_mode) as image_file:
        if offset_bytes:
            image_file.seek(offset_bytes)

        print("  Writing boot sector...")
        image_file.write(create_boot_sector())

        total_clusters = (total_sectors - data_start_sector) // SECTORS_PER_CLUSTER
        free_clusters = total_clusters - (next_cluster - ROOT_DIRECTORY_CLUSTER)

        print("  Writing FSInfo...")
        image_file.write(create_fsinfo(free_clusters, next_cluster))

        write_reserved_area(image_file)

        print(f"  Writing FAT tables ({NUM_FATS} copies)...")
        fat_data = create_fat(
            total_sectors,
            FAT_SIZE_SECTORS,
            init_record=init_record,
            syscalls_record=syscalls_record,
            bin_programs=bin_programs,
            run_files=run_files,
            home_files=home_files,
        )
        for _ in range(NUM_FATS):
            image_file.write(fat_data)

        print("  Writing cluster 2 (root directory)...")
        image_file.write(create_root_directory())
        print("  Writing cluster 3 (/init directory)...")
        image_file.write(create_init_directory())
        print("  Writing cluster 4 (/bin directory)...")
        image_file.write(create_bin_directory(init_record=init_record, bin_programs=bin_programs))
        print("  Writing cluster 5 (/run directory)...")
        image_file.write(create_run_directory(run_files))
        print("  Writing cluster 6 (/home directory)...")
        image_file.write(create_home_directory(home_files))
        print("  Writing cluster 7 (/include directory)...")
        image_file.write(create_include_directory(syscalls_record))

        for record in [init_record, syscalls_record] + bin_programs + run_files + home_files:
            write_record_payload(image_file, record)

        current_pos = image_file.tell()
        total_size = total_sectors * BYTES_PER_SECTOR
        target_end_pos = offset_bytes + total_size
        remaining = target_end_pos - current_pos
        if remaining > 0:
            print(f"  Filling remaining space ({remaining // 1024}KB)...")
            chunk_size = 1024 * 1024
            full_chunks = remaining // chunk_size
            tail_bytes = remaining % chunk_size
            for _ in range(full_chunks):
                image_file.write(b"\x00" * chunk_size)
            if tail_bytes:
                image_file.write(b"\x00" * tail_bytes)

    print(f"\nSuccessfully created {output_file}")
    print("\nDisk structure:")
    print(f"  Boot sector + reserved: sectors 0-{RESERVED_SECTORS - 1}")
    print(
        f"  FAT copies: sectors {RESERVED_SECTORS}-"
        f"{RESERVED_SECTORS + NUM_FATS * FAT_SIZE_SECTORS - 1}"
    )
    print(f"  Data area: sectors {data_start_sector}+")
    print(f"    Cluster 2 (sector {data_start_sector}): Root directory")
    print(f"    Cluster 3 (sector {data_start_sector + SECTORS_PER_CLUSTER}): /init directory")
    print(f"    Cluster 4 (sector {data_start_sector + 2 * SECTORS_PER_CLUSTER}): /bin directory")
    print(f"    Cluster 5 (sector {data_start_sector + 3 * SECTORS_PER_CLUSTER}): /run directory")
    print(f"    Cluster 6 (sector {data_start_sector + 4 * SECTORS_PER_CLUSTER}): /home directory")
    print(f"    Cluster 7 (sector {data_start_sector + 5 * SECTORS_PER_CLUSTER}): /include directory")

    for record in [init_record, syscalls_record] + bin_programs + run_files + home_files:
        print_record_layout(record, data_start_sector)

    print("\nDisk contents:")
    print("  /init - Empty")
    if init_record:
        print_record_contents("/bin", init_record)
    elif not bin_programs and not syscalls_record:
        print("  /bin - Empty")
    for record in bin_programs:
        print_record_contents("/bin", record)
    if syscalls_record:
        print_record_contents("/include", syscalls_record)
    if run_files:
        for record in run_files:
            print_record_contents("/run", record)
    else:
        print("  /run - Empty")
    if home_files:
        for record in home_files:
            print_record_contents("/home", record)
    else:
        print("  /home - Empty")


if __name__ == "__main__":
    main()
