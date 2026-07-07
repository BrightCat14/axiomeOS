# axiomeOS — Testing Approach

> Primary verification: QEMU serial output + visual inspection
> Secondary: libc unit tests on host (future)

---

## Test Infrastructure

### Serial Output (Primary)

The kernel writes debug output to COM1 (`0x3F8`). QEMU captures this with `-serial stdio` or `-serial file:serial.log`.

```c
// kernel/arch/x86_64/serial.c
void serial_init(void);      // Init COM1 at 115200 baud
void serial_putchar(char c);  // Write single char
void serial_puts(const char *s); // Write string
```

The kernel's `printf()` sends output to both serial and framebuffer.

### Framebuffer Output (Visual)

Initial framebuffer text mode (VGA text, or GOP-based bitmap font) provides immediate visual feedback in the QEMU window.

### Test Macros

```c
// kernel/lib/test.h
#define TEST_ASSERT(cond, msg) \
    do { \
        if (!(cond)) { \
            printf("FAIL: %s (%s:%d)\n", msg, __FILE__, __LINE__); \
            for(;;); /* halt */ \
        } else { \
            printf("PASS: %s\n", msg); \
        } \
    } while(0)
```

---

## Phase-by-Phase Testing

### Phase 0 — Toolchain & Build

| Test | Method | Expected |
|------|--------|----------|
| Cross-compiler works | `x86_64-elf-gcc --version` | Prints version |
| Compile test | Compile `int main() { return 0; }` with `-ffreestanding` | No errors |
| QEMU works | `qemu-system-x86_64 --version` | Prints version |
| GRUB mkrescue | `grub-mkrescue --version` | Prints version |

### Phase 1 — GRUB Boot + Entry Point

| Test | Method | Expected |
|------|--------|----------|
| Kernel boots | `make run` | QEMU shows GRUB menu → boots kernel |
| Entry reached | Serial output from `_start` | "axiomeOS: entering kmain" |
| Framebuffer lit | Visual check | Colored pattern or text on screen |

### Phase 2 — Early Kernel Bootstrap

| Test | Method | Expected |
|------|--------|----------|
| Serial output | `serial_puts("hello")` in kmain | "hello" on QEMU stdout |
| Framebuffer renderer | `fb_puts("axiomeOS")` | Text visible in QEMU window |
| Memory map parsed | `pmm_init()` | "Memory: 512 MB available" |
| GDT loaded | Check via QEMU monitor | No triple fault |
| IDT installed | Trigger `int 0x03` via inline asm | "Caught exception 3" |

### Phase 3 — Interrupts & Drivers

| Test | Method | Expected |
|------|--------|----------|
| Timer tick | Count seconds from PIT/HPET | "Tick 100" printed per second |
| Keyboard input | Press keys | Each key printed to serial/screen |
| Mouse motion | Move mouse (QEMU USB tablet) | Coordinates printed |
| Spinlocks | Two threads increment shared counter | Correct final count (no data race) |

### Phase 4 — Memory Manager

| Test | Method | Expected |
|------|--------|----------|
| Page alloc | `pmm_alloc_frame()` 1000x | No overlap, addresses are page-aligned |
| Page free | Allocate + free + reallocate | Same address returned |
| VM mapping | Map page at `0xFFFF900000000000`, write to it | No page fault, data verifiable |
| Kernel heap | `kmalloc(64)` → write pattern → `kfree` | No corruption |
| Demand paging | Access unmapped page with valid handler | Page mapped on demand |

Stress test:
```c
for (int i = 0; i < 10000; i++) {
    void *p = kmalloc(rand() % 4096);
    memset(p, 0xAA, 64);
    kfree(p);
}
printf("PASS: kmalloc stress test\n");
```

### Phase 5 — Scheduler

| Test | Method | Expected |
|------|--------|----------|
| Thread create | `thread_create(worker_fn, NULL)` | Thread runs |
| Thread yield | Thread calls `sched_yield()` | Scheduler switches to next thread |
| Multi-thread | 5 threads increment counters | All counters increment |
| Idle thread | CPU usage near 0% when idle | QEMU host CPU usage drops |
| SMP boot | Detect APs via MADT | "CPU 1 online" per core |

### Phase 6 — Syscalls

| Test | Method | Expected |
|------|--------|----------|
| Syscall entry | Call `syscall` instruction with test number | Handler invoked, return value correct |
| `debug_putchar` | Syscall with char | Char appears on serial |

### Phase 7 — ELF Loader & Userspace

| Test | Method | Expected |
|------|--------|----------|
| ELF parse | Parse known ELF binary | Segments correct, entry point matches |
| `execve` | Load `/sbin/init.elf` | Process runs |
| Ring 3 execution | User process runs in ring 3 | `CPL` check via QEMU monitor shows ring 3 |
| Init process | `init.elf` starts after kernel boots | "init: starting..." on serial |

### Phase 8 — Libc

| Test | Method | Expected |
|------|--------|----------|
| `printf` | `printf("Hello %s %d", "world", 42)` | "Hello world 42" |
| `malloc`/`free` | Allocate, write, free | No corruption (run under QEMU + valgrind-style heap guard) |
| String functions | `strlen("test") → 4` | Correct |

### Phase 9 — Coreutils

| Test | Method | Expected |
|------|--------|----------|
| Shell boots | GRUB boots → kernel → init → shell | Shell prompt visible |
| `echo hello` | Type at shell | "hello" printed |
| `ls` | Run `ls` on ramdisk | File listing |

### Phase 10+ — VFS, IPC, Drivers, GUI

These are tested with a combination of:
- Automated: serial output from test programs
- Interactive: shell commands, GUI manipulation
- Stress: concurrent file I/O, networking loopback

---

## Automated Test Procedure

For regression testing, a Python script (`tests/run_tests.py`) can:

1. Build the kernel with test harness enabled
2. Launch QEMU with serial output to a pipe
3. Kernel runs test suite and prints `PASS: ...` / `FAIL: ...` for each test
4. Script parses serial output and reports results
5. Exit code 0 if all pass, non-zero otherwise

```python
# tests/run_tests.py (future)
import subprocess
import sys

subprocess.run(["make", "run", "SERIAL=-serial pipe:testpipe"], check=True)
# ... read from testpipe, parse results
```

---

## Debugging Techniques

| Technique | How |
|-----------|-----|
| Print debug | `printf()` / `serial_puts()` everywhere during development |
| QEMU monitor | `Ctrl+Alt+2` in QEMU window: `info registers`, `info mem`, `info cpus` |
| GDB | `make debug` → `x86_64-elf-gdb build/kernel.elf` → `target remote :1234` |
| QEMU log | `-d cpu_reset,int` shows CPU state on triple fault (invaluable) |
| Bochs | Alternative emulator with better debug output (`bochs -f bochsrc`) |
