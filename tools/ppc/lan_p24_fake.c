/* Whole service TU, with only hardware/clock/serial boundaries modeled.
 * Including it preserves its static state for assertions; no body is extracted. */
#include "lan_service.c"
#include "kboot_info.h"
#include "kboot_log.h"
#include "ppc_pd.h"
#include "mm/paging.h"
#include <stdarg.h>

static void out(const char *p) {
    u32 n=0; while(p[n]) n++;
    __asm__ volatile("int $0x80"::"a"(4),"b"(1),"c"(p),"d"(n):"memory");
}
static void die(const char *p) {
    out(p); out("\n");
    __asm__ volatile("int $0x80"::"a"(1),"b"(1)); __builtin_unreachable();
}
#define CHECK(x,m) do { if (!(x)) die("H:" m); } while(0)
void *memcpy(void *p,const void *q,u32 n) { u8 *a=p; const u8 *b=q; while(n--) *a++=*b++; return p; }
void *memset(void *p,int v,u32 n) { u8 *a=p; while(n--) *a++=(u8)v; return p; }
void *kmemcpy(void *p, const void *q, u32 n) { return memcpy(p,q,n); }
static u32 fed, lost;
void pm_terminal_feed(const u8 *p, u32 n) { (void)p; fed += n; }
void pm_terminal_line_lost(void) { lost++; }
void pm_terminal_open(void) { pm_terminal_line_lost(); }
void *kmemset(void *p, int c, u32 n) { return memset(p, c, n); }
static u32 regs[0x8000/4], phy[32], mif, completion, hold, down, freeze, tb_calls;
static u32 dma_calls, dma_first, copies, postwrites, sends[9], nic_rx;
static u64 clock_tb, now;
static const u8 host_mac[6]={2,0,0,0,0,2};
static u8 *pool;
static u8 last_dst[6], log_bytes[20000];
static u32 cache_lines, pte_flags;
static u32 log_used;
#ifdef TERM_TEST
static u8 order[64], term_dst[6];
static u32 order_n;
#endif
static struct lan_message last[9];
static struct pm_kboot_info info;
static struct pm_gmac_boot boot;
static const struct pm_log_sink *sink;
static u32 marks[40], links[40], mark_count, off_count;
volatile u32 tick_count;
static void decimal(u32 v) { char b[12]; u32 n=0; do { b[n++]=(char)('0'+v%10); v/=10; } while(v); while(n) { char a[2]={b[--n],0}; out(a); } }
void kprintf(u8 attr,const char *fmt,...) {
    va_list ap; (void)attr; va_start(ap,fmt);
    if (fmt[4]=='S') {
        u32 a=va_arg(ap,u32), b=va_arg(ap,u32);
        CHECK(mark_count<40,"marks-overflow"); marks[mark_count]=a; links[mark_count++]=b;
        out("LAN STATE state="); decimal(a); out(" link="); decimal(b); out("\n");
    } else { int a=va_arg(ap,int); CHECK(a<0,"off-code"); off_count++; out("LAN OFF code=-"); decimal((u32)-a); out("\n"); }
    va_end(ap);
}
u32 ppc_irq_save(void) { return 1; }
void ppc_irq_restore(u32 f) { CHECK(f==1,"irq"); }
const struct pm_kboot_info *ppc_boot_info(void) { return &info; }
const struct ppc_boot_devices *ppc_boot_devices(void) { static const struct ppc_boot_devices d={0}; return &d; }
u32 ppc_root_current(void) { return 0x1000; }
int ppc_pd_lookup_read(u32 root,u32 va,int store,int user,struct ppc_translation *t,ppc_pd_read_fn rd,void *ctx) {
    (void)rd; (void)ctx; CHECK(root==0x1000 && !store && !user,"translate"); t->pa=va; return 1;
}
int platform_clock_read_us(u32 *lo,u32 *hi) { *lo=tick_count*1000; *hi=0; return 0; }
int pm_lan_copy8(u32 va,u8 *p) { copies++; *p=(u8)va; return 0; }
int pm_log_add_sink(const struct pm_log_sink *p) { sink=p; return 0; }
int pm_pci_read32(struct pm_pci *p,u32 bdf,u32 reg,u32 *v) { (void)p; (void)bdf; *v=reg==0?0x0021106b:0; return 0; }
/* Observe the real DMA initializer through the linker, replacing only cache
 * instructions and page-table I/O below it. Its reuse decision stays product. */
