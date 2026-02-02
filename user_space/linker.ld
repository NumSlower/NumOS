/* User Space Linker Script for NumOS
   ===================================
   
   User-space processes are loaded at 0x400000 (4MB).
   This is position-dependent; the kernel's ELF loader handles relocation.
   
   Memory layout:
   - 0x400000 - 0x7FFFFF: User process space (code + data)
   - 0x7FC000 - 0x800000: User stack (managed by kernel)
   
   NOTE: Do NOT define heap/stack in the ELF. The kernel provides
   these at runtime. Only code and data sections go here.
*/

ENTRY(_start)

PHDRS
{
    text PT_LOAD FLAGS(5);  /* Read + Execute */
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
    } :text

    /* Uninitialized data (Read + Write) */
    .bss ALIGN(0x1000) : {
        _bss_start = .;
        *(COMMON)
        *(.bss)
        *(.bss.*)
        _bss_end = .;
    } :text

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
