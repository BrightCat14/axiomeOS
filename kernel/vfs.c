#include "vfs.h"
#include "slab.h"
#include "sched.h"
#include "printk.h"
#include "string.h"
#include <stddef.h>

/* ---- tiny string helpers (avoid depending on kernel/string.c names) ---- */
static size_t vstrlen(const char *s)
{
    size_t n = 0;
    while (s[n]) n++;
    return n;
}
static int vstrcmp(const char *a, const char *b)
{
    while (*a && *a == *b) { a++; b++; }
    return (int)(unsigned char)*a - (int)(unsigned char)*b;
}
static void vstrncpy(char *d, const char *s, size_t n)
{
    size_t i = 0;
    for (; i < n && s[i]; i++) d[i] = s[i];
    d[i] = 0;
}

/* ================================================================ *
 * Mount table (the dentry cache / namespace)
 * ================================================================ */
#define MAX_MOUNTS 8
static struct vfs_super g_mounts[MAX_MOUNTS];
static int g_nmounts;
static struct vfs_super *g_ramfs_sb;   /* ramfs super (== &g_mounts[0]) */

/* prefix compare helper (abs starts with prefix of length m) */
static int vstrncmp_prefix(const char *abs, const char *prefix, size_t m)
{
    for (size_t i = 0; i < m; i++)
        if (abs[i] != prefix[i])
            return 0;
    return 1;
}

struct vfs_super *vfs_mount(const char *mountpoint, int fs_type, struct vfs_fops *ops,
                             void *priv)
{
    if (g_nmounts >= MAX_MOUNTS)
        return 0;
    struct vfs_super *sb = &g_mounts[g_nmounts];
    vstrncpy(sb->mountpoint, mountpoint, MAX_NAME);
    sb->fs_type = fs_type;
    sb->ops = ops;
    sb->priv = priv;
    sb->root = 0;
    g_nmounts++;
    printk("VFS: mounted fs_type=%d at '%s'\n", fs_type, mountpoint);
    return sb;
}

static struct vfs_super *mount_find(const char *abs)
{
    struct vfs_super *best = 0;
    size_t bestlen = 0;
    for (int i = 0; i < g_nmounts; i++)
    {
        struct vfs_super *sb = &g_mounts[i];
        size_t m = vstrlen(sb->mountpoint);
        if (vstrcmp(abs, sb->mountpoint) == 0)
        {
            /* Exact match: keep the *last* registered mount so a persistent
               fs mounted at "/" (e.g. axiomefs) shadows the boot ramfs. */
            if (m >= bestlen) { best = sb; bestlen = m; }
            continue;
        }
        if (vstrncmp_prefix(abs, sb->mountpoint, m) &&
            (sb->mountpoint[m - 1] == '/' || abs[m] == '/' || m == 1))
        {
            if (m >= bestlen) { best = sb; bestlen = m; }
        }
    }
    return best;
}

/* ---- path aliases -------------------------------------------------------
 * The VFS keeps a single set of objects. Friendly lower-case names are aliases
 * that rewrite to canonical PascalCase directories before any resolution:
 *     /bin -> /Binaries   /home -> /Users
 *     /dev -> /Devices    /tmp  -> /Temporary
 * Resolving in exactly one place (absolutize / make_abs) means every syscall
 * and the mount table see only canonical paths; ported software is unaware. */
struct vfs_alias { const char *alias; const char *canon; };
static const struct vfs_alias g_vfs_aliases[] = {
    { "/bin",      "/Binaries" },
    { "/home",     "/Users" },
    { "/dev",      "/Devices" },
    { "/tmp",      "/Temporary" },
    { "/etc",      "/System/Configuration" },
    { "/var",      "/System/Variable" },
    { "/proc",     "/System/Process" },
    { 0, 0 }
};

