#include "kernel/process.h"
#include "drivers/graphices/vga.h"
#include "cpu/apic.h"
#include "kernel/kernel.h"

#define AP_TRAMPOLINE_ADDR      0x00007000U
#define AP_BOOT_DATA_ADDR       0x00007F00U

#define AP_STACK_SIZE           16384
#define AP_MAX_CPUS             32

struct ap_boot_data {
    uint64_t cr3;
    uint64_t stack;
    uint64_t entry;
    uint32_t apic_id;
    uint32_t ready;
} __attribute__((packed));

extern uint8_t p4_table[];

extern uint8_t _binary_build_kernel_boot_ap_trampoline_bin_start[];
extern uint8_t _binary_build_kernel_boot_ap_trampoline_bin_end[];

static uint32_t           smp_total  = 1;
static uint32_t           smp_online = 1;
static uint32_t           smp_bsp_id = 0;

static inline void cpu_pause(void) {
    __asm__ volatile("pause");
}

static void delay_loops(uint32_t loops) {
    for (uint32_t i = 0; i < loops; i++) cpu_pause();
}

static uint32_t detect_logical_cpu_count(void) {
    uint32_t eax, ebx, ecx, edx;
    __asm__ volatile("cpuid"
                     : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx)
                     : "a"(1));

    uint32_t count = (ebx >> 16) & 0xFFu;
    if (count == 0) count = 1;
    if (count > AP_MAX_CPUS) count = AP_MAX_CPUS;
    return count;
}

static void ap_entry(uint32_t apic_id) {
    (void)apic_id;
    __asm__ volatile("cli");
    while (1) __asm__ volatile("hlt");
}

void process_smp_init(void) {
    if (apic_is_initialized()) return;

    smp_total = detect_logical_cpu_count();
    smp_online = 1;

    if (smp_total <= 1) {
        if (apic_init() == 0) smp_bsp_id = apic_get_id();
        return;
    }

    __asm__ volatile("cli");

    if (apic_init() != 0) {
        __asm__ volatile("sti");
        return;
    }
    smp_bsp_id = apic_get_id();

    size_t tramp_size =
        (size_t)(_binary_build_kernel_boot_ap_trampoline_bin_end -
                 _binary_build_kernel_boot_ap_trampoline_bin_start);
    if (tramp_size > 0) {
        memcpy((void *)(uintptr_t)AP_TRAMPOLINE_ADDR,
               _binary_build_kernel_boot_ap_trampoline_bin_start,
               tramp_size);
    }

    volatile struct ap_boot_data *boot =
        (volatile struct ap_boot_data *)(uintptr_t)AP_BOOT_DATA_ADDR;

    boot->cr3   = (uint64_t)(uintptr_t)p4_table;
    boot->entry = (uint64_t)(uintptr_t)ap_entry;

    uint32_t vector = (AP_TRAMPOLINE_ADDR >> 12) & 0xFFu;

    for (uint32_t apic_id = 0; apic_id < smp_total; apic_id++) {
        if (apic_id == smp_bsp_id) continue;

        uint8_t *stack = (uint8_t *)kmalloc(AP_STACK_SIZE);
        if (!stack) continue;

        uint64_t stack_top = (uint64_t)(uintptr_t)(stack + AP_STACK_SIZE);
        stack_top &= ~0xFULL;

        boot->stack   = stack_top;
        boot->apic_id = apic_id;
        boot->ready   = 0;

        apic_send_ipi(apic_id, 0x0000C500U);
        delay_loops(200000);
        apic_send_ipi(apic_id, 0x00008500U);
        delay_loops(200000);

        apic_send_ipi(apic_id, 0x00004600U | vector);
        delay_loops(200000);
        apic_send_ipi(apic_id, 0x00004600U | vector);
        delay_loops(200000);

        int ok = 0;
        for (uint32_t t = 0; t < 5000000; t++) {
            if (boot->ready) { ok = 1; break; }
            cpu_pause();
        }

        if (ok) smp_online++;
    }

    __asm__ volatile("sti");

    vga_writestring("SMP: CPUs online ");
    print_dec((uint64_t)smp_online);
    vga_writestring(" of ");
    print_dec((uint64_t)smp_total);
    vga_writestring(" (BSP APIC ID ");
    print_dec((uint64_t)smp_bsp_id);
    vga_writestring(")\n");
}
