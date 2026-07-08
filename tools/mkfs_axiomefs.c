/*
 * mkfs_axiomefs - host-side formatter for the minimal axiomefs v1 image.
 *
 * Enhanced build: writes an axiomefs filesystem into a *partition* of an
 * existing disk image (the partition is located at `part_offset_lba`
 * sectors into the file) and populates it from a manifest file.
 *
 * Usage:
 *   mkfs_axiomefs <disk.img> <part_offset_lba> <root_manifest.txt>
 *
 * The on-disk structs are shared verbatim with the kernel via
 * kernel/axiomefs.h so byte layouts can never drift.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "kernel/axiomefs.h"

#define MAX_NODES 256
#define SECTORS_PER_BLOCK (AXFS_BLOCK_SIZE / 512)

/* ---- node in the to-be-built tree ---- */
/* Files may span up to AXFS_MAX_EXTENTS data blocks; match the on-disk inode
 * limit so modules larger than 64 KiB (e.g. nvme.kxt) embed without being
 * truncated by the formatter. */
#define MAX_DATA_EXTENTS AXFS_MAX_EXTENTS
struct node {
    char path[256];      /* canonical path without trailing slash */
    char name[256];
    int is_dir;
    uint32_t uid, gid, mode;
    int has_content;
    char *content;
    size_t content_len;
    uint64_t inode_block;
    uint64_t data_blocks[MAX_DATA_EXTENTS];
    int ndata;
    int parent;          /* index into nodes[], -1 for root */
};

static struct node g_nodes[MAX_NODES];
static int g_nnodes;

/* ---- checksum (FNV-1a 64, matches kernel) ---- */
static uint64_t fnv(const uint8_t *p, size_t len, uint64_t h)
{
    for (size_t i = 0; i < len; i++) { h ^= p[i]; h *= 1099511628211ULL; }
    return h;
}
static uint64_t axfs_checksum(const uint8_t *blk)
{
    uint64_t h = 14695981039346656037ULL;
    h = fnv(blk, 24, h);
    h = fnv(blk + 32, AXFS_BLOCK_SIZE - 32, h);
    return h;
}
static void axfs_set_checksum(uint8_t *blk)
{
    struct axfs_obj_hdr *h = (struct axfs_obj_hdr *)blk;
    h->checksum = 0;
    h->checksum = axfs_checksum(blk);
}

/* ---- manifest parsing ---- */
static int parse_octal(const char *s)
{
    int v = 0;
    while (*s >= '0' && *s <= '7')
        v = v * 8 + (*s++ - '0');
    return v;
}

/* Copy `src` into `dst` unescaping "\n" -> newline and "\\" -> backslash.
   Returns the number of bytes written (excluding the terminator). */
static size_t unescape(const char *src, char *dst, size_t max)
{
    size_t j = 0;
    for (size_t i = 0; src[i] && j + 1 < max; i++)
    {
        if (src[i] == '\\' && src[i + 1] == 'n') { dst[j++] = '\n'; i++; }
        else if (src[i] == '\\' && src[i + 1] == '\\') { dst[j++] = '\\'; i++; }
        else dst[j++] = src[i];
    }
    dst[j] = 0;
    return j;
}

static int find_parent(const char *path)
{
    /* Parent directory = the path up to and including the last '/'.
       A node with no '/' is a direct child of the synthetic root. */
    size_t pl = strlen(path);
    size_t cut = 0;
    for (size_t i = 0; i < pl; i++)
        if (path[i] == '/') cut = i + 1;
    if (cut == 0) return 0;                 /* directly under root */
    size_t n = cut - 1;                     /* strip trailing '/' */
    char pdir[256];
    if (n >= sizeof(pdir)) n = sizeof(pdir) - 1;
    memcpy(pdir, path, n);
    pdir[n] = 0;
    for (int i = 0; i < g_nnodes; i++)
        if (strcmp(g_nodes[i].path, pdir) == 0)
            return i;
    return -1;
}

