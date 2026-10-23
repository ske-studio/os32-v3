#ifndef KDEBUG_RECOVER_VIRTUAL_H
#define KDEBUG_RECOVER_VIRTUAL_H
#include "recover.h"
#include "recover_wire.h"
int recover_virtual_call(const KdebugRecoverVirtual *q, u32 hz);
void recover_virtual_detach(void);
#endif
