#include "cpu/apic.h"
#include "cpu/paging.h"
#include "kernel/kernel.h"

#define IA32_APIC_BASE_MSR    0x0000001B
#define IA32_APIC_BASE_ENABLE (1ULL << 11)

#define APIC_REG_ID      0x00000020U
#define APIC_REG_EOI     0x000000B0U
#define APIC_REG_SVR     0x000000F0U
#define APIC_REG_ICR_LOW 0x00000300U
#define APIC_REG_ICR_HIGH 0x00000310U

#define APIC_SVR_ENABLE  0x00000100U
#define APIC_SVR_VECTOR  0x000000FFU

static volatile uint32_t *lapic_mmio = NULL;
static uint32_t apic_id = 0;
static int apic_ready = 0;

static inline uint64_t rdmsr(uint32_t msr) {
    uint32_t lo, hi;
    __asm__ volatile("rdmsr" : "=a"(lo), "=d"(hi) : "c"(msr));
    return ((uint64_t)hi << 32) | lo;
}

static inline void wrmsr(uint32_t msr, uint64_t value) {
    __asm__ volatile("wrmsr" :: "a"((uint32_t)value),
                               "d"((uint32_t)(value >> 32)),
                               "c"(msr)
                     : "memory");
}

static inline void cpu_pause(void) {
    __asm__ volatile("pause");
}

static inline uint32_t lapic_read(uint32_t reg) {
    return lapic_mmio[reg / 4];
}

static inline void lapic_write(uint32_t reg, uint32_t val) {
    lapic_mmio[reg / 4] = val;
    (void)lapic_read(APIC_REG_ID);
}

static void lapic_wait_icr(void) {
    while (lapic_read(APIC_REG_ICR_LOW) & (1u << 12)) cpu_pause();
}

int apic_is_available(void) {
    uint32_t eax, ebx, ecx, edx;
    __asm__ volatile("cpuid"
                     : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx)
                     : "a"(1));
    return (edx & (1u << 9)) ? 1 : 0;
}

int apic_is_initialized(void) {
    return apic_ready;
}

int apic_init(void) {
    if (apic_ready) return 0;
    if (!apic_is_available()) return -1;

    uint64_t base_msr = rdmsr(IA32_APIC_BASE_MSR);
    base_msr |= IA32_APIC_BASE_ENABLE;
    wrmsr(IA32_APIC_BASE_MSR, base_msr);

    uint64_t apic_base = base_msr & 0xFFFFF000ULL;
    if (paging_map_page(apic_base, apic_base,
                        PAGE_PRESENT | PAGE_WRITABLE | PAGE_CACHE_DISABLE) != 0) {
        if (!paging_is_mapped(apic_base)) return -1;
    }

    lapic_mmio = (volatile uint32_t *)(uintptr_t)apic_base;
    lapic_write(APIC_REG_SVR, APIC_SVR_ENABLE | APIC_SVR_VECTOR);
    apic_id = lapic_read(APIC_REG_ID) >> 24;
    apic_ready = 1;
    return 0;
}

uint32_t apic_get_id(void) {
    return apic_id;
}

void apic_send_eoi(void) {
    if (!lapic_mmio) return;
    lapic_write(APIC_REG_EOI, 0);
}

void apic_send_ipi(uint32_t dest_apic_id, uint32_t icr_low) {
    if (!lapic_mmio) return;
    lapic_wait_icr();
    lapic_write(APIC_REG_ICR_HIGH, dest_apic_id << 24);
    lapic_write(APIC_REG_ICR_LOW, icr_low);
    lapic_wait_icr();
}