void vfs_apply_aliases(char *abs)
{
    size_t al = vstrlen(abs);
    for (int i = 0; g_vfs_aliases[i].alias; i++)
    {
        size_t a = vstrlen(g_vfs_aliases[i].alias);
        if (!vstrncmp_prefix(abs, g_vfs_aliases[i].alias, a))
            continue;
        char after = abs[a];
        if (after != 0 && after != '/')
            continue;   /* not a whole component (e.g. /binary vs /bin) */
        size_t c = vstrlen(g_vfs_aliases[i].canon);
        long delta = (long)c - (long)a;
        if ((long)al + delta >= 319)
            break;      /* no room in the working buffer; leave as-is */
        size_t rest = al - a;   /* chars from index a..al inclusive */
        for (long k = (long)rest; k >= 0; k--)
            abs[a + delta + k] = abs[a + k];
        for (size_t k = 0; k < c; k++)
            abs[k] = g_vfs_aliases[i].canon[k];
        break;
    }
}

/* Resolve `path` (relative to `cwd`) to an absolute path in `out`. */
static void absolutize(const char *path, const char *cwd, char *out)
{
    if (path[0] == '/')
    {
        vstrncpy(out, path, MAX_NAME);
        vfs_apply_aliases(out);
        return;
    }
    size_t cl = vstrlen(cwd);
    size_t i = 0;
    for (; i < cl && i < MAX_NAME; i++) out[i] = cwd[i];
    if (i > 0 && out[i - 1] != '/' && i < MAX_NAME) out[i++] = '/';
    for (size_t k = 0; path[k] && i < MAX_NAME; k++) out[i++] = path[k];
    out[i] = 0;
    vfs_apply_aliases(out);
}

/* ================================================================ *
 * ramfs implementation
 * ================================================================ */
static struct vnode *g_root;

static struct vnode *ramfs_new_node(const char *name, int type)
{
    struct vnode *n = kmalloc(sizeof(struct vnode));
    if (!n) return 0;
    memset(n, 0, sizeof(*n));
    vstrncpy(n->name, name, MAX_NAME);
    n->type = type;
    n->sb = g_ramfs_sb;
    /* Default ownership/permissions (overridden by ramfs_create). */
    n->v_uid = 0;
    n->v_gid = 0;
    n->v_mode = (type == VFS_DIR) ? 0755 : 0644;
    return n;
}

/* Set owner + mode on a freshly created ramfs node. Owner comes from the
   calling process (falls back to root when none). */
static void ramfs_set_attrs(struct vnode *n, int type)
{
    struct thread *t = sched_current();
    n->v_uid = t ? t->uid : 0;
    n->v_gid = t ? t->gid : 0;
    n->v_mode = (type == VFS_DIR) ? 0755 : 0644;
}

static void ramfs_add_child(struct vnode *parent, struct vnode *child)
{
    if (parent->nchildren >= parent->cap_children)
    {
        int ncap = parent->cap_children ? parent->cap_children * 2 : 4;
        struct vnode **nc = kmalloc(sizeof(struct vnode *) * ncap);
        if (!nc) return;
        for (int i = 0; i < parent->nchildren; i++) nc[i] = parent->children[i];
        if (parent->children) kfree(parent->children);
        parent->children = nc;
        parent->cap_children = ncap;
    }
    child->parent = parent;
    parent->children[parent->nchildren++] = child;
}

static struct vnode *ramfs_find_child(struct vnode *dir, const char *name)
{
    for (int i = 0; i < dir->nchildren; i++)
        if (vstrcmp(dir->children[i]->name, name) == 0)
            return dir->children[i];
    return 0;
}

/* Walk `relpath` from `start` (resolving . and ..). Returns node or 0. */
static struct vnode *ramfs_walk(struct vnode *start, const char *relpath)
{
    struct vnode *cur = start;
    const char *p = relpath;
    if (p[0] == '/') p++;
    while (*p)
    {
        while (*p == '/') p++;
        if (!*p) break;
        const char *seg = p;
        while (*p && *p != '/') p++;
        size_t seglen = (size_t)(p - seg);
        if (seglen == 1 && seg[0] == '.')
            continue;
        if (seglen == 2 && seg[0] == '.' && seg[1] == '.')
        {
            if (cur->parent) cur = cur->parent;
            continue;
        }
        char comp[MAX_NAME + 1];
        if (seglen > MAX_NAME) seglen = MAX_NAME;
        vstrncpy(comp, seg, seglen);
        if (cur->type != VFS_DIR)
            return 0;
        struct vnode *next = ramfs_find_child(cur, comp);
        if (!next) return 0;
        cur = next;
    }
    return cur;
}

