#include "syscall.h"
#include "printk.h"
#include "sched.h"
#include "tss.h"
#include "elf.h"
#include "vmm.h"
#include "serial.h"
#include "framebuffer.h"
#include "keyboard.h"
#include "tty.h"
#include "driver.h"
#include "slab.h"
#include "string.h"
#include "pmm.h"
#include "vfs.h"
#include "fat32.h"
#include "axiomefs.h"
#include "socket.h"
#include "security.h"
#include "module.h"

uint64_t syscall_user_rsp;
uint64_t current_kstack_top;
void syscall_set_frame(uint64_t *f)
{
    struct thread *t = sched_current();
    if (t)
        t->syscall_iret = f;
}
struct { uint64_t rip, rsp, rflags, rbx, rbp, r12, r13, r14, r15; } syscall_uregs;
static uint8_t syscall_stack_page[262144] __attribute__((aligned(16)));

/* ---- pipe / FIFO implementation (in-kernel buffered channel) ---- */

static int fd_alloc(struct thread *t)
{
    for (int i = 0; i < MAX_FD; i++)
        if (!t->fds[i].used) return i;
    return -1;
}

static uint64_t sys_write_pipe(struct vfs_file *f, const char *s, size_t len);

extern uint8_t _binary_userspace_init_elf_start[];
extern uint8_t _binary_userspace_init_elf_end[];
extern uint8_t _binary_userspace_hello_elf_start[];
extern uint8_t _binary_userspace_hello_elf_end[];
extern uint8_t _binary_userspace_cat_elf_start[];
extern uint8_t _binary_userspace_cat_elf_end[];
extern uint8_t _binary_userspace_echo_elf_start[];
extern uint8_t _binary_userspace_echo_elf_end[];
extern uint8_t _binary_userspace_sh_elf_start[];
extern uint8_t _binary_userspace_sh_elf_end[];
extern uint8_t _binary_userspace_ls_elf_start[];
extern uint8_t _binary_userspace_ls_elf_end[];
extern uint8_t _binary_userspace_mkdir_elf_start[];
extern uint8_t _binary_userspace_mkdir_elf_end[];
extern uint8_t _binary_userspace_cp_elf_start[];
extern uint8_t _binary_userspace_cp_elf_end[];
extern uint8_t _binary_userspace_mv_elf_start[];
extern uint8_t _binary_userspace_mv_elf_end[];
extern uint8_t _binary_userspace_rm_elf_start[];
extern uint8_t _binary_userspace_rm_elf_end[];
extern uint8_t _binary_userspace_touch_elf_start[];
extern uint8_t _binary_userspace_touch_elf_end[];
extern uint8_t _binary_userspace_whoami_elf_start[];
extern uint8_t _binary_userspace_whoami_elf_end[];
extern uint8_t _binary_userspace_login_elf_start[];
extern uint8_t _binary_userspace_login_elf_end[];
extern uint8_t _binary_userspace_su_elf_start[];
extern uint8_t _binary_userspace_su_elf_end[];
extern uint8_t _binary_userspace_chmod_elf_start[];
extern uint8_t _binary_userspace_chmod_elf_end[];
extern uint8_t _binary_userspace_chown_elf_start[];
extern uint8_t _binary_userspace_chown_elf_end[];
extern uint8_t _binary_userspace_vfs_test_elf_start[];
extern uint8_t _binary_userspace_vfs_test_elf_end[];
extern uint8_t _binary_userspace_ipc_test_elf_start[];
extern uint8_t _binary_userspace_ipc_test_elf_end[];
extern uint8_t _binary_userspace_proc_test_elf_start[];
extern uint8_t _binary_userspace_proc_test_elf_end[];
extern uint8_t _binary_userspace_driver_test_elf_start[];
extern uint8_t _binary_userspace_driver_test_elf_end[];
extern uint8_t _binary_userspace_net_test_elf_start[];
extern uint8_t _binary_userspace_net_test_elf_end[];
extern uint8_t _binary_userspace_kill_elf_start[];
extern uint8_t _binary_userspace_kill_elf_end[];

extern uint8_t _binary_userspace_kxtload_elf_start[];
extern uint8_t _binary_userspace_kxtload_elf_end[];
extern uint8_t _binary_userspace_kxtunload_elf_start[];
extern uint8_t _binary_userspace_kxtunload_elf_end[];

extern void syscall_entry(void);

typedef uint64_t (*syscall_fn)(uint64_t, uint64_t, uint64_t, uint64_t, uint64_t);

static uint64_t sys_print(uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5)
{
    printk("SYS_print: %s\n", (const char *)a1);
    (void)a2; (void)a3; (void)a4; (void)a5;
    return 0;
}

static uint64_t sys_yield(uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5)
{
    (void)a1; (void)a2; (void)a3; (void)a4; (void)a5;
    sched_yield();
    return 0;
}

static uint64_t sys_exit(uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5)
{
    (void)a2; (void)a3; (void)a4; (void)a5;
    sched_exit((int)a1);
    return 0;
}

static uint64_t sys_fork(uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5)
{
    (void)a1; (void)a2; (void)a3; (void)a4; (void)a5;
    return (uint64_t)fork_process(syscall_uregs.rip,
                                  syscall_uregs.rsp,
                                  syscall_uregs.rflags,
                                  syscall_uregs.rbx,
                                  syscall_uregs.rbp,
                                  syscall_uregs.r12,
                                  syscall_uregs.r13,
                                  syscall_uregs.r14,
                                  syscall_uregs.r15);
}

static uint64_t sys_spawn(uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5)
{
    (void)a2; (void)a3; (void)a4; (void)a5;
    uint8_t *start;
    size_t size;
    const char *name;
    if (a1 == 1)
    {
        start = _binary_userspace_hello_elf_start;
        size = (size_t)(_binary_userspace_hello_elf_end - start);
        name = "hello";
    }
    else
    {
        start = _binary_userspace_init_elf_start;
        size = (size_t)(_binary_userspace_init_elf_end - start);
        name = "spawned";
    }
    return (uint64_t)spawn_process(start, size, name);
}

static uint64_t sys_getpid(uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5)
{
    (void)a1; (void)a2; (void)a3; (void)a4; (void)a5;
    struct thread *t = sched_current();
    return (uint64_t)(t ? t->pid : 0);
}

static uint64_t sys_waitpid(uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5)
{
    (void)a3; (void)a4; (void)a5;
    int pid_filter = (int)a1;
    int *status_out = (int *)a2;
    struct thread *t = sched_current();
    int self_pid = t ? t->pid : 0;

    for (;;)
    {
        struct thread *z = sched_child_zombie(self_pid, pid_filter);
        if (z)
        {
            int st = z->exit_status;
            int zpid = z->pid;
            sched_reap_zombie(z);
            if (status_out)
                *status_out = st;
            return (uint64_t)zpid;
        }
        if (!sched_has_child(self_pid))
            return (uint64_t)(-1);
        sched_suspend();
    }
}

