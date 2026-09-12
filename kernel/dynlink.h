#ifndef AXIOME_DYNLINK_H
#define AXIOME_DYNLINK_H

#include <stdint.h>
#include <stddef.h>

/* ------------------------------------------------------------------ *
 * In-kernel (and host-testable) dynamic linker core.
 *
 * AIXOME_USER binaries and libc.sl are built as ET_DYN images whose load
 * bias is always zero (every image is mapped at its link-time virtual
 * address, see userspace/link.ld and userspace/libc.ld).  So relocations
 * only need to (a) bind GOT/PLT entries to symbols found in the shared
 * images and (b) apply R_X86_64_RELATIVE with the fixed base.  This module
 * keeps every read inside the raw ELF buffer it is given, so it has no
 * dependencies on the kernel allocator / MMU and can be unit-tested on the
 * host against real PIE + shared objects.
 * ------------------------------------------------------------------ */

#define DYNLINK_MAX_DEPS 8

/* Parsed view of one ELF image. All section references are file offsets
   into the image buffer, never virtual addresses. */
struct elf_dyninfo {
    int      has_dynamic;
    uint64_t base;              /* load bias (0 on axiome) */
    uint64_t entry;             /* e_entry */
    uint64_t seg_lo;            /* lowest mapped PT_LOAD vaddr */
    uint64_t seg_hi;            /* one past the highest mapped vaddr */
    /* .dynamic */
    uint64_t dyn_off;
    size_t   dyn_count;
    /* .dynsym / .dynstr */
    uint64_t dynsym_off;
    size_t   dynsym_count;
    uint64_t dynstr_off;
    size_t   dynstr_size;
    /* .rela.dyn (from DT_RELA) and .rela.plt (from DT_JMPREL) */
    uint64_t rela_off;
    size_t   rela_count;
    uint64_t jmprel_off;
    size_t   jmprel_count;
    /* DT_NEEDED: offsets of dependency names into dynstr */
    uint32_t needed_off[DYNLINK_MAX_DEPS];
    int      nneeded;
};

struct elf_object {
    const uint8_t *buf;         /* ELF file bytes */
    size_t         size;
    struct elf_dyninfo info;
};

/* Parse an ELF image in memory, filling obj->info. Returns 0 on success,
   -1 if the buffer is not a usable ELF. */
int elf_parse_dynamic(struct elf_object *obj);

/* Resolve `name` against the scope of images (executable first, then its
   dependencies). Returns the runtime address of the symbol (bias + st_value)
   or 0 when not found. If `found` is non-NULL it is set to 1/0. */
uint64_t elf_resolve_symbol(struct elf_object *scope, size_t nscope,
                            const uint8_t *name, int *found);

typedef void (*dyn_place_fn)(uint64_t target_va, uint64_t value, void *ctx);

/* Apply every relocation of `obj` (from DT_RELA / DT_JMPREL, eager binding).
   Symbol references resolve against `scope`. For each relocation the result
   is handed to `place(target_va, value, ctx)`; the caller decides where it
   lands (kernel writes into the newly mapped user address space).  Returns 0
   on success, -1 if any relocation is malformed or unresolved.  `*applied`
   receives the number of relocations applied. */
int elf_apply_relocations(struct elf_object *obj, struct elf_object *scope,
                          size_t nscope, dyn_place_fn place, void *ctx,
                          int *applied);

#endif