#include "kernel.h"
#include "vga.h"
#include "fat32.h"
#include "ata.h"

/* Current working directory */
static char current_directory[FAT32_MAX_PATH] = "/";

/* Command implementations */
void cmd_ls(const char *args);
void cmd_cd(const char *args);
void cmd_cat(const char *args);
void cmd_pwd(void);
void cmd_mkdir(const char *args);
void cmd_rmdir(const char *args);
void cmd_rm(const char *args);
void cmd_touch(const char *args);
void cmd_cp(const char *args);
void cmd_mv(const char *args);
void cmd_du(const char *args);
void cmd_mount(const char *args);
void cmd_umount(void);
void cmd_fsinfo(void);
void cmd_drives(void);
void cmd_hexdump(const char *args);

/* Helper functions */
static char* resolve_path(const char *path);
static void print_file_listing(struct fat32_dir_entry *entry, int show_details);
static void format_file_size(uint32_t size, char *buffer);
static void print_file_permissions(uint8_t attributes);

void cmd_ls(const char *args) {
    char *path = resolve_path(args && strlen(args) > 0 ? args : current_directory);
    int show_details = 0;
    int show_all = 0;
    
    // Simple argument parsing
    if (args && strstr(args, "-l")) {
        show_details = 1;
    }
    if (args && strstr(args, "-a")) {
        show_all = 1;
    }
    
    if (!fat32_exists(path)) {
        vga_writestring("ls: cannot access '");
        vga_writestring(path);
        vga_writestring("': No such file or directory\n");
        kfree(path);
        return;
    }
    
    if (!fat32_is_directory(path)) {
        // Single file listing
        vga_writestring(path);
        vga_putchar('\n');
        kfree(path);
        return;
    }
    
    struct fat32_file *dir = fat32_opendir(path);
    if (!dir) {
        vga_writestring("ls: cannot open directory '");
        vga_writestring(path);
        vga_writestring("'\n");
        kfree(path);
        return;
    }
    
    if (show_details) {
        vga_writestring("total files in ");
        vga_writestring(path);
        vga_writestring(":\n");
    }
    
    struct fat32_dir_entry *entry;
    int count = 0;
    
    while ((entry = fat32_readdir(dir)) != NULL) {
        // Skip hidden files unless -a is specified
        if (!show_all && (entry->attributes & FAT32_ATTR_HIDDEN)) {
            continue;
        }
        
        // Skip volume labels
        if (entry->attributes & FAT32_ATTR_VOLUME_LABEL) {
            continue;
        }
        
        print_file_listing(entry, show_details);
        count++;
    }
    
    fat32_closedir(dir);
    
    if (count == 0) {
        vga_writestring("(empty directory)\n");
    } else if (show_details) {
        vga_writestring("total: ");
        print_dec(count);
        vga_writestring(" items\n");
    }
    
    kfree(path);
}

void cmd_cd(const char *args) {
    if (!args || strlen(args) == 0) {
        strcpy(current_directory, "/");
        return;
    }
    
    char *new_path = resolve_path(args);
    
    if (!fat32_exists(new_path)) {
        vga_writestring("cd: no such file or directory: ");
        vga_writestring(new_path);
        vga_putchar('\n');
        kfree(new_path);
        return;
    }
    
    if (!fat32_is_directory(new_path)) {
        vga_writestring("cd: not a directory: ");
        vga_writestring(new_path);
        vga_putchar('\n');
        kfree(new_path);
        return;
    }
    
    strcpy(current_directory, new_path);
    kfree(new_path);
}