static void con_putchar(char c)
{
    if (fb_active)
        fb_putchar(c);
    serial_putchar(COM1, c);
}

static uint64_t sys_write(uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5)
{
    (void)a4; (void)a5;
    int fd = (int)a1;
    const char *s = (const char *)a2;
    size_t len = (size_t)a3;
    vfs_ensure_proc();
    struct thread *t = sched_current();
    if (fd < 0 || fd >= MAX_FD || !t->fds[fd].used)
        return (uint64_t)-1;
    struct vfs_file *f = &t->fds[fd];
    if (f->kind == FD_TTY_IN)
        return (uint64_t)-1;
    if (f->kind == FD_CONSOLE_OUT)
    {
        for (size_t i = 0; i < len; i++)
            con_putchar(s[i]);
        return len;
    }
    if (f->kind == FD_PIPE)
        return sys_write_pipe(f, s, len);
    /* FD_VNODE */
    size_t w = vfs_write(f->node, f->off, s, len);
    f->off += w;
    return w;
}

static uint64_t sys_write_pipe(struct vfs_file *f, const char *s, size_t len)
{
    if (f->node->type != VFS_PIPE_W)
        return (uint64_t)-1;
    struct pipe *p = (struct pipe *)f->node->priv;
    if (p->count + len > p->cap)
    {
        size_t ncap = p->cap ? p->cap : 256;
        while (ncap < p->count + len) ncap *= 2;
        uint8_t *nb = kmalloc(ncap);
        if (!nb) return 0;
        for (size_t i = 0; i < p->count; i++) nb[i] = p->buf[i];
        kfree(p->buf);
        p->buf = nb;
        p->cap = ncap;
    }
    for (size_t i = 0; i < len; i++)
        p->buf[p->count++] = (uint8_t)s[i];
    if (p->wait_reader)
    {
        sched_wake(p->wait_reader);
        p->wait_reader = 0;
    }
    return len;
}

static uint64_t sys_read(uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5)
{
    (void)a4; (void)a5;
    int fd = (int)a1;
    char *buf = (char *)a2;
    size_t len = (size_t)a3;
    vfs_ensure_proc();
    struct thread *t = sched_current();
    if (fd < 0 || fd >= MAX_FD || !t->fds[fd].used)
        return (uint64_t)-1;
    struct vfs_file *f = &t->fds[fd];
    if (f->kind == FD_CONSOLE_OUT)
        return (uint64_t)-1;
    if (f->kind == FD_TTY_IN)
    {
        size_t got = 0;
        while (got < len)
        {
            char c;
            if (!tty_read_char(&c))
                break;
            buf[got++] = c;
        }
        return got;
    }
    if (f->kind == FD_PIPE)
    {
        if (f->node->type != VFS_PIPE_R)
            return (uint64_t)-1;
        struct pipe *p = (struct pipe *)f->node->priv;
        while (p->count == 0)
        {
            if (p->nwriters == 0)
                return 0; /* EOF */
            p->wait_reader = sched_current();
            sched_suspend();
        }
        size_t r = (len < p->count) ? len : p->count;
        for (size_t i = 0; i < r; i++)
            buf[i] = p->buf[i];
        for (size_t i = 0; i + r < p->count; i++)
            p->buf[i] = p->buf[i + r];
        p->count -= r;
        return r;
    }
    /* FD_VNODE */
    size_t r = vfs_read(f->node, f->off, buf, len);
    f->off += r;
    return r;
}

static int copy_path(uint64_t uptr, char *buf, size_t max)
{
    const char *u = (const char *)uptr;
    size_t i = 0;
    for (; i < max - 1; i++)
    {
        char c = u[i];
        buf[i] = c;
        if (c == 0)
            break;
    }
    buf[i] = 0;
    return (int)i;
}

static void make_abs(const char *path, const char *cwd, char *out, size_t outsz)
{
    if (path[0] == '/')
    {
        size_t l = 0;
        while (path[l] && l < outsz - 1) { out[l] = path[l]; l++; }
        out[l] = 0;
    }
    else
    {
        size_t l = 0;
        while (cwd[l] && l < outsz - 1) { out[l] = cwd[l]; l++; }
        if (l > 0 && out[l - 1] != '/')
        {
            if (l < outsz - 1) { out[l] = '/'; l++; }
        }
        out[l] = 0;
        size_t k = 0;
        while (path[k] && l < outsz - 1) { out[l] = path[k]; l++; k++; }
        out[l] = 0;
    }
    size_t len = 0;
    while (out[len]) len++;
    if (len > 1 && out[len - 1] == '/')
        out[len - 1] = 0;
    vfs_apply_aliases(out);
}

static uint64_t sys_open(uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5)
{
    (void)a3; (void)a4; (void)a5;
    char path[1024];
    if (copy_path(a1, path, sizeof(path)) == 0)
        return (uint64_t)-1;
    int flags = (int)a2;
    vfs_ensure_proc();
    struct thread *t = sched_current();

    struct vnode *n = vfs_lookup(path, t->cwd);
    if (n && n->type == VFS_FIFO)
    {
        struct pipe *p = (struct pipe *)n->priv;
        if (!p) { vfs_release(n); return (uint64_t)-1; }
        int is_write = (flags & O_WRONLY) ? 1 : 0;
        struct vnode *end = kmalloc(sizeof(struct vnode));
        if (!end) { vfs_release(n); return (uint64_t)-1; }
        memset(end, 0, sizeof(*end));
        end->type = is_write ? VFS_PIPE_W : VFS_PIPE_R;
        end->priv = p;
        end->refcount = 1;
        if (is_write) p->nwriters++; else p->nreaders++;
        vfs_release(n);
        int fd = fd_alloc(t);
        if (fd < 0)
        {
            if (is_write) p->nwriters--; else p->nreaders--;
            kfree(end);
            return (uint64_t)-1;
        }
        t->fds[fd].used = 1; t->fds[fd].kind = FD_PIPE;
        t->fds[fd].node = end; t->fds[fd].off = 0;
        return (uint64_t)fd;
    }
    if (n)
    {
        if (n->type == VFS_DIR)
            return (uint64_t)-1;
        if (flags & O_TRUNC)
        {
            if (n->data) kfree(n->data);
            n->data = 0;
            n->size = 0;
            n->cap = 0;
        }
    }
    else
    {
        if (!(flags & O_CREAT))
            return (uint64_t)-1;
        if (vfs_create(path, VFS_FILE, t->cwd) != 0)
            return (uint64_t)-1;
        n = vfs_lookup(path, t->cwd);
        if (!n)
            return (uint64_t)-1;
    }

    int fd = -1;
    for (int i = 0; i < MAX_FD; i++)
        if (!t->fds[i].used) { fd = i; break; }
    if (fd < 0)
        return (uint64_t)-1;
    t->fds[fd].used = 1;
    t->fds[fd].kind = FD_VNODE;
    t->fds[fd].node = n;
    t->fds[fd].off = (flags & O_APPEND) ? n->size : 0;
    return (uint64_t)fd;
}

