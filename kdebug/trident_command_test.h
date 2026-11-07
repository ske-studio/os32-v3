#ifndef KDEBUG_TRIDENT_COMMAND_TEST_H
#define KDEBUG_TRIDENT_COMMAND_TEST_H
#include "tri_command_wire.h"
/* Synchronous serial diagnostics through the current F16 production loan.
 * Destructive observations: CPU32 overwrites the first two visible rows;
 * cursor leaves HW hidden / requests SW fallback. MODES restores its saved
 * mode ID; MODE selects arg without restoring. POLL is one foreground tick.
 */
i32 trident_command_call(void *buf, u32 size);
#endif
