; NumOS Boot Code
; 32-bit to 64-bit transition and initial setup

bits 32

global _start
extern long_mode_start

section .boot

_start:
    mov word [0xb8000], 0x0f41 ; Debug: Print 'A'
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
    mov word [0xb8000], 0x0f41 ; Debug: Print 'A'
    
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
    ; in the FLAGS register
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
    or eax, 0b11 ; present + writable
    mov [p4_table], eax
    
    ; map first P3 entry to P2 table
    mov eax, p2_table
    or eax, 0b11 ; present + writable
    mov [p3_table], eax
    
    ; map each P2 entry to a huge 2MiB page
    mov ecx, 0
    
.map_p2_table:
    ; map ecx-th P2 entry to a huge page that starts at address 2MiB*ecx
    mov eax, 0x200000  ; 2MiB
    mul ecx            ; start address of ecx-th page
    or eax, 0b10000011 ; present + writable + huge
    mov [p2_table + ecx * 8], eax ; map ecx-th entry
    
    inc ecx            ; increase counter
    cmp ecx, 512       ; if counter == 512, the whole P2 table is mapped
    jne .map_p2_table  ; else map the next entry
    
    ret

enable_paging:
    ; load P4 to cr3 register (cpu uses this to access the P4 table)
    mov eax, p4_table
    mov cr3, eax
    
    ; enable PAE-flag in cr4 (Physical Address Extension)
    mov eax, cr4
    or eax, 1 << 5
    mov cr4, eax
    
    ; set the long mode bit in the EFER MSR (model specific register)
    mov ecx, 0xC0000080
    rdmsr
    or eax, 1 << 8
    wrmsr
    
    ; enable paging in the cr0 register
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
    dq 0 ; zero entry
.code: equ $ - gdt64 ; new
    dq (1<<43) | (1<<44) | (1<<47) | (1<<53) ; code segment
.pointer:
    dw $ - gdt64 - 1
    dq gdt64