#ifndef POWERMAC_CONSOLE_H
#define POWERMAC_CONSOLE_H
#include "kernel/console/fbcon.h"
/* Target and font must outlive the attachment. Detach before closing target. */
int pm_console_attach(struct gfx_console_target *, const KcgModuleApi *, unsigned);
void pm_console_detach(void);
#endif
