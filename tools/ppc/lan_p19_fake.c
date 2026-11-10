#include "gem.h"
static u8 pool[GEM_POOL_SIZE], frame[1514];
static struct gem g;
enum { IDLE, NIC_OWN, WRITTEN, DESC_READY, CPU_READY };
static u8 ownership[32];
static u32 nic, end, available, stage, sync_error, posts, kicks, lastkick;
static u32 fail_point, fail_seen, rejected;
static void stop(const char *s);
void observe_desc(const u8 *p) {
    u32 slot=(u32)(p-(pool+GEM_POOL_RX_RING))/GEM_DESC_SIZE;
    if (ownership[slot]!=NIC_OWN && ownership[slot]!=DESC_READY) stop("H:desc-read\n");
}
void observe_buffer(const u8 *p) {
    u32 slot=(u32)(p-(pool+GEM_POOL_RX_BUF))/GEM_BUF_SIZE;
    if (ownership[slot]!=CPU_READY) stop("H:buffer-read\n");
}
static void out(const char *s) { u32 n=0; while(s[n]) n++; __asm__ volatile("int $0x80"::"a"(4),"b"(1),"c"(s),"d"(n):"memory"); }
static void stop(const char *s) { out(s); __asm__ volatile("int $0x80"::"a"(1),"b"(1)); __builtin_unreachable(); }
#define CHECK(x, msg) do { if (!(x)) stop("H:" msg "\n"); } while (0)
static u32 rd(void *c,u32 r) { (void)c;(void)r;return 0; }
static void wr(void *c,u32 r,u32 v) { (void)c; if(r==GEM_RX_KICK) { if(stage!=3) sync_error=1; stage=0; kicks++; lastkick=v; u32 delta=(v-end)&31; if(delta!=4) sync_error=1; available+=delta; end=v; } }
static int sync_dma(void *p,u32 n,enum dma_sync op) {
    u8 *x=p;
    u32 point=0, slot=0;
    if(x>=pool+GEM_POOL_RX_BUF && x<pool+GEM_POOL_SIZE) {
        slot=(u32)(x-(pool+GEM_POOL_RX_BUF))/GEM_BUF_SIZE;
        if(op==DMA_POSTREAD) point=2;
        if(op==DMA_PREREAD) point=3;
    } else if(x>=pool+GEM_POOL_RX_RING && x<pool+GEM_POOL_RX_RING+512) {
        slot=(u32)(x-(pool+GEM_POOL_RX_RING))/GEM_DESC_SIZE;
        if(op==DMA_POSTREAD) point=1;
        if(op==DMA_PREWRITE) point=n==GEM_DESC_SIZE?4:5;
    }
    if(fail_point==point && point) { fail_seen++; return -1; }
    if(point==1 && ownership[slot]==WRITTEN) ownership[slot]=DESC_READY;
    if(point==2) { if(ownership[slot]!=DESC_READY) sync_error=1; ownership[slot]=CPU_READY; }
    if(point==4) ownership[slot]=NIC_OWN;
    if(x>=pool+GEM_POOL_RX_BUF && x<pool+GEM_POOL_SIZE) {
        if(op==DMA_POSTREAD) posts++;
        if(op==DMA_PREREAD) stage=1;
    } else if(x>=pool+GEM_POOL_RX_RING && x<pool+GEM_POOL_RX_RING+512 && op==DMA_PREWRITE) {
        if(n==GEM_DESC_SIZE) { if(stage!=1) sync_error=1; stage=2; }
        else if(n==512) { if(stage!=2) sync_error=1; stage=3; }
    }
    return 0;
}
static void setup(void) {
    for(u32 i=0;i<GEM_POOL_SIZE;i++) pool[i]=0;
    g=(struct gem){0}; nic=0;end=28;available=28;stage=sync_error=posts=kicks=lastkick=0; fail_point=fail_seen=rejected=0; for(u32 i=0;i<32;i++) ownership[i]=i<28?NIC_OWN:IDLE;
    g.state=GEM_STATE_READY;g.ring_publish_step=3;g.dma.cpu=pool;g.dma.bus=0x12340000;
    g.io.read32=rd;g.io.write32=wr;g.io.sync=sync_dma;
    struct gem_dma d={pool,0,0x12340000,GEM_POOL_SIZE};
    CHECK(gem_ring_prepare(&g,&d)==0,"setup"); stage=sync_error=0;
}
static int inject_try(u32 len,int bad) {
    if (!available) { rejected++; return 0; }
    u8 *d=pool+GEM_POOL_RX_RING+nic*16, *b=pool+GEM_POOL_RX_BUF+nic*GEM_BUF_SIZE;
    u32 addr=(u32)d[8]|((u32)d[9]<<8)|((u32)d[10]<<16)|((u32)d[11]<<24);
    CHECK(available && ownership[nic]==NIC_OWN && addr==g.dma.bus+GEM_POOL_RX_BUF+nic*GEM_BUF_SIZE && (d[3]&0x80),"producer");
    for(u32 i=0;i<len && i<1600;i++) b[i+2]=(u8)(i+nic+1);
    for(u32 i=0;i<8;i++) d[i]=0;
    d[2]=(u8)len;d[3]=(u8)(len>>8);if(bad)d[7]=0x40;
    ownership[nic]=WRITTEN; nic=(nic+1)&31;available--; return 1;
}
static void inject(u32 len,int bad) { CHECK(inject_try(len,bad),"producer"); }
void _start(void) {
    u32 bytes=0; out("REACHED host\n");setup();
    g.state=GEM_STATE_OFF;CHECK(gem_rx(&g,frame,sizeof(frame),&bytes)==GEM_EBUSY,"busy");g.state=GEM_STATE_READY;
    CHECK(gem_rx(&g,frame,sizeof(frame),&bytes)==GEM_AGAIN && posts==0,"A7 rx-owned");
    inject(14,0);CHECK(gem_rx(&g,frame,sizeof(frame),&bytes)==0 && bytes==14 && frame[0]==1 && frame[13]==14,"offset");
    CHECK(posts==1,"postread");
    CHECK(pool[GEM_POOL_RX_RING+28*16+2]==0 && pool[GEM_POOL_RX_RING+28*16+3]==0x88 && pool[GEM_POOL_RX_RING+28*16+8]==0 && pool[GEM_POOL_RX_RING+28*16+9]==0xe0 && pool[GEM_POOL_RX_RING+28*16+10]==0x36 && pool[GEM_POOL_RX_RING+28*16+11]==0x12,"golden");
    inject(1514,0);CHECK(gem_rx(&g,frame,sizeof(frame),&bytes)==0 && bytes==1514,"max");
    inject(13,0);inject(1515,0);inject(14,1);inject(14,0);
    CHECK(gem_rx(&g,frame,sizeof(frame),&bytes)==0 && g.rx_drops==3,"bad");
    inject(14,0);CHECK(gem_rx(&g,frame,13,&bytes)==GEM_ERANGE,"cap");inject(14,0);CHECK(gem_rx(&g,frame,sizeof(frame),&bytes)==0,"continue");
    for(u32 i=0;i<9;i++) inject(13,0);
    u32 before=g.rx_head;CHECK(gem_rx(&g,frame,sizeof(frame),&bytes)==GEM_AGAIN && g.rx_head==((before+8)&31),"A11 budget");
    CHECK(gem_rx(&g,frame,sizeof(frame),&bytes)==GEM_AGAIN,"remaining");
    setup();
    for(u32 i=0;i<128;i++) { inject(14,0); CHECK(gem_rx(&g,frame,sizeof(frame),&bytes)==0,"receive"); CHECK(kicks==(i+1)/4,"kick-count"); if((i+1)%4==0) CHECK(lastkick==((i+1+28)&31),"kick-value"); CHECK(!sync_error,"sync-order"); }
    setup();
    for(u32 i=0;i<28;i++) inject(14,0);
    CHECK(available==0 && nic==end && !inject_try(14,0) && rejected==1,"boundary");
    for(u32 i=0;i<3;i++) CHECK(gem_rx(&g,frame,sizeof(frame),&bytes)==0 && available==0,"boundary");
    CHECK(gem_rx(&g,frame,sizeof(frame),&bytes)==0 && available==4 && end==0,"boundary");
    for(u32 i=0;i<4;i++) inject(14,0);
    CHECK(available==0 && nic==end,"boundary");
    for(u32 point=1;point<=5;point++) {
        setup();
        if(point==5) for(u32 i=0;i<3;i++) { inject(14,0); CHECK(gem_rx(&g,frame,sizeof(frame),&bytes)==0,"failure-setup"); }
        inject(14,0); u32 oldkick=kicks, oldhead=g.rx_head, oldpackets=g.rx_packets, olddrops=g.rx_drops;
        fail_point=point;
        CHECK(gem_rx(&g,frame,sizeof(frame),&bytes)==GEM_EIO && fail_seen==1,"failure-result");
        CHECK(kicks==oldkick && g.rx_head==((oldhead+(point==5))&31) && g.rx_packets==oldpackets+(point>=3) && g.rx_drops==olddrops,"failure-state");
    }
    out("PASS host\n");__asm__ volatile("int $0x80"::"a"(1),"b"(0));__builtin_unreachable();
}
