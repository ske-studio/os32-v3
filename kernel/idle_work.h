#ifndef IDLE_WORK_H
#define IDLE_WORK_H

/* Run one pass only with interrupts enabled and outside IRQ context. */
void idle_work_run(void);
/* Register up to four distinct callbacks; returns 0 or OS32_ERR_*. */
int idle_work_register(void (*fn)(void));

/* Internal dispatcher adapter; callers use idle_work_run. */
unsigned int idle_work_compat_count(void);
void idle_work_compat_run(void);

#endif
