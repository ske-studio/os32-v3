/* libc-free, link with hwdev/gf/display/ddc.c; A1 UNIT 3.
 * Independent wire oracle: EDID messages are 50/W,offset,50/R,128 bytes;
 * E-DDC prefixes 30/W,segment. Values below come from GF_FEAT4_DESIGN §4,
 * MIT nouveau c03c1a1 nvkm/subdev/i2c/busnv04.c:41-70 (pins),
 * bit.c:152-224 (MSB order, ninth ACK, final NACK, repeated START).
 * No local reference checkout is needed at runtime. */
#define GF4_HOST_UNIT 3
#include "gf4_display_fake.h"

enum {
    EDID_BYTES=128, BYTE_BITS=8, WIRE_BITS=9,
    /* Three messages at most: segment, offset, read address plus data.
     * Leave space for the partial clock preceding STOP/repeated START. */
    FRAMES=4, FRAME_WORDS=EDID_BYTES+2,
    /* Every bit needs < 64 fixture events, plus setup/STOP/restore. */
    EVENT_CAP=(EDID_BYTES+8)*WIRE_BITS*64,
    IO_ERROR=-731, CLEANUP_ERROR=-732,
    DRIVE=0x55, SENSE=0x54, OWNER=0x44,
    SAVED_DRIVE=0xbe, SAVED_OWNER=3,
    SCL=0x20, SDA=0x10, ENABLE=1,
    SENSE_SCL=4, SENSE_SDA=8,
    ENGINE_CTRL=0x600860, ENGINE_IMAGE=0xa55a1010,
    BUS=7, DEADLINE=200000, SENTINEL=0xcc
};
static struct gf4_host host;
static struct gf4_state state;
static struct gf4_host_event events[EVENT_CAP];
static struct gf3_host_reg regs[4];
static u8 payload[EDID_BYTES],output[EDID_BYTES];
struct wire_frame { u16 word[FRAME_WORDS]; u32 rises; };
static struct wire_frame frames[FRAMES];
static u32 starts,stops,read_frame,sense_reads,stretch_reads;
static u32 nack_frame,nack_byte,stop_write,release_write;
static u32 error_rises;
static bool active,stuck,saw_error;
static u8 lines;

static u32 total_rises(void)
{
    u32 n=0;
    for (u32 i=0;i<FRAMES;++i) n+=frames[i].rises;
    return n;
}

static void observe(struct gf4_host *h,const struct gf4_host_event *e)
{
    if (e->rc && !saw_error) {
        saw_error=true; error_rises=total_rises();
    }
    if (e->op==D_I2C_PORT) {
        assert(e->a[0]==BUS);
        assert(!h->calls[D_READ] && !h->calls[D_WRITE]);
    }
    if (e->op!=D_READ && e->op!=D_WRITE) return;
    assert(e->a[0]==GF_MODE_REGISTER_BAR);
    assert(e->a[1]==GF4_HOST_CR || e->a[1]==GF4_HOST_CR+1U);
    assert(e->a[2]==1); /* No head B or ENGINE_CTRL access. */
    if (e->rc && !(e->op==D_WRITE && e->nth==h->applied_write_nth)) return;
    if (e->a[1]!=GF4_HOST_CR+1U) return;
    u8 index=h->index[0][0];
    assert(index==OWNER || index==DRIVE || index==SENSE);
    if (e->op==D_READ && index==SENSE) {
        bool sda=true;
        ++sense_reads;
        if (active && starts && frames[starts-1U].rises) {
            u32 pos=frames[starts-1U].rises-1U;
            u32 byte=pos/WIRE_BITS,bit=pos%WIRE_BITS;
            if (starts==read_frame && byte>=1U && byte<=EDID_BYTES && bit<BYTE_BITS)
                sda=!!(payload[byte-1U]&(0x80U>>bit));
            else if (bit==BYTE_BITS && !(starts==read_frame && byte>=1U))
                sda=starts==nack_frame && byte==nack_byte;
        }
        *(u32 *)e->p[1]=(stuck || sense_reads<=stretch_reads ? 0U : SENSE_SCL)|
            (sda ? SENSE_SDA : 0U);
    }
    if (e->op!=D_WRITE || index!=DRIVE) return;
    u8 next=(u8)e->a[3];
    /* Except the exact final restore, retain unrelated bits and set bit0. */
    if (next!=SAVED_DRIVE) {
        assert((next&~(SCL|SDA|ENABLE))==(SAVED_DRIVE&~(SCL|SDA|ENABLE)));
        assert(next&ENABLE);
    }
    bool was_scl=!!(lines&SCL),now_scl=!!(next&SCL);
    bool was_sda=!!(lines&SDA),now_sda=!!(next&SDA);
    if (was_scl && now_scl && was_sda && !now_sda) {
        assert(starts<FRAMES);
        ++starts; active=true;
    }
    if (was_scl && now_scl && !was_sda && now_sda) {
        ++stops; active=false; stop_write=e->nth;
    }
    if (!was_scl && now_scl && active) {
        struct wire_frame *f=&frames[starts-1U];
        u32 byte=f->rises/WIRE_BITS;
        assert(byte<FRAME_WORDS);
        f->word[byte]=(u16)((f->word[byte]<<1)|now_sda);
        ++f->rises;
    }
    if (now_scl && now_sda) release_write=e->nth;
    lines=next;
}