void cmd_cat(const char *args) {
    if (!args || strlen(args) == 0) {
        vga_writestring("cat: missing file operand\n");
        return;
    }
    
    char *path = resolve_path(args);
    
    if (!fat32_exists(path)) {
        vga_writestring("cat: ");
        vga_writestring(path);
        vga_writestring(": No such file or directory\n");
        kfree(path);
        return;
    }
    
    if (fat32_is_directory(path)) {
        vga_writestring("cat: ");
        vga_writestring(path);
        vga_writestring(": Is a directory\n");
        kfree(path);
        return;
    }
    
    struct fat32_file *file = fat32_open(path, "r");
    if (!file) {
        vga_writestring("cat: cannot open ");
        vga_writestring(path);
        vga_putchar('\n');
        kfree(path);
        return;
    }
    
    char *buffer = (char*)kmalloc(1024);
    if (!buffer) {
        vga_writestring("cat: out of memory\n");
        fat32_close(file);
        kfree(path);
        return;
    }
    
    size_t bytes_read;
    while ((bytes_read = fat32_read(file, buffer, 1023)) > 0) {
        buffer[bytes_read] = '\0';
        vga_writestring(buffer);
    }
    
    vga_putchar('\n');
    
    kfree(buffer);
    fat32_close(file);
    kfree(path);
}

void cmd_pwd(void) {
    vga_writestring(current_directory);
    vga_putchar('\n');
}

void cmd_mkdir(const char *args) {
    if (!args || strlen(args) == 0) {
        vga_writestring("mkdir: missing operand\n");
        return;
    }
    
    char *path = resolve_path(args);
    
    if (fat32_exists(path)) {
        vga_writestring("mkdir: cannot create directory '");
        vga_writestring(path);
        vga_writestring("': File exists\n");
        kfree(path);
        return;
    }
    
    if (fat32_mkdir(path) != FAT32_SUCCESS) {
        vga_writestring("mkdir: cannot create directory '");
        vga_writestring(path);
        vga_writestring("'\n");
    } else {
        vga_writestring("Directory created: ");
        vga_writestring(path);
        vga_putchar('\n');
    }
    
    kfree(path);
}

void cmd_touch(const char *args) {
    if (!args || strlen(args) == 0) {
        vga_writestring("touch: missing file operand\n");
        return;
    }
    
    char *path = resolve_path(args);
    
    if (fat32_exists(path)) {
        vga_writestring("File already exists: ");
        vga_writestring(path);
        vga_putchar('\n');
        kfree(path);
        return;
    }
    
    if (fat32_create(path) != FAT32_SUCCESS) {
        vga_writestring("touch: cannot create '");
        vga_writestring(path);
        vga_writestring("'\n");
    } else {
        vga_writestring("File created: ");
        vga_writestring(path);
        vga_putchar('\n');
    }
    
    kfree(path);
}

void cmd_rm(const char *args) {
    if (!args || strlen(args) == 0) {
        vga_writestring("rm: missing operand\n");
        return;
    }
    
    char *path = resolve_path(args);
    
    if (!fat32_exists(path)) {
        vga_writestring("rm: cannot remove '");
        vga_writestring(path);
        vga_writestring("': No such file or directory\n");
        kfree(path);
        return;
    }
    
    if (fat32_is_directory(path)) {
        vga_writestring("rm: cannot remove '");
        vga_writestring(path);
        vga_writestring("': Is a directory (use rmdir)\n");
        kfree(path);
        return;
    }
    
    if (fat32_delete(path) != FAT32_SUCCESS) {
        vga_writestring("rm: cannot remove '");
        vga_writestring(path);
        vga_writestring("'\n");
    } else {
        vga_writestring("File removed: ");
        vga_writestring(path);
        vga_putchar('\n');
    }
    
    kfree(path);
}

void cmd_mount(const char *args) {
    uint32_t drive_num = 0;
    
    if (args && strlen(args) > 0) {
        // Simple drive number parsing
        if (args[0] >= '0' && args[0] <= '3') {
            drive_num = args[0] - '0';
        }
    }
    
    vga_writestring("Mounting drive ");
    print_dec(drive_num);
    vga_writestring("...\n");
    
    if (fat32_init(drive_num) != FAT32_SUCCESS) {
        vga_writestring("Failed to initialize FAT32 driver\n");
        return;
    }
    
    if (fat32_mount() != FAT32_SUCCESS) {
        vga_writestring("Failed to mount FAT32 file system\n");
        return;
    }
    
    vga_writestring("File system mounted successfully\n");
    strcpy(current_directory, "/");
}

