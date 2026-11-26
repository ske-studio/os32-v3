#ifndef OS32_SQLITE_STACK_CONTRACT_H
#define OS32_SQLITE_STACK_CONTRACT_H

#include "types.h"

void sqlite_call_on_alt_stack(void (*call)(void *), u32 top, void *frame);

#endif
