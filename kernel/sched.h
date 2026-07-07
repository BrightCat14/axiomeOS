#ifndef AXIOME_SCHED_H
#define AXIOME_SCHED_H

#include <stdint.h>
#include "vfs.h"
#include "signal.h"

#define THREAD_QUANTUM 10
#define THREAD_STACK_SIZE 32768
#define IPC_MAX 16

enum { THREAD_READY, THREAD_RUNNING, THREAD_BLOCKED, THREAD_ZOMBIE };

struct thread {
    uint64_t rsp;
    uint64_t kstack_top;
    uint64_t *pml4;
    uint64_t user_rsp;
    int pid;
    int parent_pid;
    int exit_status;
    int state;
    int priority;
    int quantum;
    struct thread *next;
    struct thread *prev;
    uint8_t *stack_base;
    void (*func)(void*);
    void *arg;
    char name[16];

    /* Per-process VFS state (Phase 10). */
    struct vfs_file fds[MAX_FD];
    char cwd[256];

    /* Signals (Phase 11). */
    uint64_t sig_pending;
    uint64_t sig_mask;
    struct sigaction sig_actions[NSIG];

    /* IPC channels (Phase 11). Each entry references a kernel ipc_chan. */
    int ipc[IPC_MAX];

    /* Signal delivery: iret frame of the in-progress syscall (per-thread). */
    uint64_t *syscall_iret;
};

void sched_init(void);
struct thread *sched_spawn(void (*func)(void*), void *arg, const char *name);
struct thread *sched_spawn_user_in(uint64_t *pml4, void *rip, void *user_rsp,
                         uint64_t rflags, const char *name,
                         uint64_t rbx, uint64_t rbp, uint64_t r12,
                         uint64_t r13, uint64_t r14, uint64_t r15);
void sched_yield(void);
void sched_exit(int status);
void sched_suspend(void);
void sched_wake(struct thread *t);
void sched_tick(void);
struct thread *sched_current(void);
int sched_new_pid(void);

struct thread *sched_child_zombie(int parent_pid, int pid_filter);
int sched_has_child(int parent_pid);
void sched_reap_zombie(struct thread *z);
struct thread *sched_find_by_pid(int pid);
void sched_foreach(void (*fn)(struct thread *, void *), void *arg);

struct proc_info {
    int pid;
    int parent_pid;
    int state;
    char name[16];
};
int sched_enum_procs(struct proc_info *buf, int max);

#endif
