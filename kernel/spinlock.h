#ifndef AXIOME_SPINLOCK_H
#define AXIOME_SPINLOCK_H

typedef struct
{
    volatile int locked;
} spinlock_t;

#define SPINLOCK_INIT {0}

static inline void spin_lock(spinlock_t *lock)
{
    while (__sync_lock_test_and_set(&lock->locked, 1))
        while (lock->locked)
            __asm__ volatile("pause");
}

static inline void spin_unlock(spinlock_t *lock)
{
    __sync_lock_release(&lock->locked);
}

static inline int spin_trylock(spinlock_t *lock)
{
    return !__sync_lock_test_and_set(&lock->locked, 1);
}

static inline unsigned long spin_lock_irq(spinlock_t *lock)
{
    unsigned long flags;
    __asm__ volatile("pushfq; popq %0; cli" : "=r"(flags));
    spin_lock(lock);
    return flags;
}

static inline void spin_unlock_irq(spinlock_t *lock, unsigned long flags)
{
    spin_unlock(lock);
    __asm__ volatile("pushq %0; popfq" : : "r"(flags) : "cc", "memory");
}

#endif
