/* Layer 4 host fixture. Include in one test TU, alongside the selected
 * product TU(s). GF_FEAT4_DESIGN G1 owns the dependency matrix below.
 * No libc, heap, GPU emulation or product calculations in this fixture.
 * Storage is borrowed: base.regs contains aligned little-endian words;
 * base.reservations/base.vram supply live keys and their CPU image.
 * All observations use events; install events/cevents before the first I/O.
 * GF4_HOST_UNIT=14 exports helpers/callbacks, but no dependency symbols.
 * X1-X4: UNIT 14 links real service/V0 and replaces the boundary links
 * below for publication/ownership integration. Never stub the new service
 * APIs there or add failure events for objects_init/suspend/restart. */
#ifndef TOOLS_TESTS_GF4_DISPLAY_FAKE_H
#define TOOLS_TESTS_GF4_DISPLAY_FAKE_H
#if defined(GF3_HOST_UNIT) && GF3_HOST_UNIT != 12
#error "GF4 requires GF3_HOST_UNIT=12 (no layer 3 dependency symbols)"
#endif
#ifndef GF3_HOST_UNIT
#define GF3_HOST_UNIT 12
#endif
#include "gf_accel_host.h"
#include "../../hwdev/gf/display/api.h"
#if !defined(GF4_HOST_UNIT) || GF4_HOST_UNIT < 0 || GF4_HOST_UNIT > 14
#error "GF4_HOST_UNIT must select B0..B12, C0, or no stubs (0..14)"
#endif
/* PM adds these same typedefs to types.h at integration. */
typedef unsigned long long u64;
typedef long long i64;
/* A0's mode_calc has six pointers (PM decision: round to eight).
 * Including lower dependencies, surface_check has five scalar arguments
 * after expanding rect.x/y/w/h/use; round that maximum to eight too. */
#define GF4_HOST_POINTER_ARGS 8U
#define GF4_HOST_VALUE_ARGS 8U
#define GF4_HOST_VGA_REGS 256U
#define GF4_HOST_AR_REGS 32U
#define GF4_HOST_RGB 3U
#define GF4_HOST_LUT_BYTES (GF4_HOST_VGA_REGS * GF4_HOST_RGB)
#define GF4_HOST_INDEX_BANKS 4U /* CR, SR, GR, AR. */
#define GF4_HOST_EDID_BYTES 128U /* data.h edid_modes input block. */

enum gf4_host_op { D_READ,D_WRITE,D_CLOCK,D_SLEEP,D_SNAPSHOT,D_IRQ,D_FENCE,
    D_FENCE_WAIT,D_FAULT,D_MATCH,D_MAP,D_UNMAP,D_FORMAT,D_SURFACE,D_ROUTE,D_LUT,
    D_HEADS,D_EDID,D_TV,D_LOAD,D_SAVE,D_RESTORE,D_SCRIPT,D_EXTERNAL,D_DPMS,
    D_I2C_PORT,D_ADMISSION,D_FAILED,D_SCAN_CHANGED,D_CHANGE_BEGIN,D_CHANGE_END,
    D_CURSOR_APPLY,D_FLIP_COMPLETE,D_PLLSEL,D_DAC,D_EXTERNAL_DPMS,D_TV_GPIO,
    D_TV_MODES,D_PROBE,D_OP_COUNT };
struct gf4_host_event {
    enum gf4_host_op op;
    u32 nth,a[GF4_HOST_VALUE_ARGS];
    const void *p[GF4_HOST_POINTER_ARGS];
    i32 rc;
};
struct gf4_host_read { u32 bar,off,width,value; };
struct gf4_host {
    struct gf3_host base;
    u64 local_now,*now;
    struct gf4_ctx ctx;
    struct gf4_state *state;
    struct gf4_exec_ops exec;
    const u8 *ddc_block; /* Borrowed B0 DDC result, GF4_HOST_EDID_BYTES. */
    struct gf4_host_event *events;
    u32 cevents,nevents,calls[D_OP_COUNT],fail_at[D_OP_COUNT];
    i32 fail_rc[D_OP_COUNT];
    const struct gf4_host_read *reads;
    u32 nreads,next_read;
    u8 cr[GF4_HEADS][GF4_HOST_VGA_REGS],sr[GF4_HEADS][GF4_HOST_VGA_REGS];
    u8 gr[GF4_HEADS][GF4_HOST_VGA_REGS],ar[GF4_HEADS][GF4_HOST_AR_REGS];
    u8 lut[GF4_HEADS][GF4_HOST_LUT_BYTES],index[GF4_HEADS][GF4_HOST_INDEX_BANKS];
    u32 lut_pos[GF4_HEADS],lut_read_pos[GF4_HEADS];
    bool ar_data[GF4_HEADS];
    struct gf_irq_stats irq;
    struct gf_vram_view *views;
    void (*observe)(struct gf4_host *,const struct gf4_host_event *);
    struct gf4_links links; struct gf4_i2c_port port;
    struct gf4_edid_state tv_modes; i32 port_rc;
    u32 applied_write_nth; i32 applied_write_rc;
    /* Observed C2 value loan/scan hook, never reservation ownership. Keeps
     * the full format/geometry/key, including 16/32bit surfaces (X1). */
    struct gf3_surface borrowed_scan;
    bool scan_hook_open;
};
static struct gf4_host *gf4_host_current;
static inline struct gf4_host_event *gf4_host_begin(struct gf4_host *h,enum gf4_host_op op)
{
    assert(h && (unsigned)op<D_OP_COUNT && h->events && h->nevents<h->cevents);
    struct gf4_host_event *e=&h->events[h->nevents++];
    gf2_host_memset(e,0,sizeof(*e));
    e->op=op; e->nth=++h->calls[op];
    /* irq.c:233-238: void snapshot always copies; it cannot fail. */
    if (op!=D_SNAPSHOT && h->fail_rc[op] &&
        (!h->fail_at[op] || h->fail_at[op]==e->nth)) e->rc=h->fail_rc[op];
    return e;
}
static inline i32 gf4_host_end(struct gf4_host *h,struct gf4_host_event *e)
{
    if (h->observe) h->observe(h,e);
    return e->rc;
}
static inline void gf4_host_trace_clear(struct gf4_host *h)
{
    assert(h); h->nevents=0;
    gf2_host_memset(h->calls,0,sizeof(h->calls));
}
static inline void gf4_host_fail_at(struct gf4_host *h,enum gf4_host_op op,u32 nth,i32 rc)
{
    assert(h && (unsigned)op<D_OP_COUNT && op!=D_SNAPSHOT);
    h->fail_at[op]=nth; h->fail_rc[op]=rc;
}
/* nth is the D_WRITE call number since the last trace clear; zero disables. */
static inline void gf4_host_write_applied_error(struct gf4_host *h,u32 nth,i32 rc)
{
    assert(h && (!nth || rc<0));
    h->applied_write_nth=nth; h->applied_write_rc=rc;
}
static inline void gf4_host_reads(struct gf4_host *h,const struct gf4_host_read *r,u32 n)
{
    assert(h && (r || !n));
    for (u32 i=0;i<n;++i) {
        assert(r[i].width==1 || r[i].width==2 || r[i].width==4);
        assert(r[i].off<=U32_MAX-(r[i].width-1U));
    }
    h->reads=r; h->nreads=n; h->next_read=0;
}
/* MIT nouveau c03c1a1 dispnv04/nvreg.h:60,82,129-139,142-153,433-437;
 * hw.h:119-140,164-235; hw.c:38-65. NV4x has two PRMVIO ranges. */
