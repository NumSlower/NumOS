ENTRY(_start)

PHDRS
{
    text    PT_LOAD    FLAGS(5);
    rodata  PT_LOAD    FLAGS(4);
    data    PT_LOAD    FLAGS(6);
    dynamic PT_DYNAMIC FLAGS(6);
    tls     PT_TLS     FLAGS(4);
}

SECTIONS
{
    . = 0x0;

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

    .dynamic ALIGN(8) : {
        *(.dynamic)
    } :data :dynamic

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

    .got ALIGN(8) : {
        *(.got)
        *(.got.*)
        *(.got.plt)
    } :data

    .rela.dyn ALIGN(8) : {
        *(.rela.dyn)
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
        *(.interp)
    }
}