static uint64_t sys_close(uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5)
{
    (void)a2; (void)a3; (void)a4; (void)a5;
    int fd = (int)a1;
    vfs_ensure_proc();
    struct thread *t = sched_current();
    if (fd < 0 || fd >= MAX_FD || !t->fds[fd].used)
        return (uint64_t)-1;
    if (t->fds[fd].kind == FD_PIPE)
    {
        struct vnode *n = t->fds[fd].node;
        struct pipe *p = (struct pipe *)n->priv;
        if (n->type == VFS_PIPE_R) p->nreaders--;
        else p->nwriters--;
        if (n->refcount > 0) n->refcount--;
        if (n->refcount == 0) kfree(n);
        if (p->shared)
        {
            if (p->dead && p->nreaders == 0 && p->nwriters == 0)
            {
                if (p->buf) kfree(p->buf);
                kfree(p);
            }
        }
        else
        {
            if (p->nreaders == 0 && p->nwriters == 0)
            {
                if (p->buf) kfree(p->buf);
                kfree(p);
            }
            else if (p->wait_reader)
            {
                sched_wake(p->wait_reader);
                p->wait_reader = 0;
            }
        }
        t->fds[fd].used = 0;
        t->fds[fd].kind = FD_FREE;
        t->fds[fd].node = 0;
        t->fds[fd].off = 0;
        return 0;
    }
    if (t->fds[fd].kind == FD_VNODE)
        vfs_release(t->fds[fd].node);
    t->fds[fd].used = 0;
    t->fds[fd].kind = FD_FREE;
    t->fds[fd].node = 0;
    t->fds[fd].off = 0;
    return 0;
}

static uint64_t sys_mkdir(uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5)
{
    (void)a2; (void)a3; (void)a4; (void)a5;
    char path[1024];
    if (copy_path(a1, path, sizeof(path)) == 0)
        return (uint64_t)-1;
    vfs_ensure_proc();
    struct thread *t = sched_current();
    if (vfs_create(path, VFS_DIR, t->cwd) != 0)
        return (uint64_t)-1;
    return 0;
}

static uint64_t sys_unlink(uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5)
{
    (void)a2; (void)a3; (void)a4; (void)a5;
    char path[1024];
    if (copy_path(a1, path, sizeof(path)) == 0)
        return (uint64_t)-1;
    vfs_ensure_proc();
    struct thread *t = sched_current();
    if (vfs_remove(path, t->cwd) != 0)
        return (uint64_t)-1;
    return 0;
}

static uint64_t sys_readdir(uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5)
{
    (void)a4; (void)a5;
    char path[1024];
    if (copy_path(a1, path, sizeof(path)) == 0)
        return (uint64_t)-1;
    struct vfs_dirent *uents = (struct vfs_dirent *)a2;
    int max = (int)a3;
    vfs_ensure_proc();
    struct thread *t = sched_current();

    int cap = max;
    if (cap < 1) cap = 1;
    if (cap > 1024) cap = 1024;
    struct vfs_dirent *kbuf = kmalloc(sizeof(struct vfs_dirent) * cap);
    if (!kbuf)
        return (uint64_t)-1;
    int count = vfs_list(path, t->cwd, kbuf, cap);
    if (count < 0)
    {
        kfree(kbuf);
        return (uint64_t)-1;
    }
    int tocopy = count;
    if (tocopy > max) tocopy = max;
    if (tocopy > cap) tocopy = cap;
    for (int i = 0; i < tocopy; i++)
        uents[i] = kbuf[i];
    kfree(kbuf);
    return (uint64_t)count;
}

static uint64_t sys_chdir(uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5)
{
    (void)a2; (void)a3; (void)a4; (void)a5;
    char path[1024];
    if (copy_path(a1, path, sizeof(path)) == 0)
        return (uint64_t)-1;
    vfs_ensure_proc();
    struct thread *t = sched_current();
    struct vnode *n = vfs_lookup(path, t->cwd);
    if (!n || n->type != VFS_DIR)
        return (uint64_t)-1;
    char abs[1024];
    make_abs(path, t->cwd, abs, sizeof(abs));
    int i = 0;
    while (abs[i] && i < 255) { t->cwd[i] = abs[i]; i++; }
    t->cwd[i] = 0;
    return 0;
}

static uint64_t sys_getcwd(uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5)
{
    (void)a3; (void)a4; (void)a5;
    char *ubuf = (char *)a1;
    size_t sz = (size_t)a2;
    vfs_ensure_proc();
    struct thread *t = sched_current();
    size_t l = 0;
    while (t->cwd[l]) l++;
    if (sz < l + 1)
        return (uint64_t)-1;
    for (size_t i = 0; i <= l; i++)
        ubuf[i] = t->cwd[i];
    return 0;
}

static uint64_t sys_fstat(uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5)
{
    (void)a3; (void)a4; (void)a5;
    int fd = (int)a1;
    struct stat *ust = (struct stat *)a2;
    vfs_ensure_proc();
    struct thread *t = sched_current();
    if (fd < 0 || fd >= MAX_FD || !t->fds[fd].used)
        return (uint64_t)-1;
    struct vfs_file *f = &t->fds[fd];
    if (f->kind != FD_VNODE || !f->node)
        return (uint64_t)-1;
    struct vnode *n = f->node;
    struct stat st;
    st.st_size = (uint64_t)n->size;
    st.st_mode = (n->type == VFS_DIR) ? S_IFDIR : S_IFREG;
    st.st_nlink = 1;
    st.st_ino = (uint32_t)(uintptr_t)n->priv;
    *ust = st;
    return 0;
}

static uint64_t sys_dup2(uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5)
{
    (void)a3; (void)a4; (void)a5;
    int oldfd = (int)a1;
    int newfd = (int)a2;
    vfs_ensure_proc();
    struct thread *t = sched_current();
    if (oldfd < 0 || oldfd >= MAX_FD || !t->fds[oldfd].used)
        return (uint64_t)-1;
    if (newfd < 0 || newfd >= MAX_FD)
        return (uint64_t)-1;
    if (oldfd == newfd)
        return (uint64_t)newfd;
    if (t->fds[newfd].used)
    {
        uint64_t r = sys_close((uint64_t)newfd, 0, 0, 0, 0);
        if (r == (uint64_t)-1)
            return (uint64_t)-1;
    }
    struct vfs_file *o = &t->fds[oldfd];
    struct vfs_file *n = &t->fds[newfd];
    n->used = 1;
    n->kind = o->kind;
    n->node = o->node;
    n->off = o->off;
    if (o->kind == FD_VNODE && o->node)
        o->node->refcount++;
    else if (o->kind == FD_PIPE && o->node)
    {
        struct pipe *p = (struct pipe *)o->node->priv;
        if (o->node->type == VFS_PIPE_R) p->nreaders++;
        else p->nwriters++;
        o->node->refcount++;
    }
    return (uint64_t)newfd;
}

