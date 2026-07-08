#include "stdio.h"
#include "syscall.h"
#include "stdlib.h"
#include "signal.h"

int main(int argc, char **argv)
{
    if (argc < 3)
    {
        puts("usage: kill <pid> <sig>");
        return 1;
    }

    int pid = atoi(argv[1]);
    int sig = atoi(argv[2]);

    if (pid <= 0)
    {
        puts("kill: invalid pid");
        return 1;
    }
    if (sig <= 0 || sig >= NSIG)
    {
        puts("kill: invalid signal");
        return 1;
    }

    int r = kill(pid, sig);
    if (r < 0)
    {
        printf("kill: cannot signal pid %d (err %d)\n", pid, r);
        return 1;
    }

    printf("kill: signal %d sent to pid %d\n", sig, pid);
    return 0;
}