static int build_tree(const char *manifest_path)
{
    FILE *f = fopen(manifest_path, "r");
    if (!f) { fprintf(stderr, "cannot open manifest %s\n", manifest_path); return -1; }

    /* synthetic root */
    g_nodes[0].path[0] = 0;
    g_nodes[0].name[0] = 0;
    g_nodes[0].is_dir = 1;
    g_nodes[0].uid = 0; g_nodes[0].gid = 0; g_nodes[0].mode = 0755;
    g_nodes[0].has_content = 0; g_nodes[0].content = 0; g_nodes[0].content_len = 0;
    g_nodes[0].parent = -1;
    g_nnodes = 1;

    char line[1024];
    int dbg_lines = 0;
    while (fgets(line, sizeof(line), f))
    {
        dbg_lines++;
        /* strip trailing newline / CR */
        size_t l = strlen(line);
        while (l > 0 && (line[l - 1] == '\n' || line[l - 1] == '\r')) line[--l] = 0;
        /* skip comments / blank */
        if (line[0] == '#' || line[0] == 0) continue;

        char *tok[8];
        int nt = 0;
        char *p = line;
        while (*p && nt < 8)
        {
            while (*p == ' ' || *p == '\t') p++;
            if (!*p) break;
            tok[nt++] = p;
            while (*p && *p != ' ' && *p != '\t') p++;
            if (*p) { *p = 0; p++; }
        }
        if (nt < 5) continue;   /* need path mode uid gid type */

        if (g_nnodes >= MAX_NODES) { fprintf(stderr, "too many nodes\n"); fclose(f); return -1; }
        struct node *n = &g_nodes[g_nnodes];
        memset(n, 0, sizeof(*n));

        /* canonical path (strip trailing slash) */
        size_t pl = strlen(tok[0]);
        while (pl > 0 && tok[0][pl - 1] == '/') tok[0][--pl] = 0;
        strncpy(n->path, tok[0], sizeof(n->path) - 1);

        /* base name */
        const char *slash = strrchr(n->path, '/');
        const char *base = slash ? slash + 1 : n->path;
        strncpy(n->name, base, sizeof(n->name) - 1);

        n->mode = (uint32_t)parse_octal(tok[1]);
        n->uid  = (uint32_t)atoi(tok[2]);
        n->gid  = (uint32_t)atoi(tok[3]);
        n->is_dir = (tok[4][0] == 'd');

        if (nt >= 6 && strncmp(tok[5], "content:", 8) == 0)
        {
            n->has_content = 1;
            n->content = malloc(4096);
            n->content_len = unescape(tok[5] + 8, n->content, 4096);
        }
        else if (nt >= 6 && strncmp(tok[5], "bin:", 4) == 0)
        {
            /* Embed a host file's bytes verbatim as the node content. */
            const char *host = tok[5] + 4;
            FILE *bf = fopen(host, "rb");
            if (!bf)
            {
                fprintf(stderr, "mkfs: cannot open bin '%s'\n", host);
                fclose(f);
                return -1;
            }
            fseek(bf, 0, SEEK_END);
            long fs = ftell(bf);
            fseek(bf, 0, SEEK_SET);
            if (fs <= 0) { fclose(bf); fprintf(stderr, "mkfs: empty bin '%s'\n", host); fclose(f); return -1; }
            n->content = malloc((size_t)fs);
            size_t rd = fread(n->content, 1, (size_t)fs, bf);
            fclose(bf);
            n->has_content = 1;
            n->content_len = rd;
        }
        g_nnodes++;
    }
    fclose(f);

    /* resolve parents and assign inode blocks (data blocks are assigned in
       write_fs once we know each node's extent count). */
    uint64_t next = 5;
    for (int i = 0; i < g_nnodes; i++)
    {
        if (i > 0)
            g_nodes[i].parent = find_parent(g_nodes[i].path);
        g_nodes[i].inode_block = next++;
    }
    return 0;
}

