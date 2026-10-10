#ifndef R1_FIXTURE_H
#define R1_FIXTURE_H

#include "types.h"

/* BSS words set through emu_write_mem, never through a public KAPI. */
#define R1_FIXTURE_ARM 0x52314658U
#define R1_FIXTURE_IRQ_ALLOC 1
#define R1_FIXTURE_UD_FREE 2
#define R1_FIXTURE_V86_FAULT 3
#define R1_FIXTURE_V86_END 4
#define R1_FIXTURE_PARENT_POISON 5
#define R1_FIXTURE_LEASE_FAIL 6
#define R1_FIXTURE_PARKED_POISON 0 /* 5b; retain the h4b seven-word arm layout. */
#define R1_FIXTURE_SLOTS 7
#define R1_FIXTURE_UD_VECTOR 6

#ifdef OS32_R1_FIXTURE
extern volatile u32 r1_fixture_arm[R1_FIXTURE_SLOTS];
/* Write target id + AS generation before publishing the corresponding arm. */
extern volatile u32 r1_fixture_id[R1_FIXTURE_SLOTS];
extern volatile u32 r1_fixture_generation[R1_FIXTURE_SLOTS];
struct addrspace;
void r1_fixture_timer(void);
void r1_fixture_exception(u32 vector);
void r1_fixture_v86_end(void);
void r1_fixture_v86_ready(void);
void r1_fixture_parent(int child_id);
void r1_fixture_resume(int id);
int r1_fixture_lease(struct addrspace *as, u32 role);
#endif
#endif
