/* I2 guest acceptance: SW pixels, fences, U1 and the v1 CLIENT bridge. */
#include "v2/os32gfx2u.h"
#include "libgfx_attach_internal.h"
#include <string.h>

#define ATTR 0x07
#define SIDE 64u
#define ROWS 4u
#define WAIT_MS 1000u
static int exercise(KernelAPI *api, u32 adapter, u32 gen);
static u32 crc(const u8 *p, u32 n);
static int fail(KernelAPI *api, const char *stage, int rc);

/* OS32X requires main to be the first function. Run from the CUI shell. */
int main(int argc, char **argv, KernelAPI *api)
{
    u32 adapter, gen;
    int rc = 1;
    (void)argc; (void)argv;
    api->gfx_init();
    for (adapter = 0; adapter < 4; adapter++) {
        u32 sid;
        if (!api->gfx2_screen_surface(adapter, 0, GFX2_ROLE_CLIENT, &sid)) break;
    }
    if (adapter == 4) fail(api, "active-client", 1);
    else if ((rc = api->gfx2_adapter_gen(adapter, &gen))) fail(api, "generation", rc);
    else rc = exercise(api, adapter, gen);
    api->gfx_shutdown();
    api->kprintf(ATTR, "gfx2_i2 %s rc=%d\n", rc ? "FAIL" : "PASS", rc);
    return rc ? 1 : 0;
}

static int fail(KernelAPI *api, const char *stage, int rc)
{
    api->kprintf(ATTR, "gfx2_i2 stage=%s rc=%d\n", stage, rc);
    return 1;
}

static u32 crc(const u8 *p, u32 n)
{
    u32 c = ~0u;
    for (u32 i = 0; i < n; i++) {
        c ^= p[i];
        for (u32 bit = 0; bit < 8; bit++) c = (c >> 1) ^ (0xedb88320u & (0u - (c & 1)));
    }
    return ~c;
}