void cmd_umount(void) {
    fat32_sync(); // Flush any pending writes
    fat32_unmount();
    vga_writestring("File system unmounted\n");
    strcpy(current_directory, "/");
}

void cmd_fsinfo(void) {
    fat32_print_fs_info();
}

void cmd_drives(void) {
    ata_print_drives();
}

void cmd_du(const char *args) {
    char *path = resolve_path(args && strlen(args) > 0 ? args : current_directory);
    
    if (!fat32_exists(path)) {
        vga_writestring("du: cannot access '");
        vga_writestring(path);
        vga_writestring("': No such file or directory\n");
        kfree(path);
        return;
    }
    
    if (!fat32_is_directory(path)) {
        uint32_t size = fat32_get_file_size(path);
        char size_str[32];
        format_file_size(size, size_str);
        vga_writestring(size_str);
        vga_putchar('\t');
        vga_writestring(path);
        vga_putchar('\n');
        kfree(path);
        return;
    }
    
    // Simple directory size calculation
    struct fat32_file *dir = fat32_opendir(path);
    if (!dir) {
        vga_writestring("du: cannot open directory\n");
        kfree(path);
        return;
    }
    
    uint64_t total_size = 0;
    int file_count = 0;
    struct fat32_dir_entry *entry;
    
    while ((entry = fat32_readdir(dir)) != NULL) {
        if (entry->attributes & (FAT32_ATTR_VOLUME_LABEL)) {
            continue;
        }
        
        if (!(entry->attributes & FAT32_ATTR_DIRECTORY)) {
            total_size += entry->file_size;
            file_count++;
        }
    }
    
    fat32_closedir(dir);
    
    char size_str[32];
    format_file_size(total_size, size_str);
    vga_writestring(size_str);
    vga_putchar('\t');
    vga_writestring(path);
    vga_writestring(" (");
    print_dec(file_count);
    vga_writestring(" files)\n");
    
    kfree(path);
}

void cmd_hexdump(const char *args) {
    if (!args || strlen(args) == 0) {
        vga_writestring("hexdump: missing file operand\n");
        return;
    }
    
    char *path = resolve_path(args);
    
    if (!fat32_exists(path)) {
        vga_writestring("hexdump: ");
        vga_writestring(path);
        vga_writestring(": No such file or directory\n");
        kfree(path);
        return;
    }
    
    if (fat32_is_directory(path)) {
        vga_writestring("hexdump: ");
        vga_writestring(path);
        vga_writestring(": Is a directory\n");
        kfree(path);
        return;
    }
    
    struct fat32_file *file = fat32_open(path, "r");
    if (!file) {
        vga_writestring("hexdump: cannot open ");
        vga_writestring(path);
        vga_putchar('\n');
        kfree(path);
        return;
    }
    
    uint8_t buffer[16];
    size_t bytes_read;
    uint32_t offset = 0;
    
    vga_writestring("Offset    Hex                                         ASCII\n");
    vga_writestring("--------  -----------------------------------------------  ----------------\n");
    
    while ((bytes_read = fat32_read(file, buffer, 16)) > 0) {
        // Print offset
        print_hex(offset);
        vga_writestring("  ");
        
        // Print hex bytes
        for (size_t i = 0; i < 16; i++) {
            if (i < bytes_read) {
                if (buffer[i] < 16) vga_putchar('0');
                print_hex(buffer[i]);
                vga_putchar(' ');
            } else {
                vga_writestring("   ");
            }
            if (i == 7) vga_putchar(' '); // Extra space in middle
        }
        
        vga_writestring(" ");
        
        // Print ASCII representation
        for (size_t i = 0; i < bytes_read; i++) {
            char c = buffer[i];
            if (c >= 32 && c <= 126) {
                vga_putchar(c);
            } else {
                vga_putchar('.');
            }
        }
        
        vga_putchar('\n');
        offset += bytes_read;
        
        // Limit output to prevent screen overflow
        if (offset >= 512) {
            vga_writestring("... (truncated, showing first 512 bytes)\n");
            break;
        }
    }
    
    fat32_close(file);
    kfree(path);
}