#define GF4_HOST_STRIDE 0x2000U
#define GF4_HOST_CR 0x6013d4U
#define GF4_HOST_SR 0x0c03c4U
#define GF4_HOST_GR 0x0c03ceU
#define GF4_HOST_AR 0x6013c0U
#define GF4_HOST_STATUS 0x6013daU
#define GF4_HOST_DAC_READ_INDEX 0x6813c7U
#define GF4_HOST_DAC_INDEX 0x6813c8U
#define GF4_HOST_DAC_DATA 0x6813c9U
static inline void gf4_host_lut_advance(u32 *pos)
{
    if (++*pos==GF4_HOST_LUT_BYTES) *pos=0;
}
static inline bool gf4_host_indexed(struct gf4_host *h,u32 off,bool write,u32 *value)
{
    for (u32 head=0;head<GF4_HEADS;++head) {
        u32 delta=head*GF4_HOST_STRIDE;
        const u32 ports[]={GF4_HOST_CR,GF4_HOST_SR,GF4_HOST_GR};
        u8 *banks[]={h->cr[head],h->sr[head],h->gr[head]};
        for (u32 k=0;k<sizeof(ports)/sizeof(ports[0]);++k) {
            if (off==ports[k]+delta) {
                if (write) h->index[head][k]=(u8)*value;
                else *value=h->index[head][k];
                return true;
            }
            if (off==ports[k]+delta+1U) {
                u8 *p=&banks[k][h->index[head][k]];
                if (write) *p=(u8)*value; else *value=*p;
                return true;
            }
        }
        if (off==GF4_HOST_STATUS+delta && !write) {
            h->ar_data[head]=false; *value=0; return true;
        }
        if (off==GF4_HOST_AR+delta) {
            if (write) {
                if (h->ar_data[head])
                    h->ar[head][h->index[head][3]&(GF4_HOST_AR_REGS-1U)]=(u8)*value;
                else h->index[head][3]=(u8)*value;
                h->ar_data[head]=!h->ar_data[head];
            } else *value=h->index[head][3];
            return true;
        }
        if (off==GF4_HOST_AR+delta+1U && !write) {
            *value=h->ar[head][h->index[head][3]&(GF4_HOST_AR_REGS-1U)];
            return true;
        }
        if (off==GF4_HOST_DAC_INDEX+delta) {
            if (write) h->lut_pos[head]=((u8)*value)*GF4_HOST_RGB;
            else *value=h->lut_pos[head]/GF4_HOST_RGB;
            return true;
        }
        if (off==GF4_HOST_DAC_READ_INDEX+delta && write) {
            h->lut_read_pos[head]=((u8)*value)*GF4_HOST_RGB; return true;
        }
        if (off==GF4_HOST_DAC_DATA+delta) {
            if (write) {
                h->lut[head][h->lut_pos[head]]=(u8)*value;
                gf4_host_lut_advance(&h->lut_pos[head]);
            } else {
                *value=h->lut[head][h->lut_read_pos[head]];
                gf4_host_lut_advance(&h->lut_read_pos[head]);
            }
            return true;
        }
    }
    return false;
}
static inline u32 gf4_host_reg_byte(struct gf4_host *h,u32 bar,u32 off)
{
    for (u32 i=0;i<h->base.nregs;++i)
        if (h->base.regs[i].bar==bar && h->base.regs[i].off==(off&~3U))
            return (h->base.regs[i].value>>((off&3U)*8U))&U8_MAX;
    return 0;
}
static inline void gf4_host_reg_byte_set(struct gf4_host *h,u32 bar,u32 off,u32 value)
{
    u32 aligned=off&~3U,shift=(off&3U)*8U,word=0;
    for (u32 i=0;i<h->base.nregs;++i)
        if (h->base.regs[i].bar==bar && h->base.regs[i].off==aligned) {
            word=h->base.regs[i].value; break;
        }
    gf3_host_set(&h->base,bar,aligned,(word&~(U8_MAX<<shift))|((value&U8_MAX)<<shift));
}
/* Unlisted addresses use the register model. Once an address occurs in the
 * script, its next read must be the next scripted entry, with exact width.
 * Looking through the whole script also rejects extra reads after exhaustion.
 * Failed reads do not consume a successful-read entry or change *out. */
