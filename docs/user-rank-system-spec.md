# axiomeOS — User Rank System & Build Fix

> Implementation specification for the security model, POSIX compatibility
> layer, and proper disk installation layout.
>
> Covers: user ranks, capabilities, POSIX adapter, new syscalls, userspace
> tools, VFS permissions, and the FAT32/axiomefs disk image.

---

## 1. Security Model Overview

```
┌─────────────┐
│ capabilities │   <-- bitmask of fine-grained permissions
└──────┬──────┘
       │
┌──────▼──────┐
│  4 roles    │   <-- guest / user / admin / system
│             │
└──────┬──────┘
       │
┌──────▼──────┐
│ POSIX layer │   <-- uid/gid / rwx bits / signals / processes
│ (adapter)   │       maps to capabilities underneath
└─────────────┘
```

Root (`uid 0`) is **not** a magic superuser. It is a POSIX compatibility
shorthand that maps to the `admin` role plus a bounded set of capabilities.
No process escapes capability checks.

---

## 2. User Ranks

| Rank | Name   | UID Range    | Description |
|------|--------|--------------|-------------|
| 0    | guest  | 65534        | Temporary files only, app launching, public resources, no system config |
| 1    | user   | 1000–65533   | Own files, own processes, own settings, standard applications |
| 2    | admin  | 1–999        | Package install, user management, system config, service management |
| 3    | system | 0            | Kernel daemons, drivers, device managers, OS services |

**POSIX mapping:**

| uid     | Role   | Rationale |
|---------|--------|-----------|
| 0       | system | Boot/init process, kernel services |
| 1–999   | admin  | System accounts (root, daemon, etc.) |
| 1000+   | user   | Human users |
| 65534   | guest  | Unauthenticated sessions |

The `uid 0 → system` mapping exists only in the POSIX adapter. Internally,
uid 0 is the "compatibility root" that gets `admin` role capabilities, not
an all-powerful entity.

---

## 3. Capability Flags

```c
/* kernel/security.h */

#define CAP_FORK             (1ULL <<  0)   /* create child processes */
#define CAP_FILE_READ        (1ULL <<  1)   /* read any file */
#define CAP_FILE_WRITE_SELF  (1ULL <<  2)   /* write files owned by self */
#define CAP_FILE_WRITE_ANY   (1ULL <<  3)   /* write any file */
#define CAP_KILL_SELF        (1ULL <<  4)   /* signal own processes */
#define CAP_KILL_ANY         (1ULL <<  5)   /* signal any process */
#define CAP_MOUNT            (1ULL <<  6)   /* mount filesystems */
#define CAP_UMOUNT           (1ULL <<  7)   /* unmount filesystems */
#define CAP_DRIVER           (1ULL <<  8)   /* load/unload drivers */
#define CAP_NET              (1ULL <<  9)   /* network sockets */
#define CAP_NET_RAW          (1ULL << 10)   /* raw sockets */
#define CAP_IPC              (1ULL << 11)   /* IPC channels / shared memory */
#define CAP_SETUID           (1ULL << 12)   /* change own uid */
#define CAP_SETGID           (1ULL << 13)   /* change own gid */
#define CAP_USER_MGMT        (1ULL << 14)   /* create/delete users */
#define CAP_SERVICE_MGMT     (1ULL << 15)   /* start/stop services */
#define CAP_SYS_ADMIN        (1ULL << 16)   /* system-wide admin ops */
#define CAP_SIGNAL_OTHER     (1ULL << 17)   /* signal processes not owned */
#define CAP_PTRACE           (1ULL << 18)   /* trace/inspect any process */
#define CAP_BOOT             (1ULL << 19)   /* reboot / shutdown */
#define CAP_TIME             (1ULL << 20)   /* set system time */
#define CAP_RESOURCE         (1ULL << 21)   /* set resource limits */
#define CAP_EXECUTE          (1ULL << 22)   /* execute programs */
#define CAP_FILE_WRITE_TMP   (1ULL << 23)   /* write to /tmp only (guest) */
```

---

## 4. Role → Capability Mapping

