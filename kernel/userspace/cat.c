#include "stdio.h"
#include "syscall.h"
#include "string.h"

int main(int argc, char **argv)
{
    if (argc < 2)
    {
        char c;
        while (read(0, &c, 1) > 0)
            putchar((unsigned char)c);
        sys_exit(0);
        return 0;
    }
    int st = 0;
    for (int i = 1; i < argc; i++)
    {
        int fd = open(argv[i], O_RDONLY);
        if (fd < 0)
        {
            printf("cat: %s: no such file\n", argv[i]);
            st = 1;
            continue;
        }
        char buf[256];
        long r;
        while ((r = read(fd, buf, sizeof buf)) > 0)
            for (long j = 0; j < r; j++)
                putchar((unsigned char)buf[j]);
        close(fd);
    }
    sys_exit(st);
    return st;
}
