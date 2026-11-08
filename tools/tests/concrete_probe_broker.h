#include "debug_probe.h"
int concrete_register(const struct probe_decl *, u32);
int concrete_arm(probe_hit_fn, u32);
int concrete_stop(void);
