#ifndef OS32_EXEC_WAIT_CONTRACT_H
#define OS32_EXEC_WAIT_CONTRACT_H

/* Probe at an interruptible KAPI boundary before processing a pending abort. */
int ring3_wait_pending(void);
void ring3_abort_check(void);

#endif