static uint64_t sys_pipe(uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5)
{
    (void)a2; (void)a3; (void)a4; (void)a5;
    int *ufds = (int *)a1;
    vfs_ensure_proc();
    struct thread *t = sched_current();
    struct pipe *p = kmalloc(sizeof(*p));
    if (!p) return (uint64_t)-1;
    p->buf = kmalloc(256);
    if (!p->buf) { kfree(p); return (uint64_t)-1; }
    p->cap = 256;
    p->count = 0;
    p->nreaders = 1;
    p->nwriters = 1;
    p->wait_reader = 0;

    struct vnode *r = kmalloc(sizeof(struct vnode));
    struct vnode *w = kmalloc(sizeof(struct vnode));
    if (!r || !w) { kfree(p->buf); kfree(p); kfree(r); kfree(w); return (uint64_t)-1; }
    memset(r, 0, sizeof(*r));
    memset(w, 0, sizeof(*w));
    r->type = VFS_PIPE_R; r->priv = p; r->refcount = 1;
    w->type = VFS_PIPE_W; w->priv = p; w->refcount = 1;

    int rfd = fd_alloc(t);
    if (rfd < 0) { kfree(p->buf); kfree(p); kfree(r); kfree(w); return (uint64_t)-1; }
    t->fds[rfd].used = 1;
    int wfd = fd_alloc(t);
    if (wfd < 0) { t->fds[rfd].used = 0; kfree(p->buf); kfree(p); kfree(r); kfree(w); return (uint64_t)-1; }
    t->fds[rfd].kind = FD_PIPE; t->fds[rfd].node = r; t->fds[rfd].off = 0;
    t->fds[wfd].used = 1; t->fds[wfd].kind = FD_PIPE; t->fds[wfd].node = w; t->fds[wfd].off = 0;

    ufds[0] = rfd;
    ufds[1] = wfd;
    return 0;
}

static uint64_t sys_mkfifo(uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5)
{
    (void)a2; (void)a3; (void)a4; (void)a5;
    char path[1024];
    if (copy_path(a1, path, sizeof(path)) == 0)
        return (uint64_t)-1;
    vfs_ensure_proc();
    struct thread *t = sched_current();
    if (vfs_create(path, VFS_FIFO, t->cwd) != 0)
        return (uint64_t)-1;
    return 0;
}

static uint64_t sys_drv_rescan(uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5)
{
    (void)a1; (void)a2; (void)a3; (void)a4; (void)a5;
    driver_rescan();
    return 0;
}

static uint64_t sys_mount(uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5)
{
    (void)a4; (void)a5;
    int fstype = (int)a1;
    char mp[1024];
    if (copy_path(a2, mp, sizeof(mp)) == 0)
        return (uint64_t)-1;
    int dev = (int)a3;
    vfs_ensure_proc();
    if (fstype == FS_RAMFS)
        return (uint64_t)vfs_mount_ramfs(mp);
    if (fstype == FS_FAT32)
    {
        int bus = (dev >> 16) & 0xFF;
        int drive = (dev >> 8) & 0xFF;
        int part = dev & 0xFF;
        return (uint64_t)fat32_mount_part(bus, drive, part, mp);
    }
    if (fstype == FS_AXIOMEFS)
    {
        int bus = (dev >> 16) & 0xFF;
        int drive = (dev >> 8) & 0xFF;
        int part = dev & 0xFF;
        return (uint64_t)axiomefs_mount_part(bus, drive, part, mp);
    }
    return (uint64_t)-1;
}

static uint64_t sys_umount(uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5)
{
    (void)a2; (void)a3; (void)a4; (void)a5;
    char mp[1024];
    if (copy_path(a1, mp, sizeof(mp)) == 0)
        return (uint64_t)-1;
    vfs_ensure_proc();
    return (uint64_t)vfs_umount(mp);
}

void vfs_ensure_proc(void)
{
    struct thread *t = sched_current();
    if (!t)
        return;
    if (t->fds[0].used && t->fds[1].used && t->fds[2].used)
    {
        if (t->cwd[0] == 0) { t->cwd[0] = '/'; t->cwd[1] = 0; }
        return;
    }
    t->fds[0].used = 1; t->fds[0].kind = FD_TTY_IN;      t->fds[0].node = 0; t->fds[0].off = 0;
    t->fds[1].used = 1; t->fds[1].kind = FD_CONSOLE_OUT; t->fds[1].node = 0; t->fds[1].off = 0;
    t->fds[2].used = 1; t->fds[2].kind = FD_CONSOLE_OUT; t->fds[2].node = 0; t->fds[2].off = 0;
    if (t->cwd[0] == 0) { t->cwd[0] = '/'; t->cwd[1] = 0; }
}

struct spawn_prog {
    const char *name;
    uint8_t *start;
    uint8_t *end;
};

static const struct spawn_prog spawn_progs[] = {
    {"init",  _binary_userspace_init_elf_start,  _binary_userspace_init_elf_end},
    {"hello", _binary_userspace_hello_elf_start, _binary_userspace_hello_elf_end},
    {"cat",   _binary_userspace_cat_elf_start,   _binary_userspace_cat_elf_end},
    {"echo",  _binary_userspace_echo_elf_start,  _binary_userspace_echo_elf_end},
    {"sh",    _binary_userspace_sh_elf_start,    _binary_userspace_sh_elf_end},
    {"ls",    _binary_userspace_ls_elf_start,    _binary_userspace_ls_elf_end},
    {"mkdir", _binary_userspace_mkdir_elf_start, _binary_userspace_mkdir_elf_end},
    {"cp",    _binary_userspace_cp_elf_start,    _binary_userspace_cp_elf_end},
    {"mv",    _binary_userspace_mv_elf_start,    _binary_userspace_mv_elf_end},
    {"rm",    _binary_userspace_rm_elf_start,    _binary_userspace_rm_elf_end},
    {"touch", _binary_userspace_touch_elf_start,  _binary_userspace_touch_elf_end},
    {"whoami", _binary_userspace_whoami_elf_start, _binary_userspace_whoami_elf_end},
    {"login", _binary_userspace_login_elf_start,  _binary_userspace_login_elf_end},
    {"su", _binary_userspace_su_elf_start,        _binary_userspace_su_elf_end},
    {"chmod", _binary_userspace_chmod_elf_start,  _binary_userspace_chmod_elf_end},
    {"chown", _binary_userspace_chown_elf_start,  _binary_userspace_chown_elf_end},
    {"vfs_test", _binary_userspace_vfs_test_elf_start, _binary_userspace_vfs_test_elf_end},
    {"ipc_test", _binary_userspace_ipc_test_elf_start, _binary_userspace_ipc_test_elf_end},
    {"proc_test", _binary_userspace_proc_test_elf_start, _binary_userspace_proc_test_elf_end},
    {"driver_test", _binary_userspace_driver_test_elf_start, _binary_userspace_driver_test_elf_end},
    {"net_test", _binary_userspace_net_test_elf_start, _binary_userspace_net_test_elf_end},
    {"kill", _binary_userspace_kill_elf_start, _binary_userspace_kill_elf_end},
    {"kxtload", _binary_userspace_kxtload_elf_start, _binary_userspace_kxtload_elf_end},
    {"kxtunload", _binary_userspace_kxtunload_elf_start, _binary_userspace_kxtunload_elf_end},
};

