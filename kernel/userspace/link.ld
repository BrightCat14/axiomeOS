OUTPUT_FORMAT("elf64-x86-64")
ENTRY(_start)

PHDRS
{
    text PT_LOAD FLAGS(5);   /* R | X */
    data PT_LOAD FLAGS(6);   /* R | W */
}

SECTIONS
{
    . = 0x10000000000;

    .text : {
        *(.text*)
        *(.text)
    } :text

    . = ALIGN(0x1000);
    .rodata : {
        *(.rodata*)
        *(.rodata)
    } :text

    . = ALIGN(0x1000);
    .data : {
        *(.data*)
        *(.sdata*)
    } :data

    . = ALIGN(0x1000);
    .bss : {
        *(.bss*)
        *(COMMON)
    } :data
}
