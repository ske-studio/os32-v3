#ifndef DEBUG_PROBE_SNAPSHOT_H
#define DEBUG_PROBE_SNAPSHOT_H

#include "debug_probe.h"

/* Fill a caller-owned buffer with the currently visible probe points. */
void debug_probe_snapshot(struct probe_list *list);

#endif
