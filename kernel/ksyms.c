#include "module.h"
#include "printk.h"
#include "slab.h"
#include "string.h"
#include "pmm.h"
#include "vmm.h"
#include "pci.h"
#include "netdev.h"
#include "net_buf.h"
#include "dhcp.h"
#include "driver.h"
#include "ioapic.h"
#include "hal/cshim.h"

/* ===========================================================================
 * Exported kernel symbols available to .kxt modules.
 *
 * Every symbol a module may reference must be listed here exactly once, with
 * a definition visible in this translation unit (so &sym resolves at final
 * link).  The KSYM macro places a {name, address} record into the .ksymtab
 * section, which module.c scans at load time.
 * =========================================================================== */

/* Diagnostics / memory. */
KSYM(printk);
KSYM(kmalloc);
KSYM(kfree);
KSYM(memcpy);
KSYM(memset);
KSYM(strcpy);
KSYM(strlen);

/* Physical / virtual memory. */
KSYM(pmm_alloc_frame);
KSYM(pmm_alloc_frames);
KSYM(pmm_free_frame);
KSYM(pmm_free_frames);
KSYM(vmm_mmap_phys);
KSYM(vmm_unmap_page);
KSYM(vmm_map_page);

/* HAL CPU control (used by shared spinlock.h in modules). */
KSYM(hal_cpu_save_irq);
KSYM(hal_cpu_restore_irq);
KSYM(hal_cpu_pause);

/* PCI bus access. */
KSYM(pci_read32);
KSYM(pci_write32);
KSYM(pci_bar_addr);
KSYM(pci_first);

/* Driver framework. */
KSYM(driver_register);
KSYM(driver_unregister);
KSYM(driver_probe_pci);
KSYM(driver_probe_all);
KSYM(device_register);
KSYM(device_find);

/* Network stack. */
KSYM(netdev_register);
KSYM(netdev_register_poll);
KSYM(netdev_unregister_poll);
KSYM(netdev_rx_poll);
KSYM(mbuf_alloc);
KSYM(mbuf_free);
KSYM(mbuf_total_len);
KSYM(dhcp_start);

/* Interrupt control. */
KSYM(ioapic_mask);
