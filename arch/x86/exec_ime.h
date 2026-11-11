#ifndef OS32_EXEC_IME_H
#define OS32_EXEC_IME_H
#include "ime.h"
static inline void exec_ime_owner_exit(int owner) { ime_owner_exit(owner); }
#define ime_owner_exit exec_ime_owner_exit
#endif
