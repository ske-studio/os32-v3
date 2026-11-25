#ifndef PM_HWB_WINDOW_H
#define PM_HWB_WINDOW_H
#include "types.h"
#include "ppc_pd.h"
void pm_hwb_window_init(void);
int pm_hwb_window_transfer(u32 pa, u32 width, int little, int store,
                           u32 count, u32 increment, u32 *value);
int paging_window_reserve(void);
int paging_window_release(void);
#endif