static int try_exec_path(const char *path, int argc, char **argv)
{
    uint8_t *buf = 0;
    size_t sz = 0;
    if (vfs_read_file(path, &buf, &sz) != 0)
        return -1;
    int pid = spawn_process_with_args(buf, sz, path, argc, argv);
    kfree(buf);
    return pid;
}

static const char *kpath_dirs[] = { "/", "/bin", "/Binaries", 0 };

static uint64_t sys_spawn_cmd(uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5)
{
    (void)a3; (void)a4; (void)a5;
    const char *u_cmd = (const char *)a1;
    size_t len = (size_t)a2;
    if (len > 255)
        len = 255;
    char cmd[256];
    for (size_t i = 0; i < len; i++)
        cmd[i] = u_cmd[i];
    cmd[len] = 0;

    char *argv[16];
    int argc = 0;
    char *p = cmd;
    while (*p && argc < 16)
    {
        while (*p == ' ' || *p == '\t')
            p++;
        if (!*p)
            break;
        argv[argc++] = p;
        while (*p && *p != ' ' && *p != '\t')
            p++;
        if (*p)
        {
            *p = 0;
            p++;
        }
    }
    if (argc == 0)
        return (uint64_t)(-1);

    {
        const char *p = argv[0];
        int has_slash = 0;
        while (*p) { if (*p == '/') { has_slash = 1; break; } p++; }
        if (has_slash)
        {
            int pid = try_exec_path(argv[0], argc, argv);
            if (pid >= 0)
                return (uint64_t)pid;
            printk("SPAWN_CMD: cannot exec '%s'\n", argv[0]);
            return (uint64_t)(-1);
        }
    }

    for (int d = 0; kpath_dirs[d]; d++)
    {
        size_t dl = strlen(kpath_dirs[d]);
        size_t nl = strlen(argv[0]);
        if (dl + 1 + nl >= 256)
            continue;
        char full[256];
        memcpy(full, kpath_dirs[d], dl);
        size_t p = dl;
        if (p > 0 && kpath_dirs[d][dl - 1] != '/')
            full[p++] = '/';
        memcpy(full + p, argv[0], nl + 1);
        int pid = try_exec_path(full, argc, argv);
        if (pid >= 0)
            return (uint64_t)pid;
    }

    for (size_t i = 0; i < sizeof(spawn_progs) / sizeof(spawn_progs[0]); i++)
    {
        if (strcmp(spawn_progs[i].name, argv[0]) == 0)
        {
            size_t sz = (size_t)(spawn_progs[i].end - spawn_progs[i].start);
            int pid = spawn_process_with_args(spawn_progs[i].start, sz,
                                              spawn_progs[i].name, argc, argv);
            return (uint64_t)pid;
        }
    }
    printk("SPAWN_CMD: unknown program '%s'\n", argv[0]);
    return (uint64_t)(-1);
}

static uint64_t sys_ps(uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5)
{
    (void)a3; (void)a4; (void)a5;
    struct proc_info *ubuf = (struct proc_info *)a1;
    int max = (int)a2;
    if (max > 64)
        max = 64;
    if (max <= 0 || !ubuf)
        return 0;
    
    struct proc_info kbuf[64];
    int n = sched_enum_procs(kbuf, max);
    if (n < 0) n = 0;
    
    for (int i = 0; i < n && i < max; i++)
        ubuf[i] = kbuf[i];
    
    return (uint64_t)n;
}

/* ---- sockets (Phase 13) ---- */

static uint64_t sys_socket_create(uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5)
{
    (void)a3; (void)a4; (void)a5;
    return (uint64_t)sock_create((int)a1, (int)a2, 0);
}

static uint64_t sys_socket_bind(uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5)
{
    (void)a4; (void)a5;
    return (uint64_t)sock_bind((int)a1, (const struct sockaddr *)a2, (int)a3);
}

static uint64_t sys_socket_connect(uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5)
{
    (void)a4; (void)a5;
    return (uint64_t)sock_connect((int)a1, (const struct sockaddr *)a2, (int)a3);
}

static uint64_t sys_socket_send(uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5)
{
    (void)a4; (void)a5;
    return (uint64_t)sock_send((int)a1, (const void *)a2, (size_t)a3);
}

static uint64_t sys_socket_recv(uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5)
{
    (void)a4; (void)a5;
    return (uint64_t)sock_recv((int)a1, (void *)a2, (size_t)a3);
}

static uint64_t sys_socket_close(uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5)
{
    (void)a2; (void)a3; (void)a4; (void)a5;
    return (uint64_t)sock_close((int)a1);
}

static uint64_t sys_socket_listen(uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5)
{
    (void)a2; (void)a3; (void)a4; (void)a5;
    return (uint64_t)sock_listen((int)a1);
}

static uint64_t sys_socket_accept(uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5)
{
    (void)a2; (void)a3; (void)a4; (void)a5;
    return (uint64_t)sock_accept((int)a1);
}

/* ---- signals (Phase 11) ---- */

static uint64_t sys_sigaction(uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5)
{
    (void)a4; (void)a5;
    int sig = (int)a1;
    const struct sigaction *act = (const struct sigaction *)a2;
    struct sigaction *oldact = (struct sigaction *)a3;
    if (sig <= 0 || sig >= NSIG)
        return (uint64_t)(-1);
    if (sig == SIGKILL || sig == SIGSTOP)
        return (uint64_t)(-1);
    struct thread *t = sched_current();
    if (!t)
        return (uint64_t)(-1);
    if (oldact)
        *oldact = t->sig_actions[sig];
    if (act)
        t->sig_actions[sig] = *act;
    return 0;
}

struct kill_arg { int sig; int self; };
static void kill_fn(struct thread *t, void *arg)
{
    struct kill_arg *ka = (struct kill_arg *)arg;
    if (t->pid == ka->self)
        return;
    if (sched_is_protected(t))
        return;
    t->sig_pending |= (1ULL << ka->sig);
}

