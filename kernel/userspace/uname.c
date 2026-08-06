#include "syscall.h"
#include "stdio.h"

static const char *fld(char s[UTSNAME_LEN])
{
    return s[0] ? s : (const char *)"unknown";
}

int main(int argc, char **argv)
{
    struct utsname u;
    if (uname(&u) < 0)
    {
        printf("uname: failed\n");
        return 1;
    }

    int all = 0;
    int sysname = 0, nodename = 0, release = 0, version = 0, machine = 0;
    for (int i = 1; i < argc; i++)
    {
        const char *a = argv[i];
        if (a[0] == '-' && a[1] != 0)
        {
            for (const char *c = a + 1; *c; c++)
            {
                if (*c == 'a')
                    all = 1;
                else if (*c == 's') sysname = 1;
                else if (*c == 'n') nodename = 1;
                else if (*c == 'r') release = 1;
                else if (*c == 'v') version = 1;
                else if (*c == 'm') machine = 1;
            }
        }
    }

    if (all)
    {
        printf("%s %s %s %s %s\n",
               fld(u.sysname), fld(u.nodename), fld(u.release),
               fld(u.version), fld(u.machine));
        return 0;
    }
    if (!sysname && !nodename && !release && !version && !machine)
        sysname = 1;

    if (sysname)   printf("%s\n", fld(u.sysname));
    if (nodename)  printf("%s\n", fld(u.nodename));
    if (release)   printf("%s\n", fld(u.release));
    if (version)   printf("%s\n", fld(u.version));
    if (machine)   printf("%s\n", fld(u.machine));
    return 0;
}