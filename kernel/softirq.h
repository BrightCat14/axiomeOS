#ifndef AXIOME_SOFTIRQ_H
#define AXIOME_SOFTIRQ_H

void softirq_schedule(void (*handler)(void *), void *arg);
void softirq_poll(void);

#endif
