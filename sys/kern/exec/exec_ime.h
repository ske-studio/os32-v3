#ifndef OS32_EXEC_IME_H
#define OS32_EXEC_IME_H
#include "exec_hooks.h"
#if OS32_HOOK_IME
#include "ime.h"
static inline void exec_ime_owner_exit(int owner) { ime_owner_exit(owner); }
#else
void exec_ime_owner_exit(int owner);
#endif
#endif
