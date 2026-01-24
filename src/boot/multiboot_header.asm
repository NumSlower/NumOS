; multiboot_header.asm - Multiboot2 header for GRUB bootloader
; This MUST be within the first 32KB of the kernel binary

section .multiboot
align 8

header_start:
    ; Multiboot2 magic number
    dd 0xe85250d6
    
    ; Architecture: 0 = i386 protected mode
    dd 0
    
    ; Header length
    dd header_end - header_start
    
    ; Checksum: -(magic + architecture + header_length)
    dd -(0xe85250d6 + 0 + (header_end - header_start))

    ; Tags go here
    
    ; Information request tag
    align 8
information_request_tag_start:
    dw 1        ; type = information request
    dw 0        ; flags
    dd information_request_tag_end - information_request_tag_start  ; size
    dd 1        ; request boot command line
    dd 3        ; request memory map
    dd 6        ; request basic memory info
information_request_tag_end:
    ; Framebuffer tag (helps avoid video mode issues)
    align 8

;framebuffer_tag_start:
;    dw 5        ; type = framebuffer
;    dw 1        ; flags = optional
;    dd framebuffer_tag_end - framebuffer_tag_start  ; size
;    dd 0        ; width (0 = no preference)
;    dd 0        ; height (0 = no preference)
;    dd 0        ; depth (0 = no preference)
;framebuffer_tag_end:
;
;    ; Module alignment tag
;    align 8
module_align_tag_start:
    dw 6        ; type = module alignment
    dw 0        ; flags
    dd module_align_tag_end - module_align_tag_start  ; size
module_align_tag_end:

    ; End tag (required)
    align 8
end_tag_start:
    dw 0        ; type = end
    dw 0        ; flags
    dd 8        ; size (always 8 for end tag)
end_tag_end:

header_end: