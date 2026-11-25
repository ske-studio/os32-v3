/* Operation-local snapshots; adapter-owned cleanup follows table restoration. */
#include "internal.h"

int gf_init_save(struct gf_init_context *ctx, u32 bar, u32 off, u32 width, u32 old)
{
    if (ctx->saved_count == GF_INIT_SAVE_MAX) return -1;
    ctx->saved[ctx->saved_count++] = (struct gf_init_saved){
        .bar = bar, .offset = off, .width = width, .old = old,
        .select_offset = RECOVER_NONE
    };
    return 0;
}
int gf_init_save_indexed(struct gf_init_context *ctx, u32 bar, u32 select_off,
                         u32 data_off, u32 index, u32 old)
{
    if (ctx->saved_count == GF_INIT_SAVE_MAX) return -1;
    ctx->saved[ctx->saved_count++] = (struct gf_init_saved){
        .bar = bar, .offset = data_off, .width = 1, .old = old,
        .select_offset = select_off, .select_value = index
    };
    return 0;
}
void gf_init_commit(struct gf_init_context *ctx)
{
    ctx->saved_count = 0;
}
int gf_init_restore(struct gf_init_context *ctx)
{
    int rc = 0;
    for (u32 i = ctx->saved_count; i; i--) {
        const struct gf_init_saved *s = &ctx->saved[i - 1];
        if (s->select_offset != RECOVER_NONE &&
            ctx->io.write(ctx->io.opaque, s->bar, s->select_offset, 1, s->select_value)) {
            rc = -1;
            continue;
        }
        if (ctx->io.write(ctx->io.opaque, s->bar, s->offset, s->width, s->old)) rc = -1;
    }
    if (!rc) gf_init_commit(ctx);
    return rc;
}
