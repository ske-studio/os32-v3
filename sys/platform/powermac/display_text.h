#ifndef OS32_PM_DISPLAY_TEXT_H
#define OS32_PM_DISPLAY_TEXT_H
#include "types.h"
/* Stage H connects stage F: report root/font/target readiness together with
 * the adapter generation bound to the console. Clear target on detach. */
void pm_console_text_state(int root, int font, int target, u32 generation);
int pm_console_text_ready(void);
#endif