static inline const struct gf4_host_read *gf4_host_script(struct gf4_host *h,u32 bar,u32 off,u32 width)
{
    for (u32 i=0;i<h->nreads;++i) {
        if (h->reads[i].bar!=bar || h->reads[i].off!=off) continue;
        assert(h->next_read<h->nreads);
        const struct gf4_host_read *r=&h->reads[h->next_read];
        assert(r->bar==bar && r->off==off && r->width==width);
        return r;
    }
    return 0;
}
static inline int gf4_host_read(void *owner,u32 bar,u32 off,u32 width,u32 *out)
{
    struct gf4_host *h=owner;
    assert(h && out && (width==1 || width==2 || width==4));
    assert(off<=U32_MAX-(width-1U));
    struct gf4_host_event *e=gf4_host_begin(h,D_READ);
    e->p[0]=owner; e->p[1]=out; e->a[0]=bar; e->a[1]=off; e->a[2]=width;
    if (!e->rc) {
        const struct gf4_host_read *r=gf4_host_script(h,bar,off,width);
        u32 value=0;
        /* Apply register side effects even when a script overrides the data
         * (notably status-read AR reset). Never interpret another BAR as VGA. */
        for (u32 b=0;b<width;++b) {
            u32 byte=0;
            if (bar!=GF_MODE_REGISTER_BAR || !gf4_host_indexed(h,off+b,false,&byte))
                byte=gf4_host_reg_byte(h,bar,off+b);
            value|=byte<<(b*8U);
        }
        if (r) { value=r->value; ++h->next_read; }
        *out=width==4 ? value : value&((1U<<(width*8U))-1U);
    }
    return gf4_host_end(h,e);
}
static inline int gf4_host_write(void *owner,u32 bar,u32 off,u32 width,u32 value)
{
    struct gf4_host *h=owner;
    assert(h && (width==1 || width==2 || width==4));
    assert(off<=U32_MAX-(width-1U));
    struct gf4_host_event *e=gf4_host_begin(h,D_WRITE);
    e->p[0]=owner; e->a[0]=bar; e->a[1]=off; e->a[2]=width; e->a[3]=value;
    bool applied=h->applied_write_nth && e->nth==h->applied_write_nth;
    if (!e->rc || applied) for (u32 b=0;b<width;++b) {
        u32 byte=(value>>(b*8U))&U8_MAX;
        if (bar!=GF_MODE_REGISTER_BAR || !gf4_host_indexed(h,off+b,true,&byte))
            gf4_host_reg_byte_set(h,bar,off+b,byte);
    }
    if (applied) e->rc=h->applied_write_rc;
    return gf4_host_end(h,e);
}
static inline int gf4_host_clock(void *owner,u32 *now)
{
    struct gf4_host *h=owner; assert(h && now && h->now);
    struct gf4_host_event *e=gf4_host_begin(h,D_CLOCK);
    e->p[0]=owner; e->p[1]=now;
    if (!e->rc) { *now=(u32)*h->now; h->base.now=*now; }
    return gf4_host_end(h,e);
}
static inline int gf4_host_wait(void *owner,u32 us)
{
    struct gf4_host *h=owner; assert(h && h->now);
    struct gf4_host_event *e=gf4_host_begin(h,D_SLEEP);
    e->p[0]=owner; e->a[0]=us;
    if (!e->rc) { *h->now+=us; h->base.now=(u32)*h->now; ++h->base.waits; }
    return gf4_host_end(h,e);
}
/* Pure data results are seeded in state->edid and state->tv[0]; calculation
 * results in state->work[head]. They are independent fixtures, not parsers.
 * observe may inspect/adjust a successful output before the caller resumes. */
