#ifndef OS32_FEP_SUSPEND_OWNER_H
#define OS32_FEP_SUSPEND_OWNER_H

void fep_suspend_owner_note(int on, int rc, int owner);
int fep_suspend_owner_take(int owner);

#endif
