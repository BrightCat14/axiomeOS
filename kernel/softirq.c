#include "softirq.h"
#include "spinlock.h"

#define MAX_PENDING 64

struct work
{
    void (*handler)(void *);
    void *arg;
};

static struct work queue[MAX_PENDING];
static volatile int head, tail;
static spinlock_t lock = SPINLOCK_INIT;

void softirq_schedule(void (*handler)(void *), void *arg)
{
    if (!handler)
        return;

    spin_lock(&lock);
    int next = (head + 1) % MAX_PENDING;
    if (next != tail)
    {
        queue[head].handler = handler;
        queue[head].arg = arg;
        head = next;
    }
    spin_unlock(&lock);
}

void softirq_poll(void)
{
    while (tail != head)
    {
        spin_lock(&lock);
        void (*h)(void *) = queue[tail].handler;
        void *a = queue[tail].arg;
        tail = (tail + 1) % MAX_PENDING;
        spin_unlock(&lock);

        if (h)
            h(a);
    }
}
