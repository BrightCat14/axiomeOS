#ifndef AXIOME_ELF_H
#define AXIOME_ELF_H

#include <stdint.h>
#include <stddef.h>

#define ELF_MAGIC   0x464C457FUL
#define ELFCLASS64  2
#define ELFDATA2LSB 1
#define EM_X86_64   62

#define PT_LOAD     1
#define PT_DYNAMIC  2
#define PT_INTERP   3

#define PF_X 1
#define PF_W 2
#define PF_R 4

/* Dynamic array tags (subset used by the in-kernel loader). */
#define DT_NULL     0
#define DT_NEEDED   1
#define DT_PLTRELSZ 2
#define DT_PLTGOT   3
#define DT_HASH     4
#define DT_STRTAB   5
#define DT_SYMTAB   6
#define DT_RELA     7
#define DT_RELASZ   8
#define DT_RELAENT  9
#define DT_STRSZ    10
#define DT_SYMENT   11
#define DT_PLTREL   20
#define DT_JMPREL   23

/* .dynsym symbol table entry. */
struct elf64_sym {
    uint32_t st_name;
    uint8_t  st_info;
    uint8_t  st_other;
    uint16_t st_shndx;
    uint64_t st_value;
    uint64_t st_size;
};

#define SHN_UNDEF 0

#define STT_FUNC  2
#define STT_OBJECT 1
#define STT_NOTYPE 0

#define ELF64_ST_TYPE(i)    ((i) & 0xf)
#define ELF64_ST_BIND(i)    ((i) >> 4)

#define STB_GLOBAL 1

/* RELA relocation entry. */
struct elf64_rela {
    uint64_t r_offset;
    uint64_t r_info;
    int64_t  r_addend;
};

#define ELF64_R_SYM(i)  ((i) >> 32)
#define ELF64_R_TYPE(i) ((i) & 0xffffffffUL)

/* x86-64 relocations understood by the loader. */
#define R_X86_64_64        1
#define R_X86_64_GLOB_DAT  6
#define R_X86_64_JUMP_SLOT 7
#define R_X86_64_RELATIVE  8

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

/* Dynamic array entry (.dynamic). */
struct elf64_dyn {
    int64_t d_tag;
    union {
        uint64_t d_val;
        uint64_t d_ptr;
    } d_un;
};

/* Section header (used to size .dynsym). */
struct elf64_shdr {
    uint32_t sh_name;
    uint32_t sh_type;
    uint64_t sh_flags;
    uint64_t sh_addr;
    uint64_t sh_offset;
    uint64_t sh_size;
    uint32_t sh_link;
    uint32_t sh_info;
    uint64_t sh_addralign;
    uint64_t sh_entsize;
};

#define SHT_RELA    4
#define SHT_DYNSYM  11

struct mmu_root;

int elf_load(struct mmu_root *mmu, const uint8_t *data, size_t size,
              uint64_t *entry, uint64_t *stack_top, int argc, char **argv,
              int envc, char **envp);

int exec_user_program(const uint8_t *elf, size_t size, const char *name);
int spawn_process_with_args(const uint8_t *elf, size_t size, const char *name,
                            int argc, char **argv);
int fork_process(uint64_t rip, uint64_t rsp, uint64_t rflags,
                  uint64_t rbx, uint64_t rbp, uint64_t r12,
                  uint64_t r13, uint64_t r14, uint64_t r15);

#endif
