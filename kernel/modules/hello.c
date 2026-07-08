#include "printk.h"
#include "module.h"

/* Minimal example module: proves the .kxt loader, symbol resolution and
   init/exit lifecycle work end-to-end without requiring any hardware. */

static int hello_mod_init(void)
{
    printk("hello.kxt: loaded — greetings from a loadable kernel module!\n");
    return 0;
}

static void hello_mod_exit(void)
{
    printk("hello.kxt: unloading — farewell.\n");
}

MODULE_INIT(hello_mod_init);
MODULE_EXIT(hello_mod_exit);
