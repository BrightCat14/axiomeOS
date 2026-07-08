#ifndef AXIOME_VFS_H
#define AXIOME_VFS_H

#include <stdint.h>
#include <stddef.h>

/* ---------------------------------------------------------------- *
 * 10.1 VFS abstraction
 *
 * The generic VFS works in terms of three classic objects:
 *   - superblock  (struct vfs_super): one per mounted filesystem
 *   - inode       (struct vnode):     a file or directory
 *   - dentry      (mount table entry + path resolution)
 * and dispatches I/O through a filesystem's `struct vfs_fops`
 * (the equivalent of file_operations).  The root filesystem is
 * always ramfs; additional filesystems (e.g. FAT32) mount on top.
 * ---------------------------------------------------------------- */

enum vtype { VFS_FILE = 0, VFS_DIR = 1, VFS_PIPE_R = 2, VFS_PIPE_W = 3,
              VFS_FIFO = 4 };

#define MAX_NAME 255

enum fs_type { FS_RAMFS = 0, FS_FAT32 = 1, FS_AXIOMEFS = 2, FS_PROC = 3, FS_DEV = 4 };

/* In-kernel pipe buffer, shared between a pipe's reader/writer ends and by a
   FIFO's vnode + its opened file descriptors. */
struct pipe {
    uint8_t *buf;
    size_t cap;
    size_t count;        /* bytes currently buffered */
    int nreaders;
    int nwriters;
    struct thread *wait_reader;
    uint8_t shared;      /* 1 = owned by a FIFO inode; don't free on fd close */
    uint8_t dead;        /* FIFO unlinked while still open; free on last close */
};

struct vnode;
struct vfs_super;
struct vfs_dirent;

/* Filesystem operations: each mounted fs implements these. All paths passed
   to the fs are relative to that fs's root (no leading '/'). */
struct vfs_fops {
    /* Resolve `relpath` under `dir` (dir may be the fs root). Returns a
       kmalloc'd vnode (the caller releases it via inode_free) or NULL. */
    struct vnode *(*lookup)(struct vfs_super *sb, struct vnode *dir,
                            const char *relpath);
    size_t (*read)(struct vfs_super *sb, struct vnode *node, size_t off,
                   void *buf, size_t len);
    int (*list)(struct vfs_super *sb, struct vnode *dir,
                struct vfs_dirent *ents, int max);
    /* Optional mutation ops (ramfs provides; read-only fs leaves NULL). */
    int (*create)(struct vfs_super *sb, const char *relpath, int type);
    int (*remove)(struct vfs_super *sb, const char *relpath);
    size_t (*write)(struct vfs_super *sb, struct vnode *node, size_t off,
                    const void *buf, size_t len);
    /* Release a vnode produced by this fs's lookup (may be a no-op). */
    void (*inode_free)(struct vnode *node);
};

/* Inode. `sb` and `priv` are filesystem-private; the remaining fields are
   the generic, fs-agnostic view used by the syscall layer. */
struct vnode {
    char name[MAX_NAME + 1];
    int type;               /* VFS_FILE or VFS_DIR */
    size_t size;
    struct vfs_super *sb;   /* owning superblock */
    void *priv;             /* fs-private inode state (e.g. first cluster) */
    uint32_t refcount;      /* open/dup references; freed by VFS when 0 */
    /* --- ramfs-only state (unused by other fs types) --- */
    uint8_t *data;          /* file content (kmalloc'd), NULL for dirs */
    size_t cap;             /* capacity of data */
    struct vnode **children;
    int nchildren;
    int cap_children;
    struct vnode *parent;
};

/* Superblock / mount record. */
struct vfs_super {
    char mountpoint[MAX_NAME + 1];  /* e.g. "/", "/boot" */
    int fs_type;
    struct vfs_fops *ops;
    void *priv;                     /* fs-private (g_root, or fat32 state) */
    struct vnode *root;            /* cached root inode for this fs */
};

/* Open flags (subset of POSIX). */
#define O_RDONLY  0x0000
#define O_WRONLY  0x0001
#define O_RDWR    0x0002
#define O_CREAT   0x0100
#define O_TRUNC   0x0200
#define O_APPEND  0x0400

/* File-descriptor kinds. */
enum fd_kind { FD_FREE = 0, FD_TTY_IN, FD_CONSOLE_OUT, FD_VNODE, FD_PIPE,
               FD_SOCKET };

#define MAX_FD 32

struct vfs_file {
    int used;
    int kind;
    struct vnode *node;
    size_t off;
};

/* Directory entry returned to userspace. */
#define DT_FILE   0
#define DT_DIR    1
struct vfs_dirent {
    uint8_t type;
    char name[MAX_NAME + 1];
};

void vfs_init(void);
struct vnode *vfs_root(void);

/* Register a filesystem at `mountpoint` (e.g. "/boot"). Returns the superblock
    pointer, or NULL on failure. */
struct vfs_super *vfs_mount(const char *mountpoint, int fs_type,
                            struct vfs_fops *ops, void *priv);

/* Mount a fresh, empty ramfs at `mountpoint` (used by the mount syscall). */
int vfs_mount_ramfs(const char *mountpoint);

/* Unmount the filesystem mounted at `mountpoint`. 0/-1. */
int vfs_umount(const char *mountpoint);

/* Resolve a (possibly relative) path into a vnode. Returns NULL if missing. */
struct vnode *vfs_lookup(const char *path, const char *cwd);

/* Create a node of given type at path (parent must already exist). 0/-1. */
int vfs_create(const char *path, int type, const char *cwd);

/* Remove a node (file or empty dir). 0/-1. */
int vfs_remove(const char *path, const char *cwd);

size_t vfs_read(struct vnode *n, size_t off, void *buf, size_t len);
size_t vfs_write(struct vnode *n, size_t off, const void *buf, size_t len);

/* List a directory into caller-allocated dirents (max entries). Returns count. */
int vfs_list(const char *path, const char *cwd, struct vfs_dirent *ents, int max);

/* Release a vnode returned by vfs_lookup (delegates to the fs). */
void vfs_release(struct vnode *n);

/* Per-process fd table bootstrap (stdin/stdout/stderr + cwd). */
void vfs_ensure_proc(void);

/* /proc virtual filesystem (Phase 11.7). */
void procfs_init(void);

/* /dev device filesystem (Phase 12.8). */
void devfs_init(void);

#endif