static struct vnode *ramfs_lookup(struct vfs_super *sb, struct vnode *dir,
                                  const char *relpath)
{
    (void)sb;
    if (relpath[0] == 0)
        return dir;
    return ramfs_walk(dir, relpath);
}

static size_t ramfs_ensure_cap(struct vnode *n, size_t need)
{
    if (need <= n->cap)
        return 0;
    size_t ncap = n->cap ? n->cap : 64;
    while (ncap < need) ncap *= 2;
    uint8_t *nd = kmalloc(ncap);
    if (!nd) return (size_t)-1;
    for (size_t i = 0; i < n->size; i++) nd[i] = n->data[i];
    if (n->data) kfree(n->data);
    n->data = nd;
    n->cap = ncap;
    return 0;
}

static size_t ramfs_read(struct vfs_super *sb, struct vnode *n, size_t off,
                         void *buf, size_t len)
{
    (void)sb;
    if (!n || n->type != VFS_FILE)
        return 0;
    if (off >= n->size)
        return 0;
    size_t avail = n->size - off;
    size_t r = (len < avail) ? len : avail;
    for (size_t i = 0; i < r; i++)
        ((uint8_t *)buf)[i] = n->data[off + i];
    return r;
}

static size_t ramfs_write(struct vfs_super *sb, struct vnode *n, size_t off,
                          const void *buf, size_t len)
{
    (void)sb;
    if (!n || n->type != VFS_FILE)
        return 0;
    size_t need = off + len;
    if (ramfs_ensure_cap(n, need) != 0)
        return 0;
    for (size_t i = 0; i < len; i++)
        n->data[off + i] = ((const uint8_t *)buf)[i];
    if (off + len > n->size)
        n->size = off + len;
    return len;
}

static int ramfs_list(struct vfs_super *sb, struct vnode *dir,
                      struct vfs_dirent *ents, int max)
{
    (void)sb;
    if (!dir || dir->type != VFS_DIR)
        return -1;
    int count = 0;
    if (count < max) { ents[count].type = DT_DIR; vstrncpy(ents[count].name, ".", MAX_NAME); }
    count++;
    if (count < max) { ents[count].type = DT_DIR; vstrncpy(ents[count].name, "..", MAX_NAME); }
    count++;
    for (int i = 0; i < dir->nchildren; i++)
    {
        if (count < max)
        {
            ents[count].type = (dir->children[i]->type == VFS_DIR) ? DT_DIR : DT_FILE;
            vstrncpy(ents[count].name, dir->children[i]->name, MAX_NAME);
        }
        count++;
    }
    return count;
}

/* Create `relpath` (relative to fs root) of given type. 0/-1. */
static int ramfs_create(struct vfs_super *sb, const char *relpath, int type)
{
    const char *slash = 0;
    for (const char *q = relpath; *q; q++)
        if (*q == '/') slash = q;
    char dirpath[MAX_NAME + 1];
    const char *base;
    if (!slash)
    {
        vstrncpy(dirpath, "", 0);
        base = relpath;
    }
    else
    {
        size_t dl = (size_t)(slash - relpath);
        if (dl > MAX_NAME) dl = MAX_NAME;
        vstrncpy(dirpath, relpath, dl);
        base = slash + 1;
    }
    struct vnode *parent = ramfs_walk(sb->root, dirpath[0] ? dirpath : "/");
    if (!parent || parent->type != VFS_DIR)
        return -1;
    if (ramfs_find_child(parent, base))
        return -1;
    struct vnode *n = ramfs_new_node(base, type);
    if (!n) return -1;
    ramfs_set_attrs(n, type);
    if (type == VFS_FIFO)
    {
        struct pipe *p = kmalloc(sizeof(*p));
        if (!p) { kfree(n); return -1; }
        p->buf = kmalloc(256);
        if (!p->buf) { kfree(p); kfree(n); return -1; }
        p->cap = 256; p->count = 0;
        p->nreaders = 0; p->nwriters = 0; p->wait_reader = 0;
        p->shared = 1; p->dead = 0;
        n->priv = p;
    }
    ramfs_add_child(parent, n);
    return 0;
}