```c
/* kernel/security.c — static table */

static const uint64_t role_caps[NROLES] = {
    [ROLE_GUEST] =
        CAP_FORK |
        CAP_FILE_READ |
        CAP_FILE_WRITE_TMP |
        CAP_EXECUTE,
    [ROLE_USER] =
        CAP_FORK |
        CAP_FILE_READ |
        CAP_FILE_WRITE_SELF |
        CAP_KILL_SELF |
        CAP_NET |
        CAP_IPC |
        CAP_SETUID |
        CAP_SETGID |
        CAP_EXECUTE,
    [ROLE_ADMIN] =
        /* all USER caps, plus: */
        CAP_FILE_WRITE_ANY |
        CAP_KILL_ANY |
        CAP_SIGNAL_OTHER |
        CAP_MOUNT |
        CAP_UMOUNT |
        CAP_DRIVER |
        CAP_NET_RAW |
        CAP_USER_MGMT |
        CAP_SERVICE_MGMT |
        CAP_SYS_ADMIN |
        CAP_PTRACE |
        CAP_BOOT |
        CAP_TIME |
        CAP_RESOURCE,
    [ROLE_SYSTEM] =
        /* everything — for kernel daemons only */
        0xFFFFFFFFULL,
};
```

---

## 5. Per-Process Security Context

### 5.1 Additions to `struct thread` (`kernel/sched.h`)

```c
/* Security context — added after existing fields */
uid_t uid;          /* real uid */
uid_t euid;         /* effective uid */
uid_t suid;         /* saved uid (for setuid) */
gid_t gid;          /* real gid */
gid_t egid;         /* effective gid */
gid_t sgid;         /* saved gid */
user_role_t role;   /* ROLE_GUEST .. ROLE_SYSTEM */
uint64_t caps_eff;  /* effective capabilities */
uint64_t caps_prm;  /* permitted capabilities */
uint64_t caps_inh;  /* inheritable capabilities */
```

### 5.2 Type Definitions (`kernel/security.h`)

```c
typedef uint32_t uid_t;
typedef uint32_t gid_t;

enum user_role {
    ROLE_GUEST  = 0,
    ROLE_USER   = 1,
    ROLE_ADMIN  = 2,
    ROLE_SYSTEM = 3,
    NROLES
};
```

### 5.3 Inheritance

- `init` process: hardcoded to `uid=0, role=ROLE_SYSTEM, caps_eff=0xFFFFFFFF`
- `fork`: child inherits parent's full security context (uid, gid, role, caps)
- `spawn`: inherits parent's context unless called by SYSTEM role with override
- `exec`: preserves uid/gid/role; resets `euid` to `uid` (non-setuid)
- Setuid programs: if binary has setuid bit, `euid` set to file owner's uid

---

## 6. VFS Permission Model

### 6.1 Additions to `struct vnode` (`kernel/vfs.h`)

```c
/* Security fields — added after existing fields */
uid_t v_uid;        /* owner uid */
gid_t v_gid;        /* owner gid */
uint16_t v_mode;    /* permission bits (S_IRWXU | S_IRWXG | S_IRWXO) */
```

### 6.2 Permission Bit Constants

```c
#define S_ISUID  04000   /* set user id */
#define S_ISGID  02000   /* set group id */
#define S_IRUSR  00400   /* owner read */
#define S_IWUSR  00200   /* owner write */
#define S_IXUSR  00100   /* owner execute */
#define S_IRGRP  00040   /* group read */
#define S_IWGRP  00020   /* group write */
#define S_IXGRP  00010   /* group execute */
#define S_IROTH  00004   /* other read */
#define S_IWOTH  00002   /* other write */
#define S_IXOTH  00001   /* other execute */
```

### 6.3 Permission Check Function

