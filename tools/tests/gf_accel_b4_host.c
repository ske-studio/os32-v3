/* B4 packet and alias admission tests, freestanding host fixture. */
#define GF3_HOST_UNIT 4
#include "gf_accel_host.h"

static struct gf3_host h;
static struct gf3_host_event events[128];
static struct gf3_host_packet packets[8];
static u32 words[32];
static struct gf_vram_reservation_node n1, n2;
static struct gf3_surface a = {0,64,16,16,4,GF3_INDEX8,1,1};
static const struct gf3_surface *expected_src, *expected_dst;
static struct gf3_surface saved_src, saved_dst;
#define DL 1000U

static void observe(struct gf3_host *host, const struct gf3_host_event *e)
{
    (void)host;
    if (e->op == H_SURFACE_SET) {
        assert(e->p[0] == &h.ctx && e->p[1] == expected_src && e->p[2] == expected_dst);
        assert(!e->a[0] && e->a[1] == DL);
        assert(!gf2_host_memcmp(e->p[1], &saved_src, sizeof(saved_src)));
        assert(!gf2_host_memcmp(e->p[2], &saved_dst, sizeof(saved_dst)));
    }
    if (e->op == H_ROP) {
        assert(e->p[0] == &h.ctx && e->a[0] == GF3_SUBC_BLIT);
        assert(e->a[1] == 3 && e->a[2] == 255 && e->a[3] == GF3_INDEX8 && e->a[4] == DL);
    }
}
static void clear(void) { gf3_host_trace_clear(&h); }
static int copy(const struct gf3_surface *s, const struct gf3_surface *d,
                struct gf3_rect r, u32 x, u32 y)
{
    expected_src = s; expected_dst = d; saved_src = *s; saved_dst = *d;
    return gf3_copy(&h.ctx, s, d, r, x, y, 3, 255, DL);
}
static void no_submit(void)
{
    assert(!h.calls[H_ROP] && !h.calls[H_SURFACE_SET] && !h.calls[H_EMIT]);
    assert(!h.calls[H_FENCE] && !h.calls[H_WAIT]);
}
static void packet(u32 in, u32 out, u32 size)
{
    assert(h.calls[H_ROP] == 1 && h.calls[H_SURFACE_SET] == 1);
    assert(h.npackets == 1 && h.nwords == 3);
    assert(packets[0].subc == GF3_SUBC_BLIT && packets[0].method == 0x300);
    assert(!packets[0].noninc && packets[0].count == 3 && packets[0].deadline == DL);
    assert(words[0] == in && words[1] == out && words[2] == size);
    assert(h.calls[H_FENCE] == 1 && h.calls[H_WAIT] == 1);
}
static void rejected(struct gf3_surface b, int rc)
{
    clear(); assert(copy(&a, &b, (struct gf3_rect){0,0,8,2}, 0, 0) == rc);
    assert(h.ctx.last_rc == rc); no_submit();
}
static void run(void)
{
    gf3_host_init(&h);
    h.events=events; h.cevents=128; h.packets=packets; h.cpackets=8;
    h.words=words; h.cwords=32; h.observe=observe;
    n1.value=(struct gf_vram_reservation){&h,0,128,1,1};
    n2.value=(struct gf_vram_reservation){&h,128,128,2,1};
    n1.next=&n2; h.reservations=&n1;
    struct gf3_surface b = {0,48,16,16,3,GF3_INDEX8,1,1};
    assert(copy(&a,&b,(struct gf3_rect){0,0,8,2},1,1)==0);
    packet(0,0x00010001,0x00020008);
    /* Explicit destination payloads: the eight neighbours and the origin. */
    static const u32 destinations[] = {0x00000003,0x00000004,0x00000005,
        0x00010003,0x00010004,0x00010005,0x00020003,0x00020004,0x00020005};
    for (u32 i=0;i<9;i++) {
        clear(); u32 p=destinations[i];
        assert(copy(&a,&a,(struct gf3_rect){4,1,4,2},p&65535,p>>16)==0);
        packet(0x00010004,p,0x00020004);
    }
    rejected((struct gf3_surface){1,63,16,15,4,GF3_INDEX8,1,1},GF3_EUNSUPPORTED);
    rejected((struct gf3_surface){16,64,16,16,4,GF3_INDEX8,1,1},GF3_EUNSUPPORTED);
    rejected((struct gf3_surface){17,63,16,15,4,GF3_INDEX8,1,1},GF3_EUNSUPPORTED);
    rejected((struct gf3_surface){0,56,20,16,3,GF3_INDEX8,1,1},GF3_EUNSUPPORTED);
    b=(struct gf3_surface){1,63,16,15,4,GF3_INDEX8,1,1};
    clear(); assert(copy(&a,&b,(struct gf3_rect){0,0,1,2},0,0)==0); packet(0,0,0x00020001);
    b=(struct gf3_surface){64,56,20,16,3,GF3_INDEX8,1,1};
    clear(); assert(copy(&a,&b,(struct gf3_rect){0,0,8,2},0,0)==0); packet(0,0,0x00020008);
    b=(struct gf3_surface){128,64,16,16,4,GF3_INDEX8,2,1};
    clear(); assert(copy(&a,&b,(struct gf3_rect){0,0,8,2},0,0)==0); packet(0,0,0x00020008);
    clear(); assert(copy(&a,&a,(struct gf3_rect){15,3,1,1},14,3)==0); packet(0x0003000f,0x0003000e,0x00010001);
    clear(); assert(copy(&a,&a,(struct gf3_rect){16,3,1,1},0,0)==GF3_ERANGE); no_submit();
    b=a; b.bytes=63; rejected(b,GF3_ERANGE);
    b=a; b.pitch=65536; rejected(b,GF3_ERANGE);
    b=a; b.height=65536; rejected(b,GF3_ERANGE);
    b=a; b.generation=2; rejected(b,GF3_ESTATE);
    b=a; b.fmt=GF3_Y8; rejected(b,GF3_EFORMAT);
    clear(); assert(copy(&a,&a,(struct gf3_rect){0,0,0,1},0,0)==0); no_submit();
    clear(); assert(gf3_copy(&h.ctx,0,&a,(struct gf3_rect){0,0,0,1},0,0,3,255,DL)==GF3_EINVAL); no_submit();
    h.ctx.ready=false; clear(); assert(copy(&a,&a,(struct gf3_rect){0,0,0,1},0,0)==GF3_ESTATE); no_submit(); h.ctx.ready=true;
    static const enum gf3_host_op fail[] = {H_SURFACE_CHECK,H_ROP,H_SURFACE_SET,H_EMIT,H_FENCE,H_WAIT};
    for (u32 i=0;i<sizeof(fail)/sizeof(fail[0]);i++) {
        clear(); gf3_host_fail_at(&h,fail[i],1,GF2_EIO);
        assert(copy(&a,&a,(struct gf3_rect){0,0,2,1},1,0)==GF2_EIO);
        assert(h.ctx.last_rc==GF2_EIO);
        gf3_host_fail_at(&h,fail[i],1,0);
    }
}
void _start(void) { run(); gf2_host_exit(0); }
