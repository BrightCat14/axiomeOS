#include "sched.h"
#include "printk.h"
#include "slab.h"
#include "pmm.h"
#include "vmm.h"
#include "string.h"

void context_switch(uint64_t *old_rsp, uint64_t new_rsp, uint64_t new_kstack_top);
extern void user_iret_stub(void);
extern char bootstrap_stack_top;

static struct thread *current;
static struct thread main_thread;
static struct thread idle_thread;
static struct thread *ready_head;
static struct thread *zombie_head;
static int next_pid = 1;

/* The userspace init process. If it ever exits (killed or naturally), the
   scheduler respawns it on the next yield. */
static struct thread *g_init_thread;
static int g_init_dead;

static void thread_init_vfs(struct thread *t)
{
    for (int i = 0; i < MAX_FD; i++)
    {
        t->fds[i].used = 0;
        t->fds[i].kind = FD_FREE;
        t->fds[i].node = 0;
        t->fds[i].off = 0;
    }
    t->cwd[0] = 0;

    /* Signals: all blocked masks clear, default (NULL) dispositions. */
    t->sig_pending = 0;
    t->sig_mask = 0;
    for (int i = 0; i < NSIG; i++)
    {
        t->sig_actions[i].sa_handler = SIG_DFL;
        t->sig_actions[i].sa_flags = 0;
        t->sig_actions[i].sa_restorer = 0;
        t->sig_actions[i].sa_mask = 0;
    }

    for (int i = 0; i < IPC_MAX; i++)
        t->ipc[i] = -1;

    /* Default security context. Real user threads inherit their parent's
       context in sched_spawn_user_in(); this is only the fallback when no
       parent context exists yet. */
    t->uid = t->euid = t->suid = 0;
    t->gid = t->egid = t->sgid = 0;
    t->role = ROLE_USER;
    t->caps_eff = t->caps_prm = t->caps_inh = role_caps[ROLE_USER];
}

/* Initialize a thread to the privileged SYSTEM role (kernel daemons / init).
   Capabilities are unbounded and the VFS permission checks are bypassed. */
static void thread_set_system(struct thread *t)
{
    t->uid = t->euid = t->suid = 0;
    t->gid = t->egid = t->sgid = 0;
    t->role = ROLE_SYSTEM;
    t->caps_eff = t->caps_prm = t->caps_inh = 0xFFFFFFFFULL;
}

static struct thread *thread_find_by_pid(int pid);

static struct thread *idle_ptr;

static void sched_check_init(void);

int sched_new_pid(void)
{
    return next_pid++;
}

static void thread_trampoline(void)
{
    struct thread *t = current;
    t->func(t->arg);
    sched_exit(0);
}

static void idle_func(void *arg)
{
    (void)arg;
    while (1)
    {
        __asm__ volatile("hlt");
        sched_yield();
    }
}

void sched_init(void)
{
    current = &main_thread;
    current->state = THREAD_RUNNING;
    current->priority = 1;
    current->quantum = THREAD_QUANTUM;
    current->func = 0;
    current->arg = 0;
    current->next = current;
    current->prev = current;
    current->stack_base = 0;
    current->kstack_top = (uint64_t)&bootstrap_stack_top;
    current->pml4 = vmm_kernel_pml4();
    current->pid = sched_new_pid();
    current->parent_pid = 0;
    current->exit_status = 0;
    __builtin_strcpy(current->name, "main");
    thread_set_system(current);

    ready_head = current;

    idle_thread.state = THREAD_READY;
    idle_thread.priority = 0;
    idle_thread.quantum = THREAD_QUANTUM;
    idle_thread.func = idle_func;
    idle_thread.arg = 0;
    __builtin_strcpy(idle_thread.name, "idle");
    idle_thread.next = 0;
    idle_thread.prev = 0;
    idle_thread.stack_base = 0;
    idle_thread.pml4 = vmm_kernel_pml4();
    idle_thread.pid = sched_new_pid();
    idle_thread.parent_pid = 0;
    idle_thread.exit_status = 0;
    thread_set_system(&idle_thread);

    idle_ptr = &idle_thread;

    klog("Sched: initialized\n");
}

struct thread *sched_spawn(void (*func)(void*), void *arg, const char *name)
{
    struct thread *t = kmalloc(sizeof(struct thread));
    if (!t) return 0;

    uint64_t stack_frames = (THREAD_STACK_SIZE + PAGE_SIZE - 1) / PAGE_SIZE;
    uint8_t *stack = pmm_alloc_frames(stack_frames);
    if (!stack)
    {
        kfree(t);
        return 0;
    }

