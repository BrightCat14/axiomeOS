# axiomeOS — Coding Conventions

> Consistency is more important than personal preference.

---

## 1. Language Standards

| Domain | Language | Standard |
|--------|----------|----------|
| Kernel | C + assembly | C11 (gnu11), Intel assembly syntax |
| Libc | C | C11 (freestanding) |
| Userland | C | C11 (hosted, using libc headers) |
| Build | Make / bash | GNU Make 4.x, bash 5.x |

No C++ allowed in the kernel. C only.

---

## 2. Naming Conventions

### 2.1 Identifiers

| Kind | Convention | Example |
|------|-----------|---------|
| Functions | `snake_case` | `pmm_alloc_page`, `vmm_map_page` |
| Variables | `snake_case` | `page_count`, `base_addr` |
| Macros | `SCREAMING_SNAKE` | `PAGE_SIZE`, `KERNEL_VIRT_BASE` |
| Enums | lowercase | `enum resource_type { rt_memory, rt_io, ... }` |
| Enum values | `SCREAMING_SNAKE` | `RT_MEMORY`, `RT_IO` |
| Typedefs | `_t` suffix | `paddr_t`, `vaddr_t`, `size_t` |
| Structs | `snake_case` tag, `_t` typedef | `typedef struct page_frame { ... } page_frame_t;` |
| Global vars | `g_` prefix | `g_kernel_page_tables` |
| Static vars | `s_` prefix | `s_alloc_lock` |

### 2.2 File Naming

- `.c` for C source, `.h` for C headers
- `.S` for assembly files (preprocessed by GCC)
- Filenames in `snake_case`
- Header guards: `#ifndef KERNEL_SUBSYSTEM_FILENAME_H` / `#define` / `#endif`

Example: `kernel/mm/pmm.h` → guard `KERNEL_MM_PMM_H`

### 2.3 Prefix Conventions

| Prefix | Subsystem | Example |
|--------|-----------|---------|
| `pmm_` | Physical memory manager | `pmm_alloc_frame()` |
| `vmm_` | Virtual memory manager | `vmm_map_page()` |
| `sched_` | Scheduler | `sched_yield()` |
| `thread_` | Thread operations | `thread_create()` |
| `vfs_` | Virtual filesystem | `vfs_open()` |
| `pci_` | PCI enumeration | `pci_scan_bus()` |

---

## 3. File Structure

### 3.1 C Source Files

```c
// 1. Header comment (if non-obvious hardware interaction)
// 2. Corresponding header include
#include <kernel/mm/pmm.h>

// 3. Other kernel headers
#include <kernel/lib/string.h>
#include <kernel/arch/x86_64/paging.h>

// 4. Standard includes (only in userland)
// #include <stdio.h>

// 5. Forward declarations
static void merge_free_regions(page_frame_t *a, page_frame_t *b);

// 6. Static globals
static spinlock_t s_alloc_lock = SPINLOCK_INIT;
static bitmap_t s_phys_bitmap;

// 7. Public functions (with doc comments)
int pmm_init(multiboot_memory_map_t *mmap)
{
    // ...
}

// 8. Static functions
static void merge_free_regions(page_frame_t *a, page_frame_t *b)
{
    // ...
}
```

### 3.2 Header Files

```c
#ifndef KERNEL_MM_PMM_H
#define KERNEL_MM_PMM_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#include <kernel/arch/x86_64/paging.h>

// Macros
#define PMM_BLOCK_SIZE PAGE_SIZE

// Types
typedef uint64_t paddr_t;
typedef struct page_frame {
    paddr_t address;
    struct page_frame *next;
} page_frame_t;

// Public API
int  pmm_init(multiboot_memory_map_t *mmap);
void *pmm_alloc_frame(void);
void pmm_free_frame(void *addr);

#endif
```

---

## 4. Error Handling

- Functions return `int` (`0` = success, negative = errno-style error)
- Use `bool` for predicates (e.g., `pmm_is_free(paddr_t addr)`)
- Never silently ignore errors

```c
// Good
int result = pmm_alloc_page(&page);
if (result < 0) {
    // handle error
    return result;
}

// Bad — ignoring return
pmm_alloc_page(&page);
```

---

## 5. Memory Management Rules

- Kernel heap: use `kmalloc(size)` / `kfree(ptr)` (slab-backed)
- Physical pages: use `pmm_alloc_frame()` / `pmm_free_frame()`
- No `malloc` / `free` in kernel unless using kernel heap wrapper
- No dynamic allocation inside interrupt handlers
- All kernel memory is non-pageable (pinned)

---

## 6. Comments

- **Do** explain *why* something is done (especially hardware interaction)
- **Do** cite the Intel/AMD manual or OSDev wiki for non-obvious behavior
- **Do not** comment obvious code (`/* increment i */`)
- **Do not** write block comments for every function — doc comment only for public API

```c
// Good: explains undocumented hardware quirk
// The I/O APIC redirection entry's mask bit is inverted on some chipsets.
// We must clear bit 16 to actually mask the interrupt.
apic_write(IOAPIC_REDIR_TBL(n), entry | IOAPIC_MASK_BIT);

// Bad: obvious comment
i++; // increment counter
```

---

## 7. Assembly Conventions

- **Syntax**: Intel (not AT&T) — use `.intel_syntax noprefix` in `.S` files
- **Register usage** follows System V AMD64 ABI
- Assembly files get preprocessed by GCC (`.S` extension), so `#include` and macros work
- Save/restore callee-saved registers in assembly functions
- Use C wrappers for assembly routines where possible

```asm
// boot.S example
.intel_syntax noprefix

.section .multiboot
    // Multiboot2 header here

.section .text
.global _start
_start:
    // Set up stack
    mov rsp, offset stack_top
    // Clear BSS
    // Call kmain
    xor rbp, rbp
    call kmain
    // kmain shouldn't return, but if it does...
    cli
    hlt
```

---

## 8. Interrupt Safety

- Interrupt handlers must be short and deterministic
- Use `spinlock_irqsave()` / `spinlock_irqrestore()` to protect shared data
- Defer heavy work to bottom halves / work queues
- Every ISR saves/restores all registers it modifies

---

## 9. Portability Notes

- Use `uintXX_t` from `<stdint.h>` for fixed-width types
- Use `size_t` and `ssize_t` for sizes
- Use `uintptr_t` for integer ↔ pointer conversions
- Minimize inline assembly; isolate it in `arch/` files
- `static_assert` for compile-time invariants

---

## 10. Formatting

- Indent: 4 spaces (no tabs)
- Line width: 100 characters max
- Braces: K&R style (open brace on same line)

```c
void example(int arg)
{
    if (arg == 0) {
        do_something();
    } else {
        do_other();
    }
}
```

- `if`/`while`/`for` always use braces, even for single statements
- One declaration per line
- Pointer `*` goes with the variable name: `int *ptr` not `int* ptr`
