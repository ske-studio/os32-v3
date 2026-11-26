#ifndef RECOVER_WINDOW_CONTRACT_H
#define RECOVER_WINDOW_CONTRACT_H
#include "types.h"
#include <stdbool.h>

#define RECOVER_WINDOW_PAGES 2U /* a peek chunk (< one page) spans at most two PTEs */
/* T2 primitives: accessors are injectable; only local_cleanup owns release.
 * T2 maps one aligned datum per short window, peek one chunk (kdebug_window);
 * IRQs never span a script or wait. */
struct recover_window_ops {
    u32 (*irq_save)(void *opaque);
    void (*irq_restore)(void *opaque, u32 flags);
    u32 (*pte_read)(void *opaque, u32 va);
    int (*map)(void *opaque, u32 va, u32 pa, u32 pages, u32 flags);
    int (*unmap)(void *opaque, u32 va, u32 raw);
    int (*load)(void *opaque, u32 va, u32 width, u32 *value);
    int (*store)(void *opaque, u32 va, u32 width, u32 value);
};
struct recover_window {
    const struct recover_window_ops *ops;
    void *opaque;
    u32 va, raw[RECOVER_WINDOW_PAGES], flags, pages;
    bool owned, failed;
};
void recover_window_hal_init(struct recover_window *w);
int recover_window_cleanup(struct recover_window *w);
int recover_window_access(struct recover_window *w, u32 pa, u32 width,
                          bool write, u32 *value);

#endif