static inline i32 gf4_host_edid_modes(const u8 block[128],u32 block_no,struct gf4_edid_state *out)
{
    struct gf4_host *h=gf4_host_current; assert(h && block && out);
    struct gf4_host_event *e=gf4_host_begin(h,D_EDID);
    e->p[0]=block; e->p[1]=out; e->a[0]=block_no;
    if (!e->rc) *out=h->state->edid;
    return gf4_host_end(h,e);
}
static inline i32 gf4_host_tv_state(u32 norm,const struct gf4_timing *timing,struct gf4_tv_state *out)
{
    struct gf4_host *h=gf4_host_current; assert(h && timing && out);
    struct gf4_host_event *e=gf4_host_begin(h,D_TV);
    e->p[0]=timing; e->p[1]=out; e->a[0]=norm;
    if (!e->rc) *out=h->state->tv[0];
    return gf4_host_end(h,e);
}
static inline i32 gf4_host_tmds_script(void *owner,const struct gf4_route *route,u32 khz,u32 dl)
{
    struct gf4_host *h=owner; assert(h);
    struct gf4_host_event *e=gf4_host_begin(h,D_SCRIPT);
    e->p[0]=owner; e->p[1]=route; e->a[0]=khz; e->a[1]=dl;
    return gf4_host_end(h,e);
}
static inline i32 gf4_host_external_mode(void *owner,const struct gf4_route *route,const struct gf4_mode *mode,u32 dl)
{
    struct gf4_host *h=owner; assert(h);
    struct gf4_host_event *e=gf4_host_begin(h,D_EXTERNAL);
    e->p[0]=owner; e->p[1]=route; e->p[2]=mode; e->a[0]=dl;
    return gf4_host_end(h,e);
}
static inline i32 gf4_host_tv_modes(u32 norm,struct gf4_edid_state *out)
{
    struct gf4_host *h=gf4_host_current; assert(h && out);
    struct gf4_host_event *e=gf4_host_begin(h,D_TV_MODES);
    e->p[0]=out; e->a[0]=norm;
    if (!e->rc) *out=h->tv_modes;
    return gf4_host_end(h,e);
}
static inline i32 gf4_host_i2c_port(void *owner,u8 bus,struct gf4_i2c_port *out)
{
    struct gf4_host *h=owner; assert(h && out);
    struct gf4_host_event *e=gf4_host_begin(h,D_I2C_PORT);
    e->p[0]=owner; e->p[1]=out; e->a[0]=bus;
    if (!e->rc) e->rc=h->port_rc;
    if (!e->rc) *out=h->port;
    return gf4_host_end(h,e);
}
static inline i32 gf4_host_admission(void *owner,bool open)
{
    struct gf4_host *h=owner; assert(h);
    struct gf4_host_event *e=gf4_host_begin(h,D_ADMISSION);
    e->p[0]=owner; e->a[0]=open;
    if (!e->rc && open && h->ctx.saved_closed && !h->ctx.diagnostic_saved)
        e->rc=GF4_ESTATE;
    if (!e->rc) {
        if (open) {
            /* C0 publishes before committing next to head. No product
             * validation here: D_ADMISSION is the seeded boundary result. */
            const struct gf4_head_image *image=h->ctx.changing ?
                h->state->next : h->state->head;
            h->borrowed_scan=image[0].enabled ? image[0].front : (struct gf3_surface){0};
            h->scan_hook_open=image[0].enabled;
        } else h->scan_hook_open=false;
        h->ctx.accepting=open;
    }
    /* X2: closing publication never closes windows, clears ready, releases
     * reservations or erases the borrowed surface value. */
    return gf4_host_end(h,e);
}
static inline i32 gf4_host_failed(void *owner,i32 cause,u32 dl)
{
    struct gf4_host *h=owner; assert(h);
    struct gf4_host_event *e=gf4_host_begin(h,D_FAILED);
    e->p[0]=owner; e->a[0]=(u32)cause; e->a[1]=dl;
    h->ctx.accepting=false; h->ctx.reusable=false; h->scan_hook_open=false;
    /* X3: return the operation's cause; a seeded stop/cleanup error goes
     * to recovery_rc. Otherwise preserve C0's recorded rollback error.
     * Real stopping/release is exercised in UNIT 14. */
    if (e->rc) h->ctx.recovery_rc=e->rc;
    e->rc=cause;
    return gf4_host_end(h,e);
}
static inline i32 gf4_host_scan_changed(void *owner,u8 head,const struct gf3_surface *front)
{
    struct gf4_host *h=owner; assert(h && front && head<GF4_HEADS);
    struct gf4_host_event *e=gf4_host_begin(h,D_SCAN_CHANGED);
    e->p[0]=owner; e->p[1]=front; e->a[0]=head;
    /* X1: notification carries the candidate while state.head is still old.
     * Head B cannot change head A's loan; failure keeps the previous loan.
     * Closed admission stays closed. B7 alone commits front/hold afterwards;
     * neither face is released here. observe sees the resulting metadata. */
    if (!e->rc && head==0) h->borrowed_scan=*front;
    return gf4_host_end(h,e);
}
static inline i32 gf4_host_external_dpms(void *owner,const struct gf4_route *route,
                                         enum gf4_dpms dpms,u32 dl)
{
    struct gf4_host *h=owner; assert(h);
    struct gf4_host_event *e=gf4_host_begin(h,D_EXTERNAL_DPMS);
    e->p[0]=owner; e->p[1]=route; e->a[0]=dpms; e->a[1]=dl;
    return gf4_host_end(h,e);
}
static inline i32 gf4_host_tv_gpio(void *owner,bool on,u32 dl)
{
    struct gf4_host *h=owner; assert(h);
    struct gf4_host_event *e=gf4_host_begin(h,D_TV_GPIO);
    e->p[0]=owner; e->a[0]=on; e->a[1]=dl;
    return gf4_host_end(h,e);
}
static const struct gf4_data_ops gf4_host_data_ops={
    .version=GF4_DATA_VERSION,.size=sizeof(struct gf4_data_ops),
    .edid_modes=gf4_host_edid_modes,.tv_state=gf4_host_tv_state,
    .tv_modes=gf4_host_tv_modes
};
/* Lower UC adapter, also usable by integration tests linking real V0.
 * Its image is borrowed, so map/unmap do not allocate or release RAM. */
static inline int gf4_host_map(void *owner,const struct recover_bar_extent *extent,u32 bytes,volatile u8 **out)
{
    struct gf4_host *h=owner; assert(h && extent && out);
    const struct recover_bar_extent *bar=&h->base.ch.bar1;
    if (!h->base.vram || !bytes || extent->base<bar->base) return GF2_EMAP;
    u32 off=extent->base-bar->base;
    if (off>h->base.vram_bytes || bytes>h->base.vram_bytes-off) return GF2_EMAP;
    assert((uptr)h->base.vram<=U32_MAX-(h->base.vram_bytes-1U));
    *out=h->base.vram+off;
    return 0;
}
static inline int gf4_host_unmap(void *owner,volatile u8 *ptr,u32 bytes)
{
    struct gf4_host *h=owner; assert(h);
    uptr start=(uptr)h->base.vram,p=(uptr)ptr;
    return !h->base.vram || !bytes || p<start || p-start>h->base.vram_bytes ||
        bytes>h->base.vram_bytes-(p-start) ? GF2_EMAP : 0;
}
static const struct gf_instmem_map_ops gf4_host_map_ops={gf4_host_map,gf4_host_unmap};
static inline void gf4_host_init(struct gf4_host *h,struct gf4_state *state)
{
    assert(h && state); gf2_host_memset(h,0,sizeof(*h));
    gf3_host_init(&h->base); gf4_host_current=h;
    h->now=&h->local_now; h->state=state;
    h->ctx.state=state; h->ctx.ch=&h->base.ch; h->ctx.accel=&h->base.ctx;
    h->ctx.data=&gf4_host_data_ops; h->ctx.epoch=h->base.ch.epoch;
    h->ctx.ready=h->ctx.accepting=h->ctx.reusable=true;
    h->exec=(struct gf4_exec_ops){
        .tmds_script=gf4_host_tmds_script,.external_mode=gf4_host_external_mode,
        .owner=h,.external_dpms=gf4_host_external_dpms,.tv_gpio=gf4_host_tv_gpio
    };
    h->links=(struct gf4_links){
        .i2c_port=gf4_host_i2c_port,.admission=gf4_host_admission,
        .failed=gf4_host_failed,.scan_changed=gf4_host_scan_changed,.owner=h
    };
    h->ctx.links=&h->links;
    h->ctx.exec=&h->exec;
    h->base.f.io.opaque=h; h->base.f.io.read=gf4_host_read; h->base.f.io.write=gf4_host_write;
    h->base.f.io.timing.clock_us=gf4_host_clock; h->base.f.io.timing.wait_us=gf4_host_wait;
    h->base.ch.map_ops=&gf4_host_map_ops; h->base.ch.map_owner=h;
    h->base.f.map_ops=&gf4_host_map_ops; h->base.f.map_owner=h;
}
/* Shared implementations have no external dependency symbols. F5 can use
 * these with its own selection without redefining a GF4 B stub. */
