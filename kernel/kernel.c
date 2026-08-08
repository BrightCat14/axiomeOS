#include "serial.h"
#include "printk.h"
#include "multiboot2.h"
#include "framebuffer.h"
#include "pmm.h"
#include "apic.h"
#include "tss.h"
#include "ioapic.h"
#include "keyboard.h"
#include "tty.h"
#include "vfs.h"
#include "pci.h"
#include "driver.h"
#include "mouse.h"
#include "softirq.h"
#include "vmm.h"
#include "slab.h"
#include "sched.h"
#include "syscall.h"
#include "elf.h"
#include "ide.h"
#include "fat32.h"
#include "axiomefs.h"
#include "security.h"
#include "netdev.h"
#include "socket.h"
#include "loopback.h"
#include "tcp.h"
#include "arp.h"
#include "module.h"
#include "acpi.h"
#include "mmap.h"
#include "hal/cshim.h"
#include "hal/hal_bootinfo.h"

void mb2_parse(unsigned long mb2_info_addr);
void isr_init(void);

/* Load the init program from the root partition and start it as PID 1.
   The buffer is freed after exec_user_program() copies it into the new
   address space. */
static void exec_init_from_disk(void)
{
    uint8_t *buf = 0;
    size_t size = 0;
    if (vfs_read_file("/bin/init", &buf, &size) != 0)
    {
        printk("Init: cannot load /bin/init from disk\n");
        return;
    }
    printk("Init: loaded %lu bytes from /bin/init\n", size);
    exec_user_program(buf, size, "init");
    kfree(buf);
}

/* Respawn the userspace init process after it has exited. */
void kernel_respawn_init(void)
{
    printk("Init: respawning userspace init\n");
    exec_init_from_disk();
}

void kmain(unsigned long magic, unsigned long mb2_info_addr)
{
    hal_init();
    serial_init(COM1);

    printk("axiomeOS booting...\n");

    mb2_parse(mb2_info_addr);

    pmm_init();
    vmm_init();
    fb_init_buffers();
    acpi_init(acpi_rsdp_addr);

    /* Boot environment as seen through the HAL (filled by the boot glue,
       e.g. mb2.c). */
    struct hal_bootinfo *bi = hal_bootinfo();

    if (bi->framebuffer_present)
    {
        fb_clear();
        fb_write("Hello, axiomeOS!\n");
        fb_write("Framebuffer active\n");
    }
    else
    {
        printk("No framebuffer available (use -vga std)\n");
    }

    void *p = pmm_alloc_frame();
    printk("PMM: allocated frame at 0x%lx\n", (unsigned long)p);
    if (p)
    {
        pmm_free_frame(p);
        printk("PMM: freed\n");
    }

    slab_init();

    printk("--- Phase 4: Memory stress test ---\n");
    {
        uint64_t before = pmm_free_count();
        void *ptrs[32];
        int n;
        for (n = 0; n < 32; n++)
        {
            ptrs[n] = kmalloc(64);
            if (!ptrs[n]) break;
            *(volatile uint64_t *)ptrs[n] = 0xCAFEBABE;
        }
        printk("Slab: allocated %d objects (64 bytes each)\n", n);

        for (int i = 0; i < n; i++)
        {
            if (*(volatile uint64_t *)ptrs[i] != 0xCAFEBABE)
                printk("  ERROR: data corruption at ptr[%d]\n", i);
            kfree(ptrs[i]);
        }
        uint64_t after = pmm_free_count();
        printk("Slab: freed all, frames before=%lu after=%lu\n", before, after);

        uint64_t phys = (uint64_t)pmm_alloc_frame();
        if (phys)
        {
            void *mapped = vmm_mmap_phys(phys, 1, MMU_WRITE);
            printk("VMM: mapped phys 0x%lx -> virt %p\n", phys, mapped);
            if (mapped)
            {
                *(volatile uint64_t *)mapped = 0xDEADBEEF;
                printk("VMM: wrote 0x%lx to mapped region, readback=0x%lx\n",
                       0xDEADBEEFull, *(volatile uint64_t *)mapped);
            }
            pmm_free_frame((void *)phys);
        }

        uint64_t demand_virt = 0xFFFFFE0000200000ULL;
        uint64_t dp_page = (uint64_t)pmm_alloc_frame();
        if (dp_page)
        {
            if (vmm_map_page(demand_virt, dp_page, MMU_WRITE) == 0)
            {
                *(volatile char *)demand_virt = 'D';
                uint64_t mapped = vmm_virt_to_phys(demand_virt);
                printk("VMM: demand-mapped 0x%lx -> phys 0x%lx (readback '%c')\n",
                       demand_virt, mapped, *(volatile char *)demand_virt);
            }
            else
                printk("VMM: demand map failed\n");
        }
    }
    printk("--- Phase 4: memory test done ---\n");

    tss_init();
    isr_init();
    hal_timer_start(0x10000);
    ioapic_init();
    keyboard_init();
    tty_init();
    serial_init_input();
    mouse_init();

    sched_init();

    syscall_init();

    vfs_init();

    procfs_init();

    pci_init();

    /* Network stack init: mbuf pool, loopback, protocol handlers.
       Must come before driver_init() because e1000 probe needs mbufs. */
    net_init();
    socket_init();

    driver_init();
    devfs_init();
    module_init_subsys();

    fat32_automount();

    /* Mount the axiomefs persistent root from partition 1 of the boot disk
       (second MBR partition, 0-indexed) and parse /etc/passwd into the
       in-kernel user database. */
    axiomefs_mount_part(0, 0, 1, "/");
    security_init();

    klog_init_late();

    uint64_t sys_ret = syscall_dispatch(SYS_PRINT, (uint64_t)"hello from syscall dispatch", 0, 0, 0, 0);
    printk("Syscall dispatch returned: %lu\n", sys_ret);

    exec_init_from_disk();

    printk("Trying demand paging via fault...\n");
    uint64_t *dp_fault = (uint64_t *)0xFFFFFE0000300000ULL;
    *dp_fault = 0x4242;
    printk("Demand paging OK: 0x%lx\n", *dp_fault);

    uint64_t yield_count = 0;
    printk("APIC: tick");
    hal_cpu_irq_enable();

    klog_flush();

    /* Hand control to the shell with a clean framebuffer: the boot logs that
       scrolled above are not kernel noise the user needs to see. Scheduler
       bookkeeping is routed to klog() (serial + /var/log/kernel.log only), so the
       running shell stays free of visual noise. */
    if (fb_active())
        fb_clear();

    sched_yield();

    while (1)
    {
        softirq_poll();
        netdev_poll_all();

        yield_count++;
        if ((yield_count % 50) == 0)
        {
            arp_tick();
            tcp_tick((uint32_t)(yield_count * 10));  /* rough ms estimate */
        }
        if ((yield_count % 5) == 0)
        {
            klog_flush();
            fb_flush();
            sched_yield();
        }

        hal_cpu_halt();
    }
}
