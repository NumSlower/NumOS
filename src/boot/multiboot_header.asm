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
    dd 20                        ; size
    dd 6                         ; request MEMORY_MAP
    dd 8                         ; request BOOTLOADER_NAME
    dd 9                         ; request VBE_INFO (VESA)
    dd 10                        ; request FRAMEBUFFER_INFO
    
    ; Framebuffer tag (request graphics mode)
    dw 5                         ; type = framebuffer
    dw 0                         ; flags
    dd 20                        ; size
    dd 1024                      ; preferred width
    dd 768                       ; preferred height
    dd 32                        ; preferred bits per pixel
    dd 0                         ; padding to 8-byte align
    
    ; End tag
    dw 0                         ; type = end
    dw 0                         ; flags
    dd 8                         ; size
multiboot_end: