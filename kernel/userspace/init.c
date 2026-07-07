#include "stdio.h"
#include "syscall.h"
#include "stdlib.h"
#include "string.h"

static volatile unsigned long g_flag = 0x1234;

int main(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    printf("INIT pid=%d\n", (int)sys_getpid());

    int status = 0;
#if 1
    long child = sys_fork();
    if (child == 0)
    {
        printf("FORK CHILD pid=%d g_flag=%lu (isolated => 0x1234)\n",
               (int)sys_getpid(), g_flag);
        sys_exit(42);
    }

    g_flag = 0xAAAA;
    printf("PARENT forked child_pid=%ld g_flag=%lu (wrote 0xAAAA after fork)\n",
           child, g_flag);

    long wpid = sys_waitpid((int)child, &status);
    printf("PARENT waitpid(%ld) => status=%d\n", wpid, status);
#endif

    long hello_pid = sys_spawn(1);
    printf("PARENT spawned hello pid=%ld\n", hello_pid);

    long hwpid = sys_waitpid((int)hello_pid, &status);
    printf("PARENT waitpid(%ld) => status=%d\n", hwpid, status);

    puts("TTY: write path works");

    puts("--- Phase 10: VFS coreutils ---");
    long echo_pid = sys_spawn_cmd("echo hello world from axiome libc",
                                   strlen("echo hello world from axiome libc"));
    int st = 0;
    if (echo_pid > 0)
    {
        sys_waitpid((int)echo_pid, &st);
        printf("echo exited status=%d\n", st);
    }

    long sh_pid = sys_spawn_cmd("sh -t", strlen("sh -t"));
    if (sh_pid > 0)
    {
        sys_waitpid((int)sh_pid, &st);
        printf("sh -t exited status=%d\n", st);
    }

    sh_pid = sys_spawn_cmd("sh", 2);
    if (sh_pid > 0)
    {
        sys_waitpid((int)sh_pid, &st);
        printf("sh exited status=%d\n", st);
    }

    void *p = malloc(16);
    printf("INIT done, exiting 0 (libc malloc test: %p)\n", p);
    sys_exit(0);
    return 0;
}
