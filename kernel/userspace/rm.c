#include "stdio.h"
#include "syscall.h"
#include "string.h"

static int is_dir(const char *path)
{
    struct stat st;
    if (stat(path, &st) < 0)
        return 0;
    return (st.st_mode & S_IFDIR) ? 1 : 0;
}

static int rm_recursive(const char *path)
{
    if (!is_dir(path))
    {
        if (unlink(path) < 0)
        {
            printf("rm: cannot remove '%s'\n", path);
            return -1;
        }
        return 0;
    }

    /* Directory: list, delete children, then the dir itself.
       Keep the on-stack dirent buffer small: 256 * MAX_NAME would put ~64KB
       on the stack and crash (issue #31: `rm -r` page-faulted). */
    struct vfs_dirent ents[64];
    int n = readdir(path, ents, 64);
    if (n < 0)
    {
        printf("rm: cannot read directory '%s'\n", path);
        return -1;
    }
    for (int i = 0; i < n; i++)
    {
        const char *name = ents[i].name;
        if (name[0] == '.' &&
            (name[1] == 0 || (name[1] == '.' && name[2] == 0)))
            continue;
        char child[512];
        size_t pl = 0;
        while (path[pl] && pl < sizeof(child) - 1) { child[pl] = path[pl]; pl++; }
        if (pl > 0 && child[pl - 1] != '/') child[pl++] = '/';
        size_t nl = 0;
        while (name[nl] && pl + nl < sizeof(child) - 1) { child[pl + nl] = name[nl]; nl++; }
        child[pl + nl] = 0;
        if (rm_recursive(child) < 0)
            return -1;
    }
    if (rmdir(path) < 0)
    {
        printf("rm: cannot remove directory '%s'\n", path);
        return -1;
    }
    return 0;
}

int main(int argc, char **argv)
{
    if (argc < 2)
    {
        puts("usage: rm [-r] <path> [...]");
        sys_exit(1);
        return 1;
    }
    int st = 0;
    int r = 0;
    int first = 1;
    for (int i = 1; i < argc; i++)
    {
        if (first && strcmp(argv[i], "-r") == 0)
        {
            r = 1;
            continue;
        }
        first = 0;
        if (r)
        {
            if (rm_recursive(argv[i]) < 0)
                st = 1;
        }
        else if (unlink(argv[i]) < 0)
        {
            printf("rm: cannot remove '%s'\n", argv[i]);
            st = 1;
        }
    }
    sys_exit(st);
    return st;
}
