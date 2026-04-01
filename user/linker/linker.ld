/* =============================================================================
 * user/linker/linker.ld - NumOS User-Space Linker Script
 *
 * Loads the executable at USER_VIRTUAL_BASE = 0x0000000000400000,
 * matching the definition in Include/cpu/paging.h.
 *
 * PHDRS are declared explicitly so the linker produces separate
 * PT_LOAD segments with correct permissions instead of one combined
 * RWX segment (which triggers a linker warning and is bad practice).
 *
 * Resulting PT_LOAD segments:
 *   [0] RX  0x400000   .text
 *   [1] R   next page  .rodata
 *   [2] RW  next page  .data + .bss
 * ============================================================================= */

ENTRY(_start)

PHDRS
{
    text    PT_LOAD FLAGS(5);   /* R + X */
    rodata  PT_LOAD FLAGS(4);   /* R     */
    data    PT_LOAD FLAGS(6);   /* R + W */
    tls     PT_TLS  FLAGS(4);   /* R     */
}

SECTIONS
{
    . = 0x0000000060000000;

    /* ------------------------------------------------------------------ */
    /* .text  -  executable code, read + execute                          */
    /* ------------------------------------------------------------------ */
    .text ALIGN(4096) : {
        *(.text)
        *(.text.*)
    } :text

    /* ------------------------------------------------------------------ */
    /* .rodata  -  read-only constants and string literals                */
    /* ------------------------------------------------------------------ */
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

    /* ------------------------------------------------------------------ */
    /* .data  -  initialised read-write data                              */
    /* ------------------------------------------------------------------ */
    .data ALIGN(4096) : {
        *(.data)
        *(.data.*)
    } :data

    /* ------------------------------------------------------------------ */
    /* .bss  -  zero-initialised read-write data (no file space used)     */
    /* ------------------------------------------------------------------ */
    .bss ALIGN(4096) : {
        __bss_start = .;
        *(COMMON)
        *(.bss)
        *(.bss.*)
        __bss_end = .;
    } :data

    /* ------------------------------------------------------------------ */
    /* Discard sections the kernel ELF loader doesn't need                */
    /* ------------------------------------------------------------------ */
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
