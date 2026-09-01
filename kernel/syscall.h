#ifndef AXIOME_SYSCALL_H
#define AXIOME_SYSCALL_H

#include <stdint.h>

/*
 * Syscall numbers are defined in userspace/libc/syscall_numbers.h so the
 * kernel and the userspace libc share a single source of truth (issue #33).
 * Application space must never renumber these.
 */
#include "userspace/libc/syscall_numbers.h"

/* File-type bits for st_mode (subset of POSIX). */
#define S_IFREG 0x8000
#define S_IFDIR 0x4000

/* Stat structure returned by fstat (layout must match userspace libc). */
struct stat {
    uint64_t st_size;
    uint32_t st_mode;
    uint32_t st_nlink;
    uint32_t st_ino;
};

/* System identity structure returned by uname (POSIX utsname). */
#define UTSNAME_LEN 65
struct utsname {
    char sysname[UTSNAME_LEN];
    char nodename[UTSNAME_LEN];
    char release[UTSNAME_LEN];
    char version[UTSNAME_LEN];
    char machine[UTSNAME_LEN];
    char domainname[UTSNAME_LEN];
};

void syscall_init(void);
uint64_t syscall_dispatch(uint64_t n, uint64_t a1, uint64_t a2, uint64_t a3,
                          uint64_t a4, uint64_t a5);

extern uint64_t current_kstack_top;

#endif
