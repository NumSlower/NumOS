/* NumOS — kernel.c — Kernel Entry */
#include "libs/types.h"
#include "libs/kprintf.h"
#include "libs/string.h"
#include "kernel/multiboot2.h"
#include "cpu/gdt.h"
#include "cpu/paging.h"
#include "cpu/idt.h"
#include "cpu/isr.h"
#include "kernel/pic.h"
#include "cpu/irq.h"
#include "kernel/nmi.h"
#include "kernel/pmm.h"
#include "kernel/vmm.h"
#include "cpu/heap.h"
#include "kernel/panic.h"
#include "kernel/spinlock.h"
#include "libs/kdebug.h"
#include "cpu/cpuid.h"
#include "cpu/sse.h"
#include "drivers/serial.h"
#include "drivers/vga.h"
#include "drivers/bga.h"
#include "drivers/keyboard.h"
#include "drivers/pit.h"
#include "drivers/acpi.h"
#include "drivers/apic.h"
#include "drivers/ioapic.h"
#include "drivers/pci.h"
#include "drivers/block.h"
#include "drivers/ata.h"
#include "tty/tty.h"
#include "fs/vfs.h"
#include "fs/fat32.h"
#include "process/process.h"
#include "process/scheduler.h"
#include "process/thread.h"
#include "process/userprog.h"
#include "kernel/syscall.h"
#include "libs/io.h"

static void print_ok(const char* name) {
    vga_set_color(WHITE, BLACK);
    kprintf("Initializing %s", name);
    int len = strlen(name) + 13; // "Initializing "
    for (int i = len; i < 40; i++) kprintf(".");
    kprintf(" ");
    vga_set_color(LIGHT_GREEN, BLACK);
    kprintf("[OK]\n");
    vga_set_color(WHITE, BLACK);
}

static void print_fail(const char* name) {
    vga_set_color(WHITE, BLACK);
    kprintf("Initializing %s", name);
    int len = strlen(name) + 13;
    for (int i = len; i < 40; i++) kprintf(".");
    kprintf(" ");
    vga_set_color(LIGHT_RED, BLACK);
    kprintf("[FAIL]\n");
    vga_set_color(WHITE, BLACK);
}

static void print_skip(const char* name) {
    vga_set_color(WHITE, BLACK);
    kprintf("Initializing %s", name);
    int len = strlen(name) + 13;
    for (int i = len; i < 40; i++) kprintf(".");
    kprintf(" ");
    vga_set_color(YELLOW, BLACK);
    kprintf("[SKIP]\n");
    vga_set_color(WHITE, BLACK);
}

static uint32_t vfs_root_entries = 0;

static uint64_t atou64(const char* s);
static void u64_to_dec(uint64_t v, char* out, uint32_t size);

/* Syscall execution depth: incremented by the int-0x80 stub on entry and
 * decremented on return. While nonzero, the PIT must not preempt: the
 * interrupted frame's CS:RIP is the user syscall site, so privilege alone
 * cannot distinguish "user code" from "kernel code running on behalf of a
 * syscall", and switching away mid-syscall corrupts dispatch state.
 * NOTE: depth stays elevated while a syscall is suspended by a yield, so
 * preemption granularity is syscall boundaries (cooperative rotation
 * inside syscalls still works via scheduler_yield). */
volatile uint64_t kernel_exec_depth = 0;

static void kernel_pit_tick(registers_t* regs) {
    bool from_ring3 = (regs->cs & 3) != 0 &&
                      regs->rip >= USER_VA_BASE && regs->rip < USER_VA_LIMIT;
    if (scheduler_tick() && kernel_exec_depth == 0) {
        scheduler_preempt_from_irq(from_ring3);
    }
}

static void kernel_tick_wrapper(registers_t* regs) {
    kernel_pit_tick(regs);
}

static void demo_worker_a(void) {
    for (uint32_t i = 1; i <= 5; i++) {
        kprintf("sched: worker A iteration %u\n", i);
        scheduler_yield();
    }
}

static void demo_worker_b(void) {
    for (uint32_t i = 1; i <= 5; i++) {
        kprintf("sched: worker B iteration %u\n", i);
        scheduler_yield();
    }
}

static int vfs_list_cb_print(const struct vfs_stat* st, void* ctx) {
    (void)ctx;
    kprintf("VFS: /%s %s (%lu bytes)\n", st->name,
            st->is_dir ? "DIR " : "FILE", st->size);
    vfs_root_entries++;
    return VFS_OK;
}

