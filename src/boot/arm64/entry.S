.section .text.boot, "ax"

.global _start
.extern arm64_boot_main

.extern _bss_start
.extern _bss_end
.extern _boot_stack_top

_start:
    mrs x4, mpidr_el1
    and x4, x4, #0xff
    cbz x4, .Lboot_cpu

.Lsecondary_wait:
    wfe
    b .Lsecondary_wait

.Lboot_cpu:
    adr x5, _boot_stack_top
    mov sp, x5

    adr x6, _bss_start
    adr x7, _bss_end
.Lclear_bss:
    cmp x6, x7
    b.hs .Lcall_c
    str xzr, [x6], #8
    b .Lclear_bss

.Lcall_c:
    bl arm64_boot_main

.Lhang:
    wfe
    b .Lhang
