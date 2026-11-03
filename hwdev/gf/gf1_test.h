/* Internal foreground dispatch shared by diagnostic owner and module. */
#ifndef HWDEV_GF1_TEST_H
#define HWDEV_GF1_TEST_H
#include "foundation.h"
enum gf1_test_op { GF1T_MASTER, GF1T_START, GF1T_STOP, GF1T_SNAPSHOT,
                  GF1T_HEADS, GF1T_INST_PAGE, GF1T_REVOKE,
                  GF1T_DISPLAY_STOP, GF1T_DISPLAY_START };
struct gf1_test_request { u32 size, op, heads; };
struct gf1_test_reply {
    int rc;
    u32 command, pmc, flags, bytes, bad_off;
    struct gf_irq_stats stats;
};
struct gf1_test_ops {
    struct gf_foundation *production;
    /* Caller lends the diagnostic lease bus in f->io.opaque, with
     * bus.foundation=f. Both survive until successful stop and release. */
    int (*prepare)(struct gf_foundation *, const struct recover_bar_extent *, u32);
    int (*call)(struct gf_foundation *, const struct gf1_test_request *, struct gf1_test_reply *);
    const struct gf2_test_ops *command;
};
extern struct gf1_test_ops gf1_module_ops;
#endif