static inline i32 gf4_host_match(struct gf4_host *h,struct gf2_chan *ch,const void *owner,
                                 u32 id,u32 generation,u32 off,u32 bytes,struct gf_vram_reservation *out)
{
    struct gf4_host_event *e=gf4_host_begin(h,D_MATCH);
    e->p[0]=ch; e->p[1]=owner; e->p[2]=out;
    e->a[0]=id; e->a[1]=generation; e->a[2]=off; e->a[3]=bytes;
    if (!e->rc) e->rc=gf3_host_match(&h->base,ch,owner,id,generation,off,bytes,out);
    return gf4_host_end(h,e);
}
static inline i32 gf4_host_view_map(struct gf4_host *h,struct gf_vram_view *view,
                                    struct gf2_chan *ch,u32 off,u32 len,volatile u8 **out)
{
    struct gf4_host_event *e=gf4_host_begin(h,D_MAP);
    e->p[0]=view; e->p[1]=ch; e->p[2]=out; e->a[0]=off; e->a[1]=len;
    const u32 mask=MEM_PAGE_SIZE-1U;
    u32 start=off&~mask,end=0,bytes=0;
    if (!e->rc && (!view || !ch || !out || view->mapped || !view->close ||
        !ch->map_ops || !ch->map_ops->map || !ch->map_ops->unmap || !ch->bar1.valid ||
        !len || ((off|len)&3U) || off>U32_MAX-len)) e->rc=GF2_EMAP;
    if (!e->rc) {
        end=off+len;
        if (end>U32_MAX-mask) e->rc=GF2_EMAP;
        else { end=(end+mask)&~mask; bytes=end-start; }
    }
    /* vram_view.h / vram_view.c: exclude the boot display map, round only
     * the mapping envelope, and leave the channel's push mapping alone. */
    const u32 display_end=(GF_MODE_VRAM_BYTES+mask)&~mask;
    if (!e->rc && (start<display_end || (ch->bar1.base&mask) ||
        start>ch->bar1.length || bytes>ch->bar1.length-start ||
        start>U32_MAX-ch->bar1.base || bytes-1U>U32_MAX-(ch->bar1.base+start)))
        e->rc=GF2_EMAP;
    if (!e->rc && ch!=&h->base.ch) e->rc=GF3_ESTATE;
    if (!e->rc) {
        bool found=false;
        for (struct gf_vram_reservation_node *n=h->base.reservations;n;n=n->next) {
            const struct gf_vram_reservation *v=&n->value;
            if (v->owner && off>=v->off && off-v->off<=v->bytes && len<=v->bytes-(off-v->off)) {
                found=true; break;
            }
        }
        if (!found) e->rc=GF3_ESTATE;
    }
    if (!e->rc) {
        struct recover_bar_extent extent=ch->bar1;
        volatile u8 *base;
        extent.base+=start; extent.length=bytes;
        if (ch->map_ops->map(ch->map_owner,&extent,bytes,&base)) e->rc=GF2_EMAP;
        else {
            *view=(struct gf_vram_view){
                .extent=extent,.va=base+(off-start),.map_base=base,
                .map_bytes=bytes,.bytes=len,.mapped=true,
                .map_ops=ch->map_ops,.map_owner=ch->map_owner,
                .ch=ch,.next=h->views,.close_owner=view->close_owner,.close=view->close
            };
            h->views=view; *out=view->va;
        }
    }
    return gf4_host_end(h,e);
}
static inline i32 gf4_host_view_unmap(struct gf4_host *h,struct gf_vram_view *view)
{
    struct gf4_host_event *e=gf4_host_begin(h,D_UNMAP); e->p[0]=view;
    if (!e->rc && (!view || !view->mapped || !view->map_ops || !view->map_ops->unmap))
        e->rc=GF2_EMAP;
    if (!e->rc && view->map_ops->unmap(view->map_owner,view->map_base,view->map_bytes))
        e->rc=GF2_EMAP;
    if (!e->rc) {
        for (struct gf_vram_view **link=&h->views;*link;link=&(*link)->next)
            if (*link==view) { *link=view->next; break; }
        *view=(struct gf_vram_view){0};
    }
    return gf4_host_end(h,e);
}
static inline i32 gf4_host_fault(struct gf4_host *h,struct gf2_chan *ch)
{
    assert(h && ch==&h->base.ch);
    struct gf4_host_event *e=gf4_host_begin(h,D_FAULT); e->p[0]=ch;
    bool new_fault=(h->irq.fifo-ch->seen.fifo)!=0 || (h->irq.graph-ch->seen.graph)!=0;
    ch->seen=h->irq;
    if (ch->fault.latched) {
        /* fault.c:57-68,134-138: counters replace, status bits accumulate;
         * repeated detection never erases the first capture or last_rc. */
        struct gf_irq_stats *v=&ch->fault.latest;
        v->entries=h->irq.entries; v->fifo=h->irq.fifo; v->graph=h->irq.graph;
        for (u32 head=0;head<GF4_HEADS;++head) v->vblank[head]=h->irq.vblank[head];
        v->pmc|=h->irq.pmc; v->pfifo|=h->irq.pfifo; v->pgraph|=h->irq.pgraph;
        v->nsource|=h->irq.nsource; v->nstatus|=h->irq.nstatus; v->unknown|=h->irq.unknown;
        e->rc=GF2_EFAULT;
    } else if (e->rc || new_fault) {
        /* fault.c:142-163: EIO is a capture detail in last_rc; the public
         * fault check still returns EFAULT and leaves the channel closed. */
        ch->fault=(struct gf2_fault){0};
        ch->fault.epoch=ch->epoch; ch->fault.first=ch->fault.latest=h->irq;
        ch->fault.latched=true; ch->state=GF2_FAULT;
        ch->last_rc=e->rc && e->rc!=GF2_EFAULT ? GF2_EIO : GF2_EFAULT;
        ch->f->wanted_fifo=ch->f->wanted_graph=0;
        e->rc=GF2_EFAULT;
    } else if (ch->state==GF2_FAULT) e->rc=GF2_EFAULT;
    return gf4_host_end(h,e);
}
static inline i32 gf4_host_fence_state(struct gf4_host *h,struct gf2_chan *ch)
{
    i32 rc=gf4_host_fault(h,ch);
    if (rc) return rc;
    return ch->state==GF2_INIT || ch->state==GF2_RUN || ch->state==GF2_DRAIN ? 0 : GF2_ESTATE;
}
#if GF4_HOST_UNIT == 0 || GF4_HOST_UNIT == 6
int gf_vram_match(struct gf2_chan *ch,const void *owner,u32 id,u32 generation,
                  u32 off,u32 bytes,struct gf_vram_reservation *out)
{
    return gf4_host_match(gf4_host_current,ch,owner,id,generation,off,bytes,out);
}
#endif
#if GF4_HOST_UNIT == 6
int gf_vram_view_map(struct gf_vram_view *view,struct gf2_chan *ch,u32 off,u32 len,volatile u8 **out)
{
    return gf4_host_view_map(gf4_host_current,view,ch,off,len,out);
}
int gf_vram_view_unmap(struct gf_vram_view *view)
{
    return gf4_host_view_unmap(gf4_host_current,view);
}
#endif
#if GF4_HOST_UNIT == 0
int gf4_ddc_probe(struct gf4_ctx *ctx,u8 bus,u32 dl)
{
    struct gf4_host *h=gf4_host_current;
    struct gf4_host_event *e=gf4_host_begin(h,D_PROBE);
    e->p[0]=ctx; e->a[0]=bus; e->a[2]=dl;
    return gf4_host_end(h,e);
}
int gf4_ddc_read(struct gf4_ctx *ctx,u8 bus,u8 block,u8 out[128],u32 dl)
{
    struct gf4_host *h=gf4_host_current;
    struct gf4_host_event *e=gf4_host_begin(h,D_EDID);
    e->p[0]=ctx; e->p[1]=out; e->a[0]=bus; e->a[1]=block; e->a[2]=dl;
    if (!e->rc) { assert(h->ddc_block && out); gf2_host_memcpy(out,h->ddc_block,GF4_HOST_EDID_BYTES); }
    return gf4_host_end(h,e);
}
#endif
#if GF4_HOST_UNIT == 0 || GF4_HOST_UNIT == 13
i32 gf4_pll_calc(const struct gf4_mode *mode,const struct gf4_route *route,
                  const struct gfinit_mode_saved *saved,const struct gfinit_pll_limits *limits,
                  struct gfinit_mode *out)
{
    struct gf4_host *h=gf4_host_current;
    struct gf4_host_event *e=gf4_host_begin(h,D_LOAD);
    e->p[0]=mode; e->p[1]=route; e->p[2]=saved; e->p[3]=limits; e->p[4]=out;
    if (!e->rc) { assert(route && route->head<GF4_HEADS && out); *out=h->state->work[route->head]; }
    return gf4_host_end(h,e);
}
#endif
#if GF4_HOST_UNIT == 0
int gf3_format_get(enum gf3_fmt fmt,struct gf3_format *out)
{
    struct gf4_host *h=gf4_host_current;
    struct gf4_host_event *e=gf4_host_begin(h,D_FORMAT);
    e->p[0]=out; e->a[0]=fmt;
    if (!e->rc) e->rc=gf3_host_format(fmt,out);
    return gf4_host_end(h,e);
}
int gf3_surface_check(struct gf3_ctx *ctx,const struct gf3_surface *s,struct gf3_rect r,
                      enum gf3_surface_use use,struct gf3_format *fmt,struct gf3_span *span)
{
    struct gf4_host *h=gf4_host_current;
    struct gf4_host_event *e=gf4_host_begin(h,D_SURFACE);
    e->p[0]=ctx; e->p[1]=s; e->p[2]=fmt; e->p[3]=span;
    e->a[0]=r.x; e->a[1]=r.y; e->a[2]=r.w; e->a[3]=r.h; e->a[4]=use;
    if (!e->rc) e->rc=gf3_host_surface(&h->base,ctx,s,r,use,fmt,span);
    return gf4_host_end(h,e);
}
#endif
#if GF4_HOST_UNIT == 5 || GF4_HOST_UNIT == 7 || GF4_HOST_UNIT == 13
int gf2_fence_emit(struct gf2_chan *ch,struct gf2_fence *out,u32 dl)
{
    struct gf4_host *h=gf4_host_current; assert(h && ch==&h->base.ch && out);
    struct gf4_host_event *e=gf4_host_begin(h,D_FENCE);
    e->p[0]=ch; e->p[1]=out; e->a[0]=dl;
    /* fence.c:21-39: a rejected state consumes no sequence. A submission
     * failure consumes it, but never publishes an output ticket. */
    i32 rc=gf4_host_fence_state(h,ch);
    if (rc) e->rc=rc;
    else if (e->rc!=GF2_EFAULT && e->rc!=GF2_ESTATE) {
        u32 seq=++ch->seq;
        if (!e->rc) *out=(struct gf2_fence){ch->epoch,seq};
    }
    return gf4_host_end(h,e);
}
int gf2_fence_wait(struct gf2_chan *ch,struct gf2_fence fence,u32 dl)
{
    struct gf4_host *h=gf4_host_current; assert(h && ch==&h->base.ch);
    struct gf4_host_event *e=gf4_host_begin(h,D_FENCE_WAIT);
    e->p[0]=ch; e->a[0]=fence.epoch; e->a[1]=fence.seq; e->a[2]=dl;
    /* base.complete_after counts polls; U32_MAX never completes. Reuse the
     * F3 fixture's completion control, with F4's shared 64bit clock/wait. */
    for (u32 n=0;!e->rc;++n) {
        u32 now;
        e->rc=gf4_host_fence_state(h,ch);
        if (e->rc) break;
        if (fence.epoch!=ch->epoch) { e->rc=GF2_EFAULT; break; }
        if (gf4_host_clock(h,&now)) { e->rc=GF2_EIO; break; }
        if ((i32)(now-dl)>=0) { e->rc=GF2_ETIME; break; }
        if (h->base.complete_after!=U32_MAX && n>=h->base.complete_after) break;
        u32 delay=dl-now;
        if (delay>GF2_WAIT_MAX_US) delay=GF2_WAIT_MAX_US;
        if (gf4_host_wait(h,delay)) { e->rc=GF2_EIO; break; }
    }
    return gf4_host_end(h,e);
}
#endif
#if GF4_HOST_UNIT == 7
int gf2_fault_check(struct gf2_chan *ch)
{
    return gf4_host_fault(gf4_host_current,ch);
}
#endif
#if GF4_HOST_UNIT == 7 || GF4_HOST_UNIT == 12
void gf_irq_snapshot(struct gf_foundation *f,struct gf_irq_stats *out)
{
    struct gf4_host *h=gf4_host_current; assert(h && f==&h->base.f && out);
    struct gf4_host_event *e=gf4_host_begin(h,D_SNAPSHOT);
    e->p[0]=f; e->p[1]=out; *out=h->irq;
    (void)gf4_host_end(h,e);
}
#endif
#if GF4_HOST_UNIT == 12
int gf_irq_enable(struct gf_foundation *f,bool fifo,bool graph,unsigned heads)
{
    struct gf4_host *h=gf4_host_current; assert(h && f==&h->base.f);
    struct gf4_host_event *e=gf4_host_begin(h,D_IRQ);
    e->p[0]=f; e->a[0]=fifo; e->a[1]=graph; e->a[2]=heads;
    /* irq.c:108-110 commits requested masks before fallible bus writes. */
    f->wanted_fifo=fifo ? U32_MAX : 0; f->wanted_graph=graph ? U32_MAX : 0;
    f->wanted_heads=heads; f->last_rc=e->rc;
    return gf4_host_end(h,e);
}
#endif
#if GF4_HOST_UNIT == 7 || GF4_HOST_UNIT == 13
i32 gf4_surface_check(struct gf4_ctx *ctx,const struct gf3_surface *s,const struct gf4_mode *mode)
{
    struct gf4_host *h=gf4_host_current;
    struct gf4_host_event *e=gf4_host_begin(h,D_SURFACE);
    e->p[0]=ctx; e->p[1]=s; e->p[2]=mode;
    /* Display validation is B0's product. This dependency is a call oracle;
     * failure cases select D_SURFACE instead of duplicating B0's rules. */
    return gf4_host_end(h,e);
}
#endif
#if GF4_HOST_UNIT == 10 || GF4_HOST_UNIT == 11 || GF4_HOST_UNIT == 13
i32 gf4_route_check(struct gf4_ctx *ctx,const struct gf4_route *route)
{
    struct gf4_host *h=gf4_host_current;
    struct gf4_host_event *e=gf4_host_begin(h,D_ROUTE);
    e->p[0]=ctx; e->p[1]=route;
    return gf4_host_end(h,e);
}
#endif
/* B5 calls encoder DPMS; C0 calls the encoder load and DPMS dependencies.
 * B9 owns analog, B10 owns TMDS, B11 owns TV: never define their own API. */