static void fs_run_selftest(void) {
    struct vfs_stat st;
    if (vfs_stat("/NUMOS.CFG", &st) == VFS_OK) {
        struct vfs_file* f = NULL;
        if (vfs_open("/NUMOS.CFG", VFS_O_RDONLY, &f) == VFS_OK) {
            char cfg[128];
            size_t got = 0;
            memset(cfg, 0, sizeof(cfg));
            vfs_read(f, cfg, sizeof(cfg) - 1, &got);
            vfs_close(f);
            cfg[got] = '\0';
            kprintf("FS: /NUMOS.CFG (%lu bytes):\n", (uint64_t)got);
            vga_set_color(WHITE, BLACK);
            kprintf("%s", cfg);
        } else {
            kprintf("FS: /NUMOS.CFG open FAILED\n");
        }
    } else {
        kprintf("FS: /NUMOS.CFG not present on disk\n");
    }

    uint64_t boot_count = 1;
    if (vfs_stat("/BOOTCNT.DAT", &st) == VFS_OK && !st.is_dir) {
        struct vfs_file* f = NULL;
        if (vfs_open("/BOOTCNT.DAT", VFS_O_RDONLY, &f) == VFS_OK) {
            char buf[32];
            size_t got = 0;
            vfs_read(f, buf, sizeof(buf) - 1, &got);
            vfs_close(f);
            buf[got] = '\0';
            const char* p = strstr(buf, "boot_count=");
            if (p) boot_count = atou64(p + 11) + 1;
        }
    }
    {
        struct vfs_file* f = NULL;
        if (vfs_open("/BOOTCNT.DAT", VFS_O_WRONLY | VFS_O_CREAT | VFS_O_TRUNC, &f) == VFS_OK) {
            char buf[32];
            strcpy(buf, "boot_count=");
            u64_to_dec(boot_count, buf + 11, sizeof(buf) - 11);
            size_t wrote = 0;
            vfs_write(f, buf, strlen(buf), &wrote);
            vfs_close(f);
        }
        kprintf("FS: persistence boot count %lu\n", boot_count);
    }

    {
        const char* msg = "NumOS FAT32 write test - phase 8\n";
        struct vfs_file* f = NULL;
        if (vfs_open("/TEST.TXT", VFS_O_WRONLY | VFS_O_CREAT | VFS_O_TRUNC, &f) == VFS_OK) {
            size_t wrote = 0;
            vfs_write(f, msg, strlen(msg), &wrote);
            vfs_close(f);

            f = NULL;
            char rb[64];
            size_t got = 0;
            memset(rb, 0, sizeof(rb));
            if (vfs_open("/TEST.TXT", VFS_O_RDONLY, &f) == VFS_OK) {
                vfs_read(f, rb, sizeof(rb) - 1, &got);
                vfs_close(f);
            }
            rb[got] = '\0';
            if (got == strlen(msg) && strcmp(rb, msg) == 0) {
                kprintf("FS: /TEST.TXT write/readback OK\n");
            } else {
                kprintf("FS: /TEST.TXT readback MISMATCH (got %lu bytes)\n", (uint64_t)got);
            }
        } else {
            kprintf("FS: /TEST.TXT create FAILED\n");
        }
    }
    vfs_sync();
}

static uint64_t atou64(const char* s) {
    uint64_t v = 0;
    while (*s >= '0' && *s <= '9') {
        v = v * 10 + (uint64_t)(*s - '0');
        s++;
    }
    return v;
}

static void u64_to_dec(uint64_t v, char* out, uint32_t size) {
    char tmp[24];
    uint32_t i = 0;
    if (v == 0) tmp[i++] = '0';
    while (v > 0 && i < sizeof(tmp)) {
        tmp[i++] = (char)('0' + (v % 10));
        v /= 10;
    }
    uint32_t n = 0;
    while (i > 0 && n < size - 1) out[n++] = tmp[--i];
    out[n] = '\0';
}

