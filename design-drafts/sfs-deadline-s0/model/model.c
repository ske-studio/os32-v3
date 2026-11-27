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
static SfsClient client;
static u32 issued, complete_at, accepts, gets, expired_gets;
static const u8 old_peer[6] = {2,0,0,0,0,1};
static const u8 new_peer[6] = {2,0,0,0,0,2};
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
        complete_at = issued == 80 ? 101 : issued + 1;
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
    if (tick_count == 91) inject(old_peer, 1);
    if (tick_count == 102) inject(new_peer, 2);
    tick_dispatch();
    if (tick_count == 92)
        printf("during retry rx=%u/%u prepared=%u linked=%u\n", lan.rx_pos, lan.rx_end, lan.prepared, lan.linked);
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
    if (tick_count == 100) expired_gets++;
    return sfs_lan_io.get(ctx);
}
int main(void)
{
    static const struct io_clock_ops clock = {clock_read, convert, 0};
    io_clock_set_ops(&clock);
    nic.dma.va = dma;
    nic.state = E82557_RUNNING;
    nic.mac[0] = 4;
    e82557_rx_init(rx_ring(), E82557_RFD_OFF, &nic.rx);
    tick_budget_config();
    if (tick_register(PC98_TICK_E82557,e82557_tick) || net_bind(&nic_ops)) abort();
    sfs_lan_init(&lan,0,model_clock(),0);
    SfsIo observed = sfs_lan_io;
    observed.get = observed_get;
    sfs_client_init(&client,&observed,&lan,SFS_LAN_BAUD);
    int rc = sfs_client_hello_deadline(&client,1,100);
    printf("deadline=100 return=%u rc=%d gets=%u at_deadline=%u rx=%u/%u prepared=%u linked=%u peer=%u resends=%u accepts=%u dma_done=%u\n",
        tick_count,rc,gets,expired_gets,lan.rx_pos,lan.rx_end,lan.prepared,lan.linked,lan.peer[5],client.resends,accepts,nic.stats[E82557_ST_TX_DMA_DONE]);
    int wrong_peer = lan.linked && lan.peer[5] == old_peer[5];
    rc = sfs_client_hello_deadline(&client,2,200);
    printf("next deadline=200 return=%u rc=%d peer=%u sid=%u accepts=%u dma_done=%u\n",tick_count,rc,lan.peer[5],client.sid,accepts,nic.stats[E82557_ST_TX_DMA_DONE]);
    return wrong_peer ? 1 : 0;
}
u32 pci_cfg_read32(u32 b,u32 d,u32 f,u32 r) { abort(); }
int pci_cfg_write16(u32 b,u32 d,u32 f,u32 r,u16 v) { abort(); }
int dma_mark_leaked(const struct dma_buf *b) { abort(); }
