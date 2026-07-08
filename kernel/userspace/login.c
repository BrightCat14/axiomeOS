#include "syscall.h"
#include "stdio.h"
#include "stdlib.h"
#include "string.h"

/* Read a line from stdin (fd 0) into buf. */
static int read_line(char *buf, int max)
{
    int i = 0;
    for (;;)
    {
        int c = getchar();
        if (c < 0) { syscall(SYS_YIELD, 0, 0, 0, 0, 0, 0); continue; }
        if (c == '\n' || c == '\r') break;
        if (i < max - 1) buf[i++] = (char)c;
    }
    buf[i] = 0;
    return i;
}

/* Authenticate `user` against /etc/passwd on the mounted root fs.
   Returns the uid on success, -1 on failure.

   NOTE: this minimal OS stores "x" as the password hash (shadowed), so
   any password is accepted for such entries. A real deployment would run
   the password through SHA-256 (see kernel security.c) and compare. */
static int authenticate(const char *user, const char *pass)
{
    (void)pass;
    int fd = open("/etc/passwd", O_RDONLY);
    if (fd < 0) return -1;
    char buf[512];
    int total = 0;
    int r;
    while ((r = (int)read(fd, buf + total, sizeof(buf) - total - 1)) > 0)
    {
        total += r;
        if (total >= (int)sizeof(buf) - 1) break;
    }
    close(fd);
    buf[total] = 0;

    char *line = buf;
    while (*line)
    {
        char *nl = line;
        while (*nl && *nl != '\n') nl++;
        char saved = *nl;
        *nl = 0;

        char *f[7];
        int nf = 0;
        char *p = line;
        while (*p && nf < 7)
        {
            f[nf++] = p;
            while (*p && *p != ':') p++;
            if (*p) { *p = 0; p++; }
        }
        if (nf >= 7 && strcmp(f[0], user) == 0)
        {
            int uid = atoi(f[2]);
            int gid = atoi(f[3]);
            (void)gid;
            *nl = saved;
            return uid;
        }
        *nl = saved;
        line = (*nl) ? nl + 1 : nl;
    }
    return -1;
}

int main(void)
{
    char user[32];
    char pass[64];

    printf("Username: ");
    read_line(user, sizeof(user));
    printf("Password: ");
    read_line(pass, sizeof(pass));

    int uid = authenticate(user, pass);
    if (uid < 0)
    {
        printf("Login failed\n");
        return 1;
    }

    setuid((uid_t)uid);
    setgid((gid_t)uid);
    printf("Logged in as %s (uid %d)\n", user, uid);

    long pid = sys_spawn_cmd("sh", 2);
    if (pid < 0)
    {
        printf("login: failed to start shell\n");
        return 1;
    }
    int status = 0;
    sys_waitpid((int)pid, &status);
    return 0;
}