```c
/* kernel/vfs.c */

#define VFS_MAY_READ  4
#define VFS_MAY_WRITE 2
#define VFS_MAY_EXEC  1

int vfs_check_perms(struct vnode *n, int mask)
{
    struct thread *t = sched_current();
    if (!t) return 0;  /* kernel thread: bypass */

    uid_t uid = t->euid;
    gid_t gid = t->egid;

    /* SYSTEM role bypasses all checks */
    if (t->role == ROLE_SYSTEM) return 0;

    /* owner check */
    if (uid == n->v_uid) {
        if ((mask & VFS_MAY_READ)  && !(n->v_mode & S_IRUSR)) return -1;
        if ((mask & VFS_MAY_WRITE) && !(n->v_mode & S_IWUSR)) return -1;
        if ((mask & VFS_MAY_EXEC)  && !(n->v_mode & S_IXUSR)) return -1;
        return 0;
    }

    /* group check */
    if (gid == n->v_gid) {
        if ((mask & VFS_MAY_READ)  && !(n->v_mode & S_IRGRP)) return -1;
        if ((mask & VFS_MAY_WRITE) && !(n->v_mode & S_IWGRP)) return -1;
        if ((mask & VFS_MAY_EXEC)  && !(n->v_mode & S_IXGRP)) return -1;
        return 0;
    }

    /* other check */
    if ((mask & VFS_MAY_READ)  && !(n->v_mode & S_IROTH)) return -1;
    if ((mask & VFS_MAY_WRITE) && !(n->v_mode & S_IWOTH)) return -1;
    if ((mask & VFS_MAY_EXEC)  && !(n->v_mode & S_IXOTH)) return -1;
    return 0;
}
```

### 6.4 VFS Entry Points Gated

| VFS function | Check |
|---|---|
| `vfs_open` (O_RDONLY) | `VFS_MAY_READ` on target |
| `vfs_open` (O_WRONLY/O_RDWR) | `VFS_MAY_WRITE` on target |
| `vfs_create` | `VFS_MAY_WRITE` on parent dir |
| `vfs_remove` | `VFS_MAY_WRITE` on parent dir |
| `vfs_list` | `VFS_MAY_READ` on dir |
| `vfs_lookup` (each component) | `VFS_MAY_EXEC` on intermediate dirs |
| `vfs_write` | `VFS_MAY_WRITE` on node |

### 6.5 Guest `/tmp` Restriction

```c
static int path_is_under_tmp(const char *abs)
{
    /* abs must start with "/tmp" and either be exactly "/tmp" or
       have a '/' after "/tmp" (i.e. "/tmp/...") */
    if (abs[0] != '/' || abs[1] != 't' || abs[2] != 'm' || abs[3] != 'p')
        return 0;
    return abs[4] == '\0' || abs[4] == '/';
}
```

In `vfs_create` and `vfs_write`:
```c
if (t->role == ROLE_GUEST && !path_is_under_tmp(abs_path))
    return -EPERM;
```

---

## 7. Permission Propagation to Filesystems

### 7.1 ramfs (`kernel/vfs.c`)

- `ramfs_new_node()` → set `v_uid = t->uid`, `v_gid = t->gid`
- Default modes: dir `0755`, file `0644`
- New helper: `ramfs_set_attrs(node, uid, gid, mode)`

### 7.2 axiomefs (`kernel/axiomefs.c`)

The on-disk `struct axfs_inode` already has `uid`, `gid`, `permissions`.
Changes:

- `axfs_make_vnode()` → copy `in.uid` → `v_uid`, `in.gid` → `v_gid`,
  `in.permissions` → `v_mode`
- `axfs_create()` → set `ci.uid = t->uid`, `ci.gid = t->gid`,
  `ci.permissions = default_mode`
- CoW write paths preserve uid/gid/mode through copy

### 7.3 FAT32 (`kernel/fat32.c`)

FAT32 has no native POSIX permissions. Strategy:

- Mount-time option `uid=X gid=X umask=022` (parsed from mount args)
- All files inherit mount uid/gid and mask-derived mode
- `v_mode` computed: `(0777 & ~umask)` for dirs, `(0666 & ~umask)` for files
- Default: uid=0, gid=0, umask=022 → dirs 0755, files 0644

---

## 8. New Syscalls

