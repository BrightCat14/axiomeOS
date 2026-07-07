#include "stdio.h"
#include "syscall.h"
#include "string.h"

int main(int argc, char **argv)
{
    if (argc < 3)
    {
        puts("usage: mv <src> <dst>");
        sys_exit(1);
        return 1;
    }
    int in = open(argv[1], O_RDONLY);
    if (in < 0)
    {
        printf("mv: %s: no such file\n", argv[1]);
        sys_exit(1);
        return 1;
    }
    int out = open(argv[2], O_CREAT | O_WRONLY | O_TRUNC);
    if (out < 0)
    {
        close(in);
        printf("mv: cannot create '%s'\n", argv[2]);
        sys_exit(1);
        return 1;
    }
    char buf[256];
    long r;
    while ((r = read(in, buf, sizeof buf)) > 0)
        write(out, buf, (size_t)r);
    close(in);
    close(out);
    unlink(argv[1]);
    sys_exit(0);
    return 0;
}
