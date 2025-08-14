#!/usr/bin/env python3
"""
NumOS Disk Image Builder
Creates a FAT32 disk image containing the kernel and other files.
"""

import os
import struct
import sys
from datetime import datetime

class Fat32DiskBuilder:
    def __init__(self, size_mb=32):
        self.size_mb = size_mb
        self.size_bytes = size_mb * 1024 * 1024
        self.sector_size = 512
        self.sectors_per_cluster = 8
        self.reserved_sectors = 32
        self.fat_count = 2
        self.root_cluster = 2
        
        # Calculate derived values
        self.total_sectors = self.size_bytes // self.sector_size
        self.sectors_per_fat = 256  # Sufficient for 32MB
        self.fat_start_sector = self.reserved_sectors
        self.data_start_sector = self.fat_start_sector + (self.fat_count * self.sectors_per_fat)
        self.bytes_per_cluster = self.sector_size * self.sectors_per_cluster
        
        # Initialize disk image
        self.disk_image = bytearray(self.size_bytes)
        self.fat_table = [0] * (self.sectors_per_fat * self.sector_size // 4)
        self.next_free_cluster = 3  # Cluster 2 is root directory
        
        print(f"Creating {size_mb}MB disk image...")
        print(f"Total sectors: {self.total_sectors}")
        print(f"FAT start sector: {self.fat_start_sector}")
        print(f"Data start sector: {self.data_start_sector}")
        
    def create_boot_sector(self):
        """Create FAT32 boot sector"""
        boot_sector = bytearray(512)
        
        # Jump instruction
        boot_sector[0:3] = [0xEB, 0x58, 0x90]
        
        # OEM Name
        boot_sector[3:11] = b"NumOS   "
        
        # BPB (BIOS Parameter Block)
        struct.pack_into("<H", boot_sector, 11, self.sector_size)          # Bytes per sector
        struct.pack_into("<B", boot_sector, 13, self.sectors_per_cluster)  # Sectors per cluster
        struct.pack_into("<H", boot_sector, 14, self.reserved_sectors)     # Reserved sectors
        struct.pack_into("<B", boot_sector, 16, self.fat_count)            # Number of FATs
        struct.pack_into("<H", boot_sector, 17, 0)                         # Root entries (0 for FAT32)
        struct.pack_into("<H", boot_sector, 19, 0)                         # Small sector count (0 for FAT32)
        struct.pack_into("<B", boot_sector, 21, 0xF8)                      # Media descriptor
        struct.pack_into("<H", boot_sector, 22, 0)                         # Sectors per FAT (0 for FAT32)
        struct.pack_into("<H", boot_sector, 24, 63)                        # Sectors per track
        struct.pack_into("<H", boot_sector, 26, 255)                       # Number of heads
        struct.pack_into("<L", boot_sector, 28, 0)                         # Hidden sectors
        struct.pack_into("<L", boot_sector, 32, self.total_sectors)        # Large sector count
        
        # FAT32 Extended BPB
        struct.pack_into("<L", boot_sector, 36, self.sectors_per_fat)      # Sectors per FAT32
        struct.pack_into("<H", boot_sector, 40, 0)                         # Extended flags
        struct.pack_into("<H", boot_sector, 42, 0)                         # Filesystem version
        struct.pack_into("<L", boot_sector, 44, self.root_cluster)         # Root cluster
        struct.pack_into("<H", boot_sector, 48, 1)                         # FSInfo sector
        struct.pack_into("<H", boot_sector, 50, 6)                         # Backup boot sector
        
        # Reserved bytes (12 bytes of zeros)
        for i in range(52, 64):
            boot_sector[i] = 0
            
        # Extended boot signature fields
        struct.pack_into("<B", boot_sector, 64, 0x80)                      # Drive number
        struct.pack_into("<B", boot_sector, 65, 0)                         # Reserved
        struct.pack_into("<B", boot_sector, 66, 0x29)                      # Boot signature
        struct.pack_into("<L", boot_sector, 67, 0x12345678)                # Volume ID
        boot_sector[71:82] = b"NumOS FAT32"                                # Volume label
        boot_sector[82:90] = b"FAT32   "                                   # Filesystem type
        
        # Boot signature
        struct.pack_into("<H", boot_sector, 510, 0xAA55)
        
        # Write boot sector
        self.disk_image[0:512] = boot_sector
        print("Created boot sector")
        
    def create_fsinfo(self):
        """Create FSInfo sector"""
        fsinfo = bytearray(512)
        
        # FSInfo signatures
        struct.pack_into("<L", fsinfo, 0, 0x41615252)      # Lead signature
        struct.pack_into("<L", fsinfo, 484, 0x61417272)    # Struct signature
        struct.pack_into("<L", fsinfo, 488, 0xFFFFFFFF)    # Free cluster count (unknown)
        struct.pack_into("<L", fsinfo, 492, 3)             # Next free cluster
        struct.pack_into("<H", fsinfo, 510, 0xAA55)        # Trail signature
        
        # Write FSInfo sector
        self.disk_image[512:1024] = fsinfo
        print("Created FSInfo sector")
        
    def initialize_fat(self):
        """Initialize FAT table"""
        # Mark first few entries
        self.fat_table[0] = 0x0FFFFF00 | 0xF8  # Media descriptor
        self.fat_table[1] = 0x0FFFFFFF          # End of chain
        self.fat_table[2] = 0x0FFFFFFF          # Root directory (single cluster)
        
        print("Initialized FAT table")
        
    def write_fat_tables(self):
        """Write FAT tables to disk"""
        fat_data = bytearray()
        for entry in self.fat_table:
            fat_data.extend(struct.pack("<L", entry))
            
        # Pad to sector boundary
        while len(fat_data) % self.sector_size != 0:
            fat_data.append(0)
            
        # Write both FAT copies
        for i in range(self.fat_count):
            start_offset = (self.fat_start_sector + i * self.sectors_per_fat) * self.sector_size
            end_offset = start_offset + len(fat_data)
            self.disk_image[start_offset:end_offset] = fat_data
            
        print(f"Written {self.fat_count} FAT tables")
        
    def cluster_to_sector(self, cluster):
        """Convert cluster number to sector number"""
        return self.data_start_sector + (cluster - 2) * self.sectors_per_cluster
        
    def allocate_cluster(self):
        """Allocate a new cluster"""
        cluster = self.next_free_cluster
        self.fat_table[cluster] = 0x0FFFFFFF  # Mark as end of chain
        self.next_free_cluster += 1
        return cluster
        
    def create_root_directory(self):
        """Create root directory"""
        root_data = bytearray(self.bytes_per_cluster)
        
        # Create volume label entry
        volume_entry = bytearray(32)
        volume_entry[0:11] = b"NumOS FAT32"
        volume_entry[11] = 0x08  # Volume label attribute
        root_data[0:32] = volume_entry
        
        # Write root directory to disk
        root_sector = self.cluster_to_sector(self.root_cluster)
        start_offset = root_sector * self.sector_size
        end_offset = start_offset + self.bytes_per_cluster
        self.disk_image[start_offset:end_offset] = root_data
        
        print("Created root directory")
        
    def add_file_to_directory(self, filename, file_data, directory_cluster=None):
        """Add a file to a directory"""
        if directory_cluster is None:
            directory_cluster = self.root_cluster
            
        # Format filename for FAT32 (8.3 format)
        name, ext = os.path.splitext(filename.upper())
        name = name[:8].ljust(8)
        ext = ext[1:4].ljust(3) if ext else "   "
        fat_filename = (name + ext).encode('ascii')
        
        print(f"Adding file: {filename} -> {fat_filename}")
        
        # Allocate clusters for file data
        file_size = len(file_data)
        clusters_needed = (file_size + self.bytes_per_cluster - 1) // self.bytes_per_cluster
        
        first_cluster = 0
        current_cluster = 0
        
        if file_size > 0:
            first_cluster = self.allocate_cluster()
            current_cluster = first_cluster
            
            # Allocate additional clusters if needed
            for i in range(1, clusters_needed):
                next_cluster = self.allocate_cluster()
                self.fat_table[current_cluster] = next_cluster
                current_cluster = next_cluster
                
        # Write file data to clusters
        for i in range(clusters_needed):
            cluster = first_cluster + i
            start_offset = self.cluster_to_sector(cluster) * self.sector_size
            
            chunk_start = i * self.bytes_per_cluster
            chunk_end = min((i + 1) * self.bytes_per_cluster, file_size)
            chunk_data = file_data[chunk_start:chunk_end]
            
            # Pad chunk to cluster size
            chunk_data += bytes(self.bytes_per_cluster - len(chunk_data))
            
            self.disk_image[start_offset:start_offset + self.bytes_per_cluster] = chunk_data
            
        # Read directory cluster
        dir_sector = self.cluster_to_sector(directory_cluster)
        dir_offset = dir_sector * self.sector_size
        dir_data = bytearray(self.disk_image[dir_offset:dir_offset + self.bytes_per_cluster])
        
        # Find empty directory entry
        for i in range(0, len(dir_data), 32):
            if dir_data[i] == 0x00 or dir_data[i] == 0xE5:  # Empty or deleted entry
                entry = bytearray(32)
                entry[0:11] = fat_filename
                entry[11] = 0x20  # Archive attribute
                
                # Creation time and date (simplified)
                now = datetime.now()
                time_val = (now.hour << 11) | (now.minute << 5) | (now.second // 2)
                date_val = ((now.year - 1980) << 9) | (now.month << 5) | now.day
                
                struct.pack_into("<H", entry, 14, time_val)  # Creation time
                struct.pack_into("<H", entry, 16, date_val)  # Creation date
                struct.pack_into("<H", entry, 18, date_val)  # Last access date
                struct.pack_into("<H", entry, 20, first_cluster >> 16)  # First cluster high
                struct.pack_into("<H", entry, 22, time_val)  # Last write time
                struct.pack_into("<H", entry, 24, date_val)  # Last write date
                struct.pack_into("<H", entry, 26, first_cluster & 0xFFFF)  # First cluster low
                struct.pack_into("<L", entry, 28, file_size)  # File size
                
                dir_data[i:i+32] = entry
                
                # Mark end of directory
                if i + 32 < len(dir_data) and dir_data[i + 32] != 0xE5:
                    dir_data[i + 32] = 0x00
                    
                break
        else:
            raise Exception(f"Directory full, cannot add {filename}")
            
        # Write directory back
        self.disk_image[dir_offset:dir_offset + self.bytes_per_cluster] = dir_data
        
        print(f"Added {filename} ({file_size} bytes, {clusters_needed} clusters)")
        
    def add_file(self, local_path, disk_filename=None):
        """Add a file from the local filesystem to the disk image"""
        if not os.path.exists(local_path):
            print(f"Warning: {local_path} not found, skipping")
            return
            
        if disk_filename is None:
            disk_filename = os.path.basename(local_path)
            
        with open(local_path, 'rb') as f:
            file_data = f.read()
            
        self.add_file_to_directory(disk_filename, file_data)
        
    def save(self, output_path):
        """Save the disk image to a file"""
        with open(output_path, 'wb') as f:
            f.write(self.disk_image)
        print(f"Saved disk image to {output_path} ({self.size_mb}MB)")

def main():
    if len(sys.argv) < 2:
        print("Usage: build_disk.py <output_image> [kernel_path] [additional_files...]")
        sys.exit(1)
        
    output_image = sys.argv[1]
    kernel_path = sys.argv[2] if len(sys.argv) > 2 else "build/kernel.bin"
    additional_files = sys.argv[3:] if len(sys.argv) > 3 else []
    
    # Create disk image
    builder = Fat32DiskBuilder(32)  # 32MB disk
    
    # Create filesystem structure
    builder.create_boot_sector()
    builder.create_fsinfo()
    builder.initialize_fat()
    builder.create_root_directory()
    
    # Add kernel
    builder.add_file(kernel_path, "KERNEL.BIN")
    
    # Add additional files
    for file_path in additional_files:
        builder.add_file(file_path)
        
    # Add some sample configuration files
    config_data = b"""# NumOS Configuration File
kernel_debug=1
heap_size=16777216
timer_freq=100
vga_mode=text

# Boot options
auto_mount_fat32=1
show_boot_info=1
"""
    builder.add_file_to_directory("CONFIG.TXT", config_data)
    
    readme_data = b"""NumOS - 64-bit Operating System

This is a demonstration FAT32 filesystem containing:
- KERNEL.BIN: The NumOS kernel binary
- CONFIG.TXT: System configuration file
- README.TXT: This file

Commands to try:
- ls: List files
- cat filename: Display file contents
- help: Show all available commands

Enjoy exploring NumOS!
"""
    builder.add_file_to_directory("README.TXT", readme_data)
    
    # Write FAT tables
    builder.write_fat_tables()
    
    # Save to file
    builder.save(output_image)
    
    print("\nDisk image creation complete!")
    print(f"Boot the image with: qemu-system-x86_64 -drive file={output_image},format=raw")

if __name__ == "__main__":
    main()