| # | Name | Arguments | Required Capability |
|---|------|-----------|---------------------|
| 41 | `SYS_GETUID` | — | none |
| 42 | `SYS_GETEUID` | — | none |
| 43 | `SYS_GETGID` | — | none |
| 44 | `SYS_GETEGID` | — | none |
| 45 | `SYS_SETUID` | `uid` | `CAP_SETUID` |
| 46 | `SYS_SETGID` | `gid` | `CAP_SETGID` |
| 47 | `SYS_GETROLE` | — | none |
| 48 | `SYS_CHMOD` | `path, mode` | owner match or `CAP_SYS_ADMIN` |
| 49 | `SYS_CHOWN` | `path, uid, gid` | owner match or `CAP_SYS_ADMIN` |
| 50 | `SYS_GETCAP` | — | none (returns own caps) |
| 51 | `SYS_SETCAP` | `cap_mask` | `CAP_SYS_ADMIN` |

### 8.1 `SYS_SETUID` Behavior

```c
static uint64_t sys_setuid(uint64_t uid, ...)
{
    struct thread *t = sched_current();
    if (!(t->caps_prm & CAP_SETUID)) return -EPERM;
    t->uid  = (uid_t)uid;
    t->euid = (uid_t)uid;
    /* recalculate role from uid */
    t->role = posix_uid_to_role((uid_t)uid);
    t->caps_eff = role_caps[t->role];
    return 0;
}
```

### 8.2 `SYS_CHMOD` Behavior

```c
static uint64_t sys_chmod(uint64_t path_ptr, uint64_t mode, ...)
{
    struct thread *t = sched_current();
    char path[256];
    if (copy_from_user(t, path_ptr, path, sizeof(path))) return -EFAULT;
    struct vnode *n = vfs_lookup(path, t->cwd);
    if (!n) return -ENOENT;
    if (n->v_uid != t->euid && !(t->caps_prm & CAP_SYS_ADMIN))
        return -EPERM;
    n->v_mode = (uint16_t)(mode & 07777);
    /* persist to disk for axiomefs */
    return 0;
}
```

### 8.3 Capability Check Helper

```c
/* kernel/security.c */

int sec_check_cap(struct thread *t, uint64_t cap)
{
    if (t->role == ROLE_SYSTEM) return 1;
    return (t->caps_eff & cap) != 0;
}
```

---

## 9. POSIX Compatibility Layer

### 9.1 UID → Role Mapping

```c
/* kernel/posix.c */

user_role_t posix_uid_to_role(uid_t uid)
{
    if (uid == 0)           return ROLE_SYSTEM;
    if (uid < 1000)         return ROLE_ADMIN;
    if (uid == 65534)       return ROLE_GUEST;
    return ROLE_USER;
}
```

### 9.2 Role → Default UID Mapping

When a new user is created (via `SYS_CHOWN` or user management):

| Role | Assigned UID |
|------|-------------|
| system | 0 |
| admin | next available in 1–999 |
| user | next available in 1000+ |
| guest | 65534 (singleton) |

### 9.3 File Permission → Capability Mapping

For legacy POSIX software, the VFS permission bits map to capabilities:

```
owner write  →  CAP_FILE_WRITE_SELF (if file owner matches)
owner read   →  (always allowed for file owner)
group r/w/x  →  checked via gid match
other r/w/x  →  checked via "other" bits
```

No new capability is needed for basic POSIX `open()`/`read()`/`write()` —
these map naturally to VFS permission checks.

### 9.4 Root Compatibility

```
uid 0  (POSIX root)
  │
  ├── role = ROLE_ADMIN
  ├── caps_eff = role_caps[ROLE_ADMIN]
  ├── can: mount, install packages, manage users, modify system config
  └── cannot: bypass kernel mechanism restrictions (no ROLE_SYSTEM access)
```

This is the key difference from classic Unix: root is not all-powerful.
It is simply the admin role for POSIX compatibility. SYSTEM role
(uid 0 kernel processes) has broader access but is only for init and
kernel daemons.

---

## 10. User Database (`/etc/passwd`)

### 10.1 Format

```
username:password_hash:uid:gid:role:home:shell
```

Example:
```
root:x:0:0:system:/root:/bin/sh
system:x:3:3:system:/sbin:/sbin/nologin
daemon:x:4:4:admin:/home/daemon:/sbin/nologin
alice:x:1000:1000:user:/home/alice:/bin/sh
bob:x:1001:1001:user:/home/bob:/bin/sh
guest:x:65534:65534:guest:/tmp:/sbin/nologin
```

### 10.2 Kernel Parser (`kernel/security.c`)

