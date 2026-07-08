OUTPUT_FORMAT(elf64-x86-64)
ENTRY(_start)

KERNEL_VIRT_BASE = 0xFFFFFFFF80000000;

SECTIONS
{
    . = 0x200000;
    . += KERNEL_VIRT_BASE;

    .text : AT(ADDR(.text) - KERNEL_VIRT_BASE)
    {
        *(.multiboot)
        *(.text*)
        *(.gnu.linkonce.t*)
    } :text

    .rodata : AT(ADDR(.rodata) - KERNEL_VIRT_BASE)
    {
        *(.rodata*)
    } :rodata

    /* Exported kernel symbols available to loadable .kxt modules. */
    .ksymtab : AT(ADDR(.ksymtab) - KERNEL_VIRT_BASE)
    {
        __ksymtab_start = .;
        KEEP(*(.ksymtab))
        __ksymtab_end = .;
    } :rodata

    .data : AT(ADDR(.data) - KERNEL_VIRT_BASE)
    {
        *(.data*)
    } :data

    _bss_start = .;
    .bss : AT(ADDR(.bss) - KERNEL_VIRT_BASE)
    {
        *(COMMON)
        *(.bss*)
        . = ALIGN(4096);
    } :data
    _bss_end = .;

    /DISCARD/ : { *(.comment) *(.eh_frame) *(.note*) }
}

PHDRS
{
    text   PT_LOAD FLAGS(5);
    rodata PT_LOAD FLAGS(4);
    data   PT_LOAD FLAGS(6);
}