static void setup(u8 block)
{
    gf4_host_init(&host,&state);
    host.events=events; host.cevents=EVENT_CAP;
    host.base.regs=regs; host.base.cregs=sizeof(regs)/sizeof(regs[0]);
    host.port=(struct gf4_i2c_port){DRIVE,SENSE};
    host.cr[0][DRIVE]=SAVED_DRIVE; host.cr[0][OWNER]=SAVED_OWNER;
    host.ctx.state->head[1].enabled=true; /* Head B must not change the bus path. */
    gf4_host_reg_byte_set(&host,0,ENGINE_CTRL,ENGINE_IMAGE&U8_MAX);
    gf4_host_reg_byte_set(&host,0,ENGINE_CTRL+1U,(ENGINE_IMAGE>>8)&U8_MAX);
    gf4_host_reg_byte_set(&host,0,ENGINE_CTRL+2U,(ENGINE_IMAGE>>16)&U8_MAX);
    gf4_host_reg_byte_set(&host,0,ENGINE_CTRL+3U,ENGINE_IMAGE>>24);
    gf2_host_memset(frames,0,sizeof(frames));
    starts=stops=sense_reads=stretch_reads=0;
    nack_frame=nack_byte=stop_write=release_write=0;
    active=stuck=saw_error=false; error_rises=0; lines=SAVED_DRIVE;
    read_frame=block>=2 ? 3U : 2U;
    for (u32 i=0;i<EDID_BYTES;++i) {
        /* Asymmetric walking data exercises every SDA bit, including ends. */
        payload[i]=(u8)(i*37U+0x53U);
        output[i]=SENTINEL;
    }
    host.observe=observe;
}

static void restored(void)
{
    assert(host.cr[0][DRIVE]==SAVED_DRIVE && host.cr[0][OWNER]==SAVED_OWNER);
    for (u32 i=0;i<4U;++i)
        assert(gf4_host_reg_byte(&host,0,ENGINE_CTRL+i)==((ENGINE_IMAGE>>(i*8U))&U8_MAX));
    assert(host.ctx.ready && host.ctx.accepting && host.ctx.reusable);
    assert(!host.calls[D_ADMISSION] && !host.calls[D_FAILED]);
}

static void unchanged(void)
{
    for (u32 i=0;i<EDID_BYTES;++i) assert(output[i]==SENTINEL);
}

static void tx_frame(u32 frame,const u8 *bytes,u32 n)
{
    assert(frames[frame].rises==n*WIRE_BITS+1U);
    for (u32 i=0;i<n;++i) {
        assert((frames[frame].word[i]>>1)==bytes[i]);
        assert(frames[frame].word[i]&1U); /* Release SDA for slave ACK. */
    }
}

static void success(u8 block,u32 stretch,bool wrap)
{
    setup(block); stretch_reads=stretch;
    u32 dl=DEADLINE;
    if (wrap) { *host.now=U32_MAX-100U; dl=(u32)*host.now+DEADLINE; }
    assert(gf4_ddc_read(&host.ctx,BUS,block,output,dl)==0);
    assert(host.ctx.last_rc==0);
    assert(!gf2_host_memcmp(output,payload,EDID_BYTES));
    assert(starts==read_frame && stops==1);
    u32 offset_frame=0;
    if (block>=2) {
        const u8 segment[]={0x60,(u8)(block>>1)};
        tx_frame(0,segment,sizeof(segment)); offset_frame=1;
    }
    const u8 offset[]={0xa0,(u8)((block&1U) ? 0x80 : 0)};
    tx_frame(offset_frame,offset,sizeof(offset));
    const struct wire_frame *f=&frames[read_frame-1U];
    assert(f->rises==(EDID_BYTES+1U)*WIRE_BITS+1U);
    assert(f->word[0]==((0xa1U<<1)|1U));
    for (u32 i=0;i<EDID_BYTES;++i) {
        /* Master releases all eight input bits, then drives ACK/NACK. */
        assert(f->word[i+1U]==((0xffU<<1)|(i==EDID_BYTES-1U)));
    }
    assert(stop_write && release_write>=stop_write);
    assert((u32)*host.now-dl>U32_MAX/2U); /* No deadline renewal. */
    restored();
}

