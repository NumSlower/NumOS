ENTRY(_start)

PHDRS
{
    text PT_LOAD FLAGS(5);
    data PT_LOAD FLAGS(6);
}

SECTIONS
{
    . = 0x40080000;

    _kernel_start = .;

    .text ALIGN(4096) : {
        _text_start = .;
        *(.text.boot)
        *(.text.boot.*)
        *(.text)
        *(.text.*)
        _text_end = .;
    } :text

    .rodata ALIGN(4096) : {
        _rodata_start = .;
        *(.rodata)
        *(.rodata.*)
        _rodata_end = .;
    } :text

    .init_array ALIGN(8) : {
        __init_array_start = .;
        KEEP(*(.init_array))
        KEEP(*(.init_array.*))
        __init_array_end = .;
    } :data

    .data ALIGN(4096) : {
        _data_start = .;
        *(.data)
        *(.data.*)
        _data_end = .;
    } :data

    .bss ALIGN(4096) : {
        _bss_start = .;
        *(COMMON)
        *(.bss)
        *(.bss.*)
        _bss_end = .;
    } :data

    . = ALIGN(16);
    .boot_stack (NOLOAD) : {
        _boot_stack_bottom = .;
        . += 65536;
        _boot_stack_top = .;
    } :data

    _kernel_end = .;

    /DISCARD/ : {
        *(.comment)
        *(.note)
        *(.note.*)
        *(.eh_frame)
        *(.eh_frame_hdr)
    }
}
