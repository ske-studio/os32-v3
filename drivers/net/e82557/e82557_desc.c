#include "e82557_desc.h"

STATIC_ASSERT(E82557_CB_OFF == 0 && E82557_CB_SIZE == 32, cb_layout);
STATIC_ASSERT(E82557_CB_HEADER + E82557_CFG_LEN <= E82557_CB_SIZE, cfg_fits);
STATIC_ASSERT(E82557_TCB_OFF == 64 && E82557_TCB_HEADER == 16, tcb_layout);
STATIC_ASSERT(E82557_TCB_HEADER + E82557_FRAME_MAX <= E82557_TCB_SIZE, tx_fits);
STATIC_ASSERT(E82557_CB_OFF + E82557_CB_SIZE <= E82557_TCB_OFF, cb_no_overlap);
STATIC_ASSERT(E82557_TCB_OFF + E82557_TCB_SIZE == E82557_RFD_OFF, tx_no_overlap);
STATIC_ASSERT(E82557_RFD_OFF + E82557_RFD_STRIDE * E82557_RFD_COUNT ==
              E82557_DMA_END && E82557_DMA_END == 14032, dma_layout);
STATIC_ASSERT(E82557_DMA_END <= E82557_DMA_SIZE, dma_fits);
STATIC_ASSERT((E82557_CB_OFF | E82557_TCB_OFF | E82557_RFD_OFF |
               E82557_RFD_STRIDE) % E82557_DMA_ALIGN == 0, dma_alignment);

u8 e82557_scb_cmd(u8 cuc, u8 ruc) { return (u8)((cuc << E82557_SCB_CUC_SHIFT) | ruc); }
void e82557_scb_decode(u16 st, u8 *cus, u8 *rus, u8 *stat)
{
    *cus = (u8)((st & E82557_CUS_MASK) >> E82557_CUS_SHIFT);
    *rus = (u8)((st & E82557_RUS_MASK) >> E82557_RUS_SHIFT);
    *stat = (u8)((st & E82557_STAT_MASK) >> E82557_STAT_SHIFT);
}
void e82557_wr16(void *dst, u16 value)
{
    u8 *p = dst;
    p[0] = (u8)value; p[1] = (u8)(value >> 8);
    E82557_DESC_ACCESS(dst, 2, 1);
}
void e82557_wr32(void *dst, u32 value)
{
    u8 *p = dst;
    e82557_wr16(p, (u16)value);
    e82557_wr16(p + 2, (u16)(value >> 16));
}
u16 e82557_rd16(const volatile void *src)
{
    const volatile u8 *p = src;
    u16 value = p[0] | ((u16)p[1] << 8);
    E82557_DESC_ACCESS(src, 2, 0);
    return value;
}
u32 e82557_rd32(const volatile void *src)
{
    const volatile u8 *p = src;
    u32 low = e82557_rd16(p);
    return low | ((u32)e82557_rd16(p + 2) << 16);
}
static void copy_bytes(u8 *dst, const u8 *src, u32 n)
{
    for (u32 k = 0; k < n; k++) dst[k] = src[k];
    E82557_DESC_ACCESS(dst, n, 1);
}
void e82557_cb_header(void *cb, u16 cmd, int el, int s, int i, u32 link)
{
    u8 *p = cb;
    e82557_wr16(p, 0);
    e82557_wr16(p + E82557_DESC_CMD, cmd | (el ? E82557_CB_EL : 0) |
                   (s ? E82557_CB_S : 0) | (i ? E82557_CB_I : 0));
    e82557_wr32(p + E82557_DESC_LINK, link);
}
void e82557_cb_ia(void *cb, const u8 *mac)
{
    e82557_cb_header(cb, E82557_CB_IA, 1, 0, 0, E82557_TBD_NONE);
    copy_bytes((u8 *)cb + E82557_CB_HEADER, mac, E82557_MAC_LEN);
}
void e82557_cb_config(void *cb, const u8 *cfg)
{
    e82557_cb_header(cb, E82557_CB_CONFIG, 1, 0, 0, E82557_TBD_NONE);
    copy_bytes((u8 *)cb + E82557_CB_HEADER, cfg, E82557_CFG_LEN);
}
void e82557_config_bytes(u8 *out, int mode)
{
    /* SDM Table 38: MII, NSAI (full Ethernet header), padding, no IRQ. */
    const u8 cfg[E82557_CFG_LEN] = {
        0x16, 0x08, 0x00, 0x00, 0x00, 0x80, 0x32, 0x03,
        0x01, 0x00, 0x2E, 0x00, 0x60, 0x00, 0xF2, 0x48,
        0x00, 0x40, 0xF2, 0x80, 0x3F, 0x05
    };
    copy_bytes(out, cfg, E82557_CFG_LEN);
    if (mode == E82557_CFG_LOOPBACK) out[E82557_CFG_LB_BYTE] = E82557_CFG_LB_VALUE;
}
int e82557_tcb_build(void *tcb, const u8 *frame, u32 len, u8 thresh, u32 link, int i)
{
    u8 *p = tcb;
    if (len < ETH_HLEN || len > E82557_FRAME_MAX) return -1;
    e82557_cb_header(p, E82557_CB_TX, 1, 0, i, link);
    e82557_wr32(p + E82557_DESC_TBD, E82557_TBD_NONE);
    e82557_wr16(p + E82557_DESC_COUNT, (u16)len | E82557_TCB_EOF);
    e82557_wr16(p + E82557_TCB_THRESH_OFF, thresh); /* TBD number = 0, simplified mode. */
    copy_bytes(p + E82557_TCB_HEADER, frame, len);
    return 0;
}
int e82557_stop_evidence(const struct e82557_stop *stop, u32 before, u16 st)
{
    return stop->reset_seq != before && stop->reset_seq != stop->start_seq &&
           !(st & (E82557_CUS_MASK | E82557_RUS_MASK));
}

