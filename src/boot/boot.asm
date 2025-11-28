; NumOS Boot Code
; 32-bit to 64-bit transition with proper page table setup

bits 32

global _start
global p4_table
global p3_table  
global p2_table
extern long_mode_start

section .boot

_start:
    ; Set up stack
    mov esp, stack_top
    
    ; Store multiboot info
    mov edi, ebx
    
    ; Check multiboot
    call check_multiboot
    
    ; Check CPUID
    call check_cpuid
    
    ; Check long mode
    call check_long_mode
    
    ; Set up paging
    call set_up_page_tables
    call enable_paging
    
    ; Load GDT
    lgdt [gdt64.pointer]
    
    ; Jump to long mode
    jmp gdt64.code:long_mode_start

check_multiboot:
    cmp eax, 0x36d76289
    jne .no_multiboot
    ret
.no_multiboot:
    mov al, "0"
    jmp error

check_cpuid:
    ; Check if CPUID is supported by attempting to flip the ID bit (bit 21)
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
    ret
.no_cpuid:
    mov al, "1"
    jmp error

check_long_mode:
    ; Test if extended processor info is available
    mov eax, 0x80000000
    cpuid
    cmp eax, 0x80000001
    jb .no_long_mode
    
    ; Use extended info to test if long mode is available
    mov eax, 0x80000001
    cpuid
    test edx, 1 << 29
    jz .no_long_mode
    ret
.no_long_mode:
    mov al, "2"
    jmp error

set_up_page_tables:
    ; Clear page tables
    mov edi, p4_table
    mov ecx, 3 * 512  ; Clear 3 tables (P4, P3, P2)
    xor eax, eax
    rep stosd
    
    ; Map first P4 entry to P3 table
    mov eax, p3_table
    or eax, 0b11      ; present + writable
    mov [p4_table], eax
    
    ; Map first P3 entry to P2 table
    mov eax, p2_table
    or eax, 0b11      ; present + writable
    mov [p3_table], eax
    
    ; Identity map first 1GB using 2MiB pages
    ; This covers 0x00000000 - 0x3FFFFFFF (first 512 * 2MB = 1GB)
    xor ecx, ecx
.map_p2_table:
    mov eax, 0x200000      ; 2MiB
    mul ecx                ; start address of ecx-th page
    or eax, 0b10000011     ; present + writable + huge page
    mov [p2_table + ecx*8], eax
    inc ecx
    cmp ecx, 512           ; Map all 512 entries (1GB)
    jne .map_p2_table
    
    ; Map higher half (kernel virtual address)
    ; Entry 510 in P4 (0xFFFFFFFF80000000)
    mov eax, p3_table
    or eax, 0b11
    mov [p4_table + 510*8], eax
    
    ret

enable_paging:
    ; Load P4 table into CR3
    mov eax, p4_table
    mov cr3, eax
    
    ; Enable PAE in CR4 (Physical Address Extension)
    mov eax, cr4
    or eax, 1 << 5
    mov cr4, eax
    
    ; Enable Long Mode in EFER MSR (Model Specific Register)
    mov ecx, 0xC0000080
    rdmsr
    or eax, 1 << 8     ; Set LME (Long Mode Enable)
    wrmsr
    
    ; Enable paging and write protect in CR0
    mov eax, cr0
    or eax, 1 << 31    ; PG (Paging)
    or eax, 1 << 16    ; WP (Write Protect)
    mov cr0, eax
    
    ret

error:
    mov dword [0xb8000], 0x4f524f45
    mov dword [0xb8004], 0x4f3a4f52
    mov dword [0xb8008], 0x4f204f20
    mov byte  [0xb800a], al
    hlt

section .bss
align 4096
p4_table:
    resb 4096
p3_table:
    resb 4096
p2_table:
    resb 4096
stack_bottom:
    resb 4096 * 4
stack_top:

section .rodata
gdt64:
    dq 0                                    ; null descriptor
.code: equ $ - gdt64
    ; Code segment (64-bit)
    ; Base = 0, Limit = 0 (ignored in 64-bit mode)
    ; Access: Present, DPL0, System, Code, Readable
    ; Flags: Long mode (L=1), Default operation size (D=0)
    dq (1<<43) | (1<<44) | (1<<47) | (1<<53) ; 0x00AF9A000000FFFF
.data: equ $ - gdt64
    ; Data segment  
    ; Access: Present, DPL0, System, Data, Writable
    dq (1<<44) | (1<<47) | (1<<41)           ; 0x00CF92000000FFFF
.pointer:
    dw $ - gdt64 - 1
    dq gdt64