#include "stdio.h"
#include "syscall.h"
#include "string.h"

#define MAXE 64

static void join(const char *dir, const char *name, char *out)
{
    int i = 0;
    while (dir[i]) { out[i] = dir[i]; i++; }
    if (i > 0 && out[i - 1] != '/') out[i++] = '/';
    int j = 0;
    while (name[j]) { out[i++] = name[j++]; }
    out[i] = 0;
}

int main(int argc, char **argv)
{
    const char *path = (argc > 1) ? argv[1] : ".";
    struct vfs_dirent ents[MAXE];
    int n = readdir(path, ents, MAXE);
    if (n < 0)
    {
        printf("ls: cannot access '%s'\n", path);
        sys_exit(1);
        return 1;
    }
    for (int i = 0; i < n && i < MAXE; i++)
    {
        char typ = (ents[i].type == DT_DIR) ? 'd' : 'f';
        if (ents[i].name[0] == '.' &&
            (ents[i].name[1] == 0 || (ents[i].name[1] == '.' && ents[i].name[2] == 0)))
        {
            printf("%c %10s %s\n", typ, "-", ents[i].name);
            continue;
        }
        char full[512];
        join(path, ents[i].name, full);
        struct stat st;
        if (typ == 'd')
        {
            printf("%c %10s %s\n", typ, "-", ents[i].name);
        }
        else if (stat(full, &st) == 0)
        {
            printf("%c %10llu %s\n", typ, (unsigned long long)st.st_size, ents[i].name);
        }
        else
        {
            printf("%c %10s %s\n", typ, "?", ents[i].name);
        }
    }
    sys_exit(0);
    return 0;
}
