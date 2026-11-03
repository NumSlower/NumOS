; NumOS — src/boot/multiboot2.asm — Multiboot2 entry and 64-bit switch

section .multiboot
align 8
header_start:
    dd 0xE85250D6                ; magic number (multiboot 2)
    dd 0                         ; architecture 0 (protected mode i386)
    dd header_end - header_start ; header length
    ; checksum
    dd 0x100000000 - (0xE85250D6 + 0 + (header_end - header_start))

    ; end tag
    align 8
    dw 0    ; type
    dw 0    ; flags
    dd 8    ; size
header_end:

section .bss
align 4096
p4_table:
    resb 4096
p3_table:
    resb 4096
p2_table_0:
    resb 4096
p2_table_1:
    resb 4096
p2_table_2:
    resb 4096
p2_table_3:
    resb 4096

stack_bottom:
    resb 16384
stack_top:

mb2_info_ptr:
    resd 1

section .rodata
gdt64:
    dq 0 ; zero entry
.code: equ $ - gdt64
    dq (1<<41) | (1<<43) | (1<<44) | (1<<47) | (1<<53) ; code: readable, executable, present, long mode
.data: equ $ - gdt64
    dq (1<<41) | (1<<44) | (1<<47)                     ; data: writable, present
.pointer:
    dw $ - gdt64 - 1
    dq gdt64

section .text
[bits 32]
global _start
extern kernel_main

_start:
    mov esp, stack_top
    mov [mb2_info_ptr], ebx

    cmp eax, 0x36D76289
    jne .no_multiboot

    ; Check CPUID
    pushfd
    pop eax
    mov ecx, eax
    xor eax, 1 << 21
    push eax
    popfd
    pushfd
    pop eax
    push ecx
    popfd
    cmp eax, ecx
    je .no_cpuid

    ; Check long mode
    mov eax, 0x80000000
    cpuid
    cmp eax, 0x80000001
    jb .no_long_mode
    mov eax, 0x80000001
    cpuid
    test edx, 1 << 29
    jz .no_long_mode

    ; Setup page tables
    mov eax, p3_table
    or eax, 0b11 ; present + writable
    mov [p4_table], eax

    mov eax, p2_table_0
    or eax, 0b11
    mov [p3_table], eax

    mov eax, p2_table_1
    or eax, 0b11
    mov [p3_table + 8], eax

    mov eax, p2_table_2
    or eax, 0b11
    mov [p3_table + 16], eax

    mov eax, p2_table_3
    or eax, 0b11
    mov [p3_table + 24], eax

    ; Fill P2 tables
    mov ecx, 0
.map_p2_table:
    mov eax, 0x200000  ; 2MiB
    mul ecx
    or eax, 0b10000011 ; present + writable + huge
    mov [p2_table_0 + ecx * 8], eax
    inc ecx
    cmp ecx, 2048      ; 4 tables * 512 entries
    jne .map_p2_table

    ; Enable paging
    mov eax, p4_table
    mov cr3, eax

    mov eax, cr4
    or eax, 1 << 5
    mov cr4, eax

    mov ecx, 0xC0000080
    rdmsr
    or eax, 1 << 8
    wrmsr

    mov eax, cr0
    or eax, 1 << 31
    mov cr0, eax

    lgdt [gdt64.pointer]
    jmp gdt64.code:long_mode_start

.no_multiboot:
.no_cpuid:
.no_long_mode:
    mov dword [0xB8000], 0x4f524f45
    mov dword [0xB8004], 0x4f3a4f52
    mov dword [0xB8008], 0x4f204f20
    hlt
    jmp $

[bits 64]
long_mode_start:
    mov ax, gdt64.data
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax

    mov rsp, stack_top
    xor rbp, rbp

    mov edi, 0x36D76289
    mov esi, [rel mb2_info_ptr]

    call kernel_main

    cli
.hlt:
    hlt
    jmp .hlt
