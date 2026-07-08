#include "syscall.h"

int main(int argc, char **argv);

/* The kernel's iretq delivers RSP pointing directly at argc (16-byte aligned).
   This entry must NOT modify RSP before reading argc/argv, so it is naked and
   reads them from the entry stack in the first instructions. */
__attribute__((naked))
void _start(void)
{
    __asm__ volatile (
        "mov (%%rsp), %%rdi\n\t"   /* argc  */
        "lea 8(%%rsp), %%rsi\n\t"  /* argv  */
        "call main\n\t"
        "mov %%eax, %%edi\n\t"
        "mov $%c[sc], %%rax\n\t"   /* SYS_EXIT */
        "syscall\n\t"
        : : [sc] "i" (SYS_EXIT) : "rax", "rdi", "rsi", "rcx", "r11", "rdx", "memory"
    );
}
