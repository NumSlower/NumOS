; NumOS Boot Code - Fixed version
; 32-bit to 64-bit transition with proper page table setup

bits 32

global _start
global p4_table
global p3_table  
global p2_table
extern long_mode_start

section .bss
align 4096
p4_table:
    resb 4096
p3_table:
    resb 4096
p2_table:
    resb 4096
stack_bottom:
    resb 16384      ; 16KB stack
stack_top:

section .text
_start:
    ; Set up stack
    mov esp, stack_top
    
    ; Store multiboot info pointer
    mov edi, ebx
    
    ; Check multiboot magic
    call check_multiboot
    
    ; Check CPUID support
    call check_cpuid
    
    ; Check long mode support
    call check_long_mode
    
    ; Set up paging
    call set_up_page_tables
    call enable_paging
    
    ; Load GDT
    lgdt [gdt64.pointer]
    
    ; Update selectors
    mov ax, gdt64.data
    mov ss, ax
    mov ds, ax
    mov es, ax
    
    ; Jump to long mode
    jmp gdt64.code:long_mode_start

; Hang on error
error:
    cli
.hang:
    hlt
    jmp .hang

check_multiboot:
    cmp eax, 0x36d76289
    jne .no_multiboot
    ret
.no_multiboot:
    mov al, "0"
    jmp error

check_cpuid:
    ; Check if CPUID is supported by attempting to flip the ID bit (bit 21)
    ; in the FLAGS register. If we can flip it, CPUID is available.
    
    ; Copy FLAGS in to EAX via stack
    pushfd
    pop eax
    
    ; Copy to ECX as well for comparing later on
    mov ecx, eax
    
    ; Flip the ID bit
    xor eax, 1 << 21
    
    ; Copy EAX to FLAGS via the stack
    push eax
    popfd
    
    ; Copy FLAGS back to EAX (with the flipped bit if CPUID is supported)
    pushfd
    pop eax
    
    ; Restore FLAGS from the old version stored in ECX (i.e. flipping the ID bit
    ; back if it was ever flipped).
    push ecx
    popfd
    
    ; Compare EAX and ECX. If they are equal then that means the bit wasn't
    ; flipped, and CPUID isn't supported.
    cmp eax, ecx
    je .no_cpuid
    ret
.no_cpuid:
    mov al, "1"
    jmp error

check_long_mode:
    ; Test if extended processor info in available
    mov eax, 0x80000000    ; implicit argument for cpuid
    cpuid                  ; get highest supported argument
    cmp eax, 0x80000001    ; it needs to be at least 0x80000001
    jb .no_long_mode       ; if it's less, the CPU is too old for long mode
    
    ; Use extended info to test if long mode is available
    mov eax, 0x80000001    ; argument for extended processor info
    cpuid                  ; returns various feature bits in ecx and edx
    test edx, 1 << 29      ; test if the LM-bit is set in the D-register
    jz .no_long_mode       ; If it's not set, there is no long mode
    ret
.no_long_mode:
    mov al, "2"
    jmp error

set_up_page_tables:
    ; Zero out the page tables
    mov edi, p4_table
    mov cr3, edi
    xor eax, eax
    mov ecx, 3072      ; 3 tables * 1024 entries
    rep stosd
    
    ; Restore edi
    mov edi, p4_table
    
    ; Map first P4 entry to P3 table
    mov eax, p3_table
    or eax, 0b11       ; present + writable
    mov [p4_table], eax
    
    ; Map first P3 entry to P2 table
    mov eax, p2_table
    or eax, 0b11       ; present + writable
    mov [p3_table], eax
    
    ; Map each P2 entry to a huge 2MiB page
    mov ecx, 0         ; counter variable
.map_p2_table:
    mov eax, 0x200000  ; 2MiB
    mul ecx            ; multiply by counter (result in eax)
    or eax, 0b10000011 ; present + writable + huge
    mov [p2_table + ecx * 8], eax ; map ecx-th entry
    
    inc ecx            ; increase counter
    cmp ecx, 512       ; if counter == 512, the whole P2 table is mapped
    jne .map_p2_table  ; else map the next entry
    
    ret

enable_paging:
    ; Load P4 to cr3 register (cpu uses this to access the P4 table)
    mov eax, p4_table
    mov cr3, eax
    
    ; Enable PAE-flag in cr4 (Physical Address Extension)
    mov eax, cr4
    or eax, 1 << 5
    mov cr4, eax
    
    ; Set the long mode bit in the EFER MSR (model specific register)
    mov ecx, 0xC0000080
    rdmsr
    or eax, 1 << 8
    wrmsr
    
    ; Enable paging in the cr0 register
    mov eax, cr0
    or eax, 1 << 31
    or eax, 1 << 16    ; Write protect
    mov cr0, eax
    
    ret

section .rodata
gdt64:
    dq 0 ; zero entry
.code: equ $ - gdt64
    dq (1<<43) | (1<<44) | (1<<47) | (1<<53) ; code segment
.data: equ $ - gdt64
    dq (1<<44) | (1<<47) | (1<<41) ; data segment
.pointer:
    dw $ - gdt64 - 1
    dq gdt64