#define GF4_HOST_ENCODER_LOAD(name) \
i32 name(struct gf4_ctx *ctx,const struct gf4_route *route,const struct gfinit_mode *mode,u32 dl) \
{ \
    struct gf4_host *h=gf4_host_current; \
    struct gf4_host_event *e=gf4_host_begin(h,D_LOAD); \
    e->p[0]=ctx; e->p[1]=route; e->p[2]=mode; e->a[0]=dl; \
    return gf4_host_end(h,e); \
}
#define GF4_HOST_ENCODER_DPMS(name) \
i32 name(struct gf4_ctx *ctx,const struct gf4_route *route,enum gf4_dpms dpms,u32 dl) \
{ \
    struct gf4_host *h=gf4_host_current; \
    struct gf4_host_event *e=gf4_host_begin(h,D_DPMS); \
    e->p[0]=ctx; e->p[1]=route; e->a[0]=dpms; e->a[1]=dl; \
    return gf4_host_end(h,e); \
}
#if GF4_HOST_UNIT == 13
GF4_HOST_ENCODER_LOAD(gf4_analog_load)
GF4_HOST_ENCODER_LOAD(gf4_tmds_load)
GF4_HOST_ENCODER_LOAD(gf4_tv_load)
#endif
#if GF4_HOST_UNIT == 5 || GF4_HOST_UNIT == 13
GF4_HOST_ENCODER_DPMS(gf4_analog_dpms)
GF4_HOST_ENCODER_DPMS(gf4_tmds_dpms)
GF4_HOST_ENCODER_DPMS(gf4_tv_dpms)
#endif
#undef GF4_HOST_ENCODER_LOAD
#undef GF4_HOST_ENCODER_DPMS
#if GF4_HOST_UNIT == 13
/* C0 owns set_mode and the aggregate mode_calc/mode_load transaction.
 * Stub the B1/B2 calculation boundaries, never those C0 entry points.
 * D_LOAD records pure calculation calls as well as encoder loads. Results
 * are precomputed work[0] (B1) / work[route->head] (B2), with no formula. */
