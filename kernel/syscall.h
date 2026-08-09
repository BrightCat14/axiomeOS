#ifndef AXIOME_SYSCALL_H
#define AXIOME_SYSCALL_H

#include <stdint.h>

#define SYS_PRINT  0
#define SYS_YIELD  1
#define SYS_EXIT   2
#define SYS_FORK   3
#define SYS_GETPID 5
#define SYS_WAITPID 6
#define SYS_WRITE  7
#define SYS_READ   8
#define SYS_SPAWN_CMD 9
#define SYS_PS    10
#define SYS_OPEN   11
#define SYS_CLOSE  12
#define SYS_MKDIR  13
#define SYS_UNLINK 14
#define SYS_READDIR 15
#define SYS_CHDIR  16
#define SYS_GETCWD 17
#define SYS_FSTAT   18
#define SYS_DUP2   19
#define SYS_PIPE   20
#define SYS_MOUNT  21
#define SYS_UMOUNT 22
#define SYS_KILL   23
#define SYS_SIGACTION 24
#define SYS_SIGRETURN 25
#define SYS_IPC_CREATE 26
#define SYS_IPC_SEND 27
#define SYS_IPC_RECV 28
#define SYS_SHM_CREATE 29
#define SYS_SHM_ATTACH 30
#define SYS_MKFIFO 31
#define SYS_DRIVER_RESCAN 32
#define SYS_SOCKET_CREATE 33
#define SYS_SOCKET_BIND   34
#define SYS_SOCKET_CONNECT 35
#define SYS_SOCKET_SEND   36
#define SYS_SOCKET_RECV   37
#define SYS_SOCKET_CLOSE  38
#define SYS_SOCKET_LISTEN 39
#define SYS_SOCKET_ACCEPT 40

/* ---- user rank system (docs/user-rank-system-spec.md) ---- */
#define SYS_GETUID   41
#define SYS_GETEUID  42
#define SYS_GETGID   43
#define SYS_GETEGID  44
#define SYS_SETUID   45
#define SYS_SETGID   46
#define SYS_GETROLE  47
#define SYS_CHMOD    48
#define SYS_CHOWN    49
#define SYS_GETCAP   50
#define SYS_SETCAP   51
#define SYS_GETPWNAM 52   /* name -> uid/gid (see sys_getpwnam) */

/* ---- loadable kernel modules (.kxt) ---- */
#define SYS_MODULE_LOAD   53  /* a1 = path (userspace ptr)          */
#define SYS_MODULE_UNLOAD 54  /* a1 = name (userspace ptr)          */
#define SYS_MMAP          55  /* a1=fd, a2=off, a3=virt, a4=len, a5=flags */

/* ---- system info ---- */
#define SYS_UNAME         56  /* a1 = struct utsname * (userspace ptr) */
#define SYS_RELOAD_USERS  57  /* re-parse /etc/passwd (first-boot OOBE) */

/* File-type bits for st_mode (subset of POSIX). */
#define S_IFREG 0x8000
#define S_IFDIR 0x4000

/* Stat structure returned by fstat (layout must match userspace libc). */
struct stat {
    uint64_t st_size;
    uint32_t st_mode;
    uint32_t st_nlink;
    uint32_t st_ino;
};

/* System identity structure returned by uname (POSIX utsname). */
#define UTSNAME_LEN 65
struct utsname {
    char sysname[UTSNAME_LEN];
    char nodename[UTSNAME_LEN];
    char release[UTSNAME_LEN];
    char version[UTSNAME_LEN];
    char machine[UTSNAME_LEN];
    char domainname[UTSNAME_LEN];
};

void syscall_init(void);
uint64_t syscall_dispatch(uint64_t n, uint64_t a1, uint64_t a2, uint64_t a3,
                          uint64_t a4, uint64_t a5);

extern uint64_t current_kstack_top;

#endif
