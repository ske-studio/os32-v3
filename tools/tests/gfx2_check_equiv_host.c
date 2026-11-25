/* Mainline PM 11-23 and OUT.md, final regression cases 1-3 (PEGC and OF). */
#define _start i1_start
#include "gfx2_i1_host.c"
#undef _start

#include "../../sys/dev/video/of_fb/backend_of_fb.h"

#include "of_fb_osri_fake.h"

void *kmalloc(u32 n) { (void)n; return 0; }
void kfree(void *p) { (void)p; }

extern const GfxBackendV2 pegc_backend_v2;
GfxBackend gfx_backend_pegc;
GfxCounters gfx_counters;
int vram_scroll_y;

static u8 pixels[3][2], data[8];
static struct gfx2_adapter *adapter = &gfx2_adapters[0];
int ring3_call_from_user(void) { return 0; }
int ring3_user_range_ok(u32 p, u32 n) { return p && n; }
int ring3_user_range_writable(u32 p, u32 n) { return p && n; }
static u32 query(const GFX2_Op *op)
{
    u32 how;
    CHECK(!gfx2_check_op(0, op, GFX2_FMT_INDEX8, GFX2_FMT_INDEX8,
                         GFX2_POOL_SYSMEM, GFX2_POOL_SYSMEM, &how));
    return how;
}

static int submit(GFX2_Op *ops, u32 count)
{
    u32 done, fence;
    int rc = gfx2_submit(0, ops, count, adapter->gen, &done, &fence);
    CHECK(done == (rc ? 0 : count));
    return rc;
}

static void exercise(struct gfx2_surf *dst)
{
    u32 d = dst->sid, src = 1;
    u32 buf = (u32)(uptr)data;
    u32 copy = GFX2_ROP_SRCCOPY, fill = GFX2_ROP_PATCOPY;
    struct {
        GFX2_Op op;
        u8 left, right;
    } draws[] = {
        {{.op=GFX2_OP_FILL,.dst=d,.dw=2,.dh=1,.fg={.v=5},.rop=fill}, 5,5},
        {{.op=GFX2_OP_LINE,.dst=d,.sx=1,.fg={.v=5},.rop=fill}, 5,0},
        {{.op=GFX2_OP_COPY,.dst=d,.src=src,.sw=2,.dw=2,.sh=1,.dh=1,.rop=copy}, 7,8},
        {{.op=GFX2_OP_MONO,.dst=d,.dw=2,.dh=1,.data=buf,.pitch=1,.fg={.v=3},.bg={.v=4}}, 3,4},
        {{.op=GFX2_OP_HOSTDATA,.dst=d,.dw=2,.dh=1,.data=buf,.pitch=2}, 128,9},
        {{.op=GFX2_OP_FILL,.dst=d,.dw=2,.dh=1,.rop=0xff}, 255,255},
        {{.op=GFX2_OP_ROP4,.dst=d,.src=src,.sw=2,.dw=2,.sh=1,.dh=1,.rop=copy,
          .pitch=1,.data=buf}, 7,0},
        {{.op=GFX2_OP_STRETCH,.dst=d,.src=src,.sw=1,.dw=2,.sh=1,.dh=1,.rop=copy}, 7,7},
        {{.op=GFX2_OP_COPY,.dst=d,.src=src,.sw=2,.dw=2,.sh=1,.dh=1,.rop=copy,
          .flags=GFX2_F_SRCKEY,.skey={.v=7,.x=7}}, 0,8},
    };
    data[0]=128; data[1]=9;
    for (u32 i=0; i<sizeof(draws)/sizeof(draws[0]); ++i) {
        dst->kva[0]=dst->kva[1]=0;
        dst->dirty=(struct gfx2_rect){0};
        CHECK(query(&draws[i].op) == GFX2_HOW_SW);
        int rc = submit(&draws[i].op, 1);
        CHECK(dst->kva[0] == draws[i].left && dst->kva[1] == draws[i].right);
        CHECK(!(dst->flags & GFX2_SF_ALIAS) || (dst->dirty.w && dst->dirty.h));
        CHECK(!rc);
    }

    GFX2_Op pattern[] = {
        {.op=GFX2_OP_SET_PATTERN,.data=buf,.fg={.v=21},.bg={.v=22}},
        {.op=GFX2_OP_FILL,.dst=d,.dw=2,.dh=1,.flags=GFX2_F_PAT,.rop=fill},
    };
    dst->dirty=(struct gfx2_rect){0};
    CHECK(query(pattern) == GFX2_HOW_SW && !submit(pattern, 1));
    CHECK(!dst->kva[0] && dst->kva[1]==8 && !dst->dirty.w);
    CHECK(query(&pattern[1]) == GFX2_HOW_SW && !submit(pattern, 2));
    CHECK(dst->kva[0]==21 && dst->kva[1]==22);
    CHECK(!(dst->flags & GFX2_SF_ALIAS) || dst->dirty.w);

    dst->dirty=(struct gfx2_rect){0};
    GFX2_Op rb = {.op=GFX2_OP_READBACK,.src=d,.sw=2,.sh=1,.data=buf,.pitch=2};
    CHECK(query(&rb) == GFX2_HOW_SW && !submit(&rb, 1));
    CHECK(data[0]==21 && data[1]==22 && dst->kva[0]==21 && dst->kva[1]==22 && !dst->dirty.w);
    GFX2_Op filter = {.op=GFX2_OP_STRETCH,.src=src,.dst=d,.sw=1,.dw=2,.sh=1,.dh=1,
                      .rop=copy,.flags=GFX2_F_FILTER};
    CHECK(query(&filter) == GFX2_HOW_NOSUPPORT);
    CHECK(submit(&filter, 1) == OS32_ERR_NOSYS && dst->kva[0]==21 && dst->kva[1]==22);
    GFX2_Op alpha = {.op=GFX2_OP_ALPHA,.src=src,.dst=d,.sw=2,.dw=2,.sh=1,.dh=1,.fg={.x=255}};
    CHECK(query(&alpha) == GFX2_HOW_NOSUPPORT);
    CHECK(submit(&alpha, 1) == OS32_ERR_NOSYS && dst->kva[0]==21 && dst->kva[1]==22);
}

