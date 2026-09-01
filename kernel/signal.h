#ifndef AXIOME_SIGNAL_H
#define AXIOME_SIGNAL_H

#include <stdint.h>
#include <stddef.h>

/* Number of signals (must stay small; used as a bitmask in uint64_t). */
#define NSIG 32

/* Signal numbers (POSIX-ish subset). */
#define SIGHUP   1
#define SIGINT   2
#define SIGQUIT  3
#define SIGILL   4
#define SIGTRAP  5
#define SIGABRT  6
#define SIGBUS   7
#define SIGFPE   8
#define SIGKILL  9
#define SIGUSR1  10
#define SIGSEGV  11
#define SIGUSR2  12
#define SIGPIPE  13
#define SIGALRM  14
#define SIGTERM  15
#define SIGCHLD  17
#define SIGCONT  18
#define SIGSTOP  19

/* Special handler dispositions. */
#define SIG_DFL ((void (*)(int))0)
#define SIG_IGN ((void (*)(int))1)

/* sigaction flags. */
#define SA_RESTART   0x01
#define SA_NOCLDSTOP 0x02
#define SA_SIGINFO   0x04
#define SA_RESTORER  0x08

struct sigaction {
    void (*sa_handler)(int);
    uint64_t sa_flags;
    void (*sa_restorer)(void);
    uint64_t sa_mask;
};

/* Full user register context saved when a signal is delivered. The kernel
   writes this to the user stack; sigreturn reads it back to resume. The GP
   register order matches the kernel stack (see syscall.S): rax,rdi,rsi,rdx,
   r10,r8,r9,rbx,rbp,r12,r13,r14,r15, then the iret frame, then oldmask. */
struct sigframe {
    uint64_t rax, rdi, rsi, rdx, r10, r8, r9, rbx, rbp, r12, r13, r14, r15;
    uint64_t rip, cs, rflags, rsp, ss;
    uint64_t oldmask;
};

/* Called from syscall.S right before iretq to deliver any pending,
   unblocked signal by rewriting the iret frame on the kernel stack. */
void syscall_deliver_signals(void);

/* Deliver pending signals to the current thread when a device IRQ (timer)
   interrupted it in user mode, so SIGINT/SIGKILL break CPU-bound loops that
   make no syscalls (issue #30). `frame` is the raw isr_frame. */
void kernel_deliver_signals_user(void *frame);

#endif