    uint64_t *rsp = (uint64_t *)(stack + THREAD_STACK_SIZE);

    *--rsp = (uint64_t)thread_trampoline;
    *--rsp = 0;
    *--rsp = 0;
    *--rsp = 0;
    *--rsp = 0;
    *--rsp = 0;
    *--rsp = 0;

    t->rsp = (uint64_t)rsp;
    t->state = THREAD_READY;
    t->priority = 1;
    t->quantum = THREAD_QUANTUM;
    t->func = func;
    t->arg = arg;
    t->stack_base = stack;
    t->kstack_top = (uint64_t)stack + THREAD_STACK_SIZE;
    t->pml4 = vmm_kernel_pml4();
    t->pid = sched_new_pid();
    t->parent_pid = 0;
    t->exit_status = 0;
    int i;
    for (i = 0; name[i] && i < 15; i++)
        t->name[i] = name[i];
    t->name[i] = 0;
    thread_init_vfs(t);

    if (ready_head)
    {
        t->next = ready_head;
        t->prev = ready_head->prev;
        ready_head->prev->next = t;
        ready_head->prev = t;
    }
    else
    {
        t->next = t;
        t->prev = t;
        ready_head = t;
    }

    klog("Sched: spawned '%s' rsp=0x%lx\n", t->name, t->rsp);
    return t;
}

struct thread *sched_spawn_user_in(uint64_t *pml4, void *rip, void *user_rsp,
                         uint64_t rflags, const char *name,
                         uint64_t rbx, uint64_t rbp, uint64_t r12,
                         uint64_t r13, uint64_t r14, uint64_t r15)
{
    struct thread *t = kmalloc(sizeof(struct thread));
    if (!t) return 0;

    uint64_t stack_frames = (THREAD_STACK_SIZE + PAGE_SIZE - 1) / PAGE_SIZE;
    uint8_t *stack = pmm_alloc_frames(stack_frames);
    if (!stack)
    {
        kfree(t);
        return 0;
    }

    uint64_t *rsp = (uint64_t *)(stack + THREAD_STACK_SIZE);

    /* iretq frame (low->high on stack): RIP, CS, RFLAGS, RSP, SS */
    *--rsp = (uint64_t)0x33;                 /* SS   (RPL3) */
    *--rsp = (uint64_t)user_rsp;             /* RSP  (user stack) */
    *--rsp = (uint64_t)rflags;               /* RFLAGS */
    *--rsp = (uint64_t)0x2B;                 /* CS   (user code, RPL3) */
    *--rsp = (uint64_t)rip;                  /* RIP  (user entry) */

    /* entry rax: popped by user_iret_stub before iretq (fork child returns 0) */
    *--rsp = (uint64_t)0;

    /* context_switch `ret` target: drop into ring 3 via iretq */
    *--rsp = (uint64_t)user_iret_stub;

    /* context_switch pop frame: must match pop order (r15 first ... rbx last),
       so push rbx first (lowest address) and r15 last (top of stack). */
    *--rsp = rbx;
    *--rsp = rbp;
    *--rsp = r12;
    *--rsp = r13;
    *--rsp = r14;
    *--rsp = r15;

    t->rsp = (uint64_t)rsp;
    t->user_rsp = (uint64_t)user_rsp;

    t->state = THREAD_READY;
    t->priority = 1;
    t->quantum = THREAD_QUANTUM;
    t->func = 0;
    t->arg = 0;
    t->stack_base = stack;
    t->kstack_top = (uint64_t)stack + THREAD_STACK_SIZE;
    t->pml4 = pml4;
    t->pid = sched_new_pid();
    t->parent_pid = (current ? current->pid : 0);
    t->exit_status = 0;
    int i;
    for (i = 0; name[i] && i < 15; i++)
        t->name[i] = name[i];
    t->name[i] = 0;
    thread_init_vfs(t);

    /* Inherit the full security context from the spawning (parent) thread.
       fork: child == parent context. spawn: child inherits unless a SYSTEM
       role overrides at exec time (not needed here). */
    {
        struct thread *parent = current;
        if (parent)
        {
            t->uid = parent->uid;   t->euid = parent->euid; t->suid = parent->suid;
            t->gid = parent->gid;   t->egid = parent->egid; t->sgid = parent->sgid;
            t->role     = parent->role;
            t->caps_eff = parent->caps_eff;
            t->caps_prm = parent->caps_prm;
            t->caps_inh = parent->caps_inh;
        }
    }

    if (ready_head)
    {
        t->next = ready_head;
        t->prev = ready_head->prev;
        ready_head->prev->next = t;
        ready_head->prev = t;
    }
    else
    {
        t->next = t;
        t->prev = t;
        ready_head = t;
    }

    klog("Sched: spawned user thread '%s' pid=%d rsp=0x%lx\n", t->name, t->pid, t->rsp);
    return t;
}

