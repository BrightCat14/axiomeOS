#include "tss.h"
#include "printk.h"

extern uint64_t gdt_tss_desc[2];

struct tss tss = {0};
static uint8_t df_stack[4096] __attribute__((aligned(16)));

void tss_init(void)
{
    uint64_t tss_base = (uint64_t)&tss;

    tss.ist[0] = (uintptr_t)df_stack + sizeof(df_stack);
    tss.iomap_base = sizeof(tss);

    uint64_t limit = sizeof(tss) - 1;
    gdt_tss_desc[0] = (limit & 0xFFFF)
                    | ((tss_base & 0xFFFF) << 16)
                    | (((tss_base >> 16) & 0xFF) << 32)
                    | ((uint64_t)0x89 << 40)
                    | ((((uint64_t)limit >> 16) & 0x0F) << 48)
                    | (((uint64_t)(tss_base >> 24) & 0xFF) << 56);

    gdt_tss_desc[1] = tss_base >> 32;

    __asm__ volatile("ltr %%ax" : : "a"((uint16_t)(7 * 8)));

    printk("TSS: loaded (base=0x%lx df_stack=0x%lx)\n",
           (unsigned long)tss_base, (unsigned long)tss.ist[0]);
}

void tss_set_rsp0(uint64_t rsp0)
{
    tss.rsp[0] = rsp0;
    printk("TSS: RSP0 set to 0x%lx\n", rsp0);
}
