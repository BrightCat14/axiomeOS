#include "stdio.h"
#include "syscall.h"
#include "string.h"

static int mkdir_parent(const char *path)
{
    char tmp[512];
    size_t len = 0;
    while (path[len] && len < sizeof(tmp) - 1) { tmp[len] = path[len]; len++; }
    tmp[len] = 0;

    /* Create each intermediate component of an absolute/relative path. */
    for (size_t i = 1; i <= len; i++)
    {
        if (tmp[i] == '/' || i == len)
        {
            if (i == len && tmp[i] == '/')
                break;
            char save = tmp[i];
            tmp[i] = 0;
            if (mkdir(tmp) < 0)
            {
                /* If the directory already exists, keep going; any other
                   error is a hard failure. */
                struct stat st;
                if (stat(tmp, &st) < 0)
                {
                    printf("mkdir: cannot create '%s'\n", tmp);
                    return -1;
                }
            }
            tmp[i] = save;
        }
    }
    return 0;
}

int main(int argc, char **argv)
{
    if (argc < 2)
    {
        puts("usage: mkdir [-p] <dir> [...]");
        sys_exit(1);
        return 1;
    }
    int st = 0;
    int p = 0;
    int first = 1;
    for (int i = 1; i < argc; i++)
    {
        if (first && strcmp(argv[i], "-p") == 0)
        {
            p = 1;
            continue;
        }
        first = 0;
        if (p)
        {
            if (mkdir_parent(argv[i]) < 0)
                st = 1;
        }
        else if (mkdir(argv[i]) < 0)
        {
            printf("mkdir: cannot create '%s'\n", argv[i]);
            st = 1;
        }
    }
    sys_exit(st);
    return st;
}
