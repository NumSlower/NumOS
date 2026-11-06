/* NumOS — user.ld — User program link script (linked at its own VA) */
ENTRY(_start)

SECTIONS
{
    . = 0x08000000;
    .text : { KEEP(*(.text.start)) *(.text .text.*) }
    .rodata : { *(.rodata .rodata.*) }
    .data : { *(.data .data.*) }
    .bss : { *(.bss .bss.* COMMON) }
    /DISCARD/ : { *(.comment) *(.note*) *(.eh_frame) }
}
