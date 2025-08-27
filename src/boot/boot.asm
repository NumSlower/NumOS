; NumOS Boot Code
; 32-bit to 64-bit transition and initial setup

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
    ; Map first P4 entry to P3 table
    mov eax, p3_table
    or eax, 0b11      ; present + writable
    mov [p4_table], eax
    
    ; Map first P3 entry to P2 table
    mov eax, p2_table
    or eax, 0b11      ; present + writable
    mov [p3_table], eax
    
    ; Map each P2 entry to a 2MiB huge page
    xor ecx, ecx
    
.map_p2_table:
    mov eax, 0x200000      ; 2MiB
    mul ecx                ; start address of ecx-th page
    or eax, 0b10000011     ; present + writable + huge page
    mov [p2_table + ecx*8], eax
    inc ecx
    cmp ecx, 512
    jne .map_p2_table
    ret

enable_paging:
    ; Load P4 table into CR3
    mov eax, p4_table
    mov cr3, eax
    
    ; Enable PAE in CR4
    mov eax, cr4
    or eax, 1 << 5
    mov cr4, eax
    
    ; Enable Long Mode in EFER
    mov ecx, 0xC0000080
    rdmsr
    or eax, 1 << 8
    wrmsr
    
    ; Enable paging in CR0
    mov eax, cr0
    or eax, 1 << 31
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
    dq 0                     ; null descriptor
.code: equ $ - gdt64         ; code segment offset
    dq (1<<43) | (1<<44) | (1<<47) | (1<<53)
.pointer:
    dw $ - gdt64 - 1
    dq gdt64
