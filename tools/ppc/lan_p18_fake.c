#include "gem.h"
static u8 pool[GEM_POOL_SIZE], frame[1514];
static struct gem g;
static u32 completion, kick, stage, sync_error;
static unsigned long long clock_value;
static void out(const char *s) { unsigned n=0; while(s[n]) n++; __asm__ volatile("int $0x80"::"a"(4),"b"(1),"c"(s),"d"(n):"memory"); }
static void stop(const char *s) { out(s); __asm__ volatile("int $0x80"::"a"(1),"b"(1)); __builtin_unreachable(); }
#define CHECK(x, msg) do { if (!(x)) stop("H:" msg "\n"); } while (0)
static u32 rd(void *ctx, u32 reg) { (void)ctx; return reg == GEM_TX_COMPLETION ? completion : 0; }
static void wr(void *ctx, u32 reg, u32 v) { (void)ctx; if (reg == GEM_TX_KICK) { if(stage != 2) sync_error=1; stage=0; kick=v; } }
static unsigned long long tb(void *ctx) { (void)ctx; return clock_value; }
static int sync_dma(void *ptr, u32 size, enum dma_sync op) {
    (void)size;
    if(op == DMA_PREWRITE) {
        if ((u8 *)ptr >= pool + GEM_POOL_TX_BUF && (u8 *)ptr < pool + GEM_POOL_RX_BUF) stage=1;
        else if ((u8 *)ptr >= pool && (u8 *)ptr < pool + GEM_POOL_RX_RING) { if(stage != 1) sync_error=1; stage=2; }
    }
    return 0;
}
static void setup(void) {
    g = (struct gem){0}; completion=kick=stage=sync_error=0; clock_value=100;
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
    CHECK(pool[0]==60 && pool[1]==0 && pool[2]==0 && pool[3]==0xc0 && pool[8]==0 && pool[9]==0 && pool[10]==0x35 && pool[11]==0x12 && pool[12]==0 && pool[13]==0 && pool[14]==0 && pool[15]==0,"descriptor");
    for(u32 i=0;i<14;i++) CHECK(pool[GEM_POOL_TX_BUF+i]==frame[i],"copy");
    for(u32 i=14;i<60;i++) CHECK(pool[GEM_POOL_TX_BUF+i]==0,"pad");
    CHECK(gem_tx(&g,frame,1514)==0,"max");
    for(u32 i=2;i<31;i++) CHECK(gem_tx(&g,frame,14)==0,"fill");
    CHECK(gem_tx(&g,frame,14)==GEM_AGAIN,"full");
    completion=31; CHECK(gem_tx_reclaim(&g,200,2)==0 && g.tx_pending==29 && g.tx_tail==2,"budget");
    CHECK(gem_tx_reclaim(&g,201,30)==0 && g.tx_pending==21 && g.tx_tail==10,"limit");
    while(g.tx_pending) CHECK(gem_tx_reclaim(&g,202,8)==0,"reclaim");
    CHECK(gem_tx(&g,frame,14)==0 && gem_tx(&g,frame,14)==0 && g.tx_head==1 && kick==1,"wrap");
    completion=2; CHECK(gem_tx_reclaim(&g,203,8)==GEM_EIO,"overrun");
    completion=0; CHECK(gem_poll(&g,204,1)==0 && g.tx_pending==1 && g.tx_tail==0,"poll");
    CHECK(gem_tx_reclaim(&g,703,8)==0,"deadline");
    CHECK(gem_tx_reclaim(&g,704,8)==GEM_AGAIN && g.state==GEM_STATE_RESETTING && g.uncertain_tx==1,"timeout");
    out("PASS host\n"); __asm__ volatile("int $0x80"::"a"(1),"b"(0)); __builtin_unreachable();
}
