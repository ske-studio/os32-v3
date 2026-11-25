/* Private declaration table, shared with the module export. */
#ifndef TRIDENT_PROBES_H
#define TRIDENT_PROBES_H
#include "debug_probe.h"
i32 trident_get_probe_decls(const struct probe_decl **table, u32 *count);
#endif
