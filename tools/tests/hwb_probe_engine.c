/* Keep the stage B fake broker/bus and real engine in a separate TU. */
#define _start probe_engine_tests
#include "probe_host.c"
#undef _start
int hwb_engine_call(struct probe_wire *w, u32 bytes) { return probe_call(w,bytes); }
void hwb_engine_reset(void) { test_point_name=0; reset(); }
void hwb_engine_hit(void) { hit(0); }
u32 hwb_engine_used(void) { return used; }
u32 hwb_engine_prints(void) { return prints; }
void hwb_engine_epoch(void) { unregister_fake(); }
void hwb_engine_long_name(void) { test_point_name="abcdefghijklmnopqrstuvwxyz12345"; }