static void nack_cases(void)
{
    /* Every write-byte ACK, including segment and read-address ACK. */
    for (u32 frame=1;frame<=3U;++frame) {
        u32 n=frame==3U ? 1U : 2U;
        for (u32 byte=0;byte<n;++byte) {
            setup(3); nack_frame=frame; nack_byte=byte;
            assert(gf4_ddc_read(&host.ctx,BUS,3,output,DEADLINE)==GF4_EDDC);
            assert(host.ctx.last_rc==GF4_EDDC);
            assert(starts==frame && stops==1); /* No later message/retry. */
            assert(frames[frame-1U].rises==(byte+1U)*WIRE_BITS+1U);
            unchanged(); restored();
        }
    }
}

static void deadlines(void)
{
    setup(0); stuck=true;
    assert(gf4_ddc_read(&host.ctx,BUS,0,output,25)==GF4_ETIME);
    assert(*host.now==25 && stops==1);
    assert(release_write>=stop_write && stop_write);
    unchanged(); restored();
    setup(0);
    assert(gf4_ddc_read(&host.ctx,BUS,0,output,0)==GF4_ETIME);
    assert(!host.calls[D_READ] && !host.calls[D_WRITE]);
    unchanged(); restored();
    /* Deadline after some data: no partial buffer publication. */
    setup(0);
    assert(gf4_ddc_read(&host.ctx,BUS,0,output,1500)==GF4_ETIME);
    assert(starts==2 && stops==1 && *host.now==1500);
    unchanged(); restored();
}

static void getters(void)
{
    const i32 errors[]={GF4_ENODEV,GF4_EUNSUPPORTED,GF4_EDDC,IO_ERROR};
    for (u32 i=0;i<sizeof(errors)/sizeof(errors[0]);++i) {
        setup(0); host.port_rc=errors[i];
        assert(gf4_ddc_read(&host.ctx,BUS,0,output,DEADLINE)==errors[i]);
        assert(host.ctx.last_rc==errors[i]);
        assert(!host.calls[D_READ] && !host.calls[D_WRITE]);
        unchanged(); restored();
    }
    setup(0);
    assert(gf4_ddc_read(0,BUS,0,output,DEADLINE)==GF4_EINVAL);
    assert(gf4_ddc_read(&host.ctx,BUS,0,0,DEADLINE)==GF4_EINVAL);
    assert(host.ctx.last_rc==GF4_EINVAL);
    assert(!host.nevents);
}

static void failures(void)
{
    /* Harvest call positions from a complete transfer, then inject once
     * at setup, middle of data, and final cleanup for every fallible op. */
    setup(0);
    assert(!gf4_ddc_read(&host.ctx,BUS,0,output,DEADLINE));
    const enum gf4_host_op ops[]={D_READ,D_WRITE,D_CLOCK,D_SLEEP};
    u32 counts[sizeof(ops)/sizeof(ops[0])];
    for (u32 i=0;i<sizeof(ops)/sizeof(ops[0]);++i) counts[i]=host.calls[ops[i]];
    for (u32 i=0;i<sizeof(ops)/sizeof(ops[0]);++i) {
        u32 points[]={1,counts[i]/2U,counts[i]};
        for (u32 j=0;j<sizeof(points)/sizeof(points[0]);++j) {
            setup(0); gf4_host_fail_at(&host,ops[i],points[j],IO_ERROR);
            assert(gf4_ddc_read(&host.ctx,BUS,0,output,DEADLINE)==IO_ERROR);
            assert(host.ctx.last_rc==IO_ERROR); unchanged();
            /* After a rejected operation only the STOP clock can follow;
             * no additional byte, repeated START or retry is sent. */
            assert(saw_error && total_rises()<=error_rises+1U);
            /* A failed final restore itself cannot guarantee restoration. */
            if (!(ops[i]==D_WRITE && points[j]==counts[i])) restored();
        }
    }
    /* Owner write can apply despite error: restoration must still happen. */
    setup(0); gf4_host_write_applied_error(&host,3,IO_ERROR);
    assert(gf4_ddc_read(&host.ctx,BUS,0,output,DEADLINE)==IO_ERROR);
    assert(!starts); unchanged(); restored();

    /* Preserve protocol error when STOP also fails; later restores run. */
    setup(0); nack_frame=1;
    assert(gf4_ddc_read(&host.ctx,BUS,0,output,DEADLINE)==GF4_EDDC);
    u32 first_stop=stop_write;
    setup(0); nack_frame=1;
    gf4_host_fail_at(&host,D_WRITE,first_stop,CLEANUP_ERROR);
    assert(gf4_ddc_read(&host.ctx,BUS,0,output,DEADLINE)==GF4_EDDC);
    assert(host.ctx.last_rc==GF4_EDDC); unchanged(); restored();
}

void _start(void)
{
    success(0,0,false); success(1,3,false);
    success(2,0,false); success(3,0,false); success(255,0,true);
    nack_cases(); deadlines(); getters(); failures();
    gf2_host_exit(0);
}
