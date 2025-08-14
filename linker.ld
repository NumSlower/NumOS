/*
 * Bare bones x86_64 linker script
 */

ENTRY(_start)

SECTIONS
{
    /* A common starting address for executables on Linux */
    . = 0x400000;

    /* Align sections to 4K page size */
    .text : ALIGN(4096)
    {
        *(.text .text.*)
    }

    .rodata : ALIGN(4096)
    {
        *(.rodata .rodata.*)
    }

    .data : ALIGN(4096)
    {
        *(.data .data.*)
    }

    .bss : ALIGN(4096)
    {
        *(COMMON)
        *(.bss .bss.*)
    }

    /* Discard unnecessary sections */
    /DISCARD/ :
    {
        *(.comment)
        *(.note.gnu.build-id)
        *(.eh_frame)
    }
}