static void reap_zombies(void)
{
    struct thread *prev = 0;
    struct thread *z = zombie_head;
    while (z)
    {
        struct thread *nxt = z->next;
        /* Keep zombies whose parent is still alive so it can waitpid() them.
           Orphans (parent gone) or parentless threads are reaped here. */
        if (z->parent_pid == 0 || !thread_find_by_pid(z->parent_pid))
        {
            if (z->stack_base)
            {
                uint64_t stack_frames = (THREAD_STACK_SIZE + PAGE_SIZE - 1) / PAGE_SIZE;
                pmm_free_frames(z->stack_base, stack_frames);
                z->stack_base = 0;
            }
            vmm_free_pml4(z->pml4);
            z->pml4 = 0;
            kfree(z);
            if (prev)
                prev->next = nxt;
            else
                zombie_head = nxt;
        }
        else
        {
            prev = z;
        }
        z = nxt;
    }
}

void sched_yield(void)
{
    reap_zombies();
    sched_check_init();

    if (!ready_head)
        return;

    struct thread *old = current;
    old->state = THREAD_READY;
    old->quantum = THREAD_QUANTUM;

    struct thread *next = old->next;
    while (next != old && next->state != THREAD_READY)
        next = next->next;

    if (next->state != THREAD_READY)
        next = &idle_thread;

    current = next;
    current->state = THREAD_RUNNING;
    current->quantum = THREAD_QUANTUM;

    vmm_switch(current->pml4);
    context_switch(&old->rsp, current->rsp, current->kstack_top);
}

void sched_exit(int status)
{
    struct thread *self = current;
    self->state = THREAD_ZOMBIE;
    self->exit_status = status;
    klog("Sched: '%s' (pid=%d) exiting status=%d\n", self->name, self->pid, status);

    if (self == g_init_thread)
    {
        g_init_dead = 1;
        klog("Sched: init (pid=%d) exited -- will respawn\n", self->pid);
    }

    if (ready_head == self)
    {
        ready_head = self->next;
        if (ready_head == self)
            ready_head = 0;
    }
    if (ready_head)
    {
        self->prev->next = self->next;
        self->next->prev = self->prev;
    }

    self->next = zombie_head;
    zombie_head = self;

    if (self->parent_pid > 0)
    {
        struct thread *parent = thread_find_by_pid(self->parent_pid);
        if (parent && parent->state == THREAD_BLOCKED)
            parent->state = THREAD_READY;
    }

    struct thread *next = (ready_head ? ready_head : &idle_thread);
    current = next;
    current->state = THREAD_RUNNING;
    current->quantum = THREAD_QUANTUM;

    vmm_switch(current->pml4);
    context_switch(&self->rsp, current->rsp, current->kstack_top);
}

void sched_tick(void)
{
    if (current && current->quantum > 0)
        current->quantum--;
}

struct thread *sched_current(void)
{
    return current;
}

static struct thread *thread_find_by_pid(int pid)
{
    if (pid <= 0)
        return 0;
    if (current && current->pid == pid)
        return current;
    if (main_thread.pid == pid)
        return &main_thread;
    if (idle_thread.pid == pid)
        return &idle_thread;
    if (!ready_head)
        return 0;
    struct thread *t = ready_head;
    do {
        if (t->pid == pid)
            return t;
        t = t->next;
    } while (t != ready_head);
    return 0;
}

struct thread *sched_find_by_pid(int pid)
{
    return thread_find_by_pid(pid);
}

void sched_mark_init(struct thread *t)
{
    g_init_thread = t;
}

int sched_is_protected(struct thread *t)
{
    return (t == &main_thread || t == &idle_thread);
}

/* Guard the kernel's own threads (main/idle) against being killed.
   Returns 0 if the kill may proceed, or a negative errno to block it.
   A privileged caller (non-USER) attempting to kill main/idle is a fatal
   mistake, so the kernel panics rather than silently allowing it. */
int sched_protected_kill(struct thread *victim, struct thread *killer)
{
    if (!sched_is_protected(victim))
        return 0;
    if (killer && killer->role != ROLE_SYSTEM)
        return -EPERM;
    kernel_panic(victim == &main_thread ? "Attempted to kill main!"
                                        : "Attempted to kill idle!");
    return 0; /* unreachable */
}