static int exercise(KernelAPI *api, u32 adapter, u32 gen)
{
    u32 sid = 0, done, fence, completed, failed, packed, pitch;
    u32 pixels[SIDE * ROWS], expected[SIDE * ROWS];
    u8 screen[SIDE * ROWS], wanted[SIDE * ROWS];
    GFX2_SurfaceDesc desc;
    GFX2_Op ops[3];
    MemStat before, after;
    int rc, result = 1;
    void *va;
#define CALL(stage, call) do { rc = (call); if (rc) { fail(api, stage, rc); goto out; } } while (0)
#define REQUIRE(stage, condition) do { if (!(condition)) { fail(api, stage, 1); goto out; } } while (0)
    CALL("color", gfx2_color_pack(GFX2_FMT_XRGB8888, 0xff123456, 0, &packed));
    CALL("create", api->gfx2_surface_create(adapter, SIDE, ROWS, GFX2_FMT_XRGB8888,
                                           GFX2_POOL_SYSMEM, GFX2_USE_RENDER2D, &sid));
    memset(ops, 0, sizeof(ops));
    ops[0] = (GFX2_Op){.op=GFX2_OP_FILL, .dst=sid, .dw=SIDE, .dh=ROWS,
                       .rop=GFX2_ROP_PATCOPY, .fg={.v=packed}};
    ops[1] = (GFX2_Op){.op=GFX2_OP_LINE, .dst=sid, .sx=4,
                       .rop=GFX2_ROP_PATCOPY, .fg={.v=0xffabcdef}};
    ops[2] = (GFX2_Op){.op=GFX2_OP_FILL, .dst=sid, .dx=4, .dw=1, .dh=1,
                       .rop=GFX2_ROP_PATCOPY, .fg={.v=packed}};
    CALL("submit", api->gfx2_submit(adapter, ops, 3, gen, &done, &fence));
    REQUIRE("done", done == 3);
    CALL("fence-wait", api->gfx2_fence_wait(adapter, fence, WAIT_MS));
    CALL("fence-query", api->gfx2_fence_query(adapter, &completed, &failed));
    REQUIRE("fence-completed", (!failed || (i32)(failed - fence) < 0) && (!fence || (i32)(completed - fence) >= 0));
    ops[0] = (GFX2_Op){.op=GFX2_OP_READBACK, .src=sid, .sw=SIDE, .sh=ROWS,
                       .pitch=SIDE * sizeof(u32), .data=(u32)pixels};
    CALL("readback", api->gfx2_submit(adapter, ops, 1, gen, &done, &fence));
    REQUIRE("readback-done", done == 1);
    for (u32 i = 0; i < SIDE * ROWS; i++) expected[i] = i < 4 ? 0xffabcdefu : packed;
    api->kprintf(ATTR, "gfx2_i2 SW crc=%x expected=%x\n", crc((u8 *)pixels, sizeof(pixels)),
                 crc((u8 *)expected, sizeof(expected)));
    REQUIRE("pixels", !memcmp(pixels, expected, sizeof(pixels)));
    /* Verify the real user mapping and audit counters after the drawing. */
    rc = api->mem_stat(0, &before, sizeof(before));
    REQUIRE("mem-before", rc == (int)sizeof(before));
    CALL("lock", api->gfx2_lock(sid, GFX2_LOCK_READONLY, &va, &pitch));
    int same = 1;
    for (u32 y = 0; y < ROWS; y++)
        if (memcmp((u8 *)va + y * pitch, expected + y * SIDE, SIDE * sizeof(u32))) same = 0;
    rc = api->gfx2_unlock(sid);
    CALL("unlock", rc);
    REQUIRE("mapped-pixels", same);
    rc = api->mem_stat(0, &after, sizeof(after));
    REQUIRE("mem-after", rc == (int)sizeof(after) && after.audit_fail == before.audit_fail);
    /* Interleave v2 writes to the real CLIENT and v1 SHADOW fill/present.
     * NP21/W PEGC is INDEX8. The CRC includes both writers' pixels. */
    u32 client;
    CALL("client", api->gfx2_screen_surface(adapter, 0, GFX2_ROLE_CLIENT, &client));
    CALL("client-desc", api->gfx2_surface_desc(client, &desc));
    REQUIRE("client-index8", desc.fmt == GFX2_FMT_INDEX8 && desc.w >= SIDE && desc.h >= ROWS);
    memset(wanted, 5, sizeof(wanted));
    ops[0] = (GFX2_Op){.op=GFX2_OP_HOSTDATA, .dst=client, .dw=SIDE, .dh=ROWS,
                       .pitch=SIDE, .data=(u32)wanted};
    CALL("client-upload", api->gfx2_submit(adapter, ops, 1, gen, &done, &fence));
    REQUIRE("client-done", done == 1);
    gfx_api = api;
    CALL("v1-attach", libos32gfx_attach_checked());
    gfx_fill_rect(0, 0, 4, 1, 7);
    libos32gfx_detach();
    api->gfx_present_rect(0, 0, SIDE, ROWS);
    memset(wanted, 7, 4);
    ops[0] = (GFX2_Op){.op=GFX2_OP_READBACK, .src=client, .sw=SIDE, .sh=ROWS,
                       .pitch=SIDE, .data=(u32)screen};
    CALL("client-readback", api->gfx2_submit(adapter, ops, 1, gen, &done, &fence));
    api->kprintf(ATTR, "gfx2_i2 bridge crc=%x expected=%x\n", crc(screen, sizeof(screen)),
                 crc(wanted, sizeof(wanted)));
    REQUIRE("bridge-pixels", done == 1 && !memcmp(screen, wanted, sizeof(screen)));
    u32 display;
    CALL("display", api->gfx2_screen_surface(adapter, 0, GFX2_ROLE_DISPLAY, &display));
    ops[0].src = display;
    CALL("display-readback", api->gfx2_submit(adapter, ops, 1, gen, &done, &fence));
    api->kprintf(ATTR, "gfx2_i2 screen crc=%x expected=%x\n", crc(screen, sizeof(screen)),
                 crc(wanted, sizeof(wanted)));
    REQUIRE("screen-pixels", done == 1 && !memcmp(screen, wanted, sizeof(screen)));
    /* Reinitialization must invalidate the borrowed sid. */
    api->gfx_init_200();
    REQUIRE("alias-stale", api->gfx2_surface_desc(client, &desc) == OS32_ERR_STALE);
    result = 0;
out:
    if (sid && (rc = api->gfx2_surface_free(sid))) result = fail(api, "free", rc);
    return result;
#undef CALL
#undef REQUIRE
}
