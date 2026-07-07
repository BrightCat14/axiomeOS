#include "apic.h"
#include "printk.h"
#include "pmm.h"

#define IA32_APIC_BASE_MSR 0x1B
#define APIC_DEFAULT_BASE 0xFEE00000ULL

#define APIC_OFFSET_ID       0x020
#define APIC_OFFSET_EOI      0x0B0
#define APIC_OFFSET_SPURIOUS 0x0F0
#define APIC_OFFSET_LVT_TIMER  0x320
#define APIC_OFFSET_TIMER_INIT 0x380
#define APIC_OFFSET_TIMER_DIV  0x3E0

#define APIC_SPURIOUS_ENABLE (1 << 8)
#define APIC_LVT_PERIODIC    (1 << 17)
#define APIC_TIMER_DIV16     3

static volatile uint32_t *apic_base;
static volatile uint64_t timer_ticks;

extern uint64_t pd_table3[512];

static uint64_t rdmsr(uint32_t msr)
{
    uint32_t lo, hi;
    __asm__ volatile("rdmsr" : "=a"(lo), "=d"(hi) : "c"(msr));
    return ((uint64_t)hi << 32) | lo;
}

static void wrmsr(uint32_t msr, uint64_t value)
{
    uint32_t lo = value & 0xFFFFFFFF;
    uint32_t hi = value >> 32;
    __asm__ volatile("wrmsr" : : "a"(lo), "d"(hi), "c"(msr));
}

static void apic_write(unsigned int reg, uint32_t val)
{
    apic_base[reg / 4] = val;
}

static uint32_t apic_read(unsigned int reg)
{
    return apic_base[reg / 4];
}

void apic_timer_tick(void)
{
    apic_eoi();
    timer_ticks++;
}

void apic_init(void)
{
    timer_ticks = 0;

    uintptr_t apic_phys = rdmsr(IA32_APIC_BASE_MSR) & 0xFFFFFF000ULL;
    printk("APIC: base=0x%lx\n", (unsigned long)apic_phys);

    if (apic_phys == 0)
        apic_phys = APIC_DEFAULT_BASE;

    unsigned int pd_idx = (apic_phys >> 21) & 0x1FF;
    pd_table3[pd_idx] = apic_phys | 0x83;
    __asm__ volatile("mov %%cr3, %%rax; mov %%rax, %%cr3" ::: "rax");

    apic_base = (volatile uint32_t *)apic_phys;

    uint64_t apic_msr = rdmsr(IA32_APIC_BASE_MSR);
    apic_msr |= (1ULL << 11);
    wrmsr(IA32_APIC_BASE_MSR, apic_msr);

    apic_write(APIC_OFFSET_SPURIOUS, 0xFF | APIC_SPURIOUS_ENABLE);
    apic_read(APIC_OFFSET_SPURIOUS);

    __asm__ volatile("outb %0, %1" : : "a"((uint8_t)0xFF), "d"((uint16_t)0xA1));
    __asm__ volatile("outb %0, %1" : : "a"((uint8_t)0xFF), "d"((uint16_t)0x21));

    apic_write(APIC_OFFSET_TIMER_DIV, APIC_TIMER_DIV16);
    apic_write(APIC_OFFSET_LVT_TIMER, 0x20 | APIC_LVT_PERIODIC);
    apic_write(APIC_OFFSET_TIMER_INIT, 0x10000);

    printk("APIC: timer started\n");
}

void apic_eoi(void)
{
    apic_write(APIC_OFFSET_EOI, 0);
}

uint64_t apic_get_ticks(void)
{
    return timer_ticks;
}
