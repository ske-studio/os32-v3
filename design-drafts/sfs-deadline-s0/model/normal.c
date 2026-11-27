#include "sys/dev/net/e82557/e82557.c"
#include "sys/dev/net/e82557/nic_e82557.c"
#include "sys/fs/serialfs/sfs_lan.h"
#include "sys/fs/serialfs/sfs_client.h"
#include "sys/kern/io_clock.h"
#include "sys/hal/time/tick_clock.h"
#include "sys/kern/irq/tick.h"
#include "sys/kern/irq/tick_budget.h"
#include "tick_order.h"
extern int printf(const char *, ...);
extern void abort(void);
extern const SfsIo *model_clock(void);
unsigned model_if = 1;
volatile u32 kctx_irq_depth;
static u8 dma[E82557_DMA_SIZE];
static SfsLan lan;

#include "sys/fs/serialfs/serialfs_persistent.c"
extern const SfsIo facade_sfs_lan_io;
extern void facade_e82557_tick(void);
extern void facade_sfs_lan_init(SfsLan *,const SfsLanNic *,const SfsIo *,void *);
extern void model_load(void);
void host_alive_snapshot(struct HostAlive *out) { *out=(struct HostAlive){0}; }
void boot_token_get(u8 out[16]) { kmemset(out,0,16); }
static const SfsIo *transport;
static u32 issued, complete_at, accepts, gets, pending_nonce, reply_at;
static const u8 peer[6]={2,0,0,0,0,2};
static void inject(const u8 peer[6], u32 nonce)
{
    u8 *rfd = e82557_rfd(rx_ring(), nic.rx.dev);
    u8 *frame = rfd + E82557_RFD_HEADER;
    kmemcpy(frame, nic.mac, 6);
    kmemcpy(frame + 6, peer, 6);
    frame[12] = SFS_LAN_ETHERTYPE >> 8;
    frame[13] = SFS_LAN_ETHERTYPE & 255;
    u8 payload[SFS_HELLO_RSP_LEN] = {0};
    sfs_put16(payload + SFS_HELLO_RSP_VER, SFS_VERSION);
    sfs_put16(payload + SFS_HELLO_RSP_MAX, SFS_LAN_MAX_PAYLOAD);
    sfs_put32(payload + SFS_HELLO_RSP_NONCE, nonce);
    sfs_encode(frame + 14, SFS_T_HELLO | SFS_T_RESP, 7, 0, payload, sizeof(payload));
    e82557_wr16(rfd + E82557_DESC_COUNT, 60);
    e82557_wr16(rfd, E82557_RFD_C | E82557_RFD_OK);
    printf("rx DMA tick=%u nonce=%u peer=%u\n", tick_count, nonce, peer[5]);
}
unsigned inp(unsigned port) { return 0; }
unsigned inpw(unsigned port) { return E82557_RUS_READY << E82557_RUS_SHIFT; }
unsigned long inpd(unsigned port) { return 0; }
void outpd(unsigned port, unsigned long value) {}
void outp(unsigned port, unsigned value)
{
    if (port == nic.base + E82557_SCB_COMMAND && value == e82557_scb_cmd(E82557_CU_START,0)) {
        issued = tick_count;

        complete_at = issued + (issued == 3 ? 2 : 1);
        const u8 *wire=dma+E82557_TCB_OFF+E82557_TCB_HEADER+14;
        printf("accepted tick=%lu type=%02x seq=%u\n",tick_count,wire[SFS_OFF_TYPE],sfs_get16(wire+SFS_OFF_SEQ));
        if (wire[SFS_OFF_TYPE]==SFS_T_HELLO) {
            pending_nonce=sfs_get32(wire+SFS_HDR_LEN+SFS_HELLO_REQ_NONCE);
            reply_at=tick_count+1;
        }
        accepts++;
        printf("accept tick=%u completion_due=%u\n", issued, complete_at);
    }
}
void _halt(void)
{
    if (!model_if || tick_count > 210) abort();
    unsigned flags = irq_save();
    kctx_irq_depth++;
    tick_clock_step();
    if (complete_at == tick_count) {
        e82557_wr16(dma + E82557_TCB_OFF, E82557_CB_C | E82557_CB_OK);
        printf("tx DMA tick=%u accepted_at=%u\n", tick_count, issued);
    }

    if (reply_at && tick_count==reply_at) inject(peer,pending_nonce);
    tick_dispatch();
    kctx_irq_depth--;
    irq_restore(flags);
}
void cpu_delay_us(u32 us) {}
u32 arch_tick_budget_cnt(u32 us) { return us; }
u32 arch_tick_budget_base(void) { return 0; }
u32 arch_tick_budget_elapsed(u32 base) { return 0; }
static int clock_read(u32 *t,u32 *s,u32 *g) { static u32 tsc; *t=++tsc; *s=*g=1; return 0; }
static int convert(u32 us,u32 s,u32 g,u32 *out) { *out=us; return 0; }
void link_init(const u8 *mac) {}
void link_tick(void) {}
void console_printf(u8 a, const char *fmt, ...) {}
static int observed_get(void *ctx)
{
    gets++;

    return transport->get(ctx);
}
static int put_until(void *ctx,const u8 *buf,u32 n,u32 deadline) {
    int rc=transport->put_until(ctx,buf,n,deadline);
    printf("put tick=%lu type=%02x deadline=%lu rc=%d\n",tick_count,buf[SFS_OFF_TYPE],deadline,rc);
    return rc;
}
static void report(const char *name,int rc,u32 deadline) {
    printf("%s deadline=%lu return=%lu rc=%d gets=%lu rx=%u/%u prepared=%u linked=%u peer=%u resends=%lu accepts=%lu\n",
        name,deadline,tick_count,rc,gets,lan.rx_pos,lan.rx_end,lan.prepared,lan.linked,lan.peer[5],resident.client.resends,accepts);
    gets=accepts=0;
}
int main(int argc,char **argv) {
    int module=argc>1;
    static const struct io_clock_ops clock = {clock_read, convert, 0};
    io_clock_set_ops(&clock);
    nic.dma.va = dma;
    nic.state = E82557_RUNNING;
    nic.mac[0] = 4;
    e82557_rx_init(rx_ring(), E82557_RFD_OFF, &nic.rx);
    tick_budget_config();

    if (module) model_load();
    if (tick_register(PC98_TICK_E82557,module?facade_e82557_tick:e82557_tick) || net_bind(&nic_ops)) abort();
    (module?facade_sfs_lan_init:sfs_lan_init)(&lan,0,model_clock(),0);
    transport=module?&facade_sfs_lan_io:&sfs_lan_io;
    SfsIo observed=*transport;
    observed.get=observed_get; observed.put_until=put_until;
    enabled=1; config.io=&observed; config.io_ctx=&lan;
    sfs_client_init(&resident.client,&observed,&lan,SFS_LAN_BAUD);
    resident.client.max_payload=SFS_LAN_MAX_PAYLOAD;
    report("setup",sfs_ensure_session(100),100);
    resident.client.seq=SFS_PERSISTENT_SEQ_LIMIT+1;
    u32 deadline=tick_count+100;
    report("renew",sfs_ensure_session(deadline),deadline);
    deadline=tick_count+1;
    u8 data[SFS_LAN_MAX_PAYLOAD], path[]={1,0,97};
    struct SfsReply reply={data,sizeof(data),0};
    report("edge call",sfs_client_call_deadline(&resident.client,SFS_T_STAT,path,sizeof(path),&reply,deadline),deadline);
    _halt(); printf("after deadline dma_done=%lu accepts=%lu\n",nic.stats[E82557_ST_TX_DMA_DONE],accepts);
    return 0;
}
u32 pci_cfg_read32(u32 b,u32 d,u32 f,u32 r) { abort(); }
int pci_cfg_write16(u32 b,u32 d,u32 f,u32 r,u16 v) { abort(); }
int dma_mark_leaked(const struct dma_buf *b) { abort(); }
