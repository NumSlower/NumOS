/* NumOS 64-bit Kernel Linker Script */
ENTRY(_start)

SECTIONS
{
    /* Kernel starts at 1MB physical address */
    . = 0x100000;
    
    /* Multiboot header must be at the very beginning */
    .multiboot ALIGN(8) : {
        *(.multiboot)
    }
    
    /* Boot code section */
    .boot ALIGN(4096) : {
        *(.boot)
    }
    
    /* Text section for code */
    .text ALIGN(4096) : {
        *(.text)
        *(.text.*)
    }
    
    /* Read-only data */
    .rodata ALIGN(4096) : {
        *(.rodata)
        *(.rodata.*)
    }
    
    /* Initialized data */
    .data ALIGN(4096) : {
        *(.data)
        *(.data.*)
    }
    
    /* Uninitialized data */
    .bss ALIGN(4096) : {
        *(COMMON)
        *(.bss)
        *(.bss.*)
    }
    
    . = ALIGN(4096);
    _kernel_end = .;
    
    /* Discard debug sections */
    /DISCARD/ : {
        *(.comment)
        *(.note)
        *(.note.*)
        *(.eh_frame)
        *(.eh_frame_hdr)
        *(.debug*)
    }
}