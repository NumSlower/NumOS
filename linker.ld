/* Kernel Linker Script for NumOS */

ENTRY(_start)

PHDRS
{
    text PT_LOAD FLAGS(5);  /* 5 = Read + Execute */
    data PT_LOAD FLAGS(6);  /* 6 = Read + Write */
}

SECTIONS
{
    /* Kernel loads at 1MB physical address */
    . = 0x100000;

    /* Provide symbols for kernel bounds */
    _kernel_start = .;

    /* Multiboot header must be first, in text segment */
    .multiboot ALIGN(4K) : {
        *(.multiboot)
    } :text

    /* Code section (Read + Execute) */
    .text ALIGN(4K) : {
        _text_start = .;
        *(.text)
        *(.text.*)
        _text_end = .;
    } :text

    /* Read-only data (Read + Execute for simplicity) */
    .rodata ALIGN(4K) : {
        _rodata_start = .;
        *(.rodata)
        *(.rodata.*)
        _rodata_end = .;
    } :text

    /* Initialized data (Read + Write) */
    .data ALIGN(4K) : {
        _data_start = .;
        *(.data)
        *(.data.*)
        _data_end = .;
    } :data

    /* Uninitialized data (Read + Write) */
    .bss ALIGN(4K) : {
        _bss_start = .;
        *(COMMON)
        *(.bss)
        *(.bss.*)
        _bss_end = .;
    } :data

    /* Stack (Read + Write) */
    .stack ALIGN(4K) : {
        _stack_start = .;
        *(.stack)
        _stack_end = .;
    } :data

    /* Provide end of kernel symbol */
    _kernel_end = .;

    /* Discard unnecessary sections */
    /DISCARD/ : {
        *(.comment)
        *(.note)
        *(.note.*)
        *(.eh_frame)
        *(.eh_frame_hdr)
    }
}