void kernel_main(uint32_t magic, uint64_t mb2_info_addr) {
    vga_init();
    serial_init();
    kprintf_register_putchar(serial_putchar_backend);
    kprintf_register_putchar(vga_putchar_backend);
    
    vga_set_color(LIGHT_CYAN, BLACK);
    kprintf("NumOS v0.1 - 64-bit Kernel\n");
    kprintf("===========================\n");
    vga_set_color(WHITE, BLACK);
    
#ifdef KDEBUG
    vga_set_color(YELLOW, BLACK);
    kprintf("*** DEBUG BUILD ***\n");
    vga_set_color(WHITE, BLACK);
#endif
    
    if (magic != 0x36D76289) {
        kpanic("Invalid Multiboot2 magic!");
    }
    
    mb2_parse(mb2_info_addr);
    print_ok("Multiboot2 Info");
    kdebug("Multiboot2 info at 0x%lx", mb2_info_addr);
    {
        uint64_t total_mem = mb2_get_total_memory();
        kprintf("Total memory: %lu MB\n", total_mem / (1024 * 1024));
    }
    {
        struct mb2_tag_framebuffer* fb = mb2_get_framebuffer();
        if (fb && fb->framebuffer_addr) {
            kprintf("Framebuffer: %lux%lux%u at 0x%lx (pitch %u, type %u)\n",
                    fb->framebuffer_width, fb->framebuffer_height,
                    fb->framebuffer_bpp, fb->framebuffer_addr, fb->framebuffer_pitch,
                    fb->framebuffer_type);
        } else {
            kprintf("Framebuffer: not provided by bootloader (text mode)\n");
        }
    }
    print_ok("Serial");
    kdebug("COM1 @ 0x3F8, 115200 baud, 8N1");
    
    gdt_init();
    print_ok("GDT");
    kdebug("GDT: 7 entries (null, kcode64, kdata, udata, ucode64, tss[2])");
    print_ok("TSS");
    kdebug("TSS: 3 IST stacks (4 KB each)");
    
    paging_init();
    print_ok("Paging");
    kdebug("Paging: EFER.NXE enabled, CR0.WP enabled");
    kdebug("Boot identity map: 4 GB (2 MB huge pages)");
    
    idt_init();
    exceptions_init();
    print_ok("IDT");
    kdebug("IDT: 256 entries loaded");
    print_ok("Exceptions");
    kdebug("Exceptions: vectors 0-31 registered (IST: NMI=1, DF=2, MC=3)");
    
    pic_init();
    print_ok("PIC");
    kdebug("PIC: remapped IRQ 0-15 -> vectors 32-47");
    
    irq_init();
    print_ok("IRQs");
    kdebug("IRQ: 16 hardware interrupt lines ready");
    
    cpuid_init();
    print_ok("CPU Features");
    kdebug("CPUID max leaf: basic=0x%x", 0); /* cpuid_init already prints vendor/brand */
    kdebug("Features: APIC=%s MSR=%s SSE=%s SSE2=%s FPU=%s TSC=%s PAE=%s NX=%s",
           cpuid_has_apic() ? "yes" : "no",
           cpuid_has_msr() ? "yes" : "no",
           cpuid_has_sse() ? "yes" : "no",
           cpuid_has_sse2() ? "yes" : "no",
           cpuid_has_fpu() ? "yes" : "no",
           cpuid_has_tsc() ? "yes" : "no",
           cpuid_has_pae() ? "yes" : "no",
           cpuid_has_nx() ? "yes" : "no");
    kdebug("Features: x2APIC=%s 1GB-pages=%s long-mode=%s",
           cpuid_has_x2apic() ? "yes" : "no",
           cpuid_has_1gb_pages() ? "yes" : "no",
           cpuid_has_long_mode() ? "yes" : "no");
    
    sse_init();
    print_ok("FPU/SSE");
    kdebug("FPU: x87 initialized (fninit)");
    kdebug("SSE: CR0.EM=0 CR0.MP=1 CR0.NE=1 CR4.OSFXSR=1 CR4.OSXMMEXCPT=1");
    kdebug("MXCSR: 0x1F80 (default mask)");
    
    nmi_init();
    print_ok("NMI");
    kdebug("NMI: unmasked (port 0x70 bit 7 cleared)");
    
    void* rsdp = mb2_get_rsdp();
    if (rsdp) {
        acpi_init(rsdp);
        print_ok("ACPI Tables");
        kdebug("RSDP at %p", rsdp);
        kdebug("LAPIC base: 0x%lx", acpi_get_lapic_base());
        kdebug("I/O APIC base: 0x%lx (ID %u, GSI base %u)",
               acpi_get_ioapic_base(), acpi_get_ioapic_id(), acpi_get_ioapic_gsi_base());
        kdebug("CPU count (MADT): %u", acpi_get_cpu_count());
        {
            uint8_t iso_count = 0;
            struct acpi_iso* isos = acpi_get_isos(&iso_count);
            for (uint32_t i = 0; i < iso_count; i++)
                kprintf("ISO: IRQ %u -> GSI %u\n", isos[i].source, isos[i].gsi);
            kdebug("Interrupt source overrides: %u", iso_count);
            for (uint8_t i = 0; i < iso_count; i++) {
                kdebug("  ISO[%u]: bus=%u src=%u -> GSI %u (flags 0x%x)",
                       i, isos[i].bus, isos[i].source, isos[i].gsi, isos[i].flags);
            }
        }
    } else {
        print_fail("ACPI Tables");
        kdebug("RSDP: not found in Multiboot2 tags");
    }
    
    if (apic_init()) {
        print_ok("APIC");
        kdebug("Local APIC enabled at 0x%lx, ID=%u", acpi_get_lapic_base(), apic_get_id());
        kdebug("SVR=0x1FF (vector 0xFF), TPR=0, LVT_ERROR=0xFE");
    } else {
        print_fail("APIC");
        kdebug("APIC: not available or no base address");
    }
    
    ioapic_init();
    print_ok("I/O APIC");
    kdebug("I/O APIC: all entries masked on init");

    if (pci_init()) {
        print_ok("PCI");
        kprintf("PCI: %lu device(s), %lu bus(es) scanned\n",
                pci_get_device_count(), pci_get_bus_count());
        for (uint32_t i = 0; i < pci_get_device_count(); i++) {
            const struct pci_device* d = pci_get_device(i);
            kprintf("PCI: %02x:%02x.%u %04x:%04x class %02x%02x (%s)\n",
                    d->bus, d->device, d->function,
                    d->vendor_id, d->device_id,
                    d->class_code, d->subclass, pci_class_name(d->class_code));
        }
    } else {
        print_skip("PCI");
        kdebug("PCI: not available (no config-space response)");
    }

    pit_init(1000);
    print_ok("PIT");
    kdebug("PIT: channel 0, rate generator mode, 1000 Hz");
    kdebug("PIT: divisor=%u (base 1193182 Hz)", 1193182 / 1000);
    
    pmm_init(mb2_get_mmap(), mb2_info_addr);
    print_ok("PMM");
    {
        uint64_t free_frames  = pmm_get_free_count();
        uint64_t total_frames = pmm_get_total_count();
        kprintf("PMM: %llu / %llu frames free\n", free_frames, total_frames);
        kdebug("PMM: %lu / %lu frames free (%lu MB / %lu MB)",
               free_frames, total_frames,
               (free_frames * 4096) / (1024*1024),
               (total_frames * 4096) / (1024*1024));
    }
    
    vmm_init();
    print_ok("VMM");
    {
        uint64_t cr3;
        __asm__ volatile("mov %%cr3, %0" : "=r"(cr3));
        kdebug("VMM: PML4 at 0x%lx (from CR3)", cr3);
    }
    
    heap_init(0x400000, 0x100000);
    print_ok("Heap");

    pit_set_tick_callback(kernel_tick_wrapper);
    tty_init();
    print_ok("TTY");
    process_init();
    scheduler_init();
    syscall_init();
    print_ok("Scheduler");

    block_init();
    if (ata_init()) {
        print_ok("Storage");
        for (uint32_t i = 0; i < block_get_device_count(); i++) {
            struct block_device* dev = block_get_device(i);
            kprintf("Storage: %s %lu MB (%lu sectors x %u bytes)\n",
                    dev->name, (uint64_t)(dev->capacity_bytes / (1024 * 1024)),
                    dev->sector_count, dev->sector_size);
        }

        struct block_device* dev = block_get_device(0);
        static uint8_t sector_buf[ATA_SECTOR_SIZE];
        int rc = block_read_blocks(dev, 0, 1, sector_buf);
        if (rc == BLOCK_OK) {
            kprintf("Storage: %s sector 0 read OK (sig %02x%02x)\n",
                    dev->name, sector_buf[510], sector_buf[511]);
        } else {
            kprintf("Storage: %s sector 0 read FAILED (rc=%d)\n", dev->name, rc);
        }

        uint64_t last_lba = dev->sector_count - 1;
        rc = block_read_blocks(dev, last_lba, 1, sector_buf);
        if (rc == BLOCK_OK) {
            static uint8_t original[ATA_SECTOR_SIZE];
            memcpy(original, sector_buf, ATA_SECTOR_SIZE);
            for (uint32_t i = 0; i < ATA_SECTOR_SIZE; i++) sector_buf[i] = (uint8_t)(i ^ 0xA5);
            int wrc = block_write_blocks(dev, last_lba, 1, sector_buf);
            block_flush(dev);
            static uint8_t verify[ATA_SECTOR_SIZE];
            int rrc = block_read_blocks(dev, last_lba, 1, verify);
            if (wrc == BLOCK_OK && rrc == BLOCK_OK && memcmp(verify, sector_buf, ATA_SECTOR_SIZE) == 0) {
                kprintf("Storage: %s write/readback test OK (last sector)\n", dev->name);
            } else {
                kprintf("Storage: %s write/readback test FAILED (w=%d r=%d)\n", dev->name, wrc, rrc);
            }
            memcpy(sector_buf, original, ATA_SECTOR_SIZE);
            int crc2 = block_write_blocks(dev, last_lba, 1, sector_buf);
            block_flush(dev);
            if (crc2 != BLOCK_OK) {
                kprintf("Storage: %s restore write FAILED (rc=%d)\n", dev->name, crc2);
            }
        } else {
            kprintf("Storage: %s last-sector read FAILED (rc=%d)\n", dev->name, rc);
        }
    } else {
        print_skip("Storage");
        kdebug("ATA: no drives detected (no disk attached or no IDE controller)");
    }

    vfs_init();
    struct block_device* root_dev = block_find("ata00");
    if (root_dev) {
        struct vfs_filesystem* fs = fat32_mount(root_dev);
        if (fs && vfs_mount("/", fs) == VFS_OK) {
            print_ok("VFS");
            kprintf("VFS: mounted %s on / (%s)\n", root_dev->name, fs->type);

            vfs_list("/", vfs_list_cb_print, NULL);
            kprintf("VFS: %u root entries\n", vfs_root_entries);
            fs_run_selftest();
        } else {
            print_skip("VFS");
            kdebug("VFS: FAT32 mount failed (no FAT32 filesystem on ata00?)");
        }
    } else {
        print_skip("VFS");
        kdebug("VFS: no block device to mount");
    }

    keyboard_init();
    print_ok("Keyboard");
    kdebug("PS/2 keyboard: IRQ 1 registered, buffer flushed");
    
    print_ok("VGA");
    kdebug("VGA: text mode 80x25, buffer at 0xB8000");
    
    if (bga_init()) {
        print_ok("BGA");
        kdebug("Bochs Graphics Adapter detected");
    } else {
        print_skip("BGA");
        kdebug("BGA: not available (not a Bochs/VirtualBox display)");
    }
    
    __asm__ volatile("sti");
    kdebug("Interrupts enabled (STI)");

    kprintf("\n");
    if (userprog_init()) {
        /* Phase 13: the boot program is the session-0 shell (a Ring 3 ELF
         * with argv/envp at entry). Session 1 gets its shell on first
         * Ctrl-2 switch. Scripted input below drives the automated boot
         * test through builtin echo, variable set/expand, external ELF
         * exec with wait/status, and the parser's conditional chaining. */
        static const char* sh_argv[] = {"sh", "0", NULL};
        static const char* sh_envp[] = {"PATH=/BIN", "NUMOS=yes", NULL};
        struct process* sh = user_process_exec("sh", user_program_start,
                                               (uint64_t)(user_program_end - user_program_start),
                                               sh_argv, sh_envp, 0, 0);
        if (sh) {
            /* Session-0 script: all output stays visible because the
             * active terminal never changes during the automated test. */
            static const char script[] =
                "echo hello from session $SESSION\n"
                "set GREET hi\n"
                "echo $GREET shell and \\$literal\n"
                "true && echo and-chain-ok\n"
                "false || echo or-chain-ok\n"
                "echo data > /OUT.TXT\n"
                "cat /OUT.TXT\n"
                "ls\n"
                "stat /OUT.TXT\n"
                "ps\n"
                "init\n"
                "echo exit=$?\n"
                "echo exit=$?\n"
                "badcmd\n"
                "history\n";
            tty_inject(tty_get(0), script, (uint32_t)(sizeof(script) - 1));
            scheduler_start();
            kprintf("sched: done - %lu switches, %lu preemptions, %u processes\n",
                    scheduler_get_context_switches(), scheduler_get_preemptions(),
                    process_get_count());
        } else {
            print_skip("Shell");
        }
    } else if (thread_create("worker-a", demo_worker_a) && thread_create("worker-b", demo_worker_b)) {
        scheduler_start();
        kprintf("sched: done - %lu switches, %u processes\n",
                scheduler_get_context_switches(), process_get_count());
    } else {
        print_skip("Threads");
    }

    kprintf("\n");
    vga_set_color(LIGHT_GREEN, BLACK);
    kprintf("Hello World!\n");
    
    while (1) {
        __asm__ volatile("hlt");
    }
}
