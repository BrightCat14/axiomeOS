#include "syscall.h"
#include "stdio.h"
#include "stdlib.h"

int main(int argc, char **argv)
{
    if (argc < 4)
    {
        printf("usage: chown <uid> <gid> <path>\n");
        return 1;
    }
    uid_t uid = (uid_t)atoi(argv[1]);
    gid_t gid = (gid_t)atoi(argv[2]);
    if (chown(argv[3], uid, gid) < 0)
    {
        printf("chown: cannot change owner of '%s'\n", argv[3]);
        return 1;
    }
    return 0;
}