```c
#define MAX_USERS 64

struct user_entry {
    char name[32];
    uint32_t uid;
    uint32_t gid;
    user_role_t role;
    char passwd_hash[64];
    char home[128];
    char shell[64];
};

static struct user_entry g_users[MAX_USERS];
static int g_nusers;

void security_init(void)
{
    /* parse /etc/passwd from mounted axiomefs */
    /* fallback to hardcoded entries if /etc/passwd missing */
}
```

### 10.3 Authentication

```c
int security_authenticate(const char *username, const char *password)
{
    /* find user by name */
    /* hash password with SHA-256 */
    /* compare to stored hash */
    /* return uid on success, -1 on failure */
}
```

---

## 11. Build System Fix: FAT32 Boot + axiomefs Root

### 11.1 Target Disk Layout

```
disk.img (256MB, MBR)
├── Partition 0: FAT32 "BOOT" (64MB, type 0x0C, LBA 2048..131071)
│   ├── kernel.elf
│   └── grub.cfg
└── Partition 1: axiomefs "ROOT" (192MB, type 0x83, LBA 131072..524287)
    ├── etc/
    │   └── passwd
    ├── home/
    │   ├── root/
    │   └── alice/
    ├── tmp/
    ├── dev/
    ├── proc/
    └── ...
```

### 11.2 New Files

| File | Purpose |
|------|---------|
| `tools/mkpart.py` | Write MBR partition table with 2 partitions |
| `root_manifest.txt` | List of files/dirs for the root filesystem |
| `tools/mkfs_axiomefs.c` | **Enhanced** to accept partition offset + manifest |

### 11.3 `tools/mkpart.py`

```python
#!/usr/bin/env python3
"""Write MBR partition table to a disk image."""
import struct, sys

def write_mbr(img_path):
    with open(img_path, 'r+b') as f:
        img = bytearray(f.read())

    # MBR signature
    img[510] = 0x55
    img[511] = 0xAA

    # Partition 1: FAT32, LBA 2048, 130024 sectors (63.5MB)
    off = 446
    struct.pack_into('<BBBBBBBB', img, off,
        0x00,           # status (not bootable)
        0x20, 0x00, 0x00,  # CHS of first sector
        0x0C,           # type: FAT32 LBA
        0xFF, 0xFF, 0xFF,  # CHS of last sector
    )
    struct.pack_into('<II', img, off+8,
        2048,           # start LBA
        130024,         # num sectors
    )

    # Partition 2: axiomefs, LBA 131072, 393216 sectors (192MB)
    off = 462
    struct.pack_into('<BBBBBBBB', img, off,
        0x00,           # status
        0x00, 0x00, 0x00,
        0x83,           # type: Linux
        0x00, 0x00, 0x00,
    )
    struct.pack_into('<II', img, off+8,
        131072,         # start LBA
        393216,         # num sectors
    )

    f.seek(0)
    f.write(img)
```

### 11.4 `root_manifest.txt`

```
# Directory structure for axiomefs root partition
# Format: path mode uid gid type [source]
# type: d=directory, f=file

etc/           0755  0  0  d
etc/passwd     0644  0  0  f  content:root:x:0:0:system:/root:/bin/sh\nalice:x:1000:1000:user:/home/alice:/bin/sh\nguest:x:65534:65534:guest:/tmp:/sbin/nologin\n

home/          0755  0  0  d
home/alice/    0755 1000 1000 d
tmp/           1777  0  0  d
dev/           0755  0  0  d
proc/          0555  0  0  d
boot/          0755  0  0  d
```

### 11.5 Enhanced `tools/mkfs_axiomefs.c`

The tool is extended to:
- Accept a disk image path, partition offset, and manifest file
- Parse the manifest to create directories and files
- Set uid/gid/permissions on each inode
- Support inline content (for small files like passwd)

Usage:
```bash
./mkfs_axiomefs disk.img 131072 root_manifest.txt
```

### 11.6 `Makefile` Changes