static uint64_t sys_kill(uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5)
{
    (void)a3; (void)a4; (void)a5;
    int pid = (int)a1;
    int sig = (int)a2;
    if (sig <= 0 || sig >= NSIG)
        return (uint64_t)(-1);
    struct thread *self = sched_current();
    if (self)
    {
        if (pid > 0)
        {
            struct thread *t = sched_find_by_pid(pid);
            if (!t)
                return (uint64_t)(-ESRCH);
            int pr = sched_protected_kill(t, self);
            if (pr != 0)
                return (uint64_t)pr;
            if (t->uid != self->euid &&
                self->role != ROLE_SYSTEM &&
                !(self->caps_prm & (CAP_KILL_ANY | CAP_SIGNAL_OTHER)))
                return (uint64_t)(-EPERM);
            t->sig_pending |= (1ULL << sig);
            return 0;
        }
        if (pid == 0)
        {
            self->sig_pending |= (1ULL << sig);
            return 0;
        }
        if (pid == -1)
        {
            if (self->role != ROLE_SYSTEM && !(self->caps_prm & CAP_KILL_ANY))
                return (uint64_t)(-EPERM);
            struct kill_arg ka = { sig, self->pid };
            sched_foreach(kill_fn, &ka);
            return 0;
        }
        return (uint64_t)(-1);
    }
    return (uint64_t)(-1);
}

static uint64_t sys_sigreturn(uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5)
{
    (void)a1; (void)a2; (void)a3; (void)a4; (void)a5;
    struct thread *t = sched_current();
    if (!t || !t->syscall_iret)
        return (uint64_t)(-1);
    uint64_t *frame = t->syscall_iret;
    uint64_t user_rsp = frame[3];
    struct sigframe *sf = (struct sigframe *)user_rsp;
    frame[-1]  = sf->rax;
    frame[-2]  = sf->rdi;
    frame[-3]  = sf->rsi;
    frame[-4]  = sf->rdx;
    frame[-5]  = sf->r10;
    frame[-6]  = sf->r8;
    frame[-7]  = sf->r9;
    frame[-8]  = sf->rbx;
    frame[-9]  = sf->rbp;
    frame[-10] = sf->r12;
    frame[-11] = sf->r13;
    frame[-12] = sf->r14;
    frame[-13] = sf->r15;
    frame[0] = sf->rip;
    frame[1] = sf->cs;
    frame[2] = sf->rflags;
    frame[3] = sf->rsp;
    frame[4] = sf->ss;
    t->sig_mask = sf->oldmask;
    return 0;
}

/* ================================================================ *
 * User rank system: identity / capability syscalls
 * ================================================================ */

static uint64_t sys_getuid(uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5)
{
    (void)a1;(void)a2;(void)a3;(void)a4;(void)a5;
    struct thread *t = sched_current();
    return (uint64_t)(t ? t->uid : 0);
}

static uint64_t sys_geteuid(uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5)
{
    (void)a1;(void)a2;(void)a3;(void)a4;(void)a5;
    struct thread *t = sched_current();
    return (uint64_t)(t ? t->euid : 0);
}

static uint64_t sys_getgid(uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5)
{
    (void)a1;(void)a2;(void)a3;(void)a4;(void)a5;
    struct thread *t = sched_current();
    return (uint64_t)(t ? t->gid : 0);
}

static uint64_t sys_getegid(uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5)
{
    (void)a1;(void)a2;(void)a3;(void)a4;(void)a5;
    struct thread *t = sched_current();
    return (uint64_t)(t ? t->egid : 0);
}

static uint64_t sys_getrole(uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5)
{
    (void)a1;(void)a2;(void)a3;(void)a4;(void)a5;
    struct thread *t = sched_current();
    return (uint64_t)(t ? (int)t->role : 0);
}

static uint64_t sys_getcap(uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5)
{
    (void)a1;(void)a2;(void)a3;(void)a4;(void)a5;
    struct thread *t = sched_current();
    return (uint64_t)(t ? t->caps_eff : 0);
}

static uint64_t sys_setuid(uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5)
{
    (void)a2;(void)a3;(void)a4;(void)a5;
    struct thread *t = sched_current();
    if (!t)
        return (uint64_t)-EPERM;
    if (t->role != ROLE_SYSTEM && !(t->caps_prm & CAP_SETUID))
        return (uint64_t)-EPERM;
    t->uid  = (uid_t)a1;
    t->euid = (uid_t)a1;
    t->suid = (uid_t)a1;
    t->role = posix_uid_to_role((uid_t)a1);
    t->caps_eff = t->caps_prm = t->caps_inh = role_caps[t->role];
    printk("SEC: pid %d setuid -> uid=%lu role=%d\n", t->pid, (unsigned long)a1, t->role);
    return 0;
}

static uint64_t sys_setgid(uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5)
{
    (void)a2;(void)a3;(void)a4;(void)a5;
    struct thread *t = sched_current();
    if (!t)
        return (uint64_t)-EPERM;
    if (t->role != ROLE_SYSTEM && !(t->caps_prm & CAP_SETGID))
        return (uint64_t)-EPERM;
    t->gid  = (gid_t)a1;
    t->egid = (gid_t)a1;
    t->sgid = (gid_t)a1;
    return 0;
}

static uint64_t sys_setcap(uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5)
{
    (void)a2;(void)a3;(void)a4;(void)a5;
    struct thread *t = sched_current();
    if (!t)
        return (uint64_t)-EPERM;
    if (t->role != ROLE_SYSTEM && !(t->caps_prm & CAP_SYS_ADMIN))
        return (uint64_t)-EPERM;
    t->caps_eff = (uint64_t)a1 & t->caps_prm;
    return 0;
}

static uint64_t sys_getpwnam(uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5)
{
    (void)a4;(void)a5;
    const char *name = (const char *)a1;
    uid_t *uout = (uid_t *)a2;
    gid_t *gout = (gid_t *)a3;
    if (!name) return (uint64_t)-1;
    const struct user_entry *u = security_lookup_name(name);
    if (!u) return (uint64_t)-1;
    if (uout) *uout = u->uid;
    if (gout) *gout = u->gid;
    return 0;
}

/* ---- loadable kernel modules (.kxt) ---- */

static int module_privileged(void)
{
    struct thread *t = sched_current();
    if (!t)
        return 1;   /* kernel thread */
    if (t->role == ROLE_SYSTEM)
        return 1;
    return (t->caps_prm & CAP_SYS_ADMIN) ? 1 : 0;
}

static uint64_t sys_module_load(uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5)
{
    (void)a2; (void)a3; (void)a4; (void)a5;
    if (!module_privileged())
        return (uint64_t)(-EPERM);
    const char *path = (const char *)a1;
    if (!path)
        return (uint64_t)(-EFAULT);
    return (uint64_t)module_load_file(path);
}

static uint64_t sys_module_unload(uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5)
{
    (void)a2; (void)a3; (void)a4; (void)a5;
    if (!module_privileged())
        return (uint64_t)(-EPERM);
    const char *name = (const char *)a1;
    if (!name)
        return (uint64_t)(-EFAULT);
    return (uint64_t)module_unload(name);
}

static uint64_t sys_chmod(uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5)
{
    (void)a3;(void)a4;(void)a5;
    char path[1024];
    if (copy_path(a1, path, sizeof(path)) == 0)
        return (uint64_t)-EFAULT;
    struct thread *t = sched_current();
    if (!t) return (uint64_t)-EPERM;
    vfs_ensure_proc();
    struct vnode *n = vfs_lookup(path, t->cwd);
    if (!n) return (uint64_t)-ENOENT;
    if (n->v_uid != t->euid && t->role != ROLE_SYSTEM && !(t->caps_prm & CAP_SYS_ADMIN))
    {
        vfs_release(n);
        return (uint64_t)-EPERM;
    }
    n->v_mode = (uint16_t)((uint64_t)a2 & 07777);
    vfs_release(n);
    return 0;
}

