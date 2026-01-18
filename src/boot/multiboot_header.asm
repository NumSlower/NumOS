; NumOS Multiboot2 Header
; This file defines the multiboot2 header for GRUB to recognize our kernel

section .multiboot
align 8

; Multiboot2 header
multiboot_start:
    dd 0xe85250d6                ; magic number
    dd 0                         ; architecture (i386)
    dd multiboot_end - multiboot_start ; header length
    dd 0x100000000 - (0xe85250d6 + 0 + (multiboot_end - multiboot_start)) ; checksum

    ; Information request tag
    dw 1                         ; type = information request
    dw 0                         ; flags
    dd 8                         ; size
    
    ; End tag
    dw 0                         ; type = end
    dw 0                         ; flags
    dd 8                         ; size
multiboot_end: