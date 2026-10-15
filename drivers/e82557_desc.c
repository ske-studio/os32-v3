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

u8 e82557_scb_cmd(u8 cuc, u8 ruc) { return (u8)((cuc << 4) | ruc); }
void e82557_scb_decode(u16 st, u8 *cus, u8 *rus, u8 *stat)
{
    *cus = (u8)((st & E82557_CUS_MASK) >> 6);
    *rus = (u8)((st & E82557_RUS_MASK) >> 2);
    *stat = (u8)((st & E82557_STAT_MASK) >> 8);
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
    e82557_wr16(p + 2, cmd | (el ? E82557_CB_EL : 0) |
                   (s ? E82557_CB_S : 0) | (i ? E82557_CB_I : 0));
    e82557_wr32(p + 4, link);
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
    if (mode == E82557_CFG_LOOPBACK) out[10] = 0x6E;
}
int e82557_tcb_build(void *tcb, const u8 *frame, u32 len, u8 thresh, u32 link, int i)
{
    u8 *p = tcb;
    if (len < E82557_FRAME_MIN || len > E82557_FRAME_MAX) return -1;
    e82557_cb_header(p, E82557_CB_TX, 1, 0, i, link);
    e82557_wr32(p + 8, E82557_TBD_NONE);
    e82557_wr16(p + 12, (u16)len | E82557_TCB_EOF);
    e82557_wr16(p + 14, thresh); /* TBD number = 0, simplified mode. */
    copy_bytes(p + E82557_TCB_HEADER, frame, len);
    return 0;
}
int e82557_stop_evidence(const struct e82557_stop *stop, u32 before, u16 st)
{
    return stop->reset_seq != before && stop->reset_seq != stop->start_seq &&
           !(st & (E82557_CUS_MASK | E82557_RUS_MASK));
}
