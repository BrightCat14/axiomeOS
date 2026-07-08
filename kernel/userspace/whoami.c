#include "syscall.h"
#include "stdio.h"

int main(void)
{
    uid_t uid  = getuid();
    uid_t euid = geteuid();
    gid_t gid  = getgid();
    gid_t egid = getegid();
    int role   = getrole();
    unsigned long long caps = getcap();

    const char *role_names[] = {"guest", "user", "admin", "system"};

    printf("uid=%d euid=%d gid=%d egid=%d role=%s caps=0x%llx\n",
           uid, euid, gid, egid,
           (role >= 0 && role <= 3) ? role_names[role] : "???",
           (unsigned long long)caps);
    return 0;
}
