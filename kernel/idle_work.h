#ifndef IDLE_WORK_H
#define IDLE_WORK_H

/* Run one pass only with interrupts enabled and outside IRQ context. */
void idle_work_run(void);
/* Register up to four distinct callbacks; returns 0 or OS32_ERR_*. */
int idle_work_register(void (*fn)(void));

#endif
