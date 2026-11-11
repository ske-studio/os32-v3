/* B4 packet and alias admission tests, freestanding host fixture. */
#define GF3_HOST_UNIT 4
#include "gf_accel_host.h"

static struct gf3_host h;
static struct gf3_host_event events[128];
static struct gf3_host_packet packets[8];
static u32 words[32];
static struct gf_vram_reservation_node n1, n2;
static struct gf3_surface a = {0,1024,256,16,4,GF3_INDEX8,1,1, 0};
static const struct gf3_surface *expected_src, *expected_dst;
static struct gf3_surface saved_src, saved_dst;
#define DL 1000U
/* IMAGE_BLIT point-in method and its three words: nv04_exa.c:255-258. */
#define BLIT_POINT_IN 0x0300U
static const enum gf3_host_op submission[] = {
    H_FAULT, H_SURFACE_CHECK, H_SURFACE_CHECK, H_SURFACE_CHECK,
    H_SURFACE_CHECK, H_ROP, H_SURFACE_SET, H_EMIT, H_FENCE, H_WAIT
};

static void prefix(u32 count)
{
    assert(count <= sizeof(submission)/sizeof(submission[0]));
    for (u32 i = 0; i < count; ++i) assert(events[i].op == submission[i]);
}

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
    const u32 word_count = 3; /* nv04_exa.c:255-258: input, output, size */
    const u32 submission_count = sizeof(submission)/sizeof(submission[0]);
    assert(h.npackets == 1 && h.nwords == word_count);
    assert(packets[0].subc == GF3_SUBC_BLIT && packets[0].method == BLIT_POINT_IN);
    assert(!packets[0].noninc && packets[0].count == word_count && packets[0].deadline == DL);
    assert(words[0] == in && words[1] == out && words[2] == size);
    assert(h.calls[H_FENCE] == 1 && h.calls[H_WAIT] == 1);
    /* WAIT's stub records its own fault and clock calls after the WAIT event. */
    assert(h.nevents == submission_count + 2);
    prefix(submission_count);
    assert(events[submission_count].op == H_FAULT && events[submission_count+1].op == H_CLOCK);
    const struct gf3_host_event *fence = &events[submission_count-2];
    const struct gf3_host_event *wait = &events[submission_count-1];
    assert(fence->p[0] == &h.ch && fence->a[0] == DL);
    assert(wait->p[0] == &h.ch && wait->a[2] == DL);
    assert(wait->a[0] == h.ch.epoch && wait->a[1] == h.ch.seq);
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
    n1.value=(struct gf_vram_reservation){&h,0,2048,1,1};
    n2.value=(struct gf_vram_reservation){&h,2048,2048,2,1};
    n1.next=&n2; h.reservations=&n1;
    clear();
    expected_src = expected_dst = &a; saved_src = saved_dst = a;
    assert(!gf3_copy_emit(&h.ctx, &a, &a, (struct gf3_rect){0,0,8,2}, 1, 1, GF3_ALU_KEEP, 0, DL));
    assert(!h.calls[H_ROP] && h.calls[H_EMIT] == 1);
    clear();
    struct gf3_surface b = {0,768,256,16,3,GF3_INDEX8,1,1, 0};
    assert(copy(&a,&b,(struct gf3_rect){0,0,8,2},1,1)==0);
    packet(0,0x00010001,0x00020008);
    /* Explicit destination payloads: the eight neighbours and the origin. */
    static const u32 destinations[] = {0x00000003,0x00000004,0x00000005,
        0x00010003,0x00010004,0x00010005,0x00020003,0x00020004,0x00020005};
    for (u32 i=0;i<sizeof(destinations)/sizeof(destinations[0]);i++) {
        clear(); u32 p=destinations[i];
        assert(copy(&a,&a,(struct gf3_rect){4,1,4,2},p&65535,p>>16)==0);
        packet(0x00010004,p,0x00020004);
    }
    b = (struct gf3_surface){0,256,64,16,4,GF3_INDEX8,1,1,GF3_HANDLE_GART_DMA};
    clear(); assert(copy(&a,&b,(struct gf3_rect){0,0,8,1},0,0)==0); packet(0,0,0x00010008);
    b.dma = GF3_HANDLE_VRAM_DMA;
    rejected(b, GF3_EUNSUPPORTED);
    /* Aligned aliases still overlap real payload rows; padding alone
     * does not make them aliases (nouveau_exa.c:434-435). */
    rejected((struct gf3_surface){256,768,256,15,3,GF3_INDEX8,1,1,0},GF3_EUNSUPPORTED);
    rejected((struct gf3_surface){256,768,256,16,3,GF3_INDEX8,1,1,0},GF3_EUNSUPPORTED);
    rejected((struct gf3_surface){0,256,64,15,4,GF3_INDEX8,1,1,0},GF3_EUNSUPPORTED);
    rejected((struct gf3_surface){0,960,320,16,3,GF3_INDEX8,1,1,0},GF3_EUNSUPPORTED);
    b=(struct gf3_surface){256,768,256,15,3,GF3_INDEX8,1,1,0};
    clear(); assert(copy(&a,&b,(struct gf3_rect){0,0,1,1},0,0)==0); packet(0,0,0x00010001);
    /* Two payloads touch at x=8 without overlapping; distinct pitches
     * ensure an overly inclusive overlap test would reject this copy. */
    b=(struct gf3_surface){0,256,64,16,4,GF3_INDEX8,1,1,0};
    clear(); assert(copy(&a,&b,(struct gf3_rect){0,0,8,1},8,0)==0); packet(0,8,0x00010008);
    b=(struct gf3_surface){1024,960,320,16,3,GF3_INDEX8,1,1,0};
    clear(); assert(copy(&a,&b,(struct gf3_rect){0,0,8,2},0,0)==0); packet(0,0,0x00020008);
    b=(struct gf3_surface){2048,1024,256,16,4,GF3_INDEX8,2,1,0};
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
    for (u32 i=0;i<sizeof(submission)/sizeof(submission[0]);i++) {
        enum gf3_host_op op = submission[i];
        u32 nth = 1;
        for (u32 j=0;j<i;j++) if (submission[j] == op) ++nth;
        clear(); gf3_host_fail_at(&h,op,nth,GF2_EIO);
        assert(copy(&a,&a,(struct gf3_rect){0,0,2,1},1,0)==GF2_EIO);
        assert(h.ctx.last_rc==GF2_EIO);
        assert(h.nevents == i+1); prefix(i+1);
        assert(events[i].rc == GF2_EIO);
        gf3_host_fail_at(&h,op,nth,0);
        /* H_FAULT injection latches the channel; isolate the next case. */
        h.ch.fault.latched=false; h.ch.state=GF2_RUN;
    }
}
void _start(void) { run(); gf2_host_exit(0); }
