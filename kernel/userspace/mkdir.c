#include "stdio.h"
#include "syscall.h"

int main(int argc, char **argv)
{
    if (argc < 2)
    {
        puts("usage: mkdir <dir> [...]");
        sys_exit(1);
        return 1;
    }
    int st = 0;
    for (int i = 1; i < argc; i++)
    {
        if (mkdir(argv[i]) < 0)
        {
            printf("mkdir: cannot create '%s'\n", argv[i]);
            st = 1;
        }
    }
    sys_exit(st);
    return st;
}
