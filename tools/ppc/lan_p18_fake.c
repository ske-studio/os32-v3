#include "gem.h"
static u8 pool[GEM_POOL_SIZE], frame[1514];
static struct gem g;
static u32 completion, kick, stage, sync_error, status, tx_status, mac_state;
struct event { enum dma_sync op; void *ptr; u32 size, tail; u8 data[GEM_BUF_SIZE]; };
static struct event events[128];
static u32 event_count;
static unsigned long long clock_value;
static void out(const char *s) { unsigned n=0; while(s[n]) n++; __asm__ volatile("int $0x80"::"a"(4),"b"(1),"c"(s),"d"(n):"memory"); }
static void stop(const char *s) { out(s); __asm__ volatile("int $0x80"::"a"(1),"b"(1)); __builtin_unreachable(); }
#define CHECK(x, msg) do { if (!(x)) stop("H:" msg "\n"); } while (0)
static u32 rd(void *ctx, u32 reg) { (void)ctx; return reg == GEM_TX_COMPLETION ? completion : reg == GEM_STATUS_ALIAS ? status : reg == GEM_MAC_TX_STATUS ? tx_status : reg == GEM_MAC_MAC_STATE ? mac_state : 0; }
static void wr(void *ctx, u32 reg, u32 v) { (void)ctx; if (reg == GEM_TX_KICK) { if(stage != 2) sync_error=1; stage=0; kick=v; } }
static unsigned long long tb(void *ctx) { (void)ctx; return clock_value; }
static int sync_dma(void *ptr, u32 size, enum dma_sync op) {
    struct event *e = &events[event_count++];
    e->op=op; e->ptr=ptr; e->size=size; e->tail=g.tx_tail;
    for (u32 i=0; i<size && i<GEM_BUF_SIZE; i++) e->data[i]=((u8 *)ptr)[i];
    if(op == DMA_PREWRITE) {
        if ((u8 *)ptr >= pool + GEM_POOL_TX_BUF && (u8 *)ptr < pool + GEM_POOL_RX_BUF) stage=1;
        else if ((u8 *)ptr >= pool && (u8 *)ptr < pool + GEM_POOL_RX_RING) { if(stage != 1) sync_error=1; stage=2; }
    }
    return 0;
}
static void setup(void) {
    g = (struct gem){0}; completion=kick=stage=sync_error=0; clock_value=100; event_count=status=tx_status=mac_state=0;
    g.state=GEM_STATE_READY; g.ring_publish_step=3; g.dma.cpu=pool; g.dma.bus=0x12340000;
    g.io.read32=rd; g.io.write32=wr; g.io.sync=sync_dma; g.io.tb=tb; g.io.tb_hz=100;
}
void _start(void) {
    out("REACHED host\n"); setup();
    g.state=GEM_STATE_OFF; CHECK(gem_tx(&g,frame,14)==GEM_EBUSY,"busy"); g.state=GEM_STATE_READY;
    CHECK(gem_tx(&g,frame,13)==GEM_EINVAL && gem_tx(&g,frame,1515)==GEM_EINVAL,"bounds");
    for(u32 i=0;i<1514;i++) frame[i]=(u8)(i+1);
    CHECK(gem_tx(&g,frame,14)==0,"send");
    CHECK(!sync_error && stage==0,"A4 dma-unpublished");
    CHECK(kick==1,"kick");
    CHECK(event_count==2 && events[0].op==DMA_PREWRITE && events[0].ptr==pool+GEM_POOL_TX_BUF && events[0].size>=60 && events[1].op==DMA_PREWRITE && events[1].ptr==pool && events[1].size>=16,"sync-range");
    for(u32 i=0;i<14;i++) CHECK(events[0].data[i]==frame[i],"sync-copy");
    for(u32 i=14;i<60;i++) CHECK(events[0].data[i]==0,"sync-pad");
    { static const u8 expected[16]={60,0,0,0xc0,0,0,0,0,0,0,0x35,0x12,0,0,0,0};
      for(u32 i=0;i<16;i++) CHECK(events[1].data[i]==expected[i],"descriptor"); }
    for(u32 i=0;i<14;i++) CHECK(pool[GEM_POOL_TX_BUF+i]==frame[i],"copy");
    for(u32 i=14;i<60;i++) CHECK(pool[GEM_POOL_TX_BUF+i]==0,"pad");
    CHECK(gem_tx(&g,frame,1514)==0,"max");
    for(u32 i=2;i<31;i++) CHECK(gem_tx(&g,frame,14)==0,"fill");
    CHECK(gem_tx(&g,frame,14)==GEM_AGAIN,"full");
    event_count=0; completion=31; CHECK(gem_tx_reclaim(&g,200,2)==0 && g.tx_pending==29 && g.tx_tail==2,"budget");
    CHECK(event_count==2 && events[0].op==DMA_POSTWRITE && events[0].ptr==pool+GEM_POOL_TX_BUF && events[0].size>=GEM_BUF_SIZE && events[0].tail==0 && events[1].op==DMA_POSTWRITE && events[1].ptr==pool+GEM_POOL_TX_BUF+GEM_BUF_SIZE && events[1].size>=GEM_BUF_SIZE && events[1].tail==1,"postwrite");
    CHECK(gem_tx_reclaim(&g,201,30)==0 && g.tx_pending==21 && g.tx_tail==10,"limit");
    while(g.tx_pending) CHECK(gem_tx_reclaim(&g,202,8)==0,"reclaim");
    CHECK(gem_tx(&g,frame,14)==0 && gem_tx(&g,frame,14)==0 && g.tx_head==1 && kick==1,"wrap");
    completion=2; CHECK(gem_tx_reclaim(&g,203,8)==GEM_EIO,"overrun");
    completion=0; CHECK(gem_poll(&g,204,1)==0 && g.tx_pending==1 && g.tx_tail==0,"poll");
    CHECK(gem_tx_reclaim(&g,703,8)==0,"deadline");
    CHECK(gem_tx_reclaim(&g,704,8)==GEM_AGAIN && g.state==GEM_STATE_RESETTING && g.uncertain_tx==1 && !g.tx_head && !g.tx_tail && !g.tx_pending,"timeout");
    CHECK(gem_poll(&g,705,8)==GEM_AGAIN && g.uncertain_tx==1,"timeout-once");
    setup(); g.tx_head=5; g.tx_tail=2; g.tx_pending=3; g.uncertain_tx=4;
    status=GEM_INTR_TX_MAC; tx_status=GEM_MAC_TX_UNDERRUN;
    CHECK(gem_poll(&g,100,8)==GEM_AGAIN && g.uncertain_tx==7 && !g.tx_head && !g.tx_tail && !g.tx_pending,"tx-reset");
    CHECK(gem_poll(&g,101,8)==GEM_AGAIN && g.uncertain_tx==7,"tx-reset-once");
    setup(); g.tx_head=7; g.tx_tail=3; g.tx_pending=4; g.uncertain_tx=2;
    g.rx_watching=1; g.rx_watch_started=0; mac_state=GEM_MAC_STATE_OVERFLOW;
    CHECK(gem_poll(&g,40,8)==GEM_AGAIN && g.uncertain_tx==6 && !g.tx_head && !g.tx_tail && !g.tx_pending,"rx-reset");
    CHECK(gem_poll(&g,41,8)==GEM_AGAIN && g.uncertain_tx==6,"rx-reset-once");
    out("PASS host\n"); __asm__ volatile("int $0x80"::"a"(1),"b"(0)); __builtin_unreachable();
}