u32 e82557_mdi_ctl(u8 op, u8 phy, u8 reg, u16 data)
{
    return ((u32)op << E82557_MDI_OP_SHIFT) | ((u32)(phy & E82557_PHY_MASK) << E82557_MDI_PHY_SHIFT) |
           ((u32)(reg & E82557_PHY_MASK) << E82557_MDI_REG_SHIFT) | data;
}
int e82557_mdi_ready(u32 ctl) { return !!(ctl & E82557_MDI_READY); }
u16 e82557_mdi_data(u32 ctl) { return (u16)ctl; }
int e82557_phy_id_valid(u16 id1) { return id1 != 0 && id1 != 0xffff; }
void e82557_phy_decode(u16 bmcr, u16 bmsr, int *link, int *an_done, int *an_en)
{
    *link = !!(bmsr & E82557_BMSR_LINK);
    *an_done = !!(bmsr & E82557_BMSR_AN);
    *an_en = !!(bmcr & E82557_BMCR_AN);
}
int e82557_link_wait_step(int link, int an_done, int an_en, u32 elapsed, u32 limit)
{
    if (link && (an_done || !an_en)) return E82557_DONE;
    return elapsed >= limit ? E82557_TIMEOUT : E82557_WAIT;
}

STATIC_ASSERT(E82557_RFD_HEADER + E82557_RFD_SIZE == E82557_RFD_STRIDE, rfd_fits);
u8 *e82557_rfd(void *ring, u32 i)
{
    return (u8 *)ring + i * E82557_RFD_STRIDE;
}
static u32 rx_next(u32 i) { return (i + 1) % E82557_RFD_COUNT; }
void e82557_rx_init(void *ring, u32 pa, struct e82557_rx *rx)
{
    rx->cons = rx->dev = rx->ready = 0;
    for (u32 i = 0; i < E82557_RFD_COUNT; i++) {
        u8 *rfd = e82557_rfd(ring, i);
        e82557_wr32(rfd + E82557_DESC_LINK, pa + rx_next(i) * E82557_RFD_STRIDE);
        e82557_wr32(rfd + E82557_RFD_RBD, E82557_TBD_NONE);
        e82557_wr16(rfd + E82557_RFD_SIZE_OFF, E82557_RFD_SIZE);
    }
    e82557_rx_reown(ring, rx);
}
/* SDM :5592-5603; FreeBSD if_fxp.c:2631-2646,2717-2719.
 * Called only after reset proof (or before the first RU Start). */
void e82557_rx_reown(void *ring, const struct e82557_rx *rx)
{
    u32 free = E82557_RFD_COUNT - rx->ready;
    for (u32 n = 0, i = rx->dev; n < free; n++, i = rx_next(i)) {
        u8 *rfd = e82557_rfd(ring, i);
        e82557_wr16(rfd, 0);
        e82557_wr16(rfd + E82557_DESC_COUNT, 0);
        E82557_BARRIER(); /* reown zero before EL */
        e82557_wr16(rfd + E82557_DESC_CMD, n + 1 == free ? E82557_RFD_EL : 0);
    }
}
void e82557_rx_scan(void *ring, struct e82557_rx *rx, int *rnr)
{
    while (rx->ready < E82557_RFD_COUNT) {
        u16 st = e82557_rd16(e82557_rfd(ring, rx->dev));
        if (!(st & E82557_RFD_C)) break;
        E82557_BARRIER(); /* scan completion acquisition */
        if (st & E82557_RFD_RNR) *rnr = 1; /* if_fxp.c:1924-1925 */
        rx->dev = rx_next(rx->dev);
        rx->ready++;
    }
}
void e82557_rfd_recycle(void *ring, struct e82557_rx *rx)
{
    u8 *rfd = e82557_rfd(ring, rx->cons);
    u8 *tail = e82557_rfd(ring, (rx->cons + E82557_RFD_COUNT - 1) % E82557_RFD_COUNT);
    e82557_wr16(rfd, 0);
    e82557_wr16(rfd + E82557_DESC_COUNT, 0);
    E82557_BARRIER(); /* recycle zero before EL */
    if (rx->ready == E82557_RFD_COUNT) {
        /* Full: RU stopped on the old EL. Remove it before publishing one slot. */
        e82557_wr16(tail + E82557_DESC_CMD, 0);
        E82557_BARRIER(); /* full old before new */
        e82557_wr16(rfd + E82557_DESC_CMD, E82557_RFD_EL);
    } else {
        /* FreeBSD if_fxp.c:2679-2692: publish new tail before releasing old. */
        e82557_wr16(rfd + E82557_DESC_CMD, E82557_RFD_EL);
        E82557_BARRIER(); /* nonfull new before old */
        e82557_wr16(tail + E82557_DESC_CMD, 0);
    }
    rx->cons = rx_next(rx->cons);
    rx->ready--;
}
