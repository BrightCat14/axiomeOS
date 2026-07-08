#include "vfs.h"
#include "sched.h"
#include "pmm.h"
#include "slab.h"
#include "printk.h"
#include "string.h"
#include <stddef.h>

static void pcpy(char *dst, const char *src, size_t max)
{
    size_t i = 0;
    for (; i < max && src[i]; i++)
        dst[i] = src[i];
    if (i < max)
        dst[i] = 0;
}


/* ---------------------------------------------------------------- *
 * 11.7 /proc virtual filesystem
 *
 * A synthetic filesystem mounted at "/System/Process" (the "/proc" alias
 * resolves here) that exposes per-process information.  Tree:
 *   /System/Process/<pid>/status     process state (pid, ppid, state, name)
 *   /System/Process/<pid>/cmdline    process name (NUL-terminated)
 *   /System/Process/self             alias for the caller's own pid dir
 *   /System/Process/meminfo          physical memory totals
 *
 * Every lookup synthesizes a fresh vnode (freed by the fs on release),
 * carrying a small descriptor in `priv`.
 * ---------------------------------------------------------------- */

enum {
    PROC_ROOT = 0,
    PROC_PID,
    PROC_STATUS,
    PROC_CMDLINE,
    PROC_MEMINFO
};

struct proc_node {
    int kind;
    int pid;
};

/* ---- tiny formatting helpers (no kernel sprintf available) ---- */
static void pmem(char *dst, size_t *pos, const char *s)
{
    while (*s) dst[(*pos)++] = *s++;
}
static void pnum(char *dst, size_t *pos, long v)
{
    char tmp[24];
    int i = 0;
    if (v < 0) { dst[(*pos)++] = '-'; v = -v; }
    if (v == 0) tmp[i++] = '0';
    while (v > 0) { tmp[i++] = '0' + (v % 10); v /= 10; }
    while (i > 0) dst[(*pos)++] = tmp[--i];
}

static char proc_state_char(int state)
{
    switch (state)
    {
        case 1: return 'R';   /* running */
        case 0: return 'R';   /* ready */
        case 2: return 'S';   /* blocked (sleeping) */
        case 3: return 'Z';   /* zombie */
        default: return '?';
    }
}

/* Build the text content for a proc file into `buf` (size `cap`).
   Returns the number of bytes produced. */
static size_t proc_render(struct proc_node *pn, char *buf, size_t cap)
{
    size_t pos = 0;
    if (pn->kind == PROC_STATUS)
    {
        struct thread *t = sched_find_by_pid(pn->pid);
        if (!t) return 0;
        pmem(buf, &pos, "Pid:\t");
        pnum(buf, &pos, t->pid);
        pmem(buf, &pos, "\nPPid:\t");
        pnum(buf, &pos, t->parent_pid);
        pmem(buf, &pos, "\nState:\t");
        buf[pos++] = proc_state_char(t->state);
        pmem(buf, &pos, " (");
        buf[pos++] = proc_state_char(t->state);
        pmem(buf, &pos, ")\nName:\t");
        for (int i = 0; t->name[i] && pos < cap - 1; i++)
            buf[pos++] = t->name[i];
        pmem(buf, &pos, "\n");
    }
    else if (pn->kind == PROC_CMDLINE)
    {
        struct thread *t = sched_find_by_pid(pn->pid);
        if (!t) return 0;
        for (int i = 0; t->name[i] && pos < cap - 1; i++)
            buf[pos++] = t->name[i];
        buf[pos++] = 0;
    }
    else if (pn->kind == PROC_MEMINFO)
    {
        uint64_t free = pmm_free_count();
        uint64_t total = pmm_total_frames();
        pmem(buf, &pos, "MemTotal: ");
        pnum(buf, &pos, (long)(total << 2));   /* 4 KiB pages -> KiB */
        pmem(buf, &pos, " kB\nMemFree:  ");
        pnum(buf, &pos, (long)(free << 2));
        pmem(buf, &pos, " kB\n");
    }
    if (pos > cap) pos = cap;
    return pos;
}

static struct vnode *proc_new(int kind, int pid, const char *name)
{
    struct vnode *n = kmalloc(sizeof(struct vnode));
    if (!n) return 0;
    memset(n, 0, sizeof(*n));
    pcpy(n->name, name, MAX_NAME);
    n->name[MAX_NAME] = 0;
    struct proc_node *pn = kmalloc(sizeof(struct proc_node));
    if (!pn) { kfree(n); return 0; }
    pn->kind = kind;
    pn->pid = pid;
    n->type = (kind == PROC_ROOT || kind == PROC_PID) ? VFS_DIR : VFS_FILE;
    n->priv = pn;
    return n;
}