int __real_pm_gmac_dma_init(const struct pm_gmac_boot *,struct gem_dma *);
int __wrap_pm_gmac_dma_init(const struct pm_gmac_boot *b,struct gem_dma *d) {
    CHECK(b==&boot,"dma-boot"); dma_calls++;
    int rc=__real_pm_gmac_dma_init(b,d);
    if(!rc) pool=d->cpu;
    return rc;
}
void ppc_dcbf_line(void *p) { (void)p; cache_lines++; }
void ppc_sync(void) { }
int paging_map_range(u32 a,u32 b,u32 c,u32 f) {
    CHECK(a==c && b-a==65536 && cache_lines==2048,"dma-map");
    dma_first++; pte_flags=f; return 0;
}
u32 paging_pte_raw(u32 a) { return a|pte_flags; }
static u64 tb(void *ctx) { (void)ctx; tb_calls++; if(!freeze) clock_tb++; return clock_tb; }
static u32 rd(void *ctx,u32 reg) {
    (void)ctx;
    if(reg==GEM_MIF_FRAME) {
        u32 addr=(mif>>23)&31, r=(mif>>18)&31, v=0xffff;
        if(addr==0) {
            if((mif&0xf0000000)==(GEM_MIF_FRAME_WRITE&0xf0000000)) phy[r]=mif&65535;
            if(r==0 && phy[0]==0x8000) phy[0]=0;
            v=r==2?0x0040:r==3?0x6210:r==1?(down?0x100:0x124):r==5?0x101:phy[r];
        }
        return mif|GEM_MIF_FRAME_TA0|v;
    }
    if(reg==GEM_TX_COMPLETION) return completion;
    if(reg==GEM_PCI_BANK2_OFFSET+GEM_RESET) return 0;
    return regs[reg/4];
}
static void wr(void *ctx,u32 reg,u32 v) {
    (void)ctx; regs[reg/4]=v;
    if(reg==GEM_MIF_FRAME) mif=v;
    if(reg==GEM_PCI_BANK2_OFFSET+GEM_RESET) { completion=0; nic_rx=0; }
    if(reg==GEM_TX_KICK && s.g.ring_publish_step==3) {
        u32 slot=(v+31)&31;
        u8 *d=pool+slot*16, *b=pool+GEM_POOL_TX_BUF+slot*GEM_BUF_SIZE;
        u32 len=(u32)d[0]|((u32)d[1]<<8);
        struct lan_message m;
        CHECK(!pm_lan_decode(b+14,len-14,&m),"tx-codec");
        CHECK(m.type<9,"tx-type"); last[m.type]=m; sends[m.type]++;
        if(m.type==PLN1_TYPE_RSP) memcpy(last_dst,b,6);
#ifdef TERM_TEST
        if (m.type==PLN1_TERM_OUT || m.type==PLN1_TERM_STATE) memcpy(term_dst,b,6);
        if ((m.type==PLN1_TYPE_LOG || m.type==PLN1_TERM_OUT) && order_n<64) order[order_n++]=m.type;
#endif
        if(m.type==PLN1_TYPE_LOG) { CHECK(log_used+m.payload_bytes<=sizeof(log_bytes),"log-size"); memcpy(log_bytes+log_used,m.payload,m.payload_bytes); log_used+=m.payload_bytes; }
        if(!hold) completion=v;
    }
}
static int sync_dma(void *p,u32 n,enum dma_sync op) { (void)p; (void)n; if(op==DMA_POSTWRITE) postwrites++; return 0; }
static int master(void *ctx) { (void)ctx; return 0; }
int pm_lan_gem_io(const struct pm_gmac_boot *b,struct gem_io *io) {
    CHECK(b==&boot,"io-boot"); *io=(struct gem_io){.read32=rd,.write32=wr,.tb=tb,.tb_hz=1000000,.sync=sync_dma,.master_enable=master}; return 0;
}
static void poll_once(void) { now+=1000; clock_tb=now; tick_count++; pm_lan_poll(now); }
static void until(u32 state) { for(u32 i=0;i<200 && (u32)s.state!=state;i++) poll_once(); CHECK((u32)s.state==state,"state-deadline"); }
static void inject(const struct lan_message *m,u32 bad,u32 filter) {
    u8 f[350]={0}; u32 n=0;
    memcpy(f,boot.mac,6); memcpy(f+6,host_mac,6);
    if(filter==1) f[0]^=2;
    if(filter==2) memset(f+6,0,6);
    if(filter==3) f[6]|=1;
    if(filter==5) f[11]^=1;
    f[12]=0x88; f[13]=0xb6;
    CHECK(!pm_lan_encode(f+14,sizeof(f)-14,m,&n),"encode"); n+=14;
    u8 *d=pool+GEM_POOL_RX_RING+nic_rx*16;
    CHECK(d[3]&0x80,"rx-owned-model");
    memcpy(pool+GEM_POOL_RX_BUF+nic_rx*GEM_BUF_SIZE+2,f,n);
    memset(d,0,8); d[2]=(u8)n; d[3]=(u8)(n>>8); if(bad) d[7]=0x40;
    nic_rx=(nic_rx+1)&31;
}
static struct lan_message request(u32 op) {
    static u32 id;
    struct lan_message q={0}; q.type=3; q.opcode=(u16)op; q.request_id=++id; q.seq=id;
    memcpy(q.boot_id,boot.boot_id,16); q.host_nonce[0]=0x42;
    if(op==4) { q.payload_bytes=4; q.payload[1]=128; q.payload[3]=28; }
    if(op==5) { q.payload_bytes=8; q.payload[1]=0x10; q.payload[4]=1; }
    return q;
}
static u32 be32(const u8 *p) { return ((u32)p[0]<<24)|((u32)p[1]<<16)|((u32)p[2]<<8)|p[3]; }
static void log_head(u64 *seq) {
    const u8 *p; u32 n,f; u64 a,b;
    CHECK(pm_lan_log_peek(&p,&n,seq,&a,&b,&f)==LAN_OK,"A8 loss-count");
}
static void scenario(void) {
    boot.profile=PM_GMAC_PROFILE_UNINORTH; boot.dma_bytes=0xffffffff;
    boot.vendor_device=0x0021106b; boot.mac[0]=2; boot.mac[5]=1; boot.peer_mac[0]=2; boot.peer_mac[5]=2; boot.boot_id[0]=17;
    info.tb_hz=1000000; info.admitted_end=0xf00000; info.image_first=0x100000; info.image_end=0x200000;
    CHECK(pm_lan_log_register()==0 && sink,"sink");
    CHECK(!sink->write(sink->ctx,"KBOOT fixture\r\n",15),"sink-write");
    u64 boot_seq; log_head(&boot_seq); CHECK(boot_seq==0,"boot-seq");
    CHECK(pm_lan_start(&boot)==0,"start"); until(RUNNING);
    CHECK(mark_count==5,"state-count"); for(u32 i=0;i<5;i++) CHECK(marks[i]==i+1 && links[i]==(i==4),"state-sequence");
    poll_once(); CHECK(sends[1]==1 && last[1].seq==0,"beacon-first");
    CHECK(log_used==15 && same(log_bytes,(const u8 *)"KBOOT fixture\r\n",15),"boot-raw");
#ifdef TERM_TEST
    { struct lan_message t={0}; t.type=PLN1_TERM_OPEN; memcpy(t.boot_id,boot.boot_id,16); t.host_nonce[0]=7;
      boot.peer_mac[5]=3; inject(&t,0,0); poll_once();
      CHECK(sends[PLN1_TERM_STATE]==1 && lost==1 && same(term_dst,host_mac,6),"term-open");
      t.type=PLN1_TERM_IN; t.payload_bytes=1; t.payload[0]='a'; inject(&t,0,0); poll_once();
      CHECK(fed==1,"term-input");
      static u8 data[1024]; pm_term_write(data,sizeof(data));
      CHECK(!sink->write(sink->ctx,(const char *)data,sizeof(data)),"term-log");
      order_n=0; out("reached=alternate\n"); poll_once();
      CHECK(order_n==4 && order[0]!=order[1] && order[0]==order[2] && order[1]==order[3],"alternate");
      while (pm_term_tx_peek(&t)) poll_once();
      while (pm_lan_log_peek((const u8 **)&(const u8 *){0}, &(u32){0}, &(u64){0}, &(u64){0}, &(u64){0}, &(u32){0})==LAN_OK) poll_once();
      out("reached=state-interval\n"); term_state(now); u32 states=sends[PLN1_TERM_STATE];
      now+=1000; clock_tb=now; pm_lan_poll(now); CHECK(sends[PLN1_TERM_STATE]==states,"state-interval");
      now=s.state_at; clock_tb=now; pm_lan_poll(now); CHECK(sends[PLN1_TERM_STATE]==states+1,"state-due");
      boot.peer_mac[5]=2;
    }
#endif
    out("reached=filters\n");
    for(u32 f=1;f<=5;f++) {
        struct lan_message q=request(1); if(f==4) q.boot_id[0]^=1;
        u32 before=sends[4]; inject(&q,0,f); poll_once();
        CHECK(sends[4]==before && !s.response.type,"A10 filter");
    }
    out("reached=diagnostics\n");
    for(u32 op=1;op<=5;op++) {
        struct lan_message q=request(op); u32 before=sends[4]; inject(&q,0,0); poll_once();
        struct lan_message *r=&last[4];
        CHECK(sends[4]==before+1 && same(last_dst,boot.peer_mac,6) && r->request_id==q.request_id && r->opcode==op && r->seq==q.seq && same(r->boot_id,q.boot_id,16) && same(r->host_nonce,q.host_nonce,16) && !r->status,"diagnostic-envelope");
        const u32 lengths[5]={68,24,32,28,256}; CHECK(r->payload_bytes==lengths[op-1],"diagnostic-length");
        if(op==1) CHECK(be32(r->payload)==5 && be32(r->payload+4)==0x40 && be32(r->payload+8)==0x6210,"status");
        if(op==2) CHECK(be32(r->payload)==tick_count && be32(r->payload+4)==1000000,"clock");
        if(op==3) CHECK(be32(r->payload+4)==info.admitted_end && be32(r->payload+24)==s.g.dma.pa,"memory");
        if(op==4) CHECK(same(r->payload,boot.boot_id,16) && same(r->payload+16,boot.mac,6) && same(r->payload+22,boot.peer_mac,6),"read-diag");
        if(op==5) for(u32 i=0;i<256;i++) CHECK(r->payload[i]==(u8)i,"peek");
    }
    /* A broadcast log peer accepts this unicast host, but replies must use
     * the request source, never the broadcast log destination. */
    memset(boot.peer_mac,255,6);
    { struct lan_message q=request(1); u32 before=sends[4];
      inject(&q,0,0); poll_once();
      CHECK(sends[4]==before+1 && same(last_dst,host_mac,6),"response-unicast"); }
    memcpy(boot.peer_mac,host_mac,6);
    /* Fill actual TX descriptors so service observes real GEM_AGAIN. */
    hold=1; completion=s.g.tx_head;
    for(u32 i=0;i<40 && s.g.tx_pending<31;i++) {
        CHECK(!sink->write(sink->ctx,"x",1),"fill-log"); poll_once();
    }
    CHECK(s.g.tx_pending==31,"tx-full");
    CHECK(!sink->write(sink->ctx,"retained",8),"retain-write");
    u64 seq_before,seq_after; log_head(&seq_before);
    out("reached=log-again\n"); poll_once(); log_head(&seq_after);
    CHECK(seq_before==seq_after,"A8 loss-count");
    out("reached=response-full\n");
    struct lan_message q=request(1); inject(&q,0,0); poll_once(); CHECK(s.response.type==4,"response-pending");
    q=request(5); q.host_nonce[0]++; u32 old_copies=copies; u64 drops=s.request_drops;
    inject(&q,0,0); poll_once();
    CHECK(s.request_drops==drops+1 && copies==old_copies && s.response.request_id!=q.request_id,"A10 response-full");
    out("reached=rx-budget\n");
    for(u32 i=0;i<9;i++) inject(&q,1,0);
    u32 head=s.g.rx_head; u64 bad=s.g.rx_drops; poll_once();
    CHECK(s.g.rx_head==((head+8)&31) && s.g.rx_drops==bad+8,"A11 budget");
    poll_once(); CHECK(s.g.rx_drops==bad+9,"rx-remainder");
    for(u32 i=0;i<9;i++) inject(&q,i!=7,0);
    head=s.g.rx_head; bad=s.g.rx_drops; poll_once();
    CHECK(s.g.rx_head==((head+8)&31) && s.g.rx_drops==bad+7,"A11 mixed-budget");
    poll_once(); CHECK(s.g.rx_drops==bad+8,"mixed-remainder");
    out("reached=budgets\n");
    hold=0; completion=s.g.tx_head; u32 reclaimed=postwrites, responses=sends[4]; poll_once();
    CHECK(postwrites-reclaimed==8 && !s.response.type && sends[4]==responses+1,"tx8-rsp1");
    { const u8 *p; u32 n,f; u64 a,b;
      CHECK(pm_lan_log_peek(&p,&n,&seq_after,&a,&b,&f)==LAN_AGAIN,"log-ok-remove"); }
    static char bulk[2048]; memset(bulk,'L',sizeof(bulk)); CHECK(!sink->write(sink->ctx,bulk,sizeof(bulk)),"bulk");
    u32 sent=sends[2]; poll_once(); CHECK(sends[2]-sent==4,"log4");
    sent=sends[2]; pm_lan_poll(now); pm_lan_poll(now+999); CHECK(sends[2]==sent,"interval");
    poll_once(); CHECK(sends[2]-sent==4,"log4-second");
    /* A link failure enters reset before any send. Retain queued log. */
    CHECK(!sink->write(sink->ctx,"reconnect",9),"reconnect-write");
    down=1; now=s.phy_retry_at; clock_tb=now; sent=sends[2]; pm_lan_poll(now);
    CHECK(s.state==RESETTING && sends[2]==sent,"link-reset");
    out("reached=reset-cursors\n"); until(PREPARE);
    CHECK(!s.g.ring_prepare_step && !s.g.ring_cursor && !s.g.ring_publish_step && !s.g.rx_head,"reset-cursors");
    down=0; until(RUNNING);
    out("reached=beacon-reconnect\n");
    u32 beacons=sends[1]; poll_once();
    CHECK(sends[1]==beacons || last[1].seq>0,"beacon-reconnect");
    CHECK(dma_calls==2 && dma_first==1 && cache_lines==2048 && s.g.rx_head==0 && s.g.ring_cursor==28,"dma-reuse");
    CHECK(sends[2]>sent,"reconnect-log");
    u64 next,records,bytes; u32 flags; pm_lan_log_stats(&next,&records,&bytes,&flags);
    now=s.beacon_at; clock_tb=now; pm_lan_poll(now);
    CHECK(sends[1]>=2 && next>0 && last[1].seq==next,"beacon-reconnect");
    out("reached=fifo-full\n");
    static char full[16384+256]; memset(full,'F',sizeof(full));
    u32 saved_bytes=log_used;
    CHECK(!sink->write(sink->ctx,full,sizeof(full)),"full-write");
    pm_lan_log_stats(&next,&records,&bytes,&flags);
    CHECK(records==1 && bytes==256,"full-loss");
    for(u32 i=0;i<16;i++) poll_once();
    CHECK(log_used==saved_bytes+16384,"full-retained");
    for(u32 i=saved_bytes;i<log_used;i++) CHECK(log_bytes[i]=='F',"full-raw");
    /* Stopped TB: MIF spin is bounded even though service's now advances.
     * With now also stopped, each call returns at the 1ms gate. */
    out("reached=tb-stopped\n"); freeze=1; u32 calls=tb_calls;
    now=s.phy_retry_at; pm_lan_poll(now); CHECK(tb_calls-calls<=50000,"tb-finite");
    calls=tb_calls; for(u32 i=0;i<100;i++) pm_lan_poll(now);
    CHECK(tb_calls==calls,"tb-gate");
    CHECK(!off_count,"unexpected-off");
    out("PASS\n");
}
void _start(void) { scenario(); __asm__ volatile("int $0x80"::"a"(1),"b"(0)); __builtin_unreachable(); }
