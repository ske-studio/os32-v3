#ifndef OS32_POWERMAC_KBOOT_LOG_H
#define OS32_POWERMAC_KBOOT_LOG_H
#include "types.h"
#define PM_LOG_SINK_MAX 2
#define PM_LOG_FATAL_WORDS 8
struct pm_log_sink { int (*write)(void *, const char *, u32); void *ctx; };
struct pm_log_stats { u32 failures, dropped; };
/* CPU0, initialization only, static lifetime. No registration during writes.
 * write returns 0 on success, nonzero on failure; LAN must only enqueue.
 * dropped counts bytes not confirmed delivered (including partial writes).
 * Counters saturate. No sink: bytes count in slot 0. */
int pm_log_add_sink(const struct pm_log_sink *sink);
void pm_log_write(const char *bytes, u32 n);
int pm_log_get_stats(u32 slot, struct pm_log_stats *out);
/* Both addresses are mapped I/G VAs, never raw PA. Caller has DR=1,
 * EE=0, and a live root; use the fixed ESCC aperture from design section 4.
 * Returns failure if unavailable or registry full. Owns a static sink. */
int pm_log_escc_init(u32 csr_alias, u32 data_alias);
/* Emergency path: no registry/heap. Caller supplies valid I/G aliases,
 * DR=1, EE=0 and readable words. With no root/alias, do not call.
 * At most PM_LOG_FATAL_WORDS words; returns after bounded output/drain. */
int pm_log_fatal_hex(u32 csr_alias, u32 data_alias,
                     const u32 *words, u32 n);
#endif
