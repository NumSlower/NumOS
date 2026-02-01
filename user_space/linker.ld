/* User Space Linker Script for NumOS
   ===================================
   
   User-space processes are loaded at 0x400000 (4MB).
   This is position-dependent; the kernel's ELF loader handles relocation.
   
   Memory layout:
   - 0x400000 - 0x400FFF: .text (code)
   - 0x401000+:         .data, .bss, .heap, .stack
*/

ENTRY(main)

PHDRS
{
    text PT_LOAD FLAGS(5);  /* Read + Execute */
    data PT_LOAD FLAGS(6);  /* Read + Write */
}

SECTIONS
{
    /* User process base address */
    . = 0x400000;

    /* Start of user process */
    _user_start = .;

    /* Code section (Read + Execute) */
    .text ALIGN(0x1000) : {
        _text_start = .;
        *(.text)
        *(.text.*)
        _text_end = .;
    } :text

    /* Read-only data (part of text segment for simplicity) */
    .rodata ALIGN(0x1000) : {
        _rodata_start = .;
        *(.rodata)
        *(.rodata.*)
        _rodata_end = .;
    } :text

    /* Initialized data (Read + Write) */
    .data ALIGN(0x1000) : {
        _data_start = .;
        *(.data)
        *(.data.*)
        _data_end = .;
    } :data

    /* Uninitialized data (Read + Write) */
    .bss ALIGN(0x1000) : {
        _bss_start = .;
        *(COMMON)
        *(.bss)
        *(.bss.*)
        _bss_end = .;
    } :data

    /* User heap (grows upward) */
    .heap ALIGN(0x1000) : {
        _heap_start = .;
        . += 0x100000;  /* 1MB heap */
        _heap_end = .;
    } :data

    /* User stack (grows downward) */
    .stack ALIGN(0x1000) : {
        _stack_start = .;
        . += 0x80000;   /* 512KB stack */
        _stack_end = .;
    } :data

    /* End of user process */
    _user_end = .;

    /* Discard unnecessary sections */
    /DISCARD/ : {
        *(.comment)
        *(.note)
        *(.note.*)
        *(.eh_frame)
        *(.eh_frame_hdr)
    }
}