/* Resolve `relpath` (relative to sb root) into a synthesized vnode. */
static struct vnode *proc_lookup(struct vfs_super *sb, struct vnode *dir,
                                 const char *relpath)
{
    (void)sb;
    if (!dir || dir->type != VFS_DIR)
        return 0;
    struct proc_node *base = (struct proc_node *)dir->priv;
    if (!base) return 0;

    const char *p = relpath;
    if (p[0] == '/') p++;
    if (*p == 0)
        return dir;

    while (*p)
    {
        while (*p == '/') p++;
        if (!*p) break;
        const char *seg = p;
        while (*p && *p != '/') p++;
        size_t seglen = (size_t)(p - seg);
        if (seglen > MAX_NAME) seglen = MAX_NAME;
        char comp[MAX_NAME + 1];
        for (size_t i = 0; i < seglen; i++) comp[i] = seg[i];
        comp[seglen] = 0;

        if (base->kind == PROC_ROOT)
        {
            if (strcmp(comp, "self") == 0)
            {
                struct thread *self = sched_current();
                if (!self) return 0;
                struct vnode *n = proc_new(PROC_PID, self->pid, comp);
                if (!n) return 0;
                n->sb = sb;
                dir = n;
                base = (struct proc_node *)n->priv;
            }
            else if (strcmp(comp, "meminfo") == 0)
            {
                struct vnode *n = proc_new(PROC_MEMINFO, 0, comp);
                if (!n) return 0;
                n->sb = sb;
                return n;
            }
            else
            {
                int pid = 0, ok = 1;
                for (size_t i = 0; i < seglen; i++)
                {
                    if (comp[i] < '0' || comp[i] > '9') { ok = 0; break; }
                    pid = pid * 10 + (comp[i] - '0');
                }
                if (!ok || !sched_find_by_pid(pid))
                    return 0;
                struct vnode *n = proc_new(PROC_PID, pid, comp);
                if (!n) return 0;
                n->sb = sb;
                dir = n;
                base = (struct proc_node *)n->priv;
            }
        }
        else if (base->kind == PROC_PID)
        {
            if (strcmp(comp, "status") == 0)
            {
                struct vnode *n = proc_new(PROC_STATUS, base->pid, comp);
                if (!n) return 0;
                n->sb = sb;
                return n;
            }
            if (strcmp(comp, "cmdline") == 0)
            {
                struct vnode *n = proc_new(PROC_CMDLINE, base->pid, comp);
                if (!n) return 0;
                n->sb = sb;
                return n;
            }
            return 0;
        }
        else
        {
            return 0;
        }
        if (*p == 0)
            break;
    }
    return dir;
}

static size_t proc_read(struct vfs_super *sb, struct vnode *n, size_t off,
                        void *buf, size_t len)
{
    (void)sb;
    if (!n || n->type != VFS_FILE || !n->priv)
        return 0;
    struct proc_node *pn = (struct proc_node *)n->priv;
    char tmp[512];
    size_t total = proc_render(pn, tmp, sizeof(tmp));
    n->size = total;
    if (off >= total)
        return 0;
    size_t avail = total - off;
    size_t r = (len < avail) ? len : avail;
    for (size_t i = 0; i < r; i++)
        ((uint8_t *)buf)[i] = tmp[off + i];
    return r;
}

static int proc_list(struct vfs_super *sb, struct vnode *dir,
                     struct vfs_dirent *ents, int max)
{
    (void)sb;
    if (!dir || dir->type != VFS_DIR || !dir->priv)
        return -1;
    struct proc_node *pn = (struct proc_node *)dir->priv;
    int count = 0;

    if (count < max)
    {
        ents[count].type = DT_DIR;
        pcpy(ents[count].name, ".", MAX_NAME);
    }
    count++;
    if (count < max)
    {
        ents[count].type = DT_DIR;
        pcpy(ents[count].name, "..", MAX_NAME);
    }
    count++;

    if (pn->kind == PROC_ROOT)
    {
        struct proc_info info[64];
        int np = sched_enum_procs(info, 64);
        for (int i = 0; i < np; i++)
        {
            char nm[16];
            int k = 0;
            int v = info[i].pid;
            if (v == 0) nm[k++] = '0';
            while (v > 0) { nm[k++] = '0' + (v % 10); v /= 10; }
            for (int j = 0; j < k / 2; j++) { char t = nm[j]; nm[j] = nm[k - 1 - j]; nm[k - 1 - j] = t; }
            nm[k] = 0;
            if (count < max)
            {
                ents[count].type = DT_DIR;
                pcpy(ents[count].name, nm, MAX_NAME);
            }
            count++;
        }
        if (count < max)
        {
            ents[count].type = DT_DIR;
            pcpy(ents[count].name, "self", MAX_NAME);
        }
        count++;
        if (count < max)
        {
            ents[count].type = DT_FILE;
            pcpy(ents[count].name, "meminfo", MAX_NAME);
        }
        count++;
    }
    else if (pn->kind == PROC_PID)
    {
        if (count < max)
        {
            ents[count].type = DT_FILE;
            pcpy(ents[count].name, "status", MAX_NAME);
        }
        count++;
        if (count < max)
        {
            ents[count].type = DT_FILE;
            pcpy(ents[count].name, "cmdline", MAX_NAME);
        }
        count++;
    }
    return count;
}

static void proc_inode_free(struct vnode *n)
{
    if (!n) return;
    if (n->priv) kfree(n->priv);
    kfree(n);
}

static struct vfs_fops g_proc_ops = {
    .lookup = proc_lookup,
    .read   = proc_read,
    .list   = proc_list,
    .inode_free = proc_inode_free,
};

void procfs_init(void)
{
    struct vnode *root = proc_new(PROC_ROOT, 0, "/");
    if (!root) return;
    struct vfs_super *sb = vfs_mount("/System/Process", FS_PROC, &g_proc_ops, 0);
    if (!sb)
    {
        proc_inode_free(root);
        return;
    }
    root->sb = sb;
    sb->root = root;
    printk("VFS: /proc virtual filesystem mounted\n");
}
