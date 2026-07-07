/*
 * mkfs_axiomefs - host-side formatter for the minimal axiomefs v1 image.
 *
 * Layout (4096-byte little-endian blocks):
 *   block 0 : superblock (primary)
 *   block 1 : superblock (checkpoint backup, identical)
 *   block 2 : free-space bitmap (bit i == 1 => block i used)
 *   block 3 : root directory inode
 *   block 4 : root directory data (holds '.' / '..' + entries)
 *   block 5 : README.TXT inode (sample file)
 *   block 6 : README.TXT data
 *
 * The on-disk structs are shared verbatim with the kernel via kernel/axiomefs.h
 * so byte layouts can never drift.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "kernel/axiomefs.h"

#define IMG_BLOCKS 1024
#define ROOT_INODE_BLOCK 3
#define BITMAP_BLOCK 2
#define DIR_DATA_BLOCK 4
#define READ_INODE_BLOCK 5
#define READ_DATA_BLOCK 6

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

static void mark_used(uint8_t *bitmap, uint64_t block)
{
    bitmap[block / 8] |= (1u << (block % 8));
}

int main(int argc, char **argv)
{
    const char *out = (argc > 1) ? argv[1] : "axfs.img";
    const uint64_t total = IMG_BLOCKS;
    uint8_t *img = calloc(total, AXFS_BLOCK_SIZE);
    if (!img) { fprintf(stderr, "out of memory\n"); return 1; }

    uint8_t *bitmap = img + BITMAP_BLOCK * AXFS_BLOCK_SIZE;
    for (uint64_t i = 0; i <= DIR_DATA_BLOCK; i++) mark_used(bitmap, i);

    /* ---- superblock (primary + checkpoint backup) ---- */
    {
        uint8_t *blk = img + 0 * AXFS_BLOCK_SIZE;
        struct axfs_super *s = (struct axfs_super *)blk;
        memset(s, 0, sizeof(*s));
        memcpy(s->magic, AXFS_MAGIC, 8);
        s->block_size = AXFS_BLOCK_SIZE;
        s->total_blocks = total;
        s->root_inode = ROOT_INODE_BLOCK;
        s->free_bitmap_block = BITMAP_BLOCK;
        s->transaction_id = 1;
        s->hdr.type = AXFS_OBJ_SUPER;
        s->hdr.object_id = 0;
        s->hdr.transaction_id = 1;
        axfs_set_checksum(blk);
        memcpy(img + 1 * AXFS_BLOCK_SIZE, blk, AXFS_BLOCK_SIZE);
    }

    /* ---- root directory inode ---- */
    {
        uint8_t *blk = img + ROOT_INODE_BLOCK * AXFS_BLOCK_SIZE;
        struct axfs_inode *in = (struct axfs_inode *)blk;
        memset(in, 0, sizeof(*in));
        in->hdr.type = AXFS_OBJ_INODE;
        in->hdr.object_id = ROOT_INODE_BLOCK;
        in->hdr.transaction_id = 1;
        in->inode_number = ROOT_INODE_BLOCK;
        in->in_type = AXFS_INODE_DIR;
        in->size = AXFS_BLOCK_SIZE;
        in->extent_count = 1;
        in->extents[0].physical_block = DIR_DATA_BLOCK;
        in->extents[0].block_count = 1;
        in->extents[0].reference_count = 1;
        axfs_set_checksum(blk);
    }

    /* ---- root directory data: '.' and '..' ---- */
    {
        uint8_t *blk = img + DIR_DATA_BLOCK * AXFS_BLOCK_SIZE;
        memset(blk, 0, AXFS_BLOCK_SIZE);
        struct axfs_dent *d;
        d = (struct axfs_dent *)(blk + 0 * sizeof(struct axfs_dent));
        d->inode_id = ROOT_INODE_BLOCK; d->type = AXFS_INODE_DIR;
        d->namelen = 1; d->name[0] = '.';
        d = (struct axfs_dent *)(blk + 1 * sizeof(struct axfs_dent));
        d->inode_id = ROOT_INODE_BLOCK; d->type = AXFS_INODE_DIR;
        d->namelen = 2; d->name[0] = '.'; d->name[1] = '.';
    }

    /* ---- sample file README.TXT ---- */
    {
        const char *content = "axiomefs v1 ready\n";
        size_t clen = strlen(content);
        uint8_t *db = img + READ_DATA_BLOCK * AXFS_BLOCK_SIZE;
        memset(db, 0, AXFS_BLOCK_SIZE);
        memcpy(db, content, clen);

        uint8_t *ib = img + READ_INODE_BLOCK * AXFS_BLOCK_SIZE;
        struct axfs_inode *in = (struct axfs_inode *)ib;
        memset(in, 0, sizeof(*in));
        in->hdr.type = AXFS_OBJ_INODE;
        in->hdr.object_id = READ_INODE_BLOCK;
        in->hdr.transaction_id = 1;
        in->inode_number = READ_INODE_BLOCK;
        in->in_type = AXFS_INODE_FILE;
        in->size = clen;
        in->extent_count = 1;
        in->extents[0].physical_block = READ_DATA_BLOCK;
        in->extents[0].block_count = 1;
        in->extents[0].reference_count = 1;
        axfs_set_checksum(ib);

        uint8_t *rb = img + DIR_DATA_BLOCK * AXFS_BLOCK_SIZE;
        struct axfs_dent *d = (struct axfs_dent *)(rb + 2 * sizeof(struct axfs_dent));
        d->inode_id = READ_INODE_BLOCK; d->type = AXFS_INODE_FILE;
        d->namelen = (uint8_t)strlen("README.TXT");
        memcpy(d->name, "README.TXT", d->namelen);

        mark_used(bitmap, READ_INODE_BLOCK);
        mark_used(bitmap, READ_DATA_BLOCK);
    }

    FILE *f = fopen(out, "wb");
    if (!f) { fprintf(stderr, "cannot open %s\n", out); free(img); return 1; }
    size_t wrote = fwrite(img, 1, (size_t)total * AXFS_BLOCK_SIZE, f);
    fclose(f);
    free(img);
    if (wrote != (size_t)total * AXFS_BLOCK_SIZE) {
        fprintf(stderr, "short write\n"); return 1;
    }
    printf("wrote %s (%llu blocks, %llu bytes)\n", out,
           (unsigned long long)total, (unsigned long long)wrote);
    return 0;
}
