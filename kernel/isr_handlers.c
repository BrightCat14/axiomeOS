#include "idt.h"
#include "printk.h"
#include "apic.h"
#include "keyboard.h"
#include "mouse.h"
#include "serial.h"
#include "tty.h"
#include "vmm.h"
#include "pmm.h"
#include "sched.h"
#include "hal/cshim.h"

static const char *exception_names[] = {
    [0]  = "Divide Error",
    [1]  = "Debug",
    [2]  = "NMI",
    [3]  = "Breakpoint",
    [4]  = "Overflow",
    [5]  = "Bound Range",
    [6]  = "Invalid Opcode",
    [7]  = "Device Not Available",
    [8]  = "Double Fault",
    [9]  = "Coprocessor Segment",
    [10] = "Invalid TSS",
    [11] = "Segment Not Present",
    [12] = "Stack Segment",
    [13] = "General Protection",
    [14] = "Page Fault",
    [15] = "(reserved 15)",
    [16] = "x87 FPU Error",
    [17] = "Alignment Check",
    [18] = "Machine Check",
    [19] = "SIMD FPU",
    [20] = "Virtualization",
    [21] = "Control Protection",
};

void isr_handler(struct isr_frame *frame)
{
    if (frame->int_no < 32)
    {
        const char *name = exception_names[frame->int_no];
        if (!name)
            name = "(unknown)";

        printk("\n!!! EXCEPTION: %s (%d)\n", name, frame->int_no);
        printk("    err_code=0x%lx  RIP=0x%lx  CS=0x%lx  RFLAGS=0x%lx\n",
               frame->err_code, frame->rip, frame->cs, frame->rflags);
        printk("    RSP=0x%lx  SS=0x%lx\n", frame->rsp, frame->ss);

        if (frame->int_no == 14)
        {
            unsigned long cr2 = (unsigned long)hal_cpu_fault_address();
            int user = (frame->cs & 3) != 0;
            printk("!!! PAGE FAULT @0x%lx ec=0x%lx (user=%d) RIP=0x%lx\n",
                   cr2, frame->err_code, user, frame->rip);
            if (user)
                sched_exit(139);
        }

        if (frame->int_no == 8)
            printk("    *** DOUBLE FAULT (Trying to dump state) ***\n");

        while (1)
            hal_cpu_halt();
    }
    else if (frame->int_no >= 0x20)
    {
        /* Device interrupts: dispatch through the HAL so handlers are
           registered centrally (see isr_init) and the APIC/IOAPIC wiring
           stays behind the HAL interface. */
        hal_irq_dispatch((int)frame->int_no);
    }
}

/* HAL device-vector wrappers. Timer and the PS/2 / serial devices keep the
   exact EOI ordering of the old hardcoded dispatch. */

static void timer_irq_wrapper(void *ctx)
{
    (void)ctx;
    apic_timer_tick();
    sched_tick();
    if (sched_current() && sched_current()->quantum <= 0)
        sched_yield();
}

static void kbd_irq_wrapper(void *ctx)
{
    (void)ctx;
    keyboard_irq_handler();
    hal_irq_eoi();
}

static void mouse_irq_wrapper(void *ctx)
{
    (void)ctx;
    mouse_irq_handler();
    hal_irq_eoi();
}

static void serial_irq_wrapper(void *ctx)
{
    (void)ctx;
    serial_irq_handler();
    hal_irq_eoi();
}

void isr_init(void)
{
    extern void isr0(void), isr1(void), isr2(void), isr3(void);
    extern void isr4(void), isr5(void), isr6(void), isr7(void);
    extern void isr8(void), isr9(void), isr10(void), isr11(void);
    extern void isr12(void), isr13(void), isr14(void), isr15(void);
    extern void isr16(void), isr17(void), isr18(void), isr19(void);
    extern void isr20(void), isr21(void), isr22(void), isr23(void);
    extern void isr24(void), isr25(void), isr26(void), isr27(void);
    extern void isr28(void), isr29(void), isr30(void), isr31(void);

    extern void isr_timer(void);
    extern void isr_kbd(void);
    extern void isr_mouse(void);
    extern void isr_serial(void);

    uintptr_t handlers[32] = {
        (uintptr_t)isr0,  (uintptr_t)isr1,  (uintptr_t)isr2,  (uintptr_t)isr3,
        (uintptr_t)isr4,  (uintptr_t)isr5,  (uintptr_t)isr6,  (uintptr_t)isr7,
        (uintptr_t)isr8,  (uintptr_t)isr9,  (uintptr_t)isr10, (uintptr_t)isr11,
        (uintptr_t)isr12, (uintptr_t)isr13, (uintptr_t)isr14, (uintptr_t)isr15,
        (uintptr_t)isr16, (uintptr_t)isr17, (uintptr_t)isr18, (uintptr_t)isr19,
        (uintptr_t)isr20, (uintptr_t)isr21, (uintptr_t)isr22, (uintptr_t)isr23,
        (uintptr_t)isr24, (uintptr_t)isr25, (uintptr_t)isr26, (uintptr_t)isr27,
        (uintptr_t)isr28, (uintptr_t)isr29, (uintptr_t)isr30, (uintptr_t)isr31,
    };

    for (int i = 0; i < 32; i++)
    {
        uint8_t flags = 0x8E;
        uint8_t ist = 0;
        if (i == 8)
            ist = 1;
        idt_set_gate(i, handlers[i], flags, ist);
    }

    idt_set_gate(0x20, (uintptr_t)isr_timer, 0x8E, 0);
    idt_set_gate(0x21, (uintptr_t)isr_kbd, 0x8E, 0);
    idt_set_gate(0x22, (uintptr_t)isr_mouse, 0x8E, 0);
    idt_set_gate(0x24, (uintptr_t)isr_serial, 0x8E, 0);

    /* Bind device vectors to their HAL-registered handlers. */
    hal_irq_register(0x20, timer_irq_wrapper, 0);
    hal_irq_register(0x21, kbd_irq_wrapper, 0);
    hal_irq_register(0x22, mouse_irq_wrapper, 0);
    hal_irq_register(0x24, serial_irq_wrapper, 0);
}