static uint64_t sys_chown(uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5)
{
    (void)a4;(void)a5;
    char path[1024];
    if (copy_path(a1, path, sizeof(path)) == 0)
        return (uint64_t)-EFAULT;
    struct thread *t = sched_current();
    if (!t) return (uint64_t)-EPERM;
    vfs_ensure_proc();
    struct vnode *n = vfs_lookup(path, t->cwd);
    if (!n) return (uint64_t)-ENOENT;
    if (n->v_uid != t->euid && t->role != ROLE_SYSTEM && !(t->caps_prm & CAP_SYS_ADMIN))
    {
        vfs_release(n);
        return (uint64_t)-EPERM;
    }
    n->v_uid = (uid_t)a2;
    n->v_gid = (gid_t)a3;
    vfs_release(n);
    return 0;
}

void syscall_deliver_signals(void)
{
    struct thread *t = sched_current();
    if (!t || !t->syscall_iret)
        return;
    uint64_t pending = t->sig_pending & ~t->sig_mask;
    if (!pending)
        return;
    int sig = 0;
    for (int i = 1; i < NSIG; i++)
        if (pending & ((uint64_t)1 << i)) { sig = i; break; }
    if (!sig)
        return;

    t->sig_pending &= ~((uint64_t)1 << sig);
    struct sigaction *sa = &t->sig_actions[sig];

    if (sig == SIGKILL) { sched_exit(137); return; }

    if (sa->sa_handler == SIG_DFL)
    {
        if (sig != SIGCHLD && sig != SIGCONT)
            sched_exit(128 + sig);
        return;
    }
    if (sa->sa_handler == SIG_IGN)
        return;
    if (!sa->sa_restorer)
        return;

    uint64_t *frame = t->syscall_iret;
    uint64_t u_rsp = frame[3];

    size_t fsz = sizeof(struct sigframe);
    uint64_t new_rsp = u_rsp - fsz - 8;
    struct sigframe *sf = (struct sigframe *)(new_rsp + 8);

    sf->rax = frame[-1]; sf->rdi = frame[-2]; sf->rsi = frame[-3];
    sf->rdx = frame[-4]; sf->r10 = frame[-5]; sf->r8 = frame[-6];
    sf->r9  = frame[-7]; sf->rbx = frame[-8]; sf->rbp = frame[-9];
    sf->r12 = frame[-10]; sf->r13 = frame[-11]; sf->r14 = frame[-12];
    sf->r15 = frame[-13];
    sf->rip = frame[0]; sf->cs = frame[1]; sf->rflags = frame[2];
    sf->rsp = frame[3]; sf->ss = frame[4];
    sf->oldmask = t->sig_mask;

    *(uint64_t *)new_rsp = (uint64_t)sa->sa_restorer;

    t->sig_mask |= (1ULL << sig);

    frame[0] = (uint64_t)sa->sa_handler;
    frame[3] = new_rsp;
    frame[-2] = (uint64_t)sig;
}

/* ---- IPC channels (Phase 11) ---- */
#define IPC_CHANS 16
#define IPC_MSG_MAX 32
#define IPC_MSG_LEN 256

struct ipc_msg {
    uint8_t data[IPC_MSG_LEN];
    size_t len;
};

struct ipc_chan {
    int id;
    int used;
    struct ipc_msg msgs[IPC_MSG_MAX];
    int count;
    int head;
    struct thread *wait_recv;
};

static struct ipc_chan g_ipc[IPC_CHANS];
static int g_ipc_next_id;

static uint64_t sys_ipc_create(uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5)
{
    (void)a1; (void)a2; (void)a3; (void)a4; (void)a5;
    struct thread *t = sched_current();
    if (!t)
        return (uint64_t)(-1);
    int c = -1;
    for (int i = 0; i < IPC_CHANS; i++)
        if (!g_ipc[i].used) { c = i; break; }
    if (c < 0)
        return (uint64_t)(-1);
    g_ipc[c].used = 1;
    g_ipc[c].id = ++g_ipc_next_id;
    g_ipc[c].count = 0;
    g_ipc[c].head = 0;
    g_ipc[c].wait_recv = 0;

    int h = -1;
    for (int i = 0; i < IPC_MAX; i++)
        if (t->ipc[i] < 0) { h = i; break; }
    if (h < 0) { g_ipc[c].used = 0; return (uint64_t)(-1); }
    t->ipc[h] = c;
    return (uint64_t)h;
}

static uint64_t sys_ipc_send(uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5)
{
    (void)a4; (void)a5;
    int handle = (int)a1;
    const void *ubuf = (const void *)a2;
    size_t len = (size_t)a3;
    struct thread *t = sched_current();
    if (!t || handle < 0 || handle >= IPC_MAX)
        return (uint64_t)(-1);
    int c = t->ipc[handle];
    if (c < 0 || c >= IPC_CHANS || !g_ipc[c].used)
        return (uint64_t)(-1);
    struct ipc_chan *ch = &g_ipc[c];
    if (len > IPC_MSG_LEN)
        len = IPC_MSG_LEN;
    if (ch->count >= IPC_MSG_MAX)
        return (uint64_t)(-1);
    int tail = (ch->head + ch->count) % IPC_MSG_MAX;
    struct ipc_msg *m = &ch->msgs[tail];
    for (size_t i = 0; i < len; i++)
        m->data[i] = ((const uint8_t *)ubuf)[i];
    m->len = len;
    ch->count++;
    if (ch->wait_recv)
    {
        sched_wake(ch->wait_recv);
        ch->wait_recv = 0;
    }
    return (uint64_t)len;
}

static uint64_t sys_ipc_recv(uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5)
{
    (void)a4; (void)a5;
    int handle = (int)a1;
    void *ubuf = (void *)a2;
    size_t max = (size_t)a3;
    struct thread *t = sched_current();
    if (!t || handle < 0 || handle >= IPC_MAX)
        return (uint64_t)(-1);
    int c = t->ipc[handle];
    if (c < 0 || c >= IPC_CHANS || !g_ipc[c].used)
        return (uint64_t)(-1);
    struct ipc_chan *ch = &g_ipc[c];
    for (;;)
    {
        if (ch->count > 0)
        {
            struct ipc_msg *m = &ch->msgs[ch->head];
            size_t n = m->len < max ? m->len : max;
            for (size_t i = 0; i < n; i++)
                ((uint8_t *)ubuf)[i] = m->data[i];
            ch->head = (ch->head + 1) % IPC_MSG_MAX;
            ch->count--;
            return (uint64_t)n;
        }
        ch->wait_recv = t;
        t->state = THREAD_BLOCKED;
        sched_suspend();
    }
}