```makefile
# New targets

BUILD_DIR := $(REPO_ROOT)/build

# Create disk image with boot + root partitions
disk.img: kernel
    @mkdir -p $(BUILD_DIR)
    dd if=/dev/zero of=$(BUILD_DIR)/disk.img bs=1M count=256
    $(PYTHON) tools/mkpart.py $(BUILD_DIR)/disk.img
    # Format FAT32 boot partition (offset 2048 sectors = 1MiB)
    mkfs.fat -F 32 -n BOOT $(BUILD_DIR)/disk.img --offset 2048 130024
    # Copy kernel to boot
    mcopy -i $(BUILD_DIR)/disk.img@@1048576 $(BUILD_DIR)/kernel/kernel.elf ::kernel.elf
    # Format axiomefs root partition
    $(BUILD_DIR)/kernel/mkfs_axiomefs $(BUILD_DIR)/disk.img 131072 root_manifest.txt

# Updated run target
run: iso disk.img
    sudo ip tuntap add dev tap0 mode tap user $(USER) || true
    sudo ip addr add 10.0.2.1/24 dev tap0 || true
    sudo ip link set tap0 up || true
    qemu-system-x86_64 -bios /usr/share/ovmf/OVMF.fd \
        -cdrom $(BUILD_DIR)/axiome.iso \
        -drive file=$(BUILD_DIR)/disk.img,format=raw,if=ide,index=0,media=disk \
        -m 512M -serial stdio -display sdl \
        -netdev tap,id=net0,ifname=tap0,script=no,downscript=no \
        -device e1000,netdev=net0
```

### 11.7 Kernel Boot Changes (`kernel/kernel.c`)

After `fat32_automount()`, mount axiomefs root:

```c
/* Mount axiomefs root from partition 1 (second MBR partition, 0-indexed) */
axiomefs_mount_part(0, 0, 1, "/");

/* Parse /etc/passwd for user database */
security_init();
```

---

## 12. Userspace Tools

| Program | Syscalls Used | Description |
|---------|---------------|-------------|
| `whoami` | `GETUID`, `GETEUID`, `GETGID`, `GETEGID`, `GETROLE`, `GETCAP` | Print current user identity |
| `login` | `OPEN`, `READ`, `GETUID`, `SETUID`, `SETGID`, `EXEC` | Authenticate against /etc/passwd, spawn shell |
| `su` | `SETUID`, `SETGID`, `EXEC` | Switch user (needs CAP_SETUID) |
| `chmod` | `OPEN`, `CHMOD` | Change file permissions |
| `chown` | `OPEN`, `CHOWN` | Change file owner |

### 12.1 `whoami.c` (skeleton)

```c
#include "syscall.h"
#include "stdio.h"

int main(void)
{
    uid_t uid  = getuid();
    uid_t euid = geteuid();
    gid_t gid  = getgid();
    gid_t egid = getegid();
    int role   = getrole();
    uint64_t caps = getcap();

    const char *role_names[] = {"guest", "user", "admin", "system"};

    printf("uid=%d euid=%d gid=%d egid=%d role=%s caps=0x%llx\n",
           uid, euid, gid, egid,
           (role >= 0 && role <= 3) ? role_names[role] : "???",
           (unsigned long long)caps);
    return 0;
}
```

### 12.2 `login.c` (skeleton)

```c
#include "syscall.h"
#include "stdio.h"
#include "string.h"

int main(void)
{
    char username[32], password[64];
    printf("Username: ");
    /* read from stdin */
    /* printf("Password: "); */
    /* read from stdin (no echo) */

    int uid = sys_authenticate(username, password);
    if (uid < 0) {
        printf("Login failed\n");
        return 1;
    }

    setuid(uid);
    /* look up gid from /etc/passwd */
    setgid(gid_for_uid(uid));

    /* spawn shell */
    exec("/bin/sh");
    return 0;
}
```

---

## 13. Signal & Process Isolation

### 13.1 `SYS_KILL` Gate

```c
static uint64_t sys_kill(uint64_t pid, uint64_t sig, ...)
{
    struct thread *t = sched_current();
    struct thread *target = sched_find_by_pid((int)pid);
    if (!target) return -ESRCH;

    /* can always signal own processes */
    if (target->uid == t->euid) { /* allowed */ }
    else if (t->caps_prm & CAP_KILL_ANY) { /* allowed */ }
    else return -EPERM;

    /* deliver signal */
    target->sig_pending |= (1ULL << sig);
    return 0;
}
```