/* If the userspace init process died, reap its zombie and respawn it. */
static void sched_check_init(void)
{
    if (!g_init_dead)
        return;
    g_init_dead = 0;
    if (g_init_thread)
    {
        sched_reap_zombie(g_init_thread);
        g_init_thread = 0;
    }
    kernel_respawn_init();
}

void sched_foreach(void (*fn)(struct thread *, void *), void *arg)
{
    if (!fn)
        return;
    if (current)
        fn(current, arg);
    if (&main_thread != current)
        fn(&main_thread, arg);
    if (&idle_thread != current && &idle_thread != &main_thread)
        fn(&idle_thread, arg);
    if (!ready_head)
        return;
    struct thread *t = ready_head;
    do {
        if (t != current && t != &main_thread && t != &idle_thread)
            fn(t, arg);
        t = t->next;
    } while (t != ready_head);
}

void sched_suspend(void)
{
    if (!ready_head)
    {
        __asm__ volatile("hlt");
        return;
    }

    struct thread *old = current;
    struct thread *next = old->next;
    while (next != old && next->state != THREAD_READY)
        next = next->next;

    if (next->state != THREAD_READY)
        next = &idle_thread;

    old->state = THREAD_BLOCKED;

    current = next;
    current->state = THREAD_RUNNING;
    current->quantum = THREAD_QUANTUM;

    vmm_switch(current->pml4);
    context_switch(&old->rsp, current->rsp, current->kstack_top);
}

void sched_wake(struct thread *t)
{
    if (!t)
        return;
    if (t->state == THREAD_BLOCKED)
        t->state = THREAD_READY;
}

struct thread *sched_child_zombie(int parent_pid, int pid_filter)
{
    struct thread *z = zombie_head;
    while (z)
    {
        if (z->parent_pid == parent_pid &&
            (pid_filter <= 0 || z->pid == pid_filter))
            return z;
        z = z->next;
    }
    return 0;
}

int sched_has_child(int parent_pid)
{
    struct thread *t = ready_head;
    if (t)
    {
        struct thread *start = t;
        do {
            if (t->parent_pid == parent_pid)
                return 1;
            t = t->next;
        } while (t != start);
    }
    if (zombie_head)
    {
        struct thread *z = zombie_head;
        while (z)
        {
            if (z->parent_pid == parent_pid)
                return 1;
            z = z->next;
        }
    }
    return 0;
}

void sched_reap_zombie(struct thread *z)
{
    if (!z)
        return;
    if (zombie_head == z)
        zombie_head = z->next;
    else if (zombie_head)
    {
        struct thread *p = zombie_head;
        while (p->next && p->next != z)
            p = p->next;
        if (p->next == z)
            p->next = z->next;
    }
    if (z->stack_base)
    {
        uint64_t stack_frames = (THREAD_STACK_SIZE + PAGE_SIZE - 1) / PAGE_SIZE;
        pmm_free_frames(z->stack_base, stack_frames);
        z->stack_base = 0;
    }
    vmm_free_pml4(z->pml4);
    z->pml4 = 0;
    kfree(z);
}

int sched_enum_procs(struct proc_info *buf, int max)
{
    int n = 0;
    if (!buf || max <= 0)
        return 0;

    #define ADD(t) do { \
        if (n < max && (t)) { \
            buf[n].pid = (t)->pid; \
            buf[n].parent_pid = (t)->parent_pid; \
            buf[n].state = (t)->state; \
            buf[n].uid = (int)(t)->uid; \
            int _i; \
            for (_i = 0; _i < 15 && (t)->name[_i]; _i++) \
                buf[n].name[_i] = (t)->name[_i]; \
            buf[n].name[_i] = 0; \
            n++; \
        } \
    } while (0)

    ADD(&main_thread);
    ADD(&idle_thread);
    if (current && current != &main_thread && current != &idle_thread)
        ADD(current);
    
    if (ready_head)
    {
        struct thread *start = ready_head;
        struct thread *t = ready_head;
        do {
            if (t != &main_thread && t != &idle_thread && t != current)
                ADD(t);
            t = t->next;
        } while (t != start);
    }
    
    if (zombie_head)
    {
        struct thread *z = zombie_head;
        while (z)
        {
            if (z != &main_thread && z != &idle_thread && z != current)
                ADD(z);
            z = z->next;
        }
    }

    #undef ADD
    return n;
}
