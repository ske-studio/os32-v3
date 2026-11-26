#ifndef OS32_FONT_FAULT_CONTRACT_H
#define OS32_FONT_FAULT_CONTRACT_H

#include "types.h"

void font_guard_fault(u32 vector, u32 eip, u32 address) __attribute__((noreturn));

#endif
