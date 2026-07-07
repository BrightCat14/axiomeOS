#include "stdio.h"
#include "syscall.h"
#include "string.h"

int main(int argc, char **argv)
{
    if (argc < 3)
    {
        puts("usage: cp <src> <dst>");
        sys_exit(1);
        return 1;
    }
    int in = open(argv[1], O_RDONLY);
    if (in < 0)
    {
        printf("cp: %s: no such file\n", argv[1]);
        sys_exit(1);
        return 1;
    }
    int out = open(argv[2], O_CREAT | O_WRONLY | O_TRUNC);
    if (out < 0)
    {
        printf("cp: cannot create '%s'\n", argv[2]);
        close(in);
        sys_exit(1);
        return 1;
    }
    char buf[256];
    long r;
    long total = 0;
    while ((r = read(in, buf, sizeof buf)) > 0)
    {
        long w = write(out, buf, (size_t)r);
        if (w < 0)
            break;
        total += w;
    }
    close(in);
    close(out);
    printf("cp: %ld bytes copied\n", total);
    sys_exit(0);
    return 0;
}