static int ramfs_remove(struct vfs_super *sb, const char *relpath)
{
    const char *slash = 0;
    for (const char *q = relpath; *q; q++)
        if (*q == '/') slash = q;
    char dirpath[MAX_NAME + 1];
    const char *base;
    if (!slash) { dirpath[0] = 0; base = relpath; }
    else
    {
        size_t dl = (size_t)(slash - relpath);
        if (dl > MAX_NAME) dl = MAX_NAME;
        vstrncpy(dirpath, relpath, dl);
        base = slash + 1;
    }
    struct vnode *parent = ramfs_walk(sb->root, dirpath[0] ? dirpath : "/");
    if (!parent) return -1;
    struct vnode *n = ramfs_find_child(parent, base);
    if (!n || n == g_root) return -1;
    if (n->type == VFS_DIR && n->nchildren > 0) return -1; /* not empty */
    for (int i = 0; i < parent->nchildren; i++)
    {
        if (parent->children[i] == n)
        {
            for (int j = i; j < parent->nchildren - 1; j++)
                parent->children[j] = parent->children[j + 1];
            parent->nchildren--;
            break;
        }
    }
    if (n->type == VFS_FIFO && n->priv)
    {
        struct pipe *p = (struct pipe *)n->priv;
        if (p->nreaders == 0 && p->nwriters == 0)
        {
            if (p->buf) kfree(p->buf);
            kfree(p);
        }
        else
            p->dead = 1;   /* free when the last fd is closed */
        n->priv = 0;
    }
    if (n->data) kfree(n->data);
    kfree(n);
    return 0;
}

static void ramfs_inode_free(struct vnode *n)
{
    (void)n; /* ramfs inodes are tree-resident; never freed via lookup */
}

static struct vfs_fops g_ramfs_ops = {
    .lookup = ramfs_lookup,
    .read   = ramfs_read,
    .list   = ramfs_list,
    .create = ramfs_create,
    .remove = ramfs_remove,
    .write  = ramfs_write,
    .inode_free = ramfs_inode_free,
};

/* ================================================================ *
 * Generic VFS dispatch (used by the syscall layer)
 * ================================================================ */

int vfs_path_is_under_tmp(const char *abs)
{
    /* Canonical form is /Temporary (the /tmp alias is rewritten before this
       gate runs); accept both during any transition. */
    if (vstrncmp_prefix(abs, "/tmp", 4))
        return abs[4] == '\0' || abs[4] == '/';
    if (vstrncmp_prefix(abs, "/Temporary", 10))
        return abs[10] == '\0' || abs[10] == '/';
    return 0;
}

/* Write the parent directory of `abs` into `out` (e.g. "/a/b" -> "/a").
   "/" yields "/". */
static void vfs_parent_path(const char *abs, char *out)
{
    size_t len = 0;
    while (abs[len]) len++;
    if (len <= 1) { out[0] = '/'; out[1] = 0; return; }
    size_t i = len - 1;
    while (i > 0 && abs[i] != '/') i--;
    if (i == 0) { out[0] = '/'; out[1] = 0; return; }
    size_t k = 0;
    for (; k < i; k++) out[k] = abs[k];
    out[k] = 0;
}

/* Common gate for a create/remove/write by the current process:
   - guest may only act under /tmp
   - otherwise needs a write capability AND write permission on `dir`
   Returns 0 if allowed, -EPERM if denied. */
static int vfs_gate_write_file(const char *abs, struct vnode *dir)
{
    struct thread *t = sched_current();
    if (!t || t->role == ROLE_SYSTEM)
        return 0;
    if (t->role == ROLE_GUEST && !vfs_path_is_under_tmp(abs))
        return -EPERM;
    if (!(t->caps_eff & (CAP_FILE_WRITE_ANY | CAP_FILE_WRITE_SELF)))
        return -EPERM;
    if (dir && vfs_check_perms(dir, VFS_MAY_WRITE) != 0)
        return -EPERM;
    return 0;
}

/* Resolve `path` (relative to `cwd`) to an absolute path in `out`. */
static void absolutize(const char *path, const char *cwd, char *out); /* fwd */

