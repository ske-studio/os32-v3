#include "sync_int.h"
GfxSyncCtx gfxsync_ctx_pool[4];
static struct gfxsync_cfg config;
static int lookup_rc;
int gfxsync_cfg_lookup(const char *key, struct gfxsync_cfg *out) { (void)key; *out = config; return lookup_rc; }
u32 gfxsync_gap_loops(u32 l, u32 n) { return l + n; }
u32 cpu_loops_per_tick(void) { return 7; }
void kprintf(int attr, const char *fmt, ...) { (void)attr; (void)fmt; }
static int hook(void *p) { (void)p; return 0; }
static int read_hook(void *p, u32 off, u32 *v) { (void)p; (void)off; (void)v; return 0; }
static int run(void)
{
    static u8 buf[64];
    GfxSyncTarget t = { .va = buf, .bytes = 64, .pitch = 8 };
    GfxSyncCtx *c[5]; char name[16];
    config = (struct gfxsync_cfg){ .mode=GFXSYNC_DIRECT, .unit=4, .read=GFXSYNC_R_DIRECT, .key="pci:1023:*" };
    t.read32 = read_hook;
    if (gfxsync_bind("x", &t, &c[0]) || gfxsync_mode(c[0]) != GFXSYNC_DIRECT) return 1;
    gfxsync_profile(c[0], name); if (name[0] != 'p') return 2;
    for (int i=1;i<4;i++) if (gfxsync_bind("x", &t, &c[i])) return 3;
    if (gfxsync_bind("x", &t, &c[4]) != OS32_ERR_FULL) return 4;
    for (int i=0;i<4;i++) gfxsync_unbind(c[i]);
    for (int i=0;i<5;i++) {
        struct gfxsync_cfg bad = config;
        if (i==0) bad.method=GFXSYNC_M_SYNC_READ;
        if (i==1) bad.method=GFXSYNC_M_WAIT_PORT;
        if (i==2) bad.flush=GFXSYNC_F_READBACK;
        if (i==3) bad.read=GFXSYNC_R_SHADOW;
        if (i==4) bad.unit=3;
        config=bad;
        if (gfxsync_bind("x", &t, &c[0]) || gfxsync_mode(c[0]) != GFXSYNC_SHADOW) return 5+i;
        gfxsync_profile(c[0], name); if (name[0]) return 10;
        gfxsync_unbind(c[0]);
        config=(struct gfxsync_cfg){ .mode=GFXSYNC_DIRECT, .unit=4, .read=GFXSYNC_R_DIRECT };
    }
    t.sync_read=hook; t.wait_port=hook; t.flush=hook;
    config.mode=GFXSYNC_SHADOW; t.read32=0;
    if (gfxsync_bind("x", &t, &c[0]) || c[0]->cfg.read != GFXSYNC_R_SHADOW) return 11;
    if (gfxsync_override(c[0], GFXSYNC_M_GAP, 4, 80) ||
        c[0]->cfg.method != GFXSYNC_M_GAP || c[0]->cfg.unit != 4 ||
        c[0]->gap_loops != 87) return 13;
    if (gfxsync_override(c[0], 256, 1, 0) != OS32_ERR_INVAL ||
        gfxsync_override(c[0], GFXSYNC_M_PLAIN, 3, 0) != OS32_ERR_INVAL ||
        gfxsync_override(c[0], GFXSYNC_M_PLAIN, 1, 100001) != OS32_ERR_INVAL ||
        c[0]->cfg.method != GFXSYNC_M_GAP || c[0]->gap_loops != 87) return 14;
    if (gfxsync_override(c[0], GFXSYNC_M_SYNC_READ, 2, 20)) return 15;
    gfxsync_override_clear(c[0]);
    if (c[0]->cfg.method != config.method || c[0]->cfg.unit != config.unit ||
        c[0]->cfg.gap_ns != config.gap_ns || c[0]->gap_loops != 7 + config.gap_ns) return 16;
    t.sync_read = 0;
    gfxsync_unbind(c[0]);
    if (gfxsync_override(c[0], 0, 1, 0) != OS32_ERR_INVAL ||
        gfxsync_override(0, 0, 1, 0) != OS32_ERR_INVAL) return 17;
    gfxsync_override_clear(c[0]); gfxsync_override_clear(0);
    if (gfxsync_bind("x", &t, &c[0])) return 18;
    if (gfxsync_override(c[0], GFXSYNC_M_SYNC_READ, 1, 0) != OS32_ERR_INVAL) return 19;
    gfxsync_unbind(c[0]); t.va=0;
    if (gfxsync_bind("x", &t, &c[0]) != OS32_ERR_INVAL) return 12;
    return 0;
}
void _start(void) { int rc=run(); __asm__ volatile("int $0x80" : : "a"(1), "b"(rc)); __builtin_unreachable(); }
