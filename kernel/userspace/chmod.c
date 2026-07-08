#include "syscall.h"
#include "stdio.h"
#include "stdlib.h"

/* Parse an octal permission string (e.g. "0644" or "644"). */
static unsigned int parse_octal(const char *s)
{
    unsigned int v = 0;
    while (*s >= '0' && *s <= '7')
        v = v * 8 + (unsigned int)(*s - '0');
    return v;
}

int main(int argc, char **argv)
{
    if (argc < 3)
    {
        printf("usage: chmod <mode> <path>\n");
        return 1;
    }
    unsigned int mode = parse_octal(argv[1]);
    if (chmod(argv[2], mode & 07777) < 0)
    {
        printf("chmod: cannot change mode of '%s'\n", argv[2]);
        return 1;
    }
    return 0;
}
