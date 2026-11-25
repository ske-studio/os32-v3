#ifndef OS32_EXEC_WAIT_CONTRACT_H
#define OS32_EXEC_WAIT_CONTRACT_H

/* Probe at an interruptible KAPI boundary before processing a pending abort. */
int ring3_wait_pending(void);
void ring3_abort_check(void);

/* Cooperative timed waits restart their syscall after WM resume. */
unsigned int exec_wait_begin(unsigned int now);
int exec_wait_can_park(void);
void exec_wait_park(unsigned int begin);

#endif
