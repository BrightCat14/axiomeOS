#ifndef AXIOME_LIBC_SYSCALL_H
#define AXIOME_LIBC_SYSCALL_H

#include <stddef.h>

/* Generic 6-argument syscall. rax = number, rdi/rsi/rdx/r10/r8/r9 = args. */
long syscall(long n, long a1, long a2, long a3, long a4, long a5, long a6);

/* Kernel syscall numbers (must match kernel/syscall.h). */
#define SYS_PRINT   0
#define SYS_YIELD   1
#define SYS_EXIT    2
#define SYS_FORK    3
#define SYS_SPAWN   4
#define SYS_GETPID  5
#define SYS_WAITPID 6
#define SYS_WRITE   7
#define SYS_READ    8
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

/* Open flags (subset of POSIX, must match kernel/vfs.h). */
#define O_RDONLY  0x0000
#define O_WRONLY  0x0001
#define O_RDWR    0x0002
#define O_CREAT   0x0100
#define O_TRUNC   0x0200
#define O_APPEND  0x0400

#define DT_FILE   0
#define DT_DIR    1
struct vfs_dirent {
    unsigned char type;
    char name[256];
};

/* File-type bits for st_mode (subset of POSIX, must match kernel). */
#define S_IFREG 0x8000
#define S_IFDIR 0x4000

/* Filesystem types (must match kernel enum fs_type). */
#define FS_RAMFS 0
#define FS_FAT32 1
#define FS_AXIOMEFS 2

/* Stat structure (layout must match kernel struct stat). */
struct stat {
    unsigned long long st_size;
    unsigned int st_mode;
    unsigned int st_nlink;
    unsigned int st_ino;
};

/* POSIX-ish fd wrappers (backed by the kernel VFS). */
int   open(const char *path, int flags);
int   close(int fd);
long  write(int fd, const void *buf, size_t len);
long  read(int fd, void *buf, size_t len);
int   mkdir(const char *path);
int   unlink(const char *path);
int   readdir(const char *path, struct vfs_dirent *ents, int max);
int   chdir(const char *path);
int   getcwd(char *buf, size_t size);
int   fstat(int fd, struct stat *st);
int   stat(const char *path, struct stat *st);
int   dup2(int oldfd, int newfd);
int   pipe(int fds[2]);
int   mount(int fstype, const char *mountpoint, int dev);
int   umount(const char *mountpoint);
int   mkfifo(const char *path);
long  driver_rescan(void);

/* Process helpers. */
long sys_getpid(void);
long sys_fork(void);
long sys_spawn(int which);
long sys_spawn_cmd(const char *cmdline, size_t len);
long sys_waitpid(int pid, int *status);
int  sys_ps(void *buf, int max);
void sys_exit(int code);
long sys_yield(void);

/* IPC channels (Phase 11). */
int  ipc_create(void);
long ipc_send(int chan, const void *buf, size_t len);
long ipc_recv(int chan, void *buf, size_t max);

/* Shared memory (Phase 11). */
long   shm_create(size_t bytes);
void  *shm_attach(long id);

/* ---- Network sockets (Phase 13) ---- */
#define AF_INET  2
#define SOCK_DGRAM  2
#define SOCK_STREAM 1

struct sockaddr {
    unsigned short sa_family;
    char           sa_data[14];
};

struct sockaddr_in {
    unsigned short sin_family;
    unsigned short sin_port;
    unsigned int   sin_addr;
    char           sin_zero[8];
};

int   sock_create(int domain, int type, int proto);
int   sock_bind(int fd, const struct sockaddr *addr, int addrlen);
int   sock_connect(int fd, const struct sockaddr *addr, int addrlen);
int   sock_listen(int fd);
int   sock_accept(int fd);
long  sock_send(int fd, const void *buf, size_t len);
long  sock_recv(int fd, void *buf, size_t max);
int   sock_close(int fd);

/* Process listing (layout must match kernel struct proc_info). */
struct proc_info {
    int pid;
    int parent_pid;
    int state;
    char name[16];
};

/* Thread states (matches kernel enum). */
#define PROC_READY    0
#define PROC_RUNNING  1
#define PROC_BLOCKED  2
#define PROC_ZOMBIE   3

#endif
