ENTRY(_start)

PHDRS
{
    text PT_LOAD FLAGS(5);  /* 5 = Read + Execute */
    data PT_LOAD FLAGS(6);  /* 6 = Read + Write */
}

SECTIONS
{
    . = 0x100000;

    /* Multiboot header must be first, in text segment */
    .multiboot ALIGN(4K) : {
        *(.multiboot)
    } :text

    /* Code (RX) */
    .text ALIGN(4K) : {
        *(.text)
        *(.text.*)
    } :text

    /* Read-only data (RX) */
    .rodata ALIGN(4K) : {
        *(.rodata)
        *(.rodata.*)
    } :text

    /* Initialized data (RW) */
    .data ALIGN(4K) : {
        *(.data)
        *(.data.*)
    } :data

    /* Uninitialized data (RW) */
    .bss ALIGN(4K) : {
        *(COMMON)
        *(.bss)
        *(.bss.*)
    } :data

    /* Stack (RW) */
    .stack ALIGN(4K) : {
        *(.stack)
    } :data

    /DISCARD/ : {
        *(.comment)
        *(.note)
        *(.note.*)
        *(.eh_frame)
        *(.eh_frame_hdr)
    }
}