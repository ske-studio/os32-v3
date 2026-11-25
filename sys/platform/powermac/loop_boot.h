#pragma once

#if OS32_FEATURE_LOOP
void pm_loop_boot(void);
#else
static inline void pm_loop_boot(void) {}
#endif
