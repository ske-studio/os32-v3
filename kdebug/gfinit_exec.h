#ifndef KDEBUG_GFINIT_EXEC_H
#define KDEBUG_GFINIT_EXEC_H
#include "../hwdev/gf/internal.h"
struct gfinit_exec {
    const u8 *rom;
    u32 rom_size, main_id;
    const struct gfinit_exec_ops *ops;
    void *opaque;
    const struct gf_init_program *program;
    struct gf_init_context init;
    struct recover *recover;
    u32 steps, fail_pc, substep;
    bool executing;
};
/* trial_step approves exactly one bound main marker. */
int gfinit_exec_run(struct recover *r, void *arg);
int gfinit_exec_prepare(struct recover *r, void *arg);
/* Called by the owner's local_cleanup BEFORE quiesce/S0, also on cancellation. */
int gfinit_exec_restore(struct gfinit_exec *e);
#endif
