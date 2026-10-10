#ifndef R1_FIXTURE_H
#define R1_FIXTURE_H

#include "types.h"

/* BSS words set through emu_write_mem, never through a public KAPI. */
#define R1_FIXTURE_ARM 0x52314658U
#define R1_FIXTURE_IRQ_ALLOC 1
#define R1_FIXTURE_UD_FREE 2
#define R1_FIXTURE_V86_END 4
#define R1_FIXTURE_SLOTS 7
#define R1_FIXTURE_UD_VECTOR 6

#ifdef OS32_R1_FIXTURE
extern volatile u32 r1_fixture_arm[R1_FIXTURE_SLOTS];
void r1_fixture_timer(void);
void r1_fixture_exception(u32 vector);
void r1_fixture_v86_end(void);
#endif
#endif
