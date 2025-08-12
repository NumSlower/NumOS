/* NumOS 64-bit Kernel Linker Script */
ENTRY(_start)

SECTIONS
{
    /* Kernel starts at 1MB physical address */
    . = 0x100000;
    
    /* Multiboot header must be at the beginning */
    .multiboot ALIGN(4K) : {
        *(.multiboot)
    }
    
    /* Boot code section */
    .boot ALIGN(4K) : {
        *(.boot)
    }
    
    /* Text section for code */
    .text ALIGN(4K) : {
        *(.text)
        *(.text.*)
    }
    
    /* Read-only data */
    .rodata ALIGN(4K) : {
        *(.rodata)
        *(.rodata.*)
    }
    
    /* Initialized data */
    .data ALIGN(4K) : {
        *(.data)
        *(.data.*)
    }
    
    /* Uninitialized data */
    .bss ALIGN(4K) : {
        *(COMMON)
        *(.bss)
        *(.bss.*)
    }
    
    /* Stack section */
    .stack ALIGN(4K) : {
        *(.stack)
    }
    
    /* Discard unnecessary sections */
    /DISCARD/ : {
        *(.comment)
        *(.note)
        *(.note.*)
        *(.eh_frame)
        *(.eh_frame_hdr)
    }
}