### 13.2 `SYS_PS` Gate

```c
static uint64_t sys_ps(uint64_t buf_ptr, uint64_t max, ...)
{
    struct thread *t = sched_current();
    /* if not SYS_ADMIN, filter to own processes only */
    if (!(t->caps_prm & CAP_SYS_ADMIN)) {
        /* only enumerate processes where p->uid == t->euid */
    }
    /* ... */
}
```

---

## 14. Implementation Order

| Step | Phase | Files | Est. Lines |
|------|-------|-------|-----------|
| 1 | Security core | `security.h`, `security.c` (new) | ~300 |
| 2 | Thread context | `sched.h`, `sched.c`, `kernel.c` | ~80 |
| 3 | VFS permissions | `vfs.h`, `vfs.c`, `syscall.c` | ~250 |
| 4 | FS backend updates | `axiomefs.c`, `fat32.c`, `vfs.c` | ~100 |
| 5 | /etc/passwd parser | `security.c` | ~150 |
| 6 | POSIX layer + new syscalls | `posix.c` (new), `syscall.h`, `syscall.c`, libc | ~300 |
| 7 | Userspace tools | `whoami.c`, `login.c`, `su.c`, `chmod.c`, `chown.c` (new) | ~400 |
| 8 | Signal/process isolation | `syscall.c` | ~50 |
| 9 | Build system | `Makefile`, `mkpart.py` (new), `mkfs_axiomefs.c` | ~120 |
| 10 | Build system integration | `root_manifest.txt` (new), `kernel.c` | ~20 |

**Total: ~1,770 new/modified lines across ~12 new files + ~10 modified files.**

---

## 15. Testing Strategy

1. **Unit tests**: Each new syscall tested from a userspace test program
2. **Permission tests**: Create files as user, verify admin can't read without perms
3. **Role transition tests**: `su` from user to admin, verify caps change
4. **Guest isolation tests**: Guest writes to `/tmp` succeed, writes elsewhere fail
5. **Build tests**: `make disk.img` creates valid disk, QEMU boots and mounts root
6. **Integration tests**: `login` authenticates, `whoami` shows correct role

---

## 16. Files Reference

| File | Status | Purpose |
|------|--------|---------|
| `kernel/security.h` | **NEW** | Role definitions, capability flags, user struct |
| `kernel/security.c` | **NEW** | Capability checks, user DB, auth |
| `kernel/posix.c` | **NEW** | POSIX adapter: uid↔role mapping, chmod/chown |
| `kernel/sched.h` | MODIFY | Add security fields to `struct thread` |
| `kernel/sched.c` | MODIFY | Initialize security context in spawn/fork |
| `kernel/vfs.h` | MODIFY | Add uid/gid/mode to `struct vnode` |
| `kernel/vfs.c` | MODIFY | Add `vfs_check_perms()`, gate VFS ops |
| `kernel/syscall.h` | MODIFY | Add 11 new syscall numbers (41–51) |
| `kernel/syscall.c` | MODIFY | Implement new syscalls, gate existing ones |
| `kernel/axiomefs.c` | MODIFY | Propagate uid/gid/mode through CoW |
| `kernel/fat32.c` | MODIFY | Mount-time uid/gid/mask defaults |
| `kernel/kernel.c` | MODIFY | Mount axiomefs root, init security |
| `kernel/Makefile` | MODIFY | Add new .c files to build |
| `tools/mkpart.py` | **NEW** | MBR partition table writer |
| `tools/mkfs_axiomefs.c` | MODIFY | Accept manifest, create root FS |
| `root_manifest.txt` | **NEW** | Root filesystem file listing |
| `Makefile` | MODIFY | Add `disk.img` target, update `run` |
| `kernel/userspace/whoami.c` | **NEW** | Print current user identity |
| `kernel/userspace/login.c` | **NEW** | Authenticate and spawn shell |
| `kernel/userspace/su.c` | **NEW** | Switch user |
| `kernel/userspace/chmod.c` | **NEW** | Change file permissions |
| `kernel/userspace/chown.c` | **NEW** | Change file owner |
