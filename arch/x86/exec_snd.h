#ifndef OS32_EXEC_SND_H
#define OS32_EXEC_SND_H
#include "snd_engine.h"
static inline void exec_snd_owner_exit(int owner) { snd_owner_exit(owner); }
#define snd_owner_exit exec_snd_owner_exit
#endif