int vfs_check_perms(struct vnode *n, int mask)
{
    struct thread *t = sched_current();
    if (!t) return 0;                  /* kernel thread: bypass */

    /* SYSTEM role bypasses all checks (kernel daemons only). */
    if (t->role == ROLE_SYSTEM) return 0;

    uid_t uid = t->euid;
    gid_t gid = t->egid;

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

struct vnode *vfs_lookup(const char *path, const char *cwd)
{
    char abs[320];
    absolutize(path, cwd, abs);
    struct vfs_super *sb = mount_find(abs);
    if (!sb) return 0;
    const char *rel = abs + vstrlen(sb->mountpoint);
    if (*rel == '/') rel++;
    struct vnode *n;
    if (*rel == 0)
        n = sb->root;
    else
        n = sb->ops->lookup(sb, sb->root, rel);
    if (n)
        n->refcount++;
    return n;
}

size_t vfs_read(struct vnode *n, size_t off, void *buf, size_t len)
{
    if (!n || !n->sb) return 0;
    return n->sb->ops->read(n->sb, n, off, buf, len);
}

size_t vfs_write(struct vnode *n, size_t off, const void *buf, size_t len)
{
    if (!n || !n->sb || !n->sb->ops->write) return 0;
    struct thread *t = sched_current();
    if (t && t->role != ROLE_SYSTEM)
    {
        if (!(t->caps_eff & (CAP_FILE_WRITE_ANY | CAP_FILE_WRITE_SELF)))
            return 0;
        if (n->type == VFS_FILE && vfs_check_perms(n, VFS_MAY_WRITE) != 0)
            return 0;
    }
    return n->sb->ops->write(n->sb, n, off, buf, len);
}

int vfs_read_file(const char *path, uint8_t **out_buf, size_t *out_size)
{
    struct vnode *n = vfs_lookup(path, "/");
    if (!n || n->type != VFS_FILE)
    {
        if (n) vfs_release(n);
        return -1;
    }
    size_t sz = n->size;
    if (sz == 0)
    {
        vfs_release(n);
        return -1;
    }
    uint8_t *buf = kmalloc(sz);
    if (!buf)
    {
        vfs_release(n);
        return -1;
    }
    size_t done = 0;
    while (done < sz)
    {
        size_t r = vfs_read(n, done, buf + done, sz - done);
        if (r == 0) break;
        done += r;
    }
    vfs_release(n);
    if (done != sz)
    {
        kfree(buf);
        return -1;
    }
    *out_buf = buf;
    *out_size = sz;
    return 0;
}

int vfs_list(const char *path, const char *cwd, struct vfs_dirent *ents, int max)
{
    struct vnode *dir = vfs_lookup(path, cwd);
    if (!dir || dir->type != VFS_DIR || !dir->sb) return -1;
    return dir->sb->ops->list(dir->sb, dir, ents, max);
}

int vfs_create(const char *path, int type, const char *cwd)
{
    char abs[320];
    absolutize(path, cwd, abs);

    /* Permission gate: need write capability + write perm on parent dir. */
    char parent[320];
    vfs_parent_path(abs, parent);
    struct vnode *pd = vfs_lookup(parent, "/");
    int gate = vfs_gate_write_file(abs, pd);
    vfs_release(pd);
    if (gate != 0)
        return -1;

    struct vfs_super *sb = mount_find(abs);
    if (!sb || !sb->ops->create) return -1;
    const char *rel = abs + vstrlen(sb->mountpoint);
    if (*rel == '/') rel++;
    return sb->ops->create(sb, rel, type);
}

int vfs_remove(const char *path, const char *cwd)
{
    char abs[320];
    absolutize(path, cwd, abs);

    char parent[320];
    vfs_parent_path(abs, parent);
    struct vnode *pd = vfs_lookup(parent, "/");
    int gate = vfs_gate_write_file(abs, pd);
    vfs_release(pd);
    if (gate != 0)
        return -1;

    struct vfs_super *sb = mount_find(abs);
    if (!sb || !sb->ops->remove) return -1;
    const char *rel = abs + vstrlen(sb->mountpoint);
    if (*rel == '/') rel++;
    return sb->ops->remove(sb, rel);
}

void vfs_release(struct vnode *n)
{
    if (!n) return;
    if (n->refcount > 0)
        n->refcount--;
    if (n->refcount == 0 && n->sb && n->sb->ops->inode_free)
        n->sb->ops->inode_free(n);
}

int vfs_mount_ramfs(const char *mountpoint)
{
    if (mountpoint[0] != '/')
        return -1;
    struct vnode *root = ramfs_new_node("/", VFS_DIR);
    if (!root) return -1;
    struct vfs_super *sb = vfs_mount(mountpoint, FS_RAMFS, &g_ramfs_ops, 0);
    if (!sb)
    {
        kfree(root);
        return -1;
    }
    root->sb = sb;
    sb->root = root;
    return 0;
}

/* Recursively free a ramfs tree (used when unmounting a ramfs mount). */
static void ramfs_destroy(struct vnode *n)
{
    if (!n) return;
    for (int i = 0; i < n->nchildren; i++)
        ramfs_destroy(n->children[i]);
    if (n->children) kfree(n->children);
    if (n->data) kfree(n->data);
    kfree(n);
}

int vfs_umount(const char *mountpoint)
{
    int idx = -1;
    for (int i = 0; i < g_nmounts; i++)
    {
        if (vstrcmp(g_mounts[i].mountpoint, mountpoint) == 0) { idx = i; break; }
    }
    if (idx < 0)
        return -1;
    struct vfs_super *sb = &g_mounts[idx];
    if (sb == g_ramfs_sb)
        return -1; /* cannot unmount the root filesystem */

    /* Refuse if the current process still has an fd into this fs. */
    struct thread *t = sched_current();
    if (t)
    {
        for (int i = 0; i < MAX_FD; i++)
        {
            if (t->fds[i].used && t->fds[i].kind == FD_VNODE &&
                t->fds[i].node && t->fds[i].node->sb == sb)
                return -1;
        }
    }

    if (sb->fs_type == FS_RAMFS)
        ramfs_destroy(sb->root);
    else if (sb->fs_type == FS_FAT32)
    {
        kfree(sb->priv);
        kfree(sb->root);
    }
    for (int j = idx; j < g_nmounts - 1; j++)
        g_mounts[j] = g_mounts[j + 1];
    g_nmounts--;
    printk("VFS: unmounted '%s'\n", mountpoint);
    return 0;
}

struct vnode *vfs_root(void)
{
    return g_root;
}

/* ================================================================ *
 * Init: mount ramfs at "/" and populate the built-in initrd.
 * ================================================================ */
struct initrd_ent {
    const char *path;
    const char *data;
    int dir;
};

static const struct initrd_ent initrd[] = {
    { "/readme.txt", "axiomeOS in-memory filesystem (ramfs).\n"
                     "Try: ls, cat readme.txt, mkdir tmp, echo hi > tmp/a.txt\n", 0 },
    { "/welcome.txt", "Welcome to axiomeOS Phase 10 (VFS).\n", 0 },
    { "/docs", NULL, 1 },
    { "/docs/notes.txt", "Phase 10 brings a minimal Unix-like VFS over a ramfs.\n", 0 },
};

void vfs_init(void)
{
    g_root = ramfs_new_node("/", VFS_DIR);
    if (!g_root) return;
    g_ramfs_sb = vfs_mount("/", FS_RAMFS, &g_ramfs_ops, 0);
    g_root->sb = g_ramfs_sb;
    g_ramfs_sb->root = g_root;

    for (size_t i = 0; i < sizeof(initrd) / sizeof(initrd[0]); i++)
    {
        const struct initrd_ent *e = &initrd[i];
        if (e->dir)
        {
            vfs_create(e->path, VFS_DIR, "/");
        }
        else
        {
            if (vfs_create(e->path, VFS_FILE, "/") == 0)
            {
                struct vnode *n = vfs_lookup(e->path, "/");
                if (n)
                    vfs_write(n, 0, e->data, vstrlen(e->data));
            }
        }
    }
    printk("VFS: ramfs mounted at '/'\n");
}
