; AP trampoline binary for SMP startup
;
; This file assembles to a raw 4 KB page that the BSP copies to 0x7000 and
; starts via SIPI vector 0x07. The BSP fills the data block at 0x7F00 with:
;   - CR3 physical address
;   - 64-bit stack pointer
;   - 64-bit entry function address
;   - 32-bit APIC ID
;   - 32-bit ready flag (written by the AP)
;
; The trampoline switches the AP from real mode to protected mode, enables
; long mode using the BSP page tables, then jumps to the provided entry.

bits 16
org 0x7000

ap_start16:
    cli
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7E00

    lgdt [gdt32_ptr]

    mov eax, cr0
    or  eax, 0x1
    mov cr0, eax

    jmp 0x08:ap_start32

; ---------------------------- 32-bit protected mode ------------------------

bits 32
ap_start32:
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov fs, ax
    mov gs, ax

    mov esp, 0x7E00

    ; Enable PAE
    mov eax, cr4
    or  eax, (1 << 5)
    mov cr4, eax

    ; Load CR3 from data block (low 32 bits)
    mov eax, dword [ap_data_cr3]
    mov cr3, eax

    ; Enable long mode (EFER.LME)
    mov ecx, 0xC0000080
    rdmsr
    or  eax, (1 << 8)
    wrmsr

    ; Enable paging
    mov eax, cr0
    or  eax, 0x80000000
    mov cr0, eax

    lgdt [gdt64_ptr]
    jmp 0x08:ap_start64

; ---------------------------- 64-bit long mode -----------------------------

bits 64
ap_start64:
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov fs, ax
    mov gs, ax

    mov rsp, qword [ap_data_stack]
    and rsp, -16

    mov edi, dword [ap_data_apic_id]

    mov dword [ap_data_ready], 1

    mov rax, qword [ap_data_entry]
    call rax

.halt:
    hlt
    jmp .halt

; ---------------------------- GDTs -----------------------------------------

align 8
gdt32:
    dq 0
    dq 0x00CF9A000000FFFF
    dq 0x00CF92000000FFFF
gdt32_end:

gdt32_ptr:
    dw gdt32_end - gdt32 - 1
    dd gdt32

align 8
gdt64:
    dq 0
    dq (1<<43) | (1<<44) | (1<<47) | (1<<53)
    dq (1<<44) | (1<<47) | (1<<41)
gdt64_end:

gdt64_ptr:
    dw gdt64_end - gdt64 - 1
    dd gdt64

; ---------------------------- Data block at 0x7F00 -------------------------

times 0x0F00 - ($ - $$) db 0

ap_data_cr3:       dq 0
ap_data_stack:     dq 0
ap_data_entry:     dq 0
ap_data_apic_id:   dd 0
ap_data_ready:     dd 0

times 0x1000 - ($ - $$) db 0

