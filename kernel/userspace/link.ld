OUTPUT_FORMAT("elf64-x86-64")
ENTRY(_start)

SECTIONS
{
    . = 0x10000000000;

    .text : {
        *(.text*)
    }

    .rodata : {
        *(.rodata*)
        *(.rodata)
    }

    .data : {
        *(.data*)
    }

    .bss : {
        *(.bss*)
        *(COMMON)
    }
}