void _start(void)
{
    as.pd_phys=1; as.owner=7;
    for (u32 i=0; i<3; i++)
        gfx2_surfs[i+1]=(struct gfx2_surf){.sid=i+1,.owner=as.owner,.w=2,.h=1,.pitch=2,
            .fmt=GFX2_FMT_INDEX8,.state=GFX2_ST_IDLE,.pool=GFX2_POOL_SYSMEM,.kva=pixels[i]};
    gfx2_surfs[3].role=GFX2_ROLE_DISPLAY;
    gfx2_surfs[3].flags=GFX2_SF_ALIAS;
    gfx2_surfs[3].owner=GFX2_OWNER_SCREEN;
    pixels[0][0]=7; pixels[0][1]=8;
    const GfxBackendV2 *backends[] = {&pegc_backend_v2, &of_fb_v2};
    const u32 kinds[] = {GFX2_KIND_PEGC, GFX2_KIND_OF_FB};
    for (u32 i = 0; i < 2; ++i) {
        *adapter = (struct gfx2_adapter){.be=backends[i], .ctx=backends[i]->ctx,
            .v1=backends[i]->ctx, .kind=kinds[i], .heads=1, .flags=GFX2_AF_ACTIVE};
        GfxSyncCtx ctx = {.used=1, .cfg={.mode=GFXSYNC_SHADOW}};
        CHECK(!gfx2_sync_publish(adapter->kind, 0, &ctx));
        exercise(&gfx2_surfs[2]);
        exercise(&gfx2_surfs[3]);
        CHECK(!gfx2_sync_publish(adapter->kind, 0, NULL));
    }
    SAY("GREEN PEGC and OF INDEX8 SYSMEM and SHADOW DISPLAY");
    die(0);
}
