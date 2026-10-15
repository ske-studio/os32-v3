/* Byte serialization only: no device I/O, packed structs or DMA allocation. */
#ifndef E82557_DESC_H
#define E82557_DESC_H
#include "types.h"
#include "e82557_regs.h"
#ifndef E82557_BARRIER
#define E82557_BARRIER() __asm__ volatile("" ::: "memory")
#endif
/* Test hooks record accesses, never supply ordering for production. */
#ifndef E82557_DESC_ACCESS
#define E82557_DESC_ACCESS(p, n, write) ((void)0)
#endif
struct e82557_stop {
    u32 reset_seq;
    u32 start_seq;
};
u8 e82557_scb_cmd(u8 cuc, u8 ruc);
void e82557_scb_decode(u16 st, u8 *cus, u8 *rus, u8 *stat);
void e82557_wr16(void *dst, u16 value);
void e82557_wr32(void *dst, u32 value);
u16 e82557_rd16(const volatile void *src);
u32 e82557_rd32(const volatile void *src);
void e82557_cb_header(void *cb, u16 cmd, int el, int s, int i, u32 link);
void e82557_cb_ia(void *cb, const u8 *mac);
void e82557_cb_config(void *cb, const u8 *cfg);
void e82557_config_bytes(u8 *out, int mode);
int e82557_tcb_build(void *tcb, const u8 *frame, u32 len, u8 thresh, u32 link, int i);
int e82557_stop_evidence(const struct e82557_stop *stop, u32 before, u16 st);
enum e82557_fix { E82557_FIX_NONE, E82557_FIX_WRITE, E82557_FIX_RESET_BUSY };
enum e82557_wait { E82557_WAIT, E82557_DONE, E82557_TIMEOUT };
u32 e82557_mdi_ctl(u8 op, u8 phy, u8 reg, u16 data);
int e82557_mdi_ready(u32 ctl);
u16 e82557_mdi_data(u32 ctl);
int e82557_phy_id_valid(u16 id1);
void e82557_phy_decode(u16 bmcr, u16 bmsr, int *link, int *an_done, int *an_en);
int e82557_bmcr_fix(u16 bmcr, u16 *out);
int e82557_bmcr_fixed(u16 readback);
int e82557_link_wait_step(int link, int an_done, int an_en, u32 elapsed, u32 limit);
int e82557_stats_done(const void *area);
u32 e82557_stats_read(const void *area, u32 idx);
int e82557_stats_step(u32 good, u32 expect, u32 elapsed, u32 limit);
#endif