/* ---- shared memory (Phase 11) ---- */
#define SHM_MAX 16
#define SHM_BASE 0x50000000000ULL
#define SHM_SLOT 0x200000ULL

struct shm_obj {
    int id;
    int used;
    void *phys;
    uint64_t pages;
};
static struct shm_obj g_shm[SHM_MAX];
static int g_shm_next_id;

static uint64_t sys_shm_create(uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5)
{
    (void)a2; (void)a3; (void)a4; (void)a5;
    size_t bytes = (size_t)a1;
    if (bytes == 0)
        return (uint64_t)(-1);
    uint64_t pages = (bytes + PAGE_SIZE - 1) / PAGE_SIZE;
    int s = -1;
    for (int i = 0; i < SHM_MAX; i++)
        if (!g_shm[i].used) { s = i; break; }
    if (s < 0)
        return (uint64_t)(-1);
    void *phys = pmm_alloc_frames(pages);
    if (!phys)
        return (uint64_t)(-1);
    g_shm[s].used = 1;
    g_shm[s].id = ++g_shm_next_id;
    g_shm[s].phys = phys;
    g_shm[s].pages = pages;
    return (uint64_t)g_shm[s].id;
}

static uint64_t sys_shm_attach(uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5)
{
    (void)a2; (void)a3; (void)a4; (void)a5;
    int id = (int)a1;
    struct thread *t = sched_current();
    if (!t)
        return (uint64_t)(-1);
    int s = -1;
    for (int i = 0; i < SHM_MAX; i++)
        if (g_shm[i].used && g_shm[i].id == id) { s = i; break; }
    if (s < 0)
        return (uint64_t)(-1);
    uint64_t va = SHM_BASE + (uint64_t)(id - 1) * SHM_SLOT;
    uint64_t phys = (uint64_t)g_shm[s].phys;
    for (uint64_t p = 0; p < g_shm[s].pages; p++)
    {
        if (vmm_map_page_in(t->pml4, va + p * PAGE_SIZE, phys + p * PAGE_SIZE,
                             PTE_USER | PTE_WRITE) < 0)
            return (uint64_t)(-1);
    }
    return va;
}

static syscall_fn syscall_table[] = {
    [SYS_PRINT]  = sys_print,
    [SYS_YIELD]  = sys_yield,
    [SYS_EXIT]   = sys_exit,
    [SYS_FORK]   = sys_fork,
    [SYS_SPAWN]  = sys_spawn,
    [SYS_GETPID] = sys_getpid,
    [SYS_WAITPID] = sys_waitpid,
    [SYS_WRITE]  = sys_write,
    [SYS_READ]   = sys_read,
    [SYS_SPAWN_CMD] = sys_spawn_cmd,
    [SYS_PS]     = sys_ps,
    [SYS_OPEN]    = sys_open,
    [SYS_CLOSE]   = sys_close,
    [SYS_MKDIR]   = sys_mkdir,
    [SYS_UNLINK]  = sys_unlink,
    [SYS_READDIR] = sys_readdir,
    [SYS_CHDIR]   = sys_chdir,
    [SYS_GETCWD]  = sys_getcwd,
    [SYS_FSTAT]   = sys_fstat,
    [SYS_DUP2]    = sys_dup2,
    [SYS_PIPE]    = sys_pipe,
    [SYS_MOUNT]   = sys_mount,
    [SYS_UMOUNT]  = sys_umount,
    [SYS_KILL]    = sys_kill,
    [SYS_SIGACTION] = sys_sigaction,
    [SYS_SIGRETURN] = sys_sigreturn,
    [SYS_IPC_CREATE] = sys_ipc_create,
    [SYS_IPC_SEND] = sys_ipc_send,
    [SYS_IPC_RECV] = sys_ipc_recv,
    [SYS_SHM_CREATE] = sys_shm_create,
    [SYS_SHM_ATTACH] = sys_shm_attach,
    [SYS_MKFIFO]  = sys_mkfifo,
    [SYS_DRIVER_RESCAN] = sys_drv_rescan,
    [SYS_SOCKET_CREATE] = sys_socket_create,
    [SYS_SOCKET_BIND]   = sys_socket_bind,
    [SYS_SOCKET_CONNECT] = sys_socket_connect,
    [SYS_SOCKET_SEND]   = sys_socket_send,
    [SYS_SOCKET_RECV]   = sys_socket_recv,
    [SYS_SOCKET_CLOSE]  = sys_socket_close,
    [SYS_SOCKET_LISTEN] = sys_socket_listen,
    [SYS_SOCKET_ACCEPT] = sys_socket_accept,
    [SYS_GETUID]  = sys_getuid,
    [SYS_GETEUID] = sys_geteuid,
    [SYS_GETGID]  = sys_getgid,
    [SYS_GETEGID] = sys_getegid,
    [SYS_SETUID]  = sys_setuid,
    [SYS_SETGID]  = sys_setgid,
    [SYS_GETROLE] = sys_getrole,
    [SYS_CHMOD]   = sys_chmod,
    [SYS_CHOWN]   = sys_chown,
    [SYS_GETCAP]  = sys_getcap,
    [SYS_SETCAP]  = sys_setcap,
    [SYS_GETPWNAM] = sys_getpwnam,
    /* loadable kernel modules (.kxt) */
    [SYS_MODULE_LOAD]   = sys_module_load,
    [SYS_MODULE_UNLOAD] = sys_module_unload,
};
static int syscall_count = sizeof(syscall_table) / sizeof(syscall_fn);

uint64_t syscall_dispatch(uint64_t n, uint64_t a1, uint64_t a2, uint64_t a3,
                          uint64_t a4, uint64_t a5)
{
    if (n >= syscall_count || !syscall_table[n])
    {
        printk("SYS: unknown syscall %lu\n", n);
        return -1;
    }
    uint64_t ret = syscall_table[n](a1, a2, a3, a4, a5);
    syscall_deliver_signals();
    return ret;
}

void syscall_init(void)
{
    current_kstack_top = (uint64_t)syscall_stack_page + sizeof(syscall_stack_page);

    tss_set_rsp0(current_kstack_top);

    uint64_t star = 0;
    star |= (uint64_t)0x18 << 32;
    star |= (uint64_t)0x10 << 48;

    uint64_t lstar = (uint64_t)syscall_entry;
    uint64_t sfmask = 0x200;

    __asm__ volatile("wrmsr" : : "a"((uint32_t)star), "d"((uint32_t)(star >> 32)), "c"(0xC0000081));
    __asm__ volatile("wrmsr" : : "a"((uint32_t)lstar), "d"((uint32_t)(lstar >> 32)), "c"(0xC0000082));
    __asm__ volatile("wrmsr" : : "a"((uint32_t)sfmask), "d"((uint32_t)(sfmask >> 32)), "c"(0xC0000084));

    printk("Syscall: MSRs configured (entry=0x%lx)\n", lstar);
}
