#ifndef AXIOME_ELF_H
#define AXIOME_ELF_H

#include <stdint.h>
#include <stddef.h>

#define ELF_MAGIC   0x464C457FUL
#define ELFCLASS64  2
#define ELFDATA2LSB 1
#define EM_X86_64   62

#define PT_LOAD 1

#define PF_X 1
#define PF_W 2
#define PF_R 4

struct elf64_ehdr {
    uint32_t ei_magic;
    uint8_t  ei_class;
    uint8_t  ei_data;
    uint8_t  ei_version;
    uint8_t  ei_osabi;
    uint8_t  ei_abiversion;
    uint8_t  ei_pad[7];
    uint16_t e_type;
    uint16_t e_machine;
    uint32_t e_version;
    uint64_t e_entry;
    uint64_t e_phoff;
    uint64_t e_shoff;
    uint32_t e_flags;
    uint16_t e_ehsize;
    uint16_t e_phentsize;
    uint16_t e_phnum;
    uint16_t e_shentsize;
    uint16_t e_shnum;
    uint16_t e_shstrndx;
};

struct elf64_phdr {
    uint32_t p_type;
    uint32_t p_flags;
    uint64_t p_offset;
    uint64_t p_vaddr;
    uint64_t p_paddr;
    uint64_t p_filesz;
    uint64_t p_memsz;
    uint64_t p_align;
};

int elf_load(uint64_t *pml4, const uint8_t *data, size_t size,
              uint64_t *entry, uint64_t *stack_top, int argc, char **argv);

int exec_user_program(const uint8_t *elf, size_t size, const char *name);
int spawn_process(const uint8_t *elf, size_t size, const char *name);
int spawn_process_with_args(const uint8_t *elf, size_t size, const char *name,
                            int argc, char **argv);
int fork_process(uint64_t rip, uint64_t rsp, uint64_t rflags,
                  uint64_t rbx, uint64_t rbp, uint64_t r12,
                  uint64_t r13, uint64_t r14, uint64_t r15);

#endif
