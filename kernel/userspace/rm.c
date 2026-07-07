#include "stdio.h"
#include "syscall.h"

int main(int argc, char **argv)
{
    if (argc < 2)
    {
        puts("usage: rm <file> [...]");
        sys_exit(1);
        return 1;
    }
    int st = 0;
    for (int i = 1; i < argc; i++)
    {
        if (unlink(argv[i]) < 0)
        {
            printf("rm: cannot remove '%s'\n", argv[i]);
            st = 1;
        }
    }
    sys_exit(st);
    return st;
}