/* ---- emit the filesystem into `img` (already sized to part_blocks blocks) ---- */
static void write_fs(uint8_t *img, uint64_t total_blocks)
{
    uint8_t *bitmap = img + 2 * AXFS_BLOCK_SIZE;

    /* Assign data blocks per node (files span multiple blocks; directories
       with >15 children span multiple 15-entry blocks). */
    uint64_t dblk = 5 + (uint64_t)g_nnodes;   /* first data block, after inodes */
    for (int i = 0; i < g_nnodes; i++)
    {
        struct node *n = &g_nodes[i];
        int nd;
        if (n->is_dir)
        {
            int nchild = 0;
            for (int c = 0; c < g_nnodes; c++)
                if (g_nodes[c].parent == i) nchild++;
            nd = (2 + nchild + 14) / 15;
            if (nd < 1) nd = 1;
        }
        else
        {
            size_t cl = n->content_len;
            nd = (int)((cl + AXFS_BLOCK_SIZE - 1) / AXFS_BLOCK_SIZE);
            if (nd < 1) nd = 1;
        }
        if (nd > MAX_DATA_EXTENTS) nd = MAX_DATA_EXTENTS;
        n->ndata = nd;
        for (int k = 0; k < nd; k++)
            n->data_blocks[k] = dblk++;
    }
    uint64_t top = dblk;

    for (uint64_t b = 0; b < top; b++)
        bitmap[b / 8] |= (1u << (b % 8));

    /* superblock */
    {
        uint8_t *blk = img + 0 * AXFS_BLOCK_SIZE;
        struct axfs_super *s = (struct axfs_super *)blk;
        memset(s, 0, sizeof(*s));
        memcpy(s->magic, AXFS_MAGIC, 8);
        s->block_size = AXFS_BLOCK_SIZE;
        s->total_blocks = total_blocks;
        s->root_inode = g_nodes[0].inode_block;
        s->free_bitmap_block = 2;
        s->transaction_id = 1;
        s->hdr.type = AXFS_OBJ_SUPER;
        s->hdr.object_id = 0;
        s->hdr.transaction_id = 1;
        axfs_set_checksum(blk);
        memcpy(img + 1 * AXFS_BLOCK_SIZE, blk, AXFS_BLOCK_SIZE);
    }

    /* per-node inodes */
    for (int i = 0; i < g_nnodes; i++)
    {
        struct node *n = &g_nodes[i];
        uint8_t *ib = img + n->inode_block * AXFS_BLOCK_SIZE;
        struct axfs_inode *in = (struct axfs_inode *)ib;
        memset(in, 0, sizeof(*in));
        in->hdr.type = AXFS_OBJ_INODE;
        in->hdr.object_id = n->inode_block;
        in->hdr.transaction_id = 1;
        in->inode_number = n->inode_block;
        in->in_type = n->is_dir ? AXFS_INODE_DIR : AXFS_INODE_FILE;
        in->size = n->is_dir ? AXFS_BLOCK_SIZE : (uint64_t)n->content_len;
        in->uid = n->uid;
        in->gid = n->gid;
        in->permissions = n->mode;
        in->extent_count = n->ndata;
        for (int k = 0; k < n->ndata; k++)
        {
            in->extents[k].physical_block = n->data_blocks[k];
            in->extents[k].block_count = 1;
            in->extents[k].reference_count = 1;
        }
        axfs_set_checksum(ib);
    }

    /* directory data blocks ('.', '..', children) and file data blocks */
    for (int i = 0; i < g_nnodes; i++)
    {
        struct node *n = &g_nodes[i];
        if (n->is_dir)
        {
            uint64_t self = n->inode_block;
            uint64_t parent = (n->parent >= 0) ? (uint64_t)g_nodes[n->parent].inode_block
                                              : self;  /* root: .. -> root */
            int child = 0;
            for (int b = 0; b < n->ndata; b++)
            {
                uint8_t *db = img + n->data_blocks[b] * AXFS_BLOCK_SIZE;
                memset(db, 0, AXFS_BLOCK_SIZE);
                int local = 0;
                if (b == 0)
                {
                    struct axfs_dent *d;
                    d = (struct axfs_dent *)(db + 0);
                    d->inode_id = (uint32_t)self; d->type = AXFS_INODE_DIR;
                    d->namelen = 1; d->name[0] = '.';
                    d = (struct axfs_dent *)(db + sizeof(struct axfs_dent));
                    d->inode_id = (uint32_t)parent; d->type = AXFS_INODE_DIR;
                    d->namelen = 2; d->name[0] = '.'; d->name[1] = '.';
                    local = 2;
                }
                while (local < 15 && child < g_nnodes)
                {
                    if (g_nodes[child].parent == i)
                    {
                        struct axfs_dent *cd =
                            (struct axfs_dent *)(db + local * sizeof(struct axfs_dent));
                        cd->inode_id = (uint32_t)g_nodes[child].inode_block;
                        cd->type = g_nodes[child].is_dir ? AXFS_INODE_DIR : AXFS_INODE_FILE;
                        cd->namelen = (uint8_t)strlen(g_nodes[child].name);
                        memcpy(cd->name, g_nodes[child].name, cd->namelen);
                        local++;
                    }
                    child++;
                }
            }
        }
        else if (n->has_content && n->content)
        {
            for (int b = 0; b < n->ndata; b++)
            {
                uint8_t *db = img + n->data_blocks[b] * AXFS_BLOCK_SIZE;
                memset(db, 0, AXFS_BLOCK_SIZE);
                size_t off = (size_t)b * AXFS_BLOCK_SIZE;
                size_t rem = n->content_len - off;
                size_t c = rem > AXFS_BLOCK_SIZE ? AXFS_BLOCK_SIZE : rem;
                if (c > 0) memcpy(db, n->content + off, c);
            }
        }
    }
}

