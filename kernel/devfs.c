#include "vfs.h"
#include "driver.h"
#include "slab.h"
#include "printk.h"
#include "string.h"
#include <stddef.h>

/* Synthetic filesystem mounted at "/Devices". The device registry (driver.c)
   is the source of truth; each registered device becomes a node. */

enum { DEV_ROOT = 0 };

struct dev_node {
    int kind;
    struct device *dev;
};

static void dcopy(char *dst, const char *src)
{
    int i = 0;
    for (; src[i] && i < MAX_NAME; i++)
        dst[i] = src[i];
    dst[i] = 0;
}

static struct vnode *dev_new(int kind, struct device *dev, const char *name)
{
    struct vnode *n = (struct vnode *)kmalloc(sizeof(struct vnode));
    if (!n) return 0;
    memset(n, 0, sizeof(*n));
    dcopy(n->name, name);
    n->type = (kind == DEV_ROOT) ? VFS_DIR : VFS_FILE;
    n->priv = kmalloc(sizeof(struct dev_node));
    if (!n->priv) { kfree(n); return 0; }
    struct dev_node *dn = (struct dev_node *)n->priv;
    dn->kind = kind;
    dn->dev = dev;
    return n;
}

static struct vnode *dev_lookup(struct vfs_super *sb, struct vnode *dir,
                                const char *relpath)
{
    (void)sb;
    if (!dir || dir->type != VFS_DIR)
        return 0;
    struct dev_node *base = (struct dev_node *)dir->priv;
    if (!base)
        return 0;

    const char *p = relpath;
    if (p[0] == '/') p++;
    if (*p == 0)
        return dir;

    while (*p == '/') p++;
    if (!*p) return dir;
    const char *seg = p;
    while (*p && *p != '/') p++;
    size_t seglen = (size_t)(p - seg);
    if (seglen > MAX_NAME) seglen = MAX_NAME;
    char comp[MAX_NAME + 1];
    for (size_t i = 0; i < seglen; i++) comp[i] = seg[i];
    comp[seglen] = 0;

    if (base->kind == DEV_ROOT)
    {
        struct device *d = device_find(comp);
        if (!d) return 0;
        struct vnode *n = dev_new(1, d, comp);
        if (!n) return 0;
        n->sb = sb;
        return n;
    }
    return 0;
}

static size_t dev_read(struct vfs_super *sb, struct vnode *n, size_t off,
                       void *buf, size_t len)
{
    (void)sb;
    if (!n || n->type == VFS_DIR || !n->priv)
        return 0;
    struct dev_node *dn = (struct dev_node *)n->priv;
    if (dn->kind == DEV_ROOT || !dn->dev)
        return 0;
    if (!dn->dev->ops.read)
        return (size_t)-1;
    long r = dn->dev->ops.read(dn->dev, off, buf, len);
    return (r < 0) ? (size_t)-1 : (size_t)r;
}

static size_t dev_write(struct vfs_super *sb, struct vnode *n, size_t off,
                        const void *buf, size_t len)
{
    (void)sb;
    if (!n || n->type == VFS_DIR || !n->priv)
        return 0;
    struct dev_node *dn = (struct dev_node *)n->priv;
    if (dn->kind == DEV_ROOT || !dn->dev)
        return 0;
    if (!dn->dev->ops.write)
        return (size_t)-1;
    long r = dn->dev->ops.write(dn->dev, off, buf, len);
    return (r < 0) ? (size_t)-1 : (size_t)r;
}

static int dev_list(struct vfs_super *sb, struct vnode *dir,
                    struct vfs_dirent *ents, int max)
{
    (void)sb;
    if (!dir || dir->type != VFS_DIR || !dir->priv)
        return -1;
    struct dev_node *dn = (struct dev_node *)dir->priv;
    if (dn->kind != DEV_ROOT)
        return -1;

    int count = 0;
    if (count < max) { ents[count].type = DT_DIR; dcopy(ents[count].name, "."); }
    count++;
    if (count < max) { ents[count].type = DT_DIR; dcopy(ents[count].name, ".."); }
    count++;

    struct device *devs[64];
    int n = device_enumerate(devs, 64);
    for (int i = 0; i < n; i++)
    {
        if (count < max)
        {
            ents[count].type = (devs[i]->type == DEV_BLOCK) ? DT_DIR : DT_FILE;
            dcopy(ents[count].name, devs[i]->name);
        }
        count++;
    }
    return count;
}

static void dev_inode_free(struct vnode *n)
{
    if (!n) return;
    if (n->priv) kfree(n->priv);
    kfree(n);
}

static long dev_mmap(struct vfs_super *sb, struct vnode *n, uint64_t off,
                      uint64_t virt, size_t len, uint64_t flags)
{
    (void)sb;
    if (!n || n->type == VFS_DIR || !n->priv)
        return -1;
    struct dev_node *dn = (struct dev_node *)n->priv;
    if (dn->kind == DEV_ROOT || !dn->dev)
        return -1;
    if (!dn->dev->ops.mmap)
        return -1;
    return dn->dev->ops.mmap(dn->dev, off, virt, len, flags);
}

static struct vfs_fops g_dev_ops = {
    .lookup = dev_lookup,
    .read   = dev_read,
    .write  = dev_write,
    .list   = dev_list,
    .mmap   = dev_mmap,
    .inode_free = dev_inode_free,
};

void devfs_init(void)
{
    struct vnode *root = dev_new(DEV_ROOT, 0, "/");
    if (!root) return;
    struct vfs_super *sb = vfs_mount("/Devices", FS_DEV, &g_dev_ops, 0);
    if (!sb)
    {
        dev_inode_free(root);
        return;
    }
    root->sb = sb;
    sb->root = root;
    printk("VFS: /Devices device filesystem mounted\n");
}