/* Helper function implementations */
static char* resolve_path(const char *path) {
    if (!path) {
        return kstrdup(current_directory);
    }
    
    if (path[0] == '/') {
        // Absolute path
        return kstrdup(path);
    } else {
        // Relative path
        size_t cwd_len = strlen(current_directory);
        size_t path_len = strlen(path);
        char *result = (char*)kmalloc(cwd_len + path_len + 2);
        
        strcpy(result, current_directory);
        if (current_directory[cwd_len - 1] != '/') {
            strcat(result, "/");
        }
        strcat(result, path);
        
        return result;
    }
}

static void print_file_listing(struct fat32_dir_entry *entry, int show_details) {
    if (show_details) {
        // Print permissions
        print_file_permissions(entry->attributes);
        vga_putchar(' ');
        
        // Print size or <DIR>
        if (entry->attributes & FAT32_ATTR_DIRECTORY) {
            vga_writestring("    <DIR> ");
        } else {
            char size_str[16];
            format_file_size(entry->file_size, size_str);
            // Right-align size in 9 characters
            int len = strlen(size_str);
            for (int i = len; i < 9; i++) {
                vga_putchar(' ');
            }
            vga_writestring(size_str);
            vga_putchar(' ');
        }
    } else {
        // Simple listing - just show directory indicator
        if (entry->attributes & FAT32_ATTR_DIRECTORY) {
            vga_putchar('/');
        } else {
            vga_putchar(' ');
        }
    }
    
    // Print filename
    char *name = fat32_format_filename(entry->name);
    vga_writestring(name);
    
    // Print file attribute indicators
    if (entry->attributes & FAT32_ATTR_READ_ONLY) {
        vga_writestring(" [RO]");
    }
    if (entry->attributes & FAT32_ATTR_HIDDEN) {
        vga_writestring(" [H]");
    }
    if (entry->attributes & FAT32_ATTR_SYSTEM) {
        vga_writestring(" [S]");
    }
    
    vga_putchar('\n');
}

static void format_file_size(uint32_t size, char *buffer) {
    if (size < 1024) {
        // Bytes
        strcpy(buffer, "");
        char temp[16];
        uint32_t temp_size = size;
        int pos = 0;
        if (temp_size == 0) {
            temp[pos++] = '0';
        } else {
            while (temp_size > 0) {
                temp[pos++] = '0' + (temp_size % 10);
                temp_size /= 10;
            }
        }
        // Reverse the string
        for (int i = 0; i < pos; i++) {
            buffer[i] = temp[pos - 1 - i];
        }
        buffer[pos] = 'B';
        buffer[pos + 1] = '\0';
    } else if (size < 1024 * 1024) {
        // Kilobytes
        uint32_t kb = size / 1024;
        strcpy(buffer, "");
        char temp[16];
        int pos = 0;
        while (kb > 0) {
            temp[pos++] = '0' + (kb % 10);
            kb /= 10;
        }
        for (int i = 0; i < pos; i++) {
            buffer[i] = temp[pos - 1 - i];
        }
        buffer[pos] = 'K';
        buffer[pos + 1] = '\0';
    } else {
        // Megabytes
        uint32_t mb = size / (1024 * 1024);
        strcpy(buffer, "");
        char temp[16];
        int pos = 0;
        while (mb > 0) {
            temp[pos++] = '0' + (mb % 10);
            mb /= 10;
        }
        for (int i = 0; i < pos; i++) {
            buffer[i] = temp[pos - 1 - i];
        }
        buffer[pos] = 'M';
        buffer[pos + 1] = '\0';
    }
}

static void print_file_permissions(uint8_t attributes) {
    vga_putchar((attributes & FAT32_ATTR_DIRECTORY) ? 'd' : '-');
    vga_putchar((attributes & FAT32_ATTR_READ_ONLY) ? 'r' : 'w');
    vga_putchar((attributes & FAT32_ATTR_HIDDEN) ? 'h' : '-');
    vga_putchar((attributes & FAT32_ATTR_SYSTEM) ? 's' : '-');
    vga_putchar((attributes & FAT32_ATTR_ARCHIVE) ? 'a' : '-');
}