i32 gf4_timing_calc(const struct gf4_mode *mode,struct gfinit_mode *out)
{
    struct gf4_host *h=gf4_host_current;
    struct gf4_host_event *e=gf4_host_begin(h,D_LOAD);
    e->p[0]=mode; e->p[1]=out;
    if (!e->rc) { assert(out); *out=h->state->work[0]; }
    return gf4_host_end(h,e);
}
i32 gf4_format_calc(const struct gf4_mode *mode,const struct gf3_surface *front,struct gfinit_mode *out)
{
    struct gf4_host *h=gf4_host_current;
    struct gf4_host_event *e=gf4_host_begin(h,D_FORMAT);
    e->p[0]=mode; e->p[1]=front; e->p[2]=out;
    if (!e->rc) { assert(out); *out=h->state->work[0]; }
    return gf4_host_end(h,e);
}
void gf4_fifo_calc(struct gfinit_mode *out)
{
    struct gf4_host *h=gf4_host_current;
    struct gf4_host_event *e=gf4_host_begin(h,D_LOAD);
    e->p[0]=out;
    /* A void pure calculation cannot fail. Shared D_LOAD injection applies
     * to its fallible siblings only, just as snapshot always copies. */
    e->rc=0; assert(out); *out=h->state->work[0];
    (void)gf4_host_end(h,e);
}
i32 gf4_lut_load(struct gf4_ctx *ctx,u8 head)
{
    struct gf4_host *h=gf4_host_current;
    struct gf4_host_event *e=gf4_host_begin(h,D_LUT);
    e->p[0]=ctx; e->a[0]=head;
    return gf4_host_end(h,e);
}
int gf4_dpms_set(struct gf4_ctx *ctx,u8 head,enum gf4_dpms dpms,u32 dl)
{
    struct gf4_host *h=gf4_host_current;
    struct gf4_host_event *e=gf4_host_begin(h,D_DPMS);
    e->p[0]=ctx; e->a[0]=head; e->a[1]=dpms; e->a[2]=dl;
    if (!e->rc) {
        assert(ctx && ctx->state && head<GF4_HEADS);
        struct gf4_head_image *image=ctx->changing ? ctx->state->next : ctx->state->head;
        image[head].dpms=dpms;
    }
    return gf4_host_end(h,e);
}
#endif
#if GF4_HOST_UNIT == 5 || GF4_HOST_UNIT == 13
i32 gf4_heads_apply(struct gf4_ctx *ctx,u32 heads)
{
    struct gf4_host *h=gf4_host_current;
    struct gf4_host_event *e=gf4_host_begin(h,D_HEADS);
    e->p[0]=ctx; e->a[0]=heads;
    if (!e->rc) {
        assert(ctx && ctx->ch && ctx->ch->f);
        ctx->ch->heads=heads; ctx->ch->f->wanted_heads=heads;
    }
    return gf4_host_end(h,e);
}
#endif
/* Final addendum (a): C0-load and C0-tx are real together in UNIT 13.
 * Neither mode_load nor change_begin/end has a fixture definition there.
 * UNIT 5 (B5's caller side) records the C0-tx boundary only (PM 2026-11-04):
 * change_end returns an injected rc if set, else the operation rc. */
