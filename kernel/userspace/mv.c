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
    int ok = 1;
    while ((r = read(in, buf, sizeof buf)) > 0)
    {
        size_t off = 0;
        while ((size_t)off < (size_t)r)
        {
            long w = write(out, buf + off, (size_t)r - off);
            if (w < 0)
            {
                printf("mv: write error\n");
                ok = 0;
                break;
            }
            off += (size_t)w;
        }
        if (!ok)
            break;
    }
    close(in);
    close(out);
    /* Only remove the source once the copy completed successfully, so a
       write failure does not destroy the original (issue #31). */
    if (!ok)
    {
        unlink(argv[2]);
        sys_exit(1);
        return 1;
    }
    unlink(argv[1]);
    sys_exit(0);
    return 0;
}
