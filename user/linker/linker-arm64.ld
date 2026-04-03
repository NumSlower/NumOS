ENTRY(_start)

PHDRS
{
    text    PT_LOAD FLAGS(5);
    rodata  PT_LOAD FLAGS(4);
    data    PT_LOAD FLAGS(6);
    tls     PT_TLS  FLAGS(4);
}

SECTIONS
{
    . = 0x0000000060000000;

    .text ALIGN(4096) : {
        *(.text)
        *(.text.*)
    } :text

    .rodata ALIGN(4096) : {
        *(.rodata)
        *(.rodata.*)
    } :rodata

    .init_array ALIGN(8) : {
        __init_array_start = .;
        KEEP(*(.init_array))
        KEEP(*(.init_array.*))
        __init_array_end = .;
    } :data

    .tdata ALIGN(16) : {
        *(.tdata)
        *(.tdata.*)
    } :data :tls

    .tbss ALIGN(16) : {
        *(.tbss)
        *(.tbss.*)
    } :data :tls

    .data ALIGN(4096) : {
        *(.data)
        *(.data.*)
    } :data

    .bss ALIGN(4096) : {
        __bss_start = .;
        *(COMMON)
        *(.bss)
        *(.bss.*)
        __bss_end = .;
    } :data

    /DISCARD/ : {
        *(.comment)
        *(.note)
        *(.note.*)
        *(.eh_frame)
        *(.eh_frame_hdr)
        *(.gnu.hash)
        *(.dynsym)
        *(.dynstr)
        *(.dynamic)
        *(.got)
        *(.got.plt)
        *(.plt)
        *(.interp)
    }
}
