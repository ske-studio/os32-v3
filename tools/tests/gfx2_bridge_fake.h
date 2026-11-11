/* Unrelated v1 regression harnesses do not link C14; C14 has its own real test. */
#ifndef TEST_GFX2_BRIDGE_FAKE_H
#define TEST_GFX2_BRIDGE_FAKE_H
#include "types.h"
__attribute__((weak)) int v1_bridge_begin(u32 role, u32 access)
{ (void)role; (void)access; return 0; }
__attribute__((weak)) void v1_bridge_end(u32 role, u32 access, u32 fence)
{ (void)role; (void)access; (void)fence; }
__attribute__((weak)) void v1_bridge_rebind(void) { }
#endif