int main(int argc, char **argv)
{
    if (argc < 4)
    {
        fprintf(stderr, "usage: %s <disk.img> <part_offset_lba> <manifest>\n", argv[0]);
        return 1;
    }
    const char *img_path = argv[1];
    uint64_t part_offset = (uint64_t)strtoull(argv[2], 0, 10);
    const char *manifest = argv[3];

    if (build_tree(manifest) != 0)
        return 1;

    /* open the disk image and determine the partition size in blocks */
    FILE *f = fopen(img_path, "rb+");
    if (!f) { fprintf(stderr, "cannot open %s\n", img_path); return 1; }
    fseek(f, 0, SEEK_END);
    long fsize = ftell(f);
    if (fsize < 0) { fprintf(stderr, "cannot stat %s\n", img_path); fclose(f); return 1; }

    long total_sectors = fsize / 512;
    long part_sectors = total_sectors - (long)part_offset;
    if (part_sectors <= 0) { fprintf(stderr, "partition offset beyond image\n"); fclose(f); return 1; }
    uint64_t part_blocks = (uint64_t)part_sectors / SECTORS_PER_BLOCK;
    if (part_blocks < 6) { fprintf(stderr, "partition too small\n"); fclose(f); return 1; }

    uint8_t *img = calloc((size_t)part_blocks, AXFS_BLOCK_SIZE);
    if (!img) { fprintf(stderr, "out of memory\n"); fclose(f); return 1; }

    write_fs(img, part_blocks);

    long byte_off = (long)(part_offset * 512);
    fseek(f, byte_off, SEEK_SET);
    size_t wrote = fwrite(img, 1, (size_t)part_blocks * AXFS_BLOCK_SIZE, f);
    fclose(f);
    free(img);
    if (wrote != (size_t)part_blocks * AXFS_BLOCK_SIZE) { fprintf(stderr, "short write\n"); return 1; }

    printf("wrote axiomefs (%llu blocks) into %s at LBA %llu\n",
           (unsigned long long)part_blocks, img_path, (unsigned long long)part_offset);
    return 0;
}
