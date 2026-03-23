; multiboot_header.asm - Multiboot2 header for GRUB bootloader
;
; The framebuffer tag (type 5) is compiled in ONLY when ENABLE_FRAMEBUFFER=1
; is passed from the Makefile via: nasm -D ENABLE_FRAMEBUFFER=1
;
; When ENABLE_FRAMEBUFFER=0 (or undefined):
;   No framebuffer tag is included. GRUB keeps VGA text mode active.
;   VGA writes to 0xB8000 work normally. No black screen.
;
; When ENABLE_FRAMEBUFFER=1:
;   GRUB calls the VBE BIOS before boot, sets the closest available
;   mode to the resolution in grub.cfg, then reports the framebuffer
;   address and pixel format in the MB2 info block (tag type 8).
;
; Reference: Multiboot2 spec §3.1.6
;            https://wiki.osdev.org/Multiboot#Multiboot_2

section .multiboot
align 8

header_start:
    dd 0xe85250d6                               ; Multiboot2 magic
    dd 0                                        ; Architecture: i386 (required)
    dd header_end - header_start                ; Header length
    dd -(0xe85250d6 + 0 + (header_end - header_start))  ; Checksum

    ; ---- Tag: Information Request (type 1) ---------------------------------
    align 8
info_request_tag_start:
    dw 1        ; type  = information request
    dw 0        ; flags = required
    dd info_request_tag_end - info_request_tag_start
    dd 1        ; boot command line
    dd 3        ; modules (disk.img ramdisk lives here)
    dd 4        ; basic memory info
    dd 6        ; memory map
    dd 8        ; framebuffer info (populated by GRUB when it sets a VBE mode)
info_request_tag_end:

%ifdef ENABLE_FRAMEBUFFER
%if ENABLE_FRAMEBUFFER

    ; ---- Tag: Framebuffer (type 5) -----------------------------------------
    ; Only present when built with -D ENABLE_FRAMEBUFFER=1.
    ; Requests a VBE/VESA graphics mode from GRUB before kernel start.
    ; Ask for a concrete safe mode because some BIOS GRUB setups keep
    ; text mode when width/height are left at zero.
    align 8
framebuffer_tag_start:
    dw 5        ; type  = framebuffer
    dw 1        ; flags = optional (don't abort if VBE is unavailable)
    dd framebuffer_tag_end - framebuffer_tag_start
    dd 1024     ; width
    dd 768      ; height
    dd 32       ; depth  = 32 bpp preferred
framebuffer_tag_end:

%endif
%endif

    ; ---- Tag: Module alignment (type 6) ------------------------------------
    align 8
module_align_tag_start:
    dw 6        ; type  = module alignment
    dw 0        ; flags = required
    dd module_align_tag_end - module_align_tag_start
module_align_tag_end:

    ; ---- Tag: End (type 0) - required terminator ---------------------------
    align 8
end_tag_start:
    dw 0
    dw 0
    dd 8
end_tag_end:

header_end:
