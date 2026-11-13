#ifndef POWERMAC_CONSOLE_H
#define POWERMAC_CONSOLE_H
#include "kernel/console/fbcon.h"
/* Resident KCG cache plus the existing VFS/USB and console allocations. */
#define PM_CONSOLE_HEAP_BYTES (1024u * 1024u)
/* Target and font must outlive the attachment. Detach before closing target. */
int pm_console_attach(struct gfx_console_target *, const KcgModuleApi *, unsigned);
void pm_console_detach(void);
void pm_console_set_colors(u8 fg, u8 bg);
void pm_console_stream_init(void);
#endif
