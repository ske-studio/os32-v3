#ifndef OS32_EXEC_SND_H
#define OS32_EXEC_SND_H
#include "exec_hooks.h"
#if OS32_HOOK_SND
#include "snd_engine.h"
static inline void exec_snd_owner_exit(int owner) { snd_owner_exit(owner); }
#else
void exec_snd_owner_exit(int owner);
#endif
#endif
