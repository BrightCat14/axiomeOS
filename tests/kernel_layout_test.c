#include "test_util.h"

#include <stddef.h>
#include <stdint.h>

#include "axiomefs.h"
#include "elf.h"

static int test_kernel_layout(void)
{
    /* ELF64 header is a fixed 64-byte layout. */
    CHECK_EQ(sizeof(struct elf64_ehdr), 64);
    CHECK_EQ(offsetof(struct elf64_ehdr, ei_magic), 0);
    CHECK_EQ(offsetof(struct elf64_ehdr, e_type), 16);
    CHECK_EQ(offsetof(struct elf64_ehdr, e_machine), 18);
    CHECK_EQ(offsetof(struct elf64_ehdr, e_entry), 24);
    CHECK_EQ(offsetof(struct elf64_ehdr, e_phoff), 32);
    CHECK_EQ(offsetof(struct elf64_ehdr, e_phentsize), 54);
    CHECK_EQ(offsetof(struct elf64_ehdr, e_phnum), 56);

    /* ELF64 program header is a fixed 56-byte layout. */
    CHECK_EQ(sizeof(struct elf64_phdr), 56);
    CHECK_EQ(offsetof(struct elf64_phdr, p_type), 0);
    CHECK_EQ(offsetof(struct elf64_phdr, p_flags), 4);
    CHECK_EQ(offsetof(struct elf64_phdr, p_offset), 8);
    CHECK_EQ(offsetof(struct elf64_phdr, p_vaddr), 16);
    CHECK_EQ(offsetof(struct elf64_phdr, p_filesz), 32);
    CHECK_EQ(offsetof(struct elf64_phdr, p_memsz), 40);
    CHECK_EQ(offsetof(struct elf64_phdr, p_align), 48);

    /* ELF constants. */
    CHECK_EQ(ELF_MAGIC, 0x464C457FUL);
    CHECK_EQ(ELFCLASS64, 2);
    CHECK_EQ(ELFDATA2LSB, 1);
    CHECK_EQ(EM_X86_64, 62);
    CHECK_EQ(PT_LOAD, 1);

    /* axiomefs: on-disk object header must be 32 bytes, checksum at [24,32)
       so the superblock checksumning in axiomefs.c stays consistent. */
    CHECK_EQ(sizeof(struct axfs_obj_hdr), 32);
    CHECK_EQ(offsetof(struct axfs_obj_hdr, object_id), 0);
    CHECK_EQ(offsetof(struct axfs_obj_hdr, transaction_id), 8);
    CHECK_EQ(offsetof(struct axfs_obj_hdr, type), 16);
    CHECK_EQ(offsetof(struct axfs_obj_hdr, checksum), 24);

    /* Superblock and inode are one 4096-byte block each. */
    CHECK_EQ(sizeof(struct axfs_super), AXFS_BLOCK_SIZE);
    CHECK_EQ(sizeof(struct axfs_inode), AXFS_BLOCK_SIZE);
    CHECK_EQ(offsetof(struct axfs_super, magic), 32);
    CHECK_EQ(offsetof(struct axfs_super, block_size), 40);
    CHECK_EQ(offsetof(struct axfs_super, total_blocks), 48);
    CHECK_EQ(offsetof(struct axfs_super, free_bitmap_block), 88);

    /* Extent is 24 bytes; 160 of them fit the inode. */
    CHECK_EQ(sizeof(struct axfs_extent), 24);
    CHECK_EQ(AXFS_MAX_EXTENTS, 160);
    CHECK_EQ(offsetof(struct axfs_extent, physical_block), 0);
    CHECK_EQ(offsetof(struct axfs_extent, block_count), 8);
    CHECK_EQ(offsetof(struct axfs_extent, reference_count), 16);

    /* Directory entry layout. */
    CHECK_EQ(sizeof(struct axfs_dent), 264);
    CHECK_EQ(offsetof(struct axfs_dent, inode_id), 0);
    CHECK_EQ(offsetof(struct axfs_dent, type), 4);
    CHECK_EQ(offsetof(struct axfs_dent, namelen), 5);
    CHECK_EQ(offsetof(struct axfs_dent, name), 8);

    /* Block/object type constants. */
    CHECK_EQ(AXFS_BLOCK_SIZE, 4096);
    CHECK_EQ(AXFS_OBJ_SUPER, 1);
    CHECK_EQ(AXFS_OBJ_INODE, 2);
    CHECK_EQ(AXFS_INODE_FILE, 1);
    CHECK_EQ(AXFS_INODE_DIR, 2);

    TEST_REPORT("kernel/layout");
}

int main(void)
{
    return test_kernel_layout();
}
