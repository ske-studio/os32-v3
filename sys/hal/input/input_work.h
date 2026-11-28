#ifndef INPUT_WORK_H
#define INPUT_WORK_H
#include "keymap.h"
struct io_slice;
int input_work_register(void);
int input_step(struct io_slice *slice);
void input_work_invalidate_locked(void);
void input_work_release_locked(u8 src, int usage);
int input_work_repeat_locked(const KmKey *key, const KmEvalResult *result,
                             u8 raw_due, u8 mods, KmState state, u8 consumed, u32 time_ms);
#endif
