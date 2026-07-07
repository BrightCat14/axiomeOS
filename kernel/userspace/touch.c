#include "stdio.h"
#include "syscall.h"

int main(int argc, char **argv)
{
    if (argc < 2)
    {
        puts("usage: touch <file> [...]");
        sys_exit(1);
        return 1;
    }
    int st = 0;
    for (int i = 1; i < argc; i++)
    {
        int fd = open(argv[i], O_CREAT | O_WRONLY);
        if (fd < 0)
        {
            printf("touch: cannot create '%s'\n", argv[i]);
            st = 1;
            continue;
        }
        close(fd);
    }
    sys_exit(st);
    return st;
}
