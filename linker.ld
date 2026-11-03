ENTRY(_start)

SECTIONS
{
    . = 0x100000;

    .boot :
    {
        *(.multiboot)
        *(.boot)
    }

    .text : ALIGN(4K)
    {
        *(.text)
    }

    .rodata : ALIGN(4K)
    {
        *(.rodata)
    }

    .data : ALIGN(4K)
    {
        *(.data)
    }

    .bss : ALIGN(4K)
    {
        *(COMMON)
        *(.bss)
    }

    /DISCARD/ : {
        *(.comment)
        *(.note)
        *(.eh_frame)
        *(.eh_frame_hdr)
    }
}