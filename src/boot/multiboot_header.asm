; NumOS Multiboot2 Header
section .multiboot
align 8

multiboot_start:
    dd 0xe85250d6                ; magic number (multiboot2)
    dd 0                         ; architecture 0 (protected mode i386)
    dd multiboot_end - multiboot_start ; header length
    dd 0x100000000 - (0xe85250d6 + 0 + (multiboot_end - multiboot_start)) ; checksum

    ; Information request tag
    dw 1                         ; type
    dw 0                         ; flags
    dd 8                         ; size

    ; End tag
    dw 0                         ; type
    dw 0                         ; flags  
    dd 8                         ; size
multiboot_end: