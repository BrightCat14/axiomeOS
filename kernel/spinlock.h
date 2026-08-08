#ifndef AXIOME_SPINLOCK_H
#define AXIOME_SPINLOCK_H

#include "hal/cshim.h"

typedef struct
{
    volatile int locked;
} spinlock_t;

#define SPINLOCK_INIT {0}

static inline void spin_lock(spinlock_t *lock)
{
    while (__sync_lock_test_and_set(&lock->locked, 1))
        while (lock->locked)
            hal_cpu_pause();
}

static inline void spin_unlock(spinlock_t *lock)
{
    __sync_lock_release(&lock->locked);
}

static inline int spin_trylock(spinlock_t *lock)
{
    return !__sync_lock_test_and_set(&lock->locked, 1);
}

/* Critical sections: save/restore the interrupt state through the HAL so the
   headr stays architecture-neutral (x86: pushfq/cli + popfq; ARM64: DAIF). */
static inline unsigned long spin_lock_irq(spinlock_t *lock)
{
    unsigned long flags = hal_cpu_save_irq();
    spin_lock(lock);
    return flags;
}

static inline void spin_unlock_irq(spinlock_t *lock, unsigned long flags)
{
    spin_unlock(lock);
    hal_cpu_restore_irq(flags);
}

#endif
