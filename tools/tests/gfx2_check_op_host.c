#include "gfx/v2/gfx2_sw.h"
static struct gfx2_adapter a;
static int hw, calls, vis = 1, mode;
static int backend_check(void *ctx, const struct gfx2_exec *ex)
{ (void)ctx; (void)ex; calls++; return hw ? 0 : OS32_ERR_NOSYS; }
static const GfxBackendV2 be = { .check = backend_check };
struct gfx2_adapter *gfx2_adapter_get(u32 i) { return i ? 0 : &a; }
int gfx2_cpu_visible(const struct gfx2_surf *s) { (void)s; return vis; }
int gfx2_sync_get(const struct gfx2_adapter *p, u32 h, GfxSyncCtx **c, u32 *m)
{ (void)p; (void)h; *c = 0; *m = mode; return 0; }
static int probe(void)
{
    GFX2_Op op = {0}; struct gfx2_surf src = {0}, dst = {0};
    struct gfx2_exec ex = {0}; u32 how, i, f, p;
    a.be = &be; op.op = GFX2_OP_COPY; src.fmt = dst.fmt = GFX2_FMT_RGB565;
    src.pool = dst.pool = GFX2_POOL_SYSMEM; ex.op = &op; ex.src = &src; ex.dst = &dst;
    for (i = 1; i <= GFX2_OP_MAX; i++) for (f = 1; f <= 10; f++) for (p = 1; p <= 3; p++) {
        op.op = i; src.fmt = dst.fmt = f; src.pool = dst.pool = p;
        a.caps.vram_cpu_bytes = 1; hw = 0; vis = 1;
        if (gfx2_check_op(0, &op, f, f, p, p, &how)) return 1;
        /* Compare the same participating surfaces for the two paths. */
        ex.src = (i == 2 || i == 3 || i == 6 || i == 7 || i == 8) ? &src : 0;
        ex.dst = (i != 6 && i != 10) ? &dst : 0;
        { u32 actual; if (gfx2_check_exec(&a, &ex, &actual) || actual != how) return 2; }
    }
    op.op = GFX2_OP_COPY; src.fmt = dst.fmt = GFX2_FMT_RGB565; ex.src = &src; ex.dst = &dst;
    hw = 1; calls = 0; if (gfx2_check_exec(&a, &ex, &how) || how != GFX2_HOW_HW) return 3;
    a.state = GFX2_AD_LOST; calls = 0; if (gfx2_check_exec(&a, &ex, &how) || how != GFX2_HOW_SW || calls) return 4;
    a.state = GFX2_AD_OK; dst.flags = GFX2_SF_ALIAS; mode = GFXSYNC_SHADOW; calls = 0;
    if (gfx2_check_exec(&a, &ex, &how) || how != GFX2_HOW_SW || calls) return 5;
    dst.flags = 0; hw = 0; vis = 0; if (gfx2_check_exec(&a, &ex, &how) || how != GFX2_HOW_NOSUPPORT) return 6;
    src.fmt = GFX2_FMT_RGB565; dst.fmt = GFX2_FMT_INDEX8; hw = 1; calls = 0;
    if (gfx2_check_exec(&a, &ex, &how) || how != GFX2_HOW_NOSUPPORT || calls) return 7;
    op.op = 0; if (gfx2_check_exec(&a, &ex, &how) != OS32_ERR_INVAL) return 8;
    return 0;
}
void _start(void) { int rc = probe(); __asm__ volatile("int $0x80" : : "a"(1), "b"(rc)); for (;;) {} }
