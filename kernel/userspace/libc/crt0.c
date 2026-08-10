#include "syscall.h"

extern char **environ;
int main(int argc, char **argv, char **envp);

__attribute__((naked))
void _start(void)
{
    __asm__ volatile (
        "mov (%%rsp), %%rdi\n\t"
        "lea 8(%%rsp), %%rsi\n\t"
        "lea 8(%%rsi,%%rdi,8), %%rdx\n\t"
        "mov %%rdx, environ(%%rip)\n\t"
        "call main\n\t"
        "mov %%eax, %%edi\n\t"
        "mov $%c[sc], %%rax\n\t"
        "syscall\n\t"
        : : [sc] "i" (SYS_EXIT) : "rax", "rdi", "rsi", "rcx", "r11", "rdx", "memory"
    );
}