#if GF4_HOST_UNIT == 5
i32 gf4_change_begin(struct gf4_ctx *ctx,u32 dl)
{
    struct gf4_host *h=gf4_host_current;
    struct gf4_host_event *e=gf4_host_begin(h,D_CHANGE_BEGIN);
    e->p[0]=ctx; e->a[0]=dl;
    return gf4_host_end(h,e);
}
i32 gf4_change_end(struct gf4_ctx *ctx,i32 operation_rc)
{
    struct gf4_host *h=gf4_host_current;
    struct gf4_host_event *e=gf4_host_begin(h,D_CHANGE_END);
    e->p[0]=ctx; e->a[0]=(u32)operation_rc;
    if (!e->rc) e->rc=operation_rc;
    return gf4_host_end(h,e);
}
#endif
#if GF4_HOST_UNIT == 13
i32 gf4_pllsel_calc(const struct gf4_head_image heads[GF4_HEADS],u32 *out)
{
    struct gf4_host *h=gf4_host_current;
    struct gf4_host_event *e=gf4_host_begin(h,D_PLLSEL);
    e->p[0]=heads; e->p[1]=out;
    if (!e->rc) { assert(heads && out); *out=h->state->work[0].pllsel; }
    return gf4_host_end(h,e);
}
i32 gf4_blank_set(struct gf4_ctx *ctx,u8 head,bool blank,u32 dl)
{
    struct gf4_host *h=gf4_host_current;
    struct gf4_host_event *e=gf4_host_begin(h,D_DPMS);
    e->p[0]=ctx; e->a[0]=head; e->a[1]=blank; e->a[2]=dl;
    if (!e->rc) {
        assert(ctx && ctx->state && head<GF4_HEADS);
        struct gf4_head_image *image=ctx->changing ? ctx->state->next : ctx->state->head;
        image[head].blank=blank;
    }
    return gf4_host_end(h,e);
}
i32 gf4_flip_complete(struct gf4_ctx *ctx,u8 head,u32 dl)
{
    struct gf4_host *h=gf4_host_current;
    struct gf4_host_event *e=gf4_host_begin(h,D_FLIP_COMPLETE);
    e->p[0]=ctx; e->a[0]=head; e->a[1]=dl;
    if (!e->rc) {
        assert(ctx && ctx->state && head<GF4_HEADS);
        struct gf4_flip_hold *hold=&ctx->hold[head];
        if (hold->pending) {
            /* A seeded successful completion confirms the new face.
             * Failure injection leaves both faces and pending untouched. */
            ctx->state->head[head].front=hold->new_front;
            hold->base_verified=true; hold->pending=false;
        }
    }
    return gf4_host_end(h,e);
}
#endif
#if GF4_HOST_UNIT == 5 || GF4_HOST_UNIT == 13
i32 gf4_cursor_apply(struct gf4_ctx *ctx,u8 head,bool visible)
{
    struct gf4_host *h=gf4_host_current;
    struct gf4_host_event *e=gf4_host_begin(h,D_CURSOR_APPLY);
    e->p[0]=ctx; e->a[0]=head; e->a[1]=visible;
    if (!e->rc) {
        assert(ctx && ctx->state && head<GF4_HEADS);
        const struct gf4_head_image *image=ctx->changing ? ctx->state->next : ctx->state->head;
        if (visible && (!image[head].enabled || image[head].dpms!=GF4_ON ||
            image[head].blank || !image[head].cursor_valid)) e->rc=GF4_ESTATE;
    }
    /* Physical visibility only: never overwrite cursor_visible demand. */
    return gf4_host_end(h,e);
}
#endif
#if GF4_HOST_UNIT == 5 || GF4_HOST_UNIT == 10 || GF4_HOST_UNIT == 11 || GF4_HOST_UNIT == 13
i32 gf4_dac_set(struct gf4_ctx *ctx,const struct gf4_route *route,bool on)
{
    struct gf4_host *h=gf4_host_current;
    struct gf4_host_event *e=gf4_host_begin(h,D_DAC);
    e->p[0]=ctx; e->p[1]=route; e->a[0]=on;
    return gf4_host_end(h,e);
}
#endif
#endif
