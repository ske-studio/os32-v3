/* HAL v2 T1: target, publication lifetime, failures and diagnostic overrides. */
#include "tools/tests/trident_device_fake.h"
#include "gfx/sync/sync_int.h"
static struct trident_fake f;
static struct trident_device d;
static u8 vram[TRI_FAKE_REAL_BYTES], client[128];
static struct trident_fake_event trace[128];
extern int tri_test_bind_rc, tri_test_write_rc, tri_test_publish_rc, tri_test_order_bad;
extern u32 tri_test_binds, tri_test_unbinds, tri_test_writes;
int tri_sync(struct trident_device *p) { return p ? 0 : TRI_E_INVAL; }
#define CHECK(c) do { if (!(c)) return __LINE__; } while (0)
static int test(void)
{
    CHECK(!trident_fake_reset(&f, vram, sizeof(vram), trace, 128));
    trident_fake_bind(&f, &d);
    f.resources.ram_fallback = true;
    f.resources.client = client; f.resources.client_bytes = sizeof(client);
    f.resources.pci.device = 0x9660;
    f.resources.generation = 1;
    d.declared_bytes = sizeof(vram); d.write_gap_lpt = 1000000;
    d.mode.client = (struct tri_surface){.bytes=128, .pitch=16, .width=8, .height=8, .cpp=1};
    d.mode.display = d.mode.client; d.mode.display.off = 32;
    CHECK(!trident_sync_bind(&d) && tri_test_binds == 1);
    GfxSyncCtx *c = d.sync_ctx;
    CHECK(c->t.va == vram + 32 && c->t.bytes == 128 && c->t.pitch == 16);
    CHECK(c->t.fmt == GFX2_FMT_INDEX8 && !c->t.head && c->t.opaque == &d);
    CHECK(c->t.sync_read && c->t.wait_port && c->t.flush && c->t.read32);
    CHECK(!trident_sync_bind(&d) && tri_test_binds == 1);
    CHECK(!tri_present_override(&d, TRI_PRESENT_DWORD_SYNC, 50));
    CHECK(c->cfg.method == GFXSYNC_M_SYNC_READ && c->cfg.unit == 4);
    CHECK(!tri_present_override(&d, TRI_PRESENT_DWORD_WAIT, 60));
    CHECK(c->cfg.method == GFXSYNC_M_WAIT_PORT && c->cfg.unit == 4);
    CHECK(!tri_present_override(&d, TRI_PRESENT_DWORD_GAP, 80));
    CHECK(c->cfg.method == GFXSYNC_M_GAP && c->cfg.gap_ns == 80);
    CHECK(tri_present_override(&d, 4, 80) == TRI_E_INVAL);
    CHECK(tri_present_override(&d, TRI_PRESENT_DWORD_GAP, 100001) != 0);
    CHECK(c->cfg.method == GFXSYNC_M_GAP && c->cfg.gap_ns == 80);
    tri_present_clear(&d);
    CHECK(c->cfg.method == GFXSYNC_M_PLAIN && c->cfg.unit == 1);
    tri_test_write_rc = -77;
    CHECK(trident_present(&d, 0, 0, 8, 8) == -77 && tri_test_writes == 1);
    tri_test_write_rc = 0;
    ++f.resources.generation;
    CHECK(!trident_sync_bind(&d) && tri_test_unbinds == 1 && tri_test_binds == 2);
    ++f.resources.generation; tri_test_bind_rc = -78;
    CHECK(trident_sync_bind(&d) == -78 && !d.sync_ctx && !d.sync_generation);
    CHECK(tri_test_unbinds == 2);
    tri_test_bind_rc = 0; tri_test_publish_rc = -79;
    CHECK(trident_sync_bind(&d) == -79 && !d.sync_ctx && !d.sync_generation);
    CHECK(tri_test_unbinds == 3);
    tri_test_publish_rc = 0;
    CHECK(!trident_sync_bind(&d));
    trident_sync_unbind(&d);
    CHECK(!d.sync_ctx && !d.sync_generation && tri_test_unbinds == 4);
    trident_sync_unbind(&d);
    CHECK(tri_test_unbinds == 4 && !tri_test_order_bad);
    return 0;
}
void _start(void)
{
    int rc = test();
    if (rc) { char line[4] = {'0'+rc/100%10,'0'+rc/10%10,'0'+rc%10,'\n'};
        __asm__ volatile("int $0x80" :: "a"(4), "b"(1), "c"(line), "d"(4) : "memory"); }
    __asm__ volatile("int $0x80" :: "a"(1), "b"(rc ? 1 : 0) : "memory");
    __builtin_unreachable();
}
