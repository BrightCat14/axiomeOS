#include "syscall.h"
#include "errno.h"
#include <stddef.h>

long syscall(long n, long a1, long a2, long a3, long a4, long a5, long a6)
{
    long ret;
    register long r0 __asm__("rax") = n;
    register long r1 __asm__("rdi") = a1;
    register long r2 __asm__("rsi") = a2;
    register long r3 __asm__("rdx") = a3;
    register long r4 __asm__("r10") = a4;
    register long r5 __asm__("r8")  = a5;
    register long r6 __asm__("r9")  = a6;
    __asm__ volatile ("syscall"
                      : "=a"(ret)
                      : "r"(r0), "r"(r1), "r"(r2), "r"(r3),
                        "r"(r4), "r"(r5), "r"(r6)
                      : "%rcx", "%r11", "memory");
    return ret;
}

int open(const char *path, int flags)
{
    return (int)syscall(SYS_OPEN, (long)path, (long)flags, 0, 0, 0, 0);
}

int close(int fd)
{
    return (int)syscall(SYS_CLOSE, (long)fd, 0, 0, 0, 0, 0);
}

long write(int fd, const void *buf, size_t len)
{
    return syscall(SYS_WRITE, (long)fd, (long)buf, (long)len, 0, 0, 0);
}

long read(int fd, void *buf, size_t len)
{
    return syscall(SYS_READ, (long)fd, (long)buf, (long)len, 0, 0, 0);
}

int mkdir(const char *path)
{
    return (int)syscall(SYS_MKDIR, (long)path, 0, 0, 0, 0, 0);
}

int unlink(const char *path)
{
    return (int)syscall(SYS_UNLINK, (long)path, 0, 0, 0, 0, 0);
}

int readdir(const char *path, struct vfs_dirent *ents, int max)
{
    return (int)syscall(SYS_READDIR, (long)path, (long)ents, (long)max, 0, 0, 0);
}

int chdir(const char *path)
{
    return (int)syscall(SYS_CHDIR, (long)path, 0, 0, 0, 0, 0);
}

int getcwd(char *buf, size_t size)
{
    return (int)syscall(SYS_GETCWD, (long)buf, (long)size, 0, 0, 0, 0);
}

int fstat(int fd, struct stat *st)
{
    return (int)syscall(SYS_FSTAT, (long)fd, (long)st, 0, 0, 0, 0);
}

int stat(const char *path, struct stat *st)
{
    int fd = open(path, O_RDONLY);
    if (fd < 0)
        return -1;
    int r = fstat(fd, st);
    close(fd);
    return r;
}

int dup2(int oldfd, int newfd)
{
    return (int)syscall(SYS_DUP2, (long)oldfd, (long)newfd, 0, 0, 0, 0);
}

int pipe(int fds[2])
{
    return (int)syscall(SYS_PIPE, (long)fds, 0, 0, 0, 0, 0);
}

int mount(int fstype, const char *mountpoint, int dev)
{
    return (int)syscall(SYS_MOUNT, (long)fstype, (long)mountpoint, (long)dev, 0, 0, 0);
}

int umount(const char *mountpoint)
{
    return (int)syscall(SYS_UMOUNT, (long)mountpoint, 0, 0, 0, 0, 0);
}

int mkfifo(const char *path)
{
    return (int)syscall(SYS_MKFIFO, (long)path, 0, 0, 0, 0, 0);
}

long driver_rescan(void)
{
    return syscall(SYS_DRIVER_RESCAN, 0, 0, 0, 0, 0, 0);
}

long sys_getpid(void) { return syscall(SYS_GETPID, 0, 0, 0, 0, 0, 0); }
long sys_yield(void) { return syscall(SYS_YIELD, 0, 0, 0, 0, 0, 0); }
long sys_fork(void)   { return syscall(SYS_FORK,   0, 0, 0, 0, 0, 0); }
long sys_spawn(int which) { return syscall(SYS_SPAWN, (long)which, 0, 0, 0, 0, 0); }
long sys_spawn_cmd(const char *cmdline, size_t len)
{
    return syscall(SYS_SPAWN_CMD, (long)cmdline, (long)len, 0, 0, 0, 0);
}
long sys_waitpid(int pid, int *status)
{
    return syscall(SYS_WAITPID, (long)pid, (long)status, 0, 0, 0, 0);
}
int sys_ps(void *buf, int max)
{
    return (int)syscall(SYS_PS, (long)buf, (long)max, 0, 0, 0, 0);
}
void sys_exit(int code) { syscall(SYS_EXIT, (long)code, 0, 0, 0, 0, 0); }

/* ---- IPC (Phase 11) ---- */
int ipc_create(void)
{
    return (int)syscall(SYS_IPC_CREATE, 0, 0, 0, 0, 0, 0);
}
long ipc_send(int chan, const void *buf, size_t len)
{
    return syscall(SYS_IPC_SEND, (long)chan, (long)buf, (long)len, 0, 0, 0);
}
long ipc_recv(int chan, void *buf, size_t max)
{
    return syscall(SYS_IPC_RECV, (long)chan, (long)buf, (long)max, 0, 0, 0);
}

/* ---- shared memory (Phase 11) ---- */
long shm_create(size_t bytes)
{
    return syscall(SYS_SHM_CREATE, (long)bytes, 0, 0, 0, 0, 0);
}
void *shm_attach(long id)
{
    return (void *)syscall(SYS_SHM_ATTACH, id, 0, 0, 0, 0, 0);
}
