#ifndef AXIOME_MODULE_H
#define AXIOME_MODULE_H

#include <stdint.h>
#include <stddef.h>

/* ===========================================================================
 * Loadable kernel module framework (".kxt").
 *
 * A module is a freestanding, -mcmodel=large ELF relocatable object.  It is
 * linked against a table of *exported* kernel symbols (see ksyms.c / the
 * .ksymtab linker section) rather than against the kernel directly, so the
 * loader relocates it at load time and resolves every external reference by
 * name.  Because modules use the large code model, all cross-module addresses
 * are 64-bit absolute, so there is no 2 GB placement constraint.
 * =========================================================================== */

/* One exported kernel symbol, collected by the linker into .ksymtab. */
struct ksym {
    const char *name;
    void       *addr;
};

/* Declare that `sym` (a kernel function or object) is available to modules.
 * Place KSYM(sym) once, anywhere a definition of `sym` is visible. */
#define KSYM(sym)                                                          \
    static const struct ksym __ksym_##sym                                  \
        __attribute__((used, section(".ksymtab"))) =                       \
        { #sym, (void *)(&sym) }

/* Module entry/exit points.  A module defines module_init()/module_exit()
 * (usually via these macros) and the loader calls them at load/unload time. */
#define MODULE_INIT(fn)   void module_init(void) { fn(); }
#define MODULE_EXIT(fn)   void module_exit(void) { fn(); }

struct module;

/* Kernel-side loader API. */
int  module_load_file(const char *path);   /* read ELF from VFS, then load   */
int  module_load(const uint8_t *elf, size_t size, const char *name);
int  module_unload(const char *name);
void module_list(void);                    /* printk the loaded modules      */
void module_init_subsys(void);

/* Generic network RX poll registry, used by NIC modules so the kernel main
 * loop can poll every registered driver without hard-coding any of them. */
void netdev_register_poll(void (*fn)(void));
void netdev_poll_all(void);

#endif
