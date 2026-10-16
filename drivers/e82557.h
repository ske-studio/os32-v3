#ifndef E82557_H
#define E82557_H
#include "pci_bind.h"
#define E82557_TRY_PATH "/var/log/lan.try"
#define E82557_SEQ_PATH "/var/log/lan.seq"
#define E82557_LOG_PATH "/var/log/lan.log"
#define E82557_TRY_CAP 32
/* Observation only; each line category is limited independently to 1/s. */
#define E82557_WATCH_TICKS (1000u / E82557_TICK_MS)
#define E82557_WATCH_SLOW_US 2000u
#define E82557_WATCH_IRQ_RATE 200u
enum e82557_trace_stage {
    E82557_TRACE_TRY = 1, E82557_TRACE_WIN, E82557_TRACE_RST,
    E82557_TRACE_BASE, E82557_TRACE_EEPROM, E82557_TRACE_DMA,
    E82557_TRACE_CFG, E82557_TRACE_IA, E82557_TRACE_TX, E82557_TRACE_STOP,
    E82557_TRACE_PHY, E82557_TRACE_LINK, E82557_TRACE_TX2, E82557_TRACE_STAT,
    E82557_TRACE_DONE,
    E82557_TRACE_IRQ, E82557_TRACE_RING, E82557_TRACE_LB, E82557_TRACE_CMP,
    E82557_TRACE_RST2, E82557_TRACE_CFG2, E82557_TRACE_IA2, E82557_TRACE_RU,
    E82557_TRACE_SWI, E82557_TRACE_BOUND
};
enum e82557_state {
    E82557_OFF, E82557_STARTING, E82557_RUNNING, E82557_RECOVER,
    E82557_RX_WAIT, E82557_FAILED, E82557_STOPPING, E82557_QUARANTINED
};
/* Foreground only; send success means queued, not transmitted on the wire. */
int e82557_send(const void *buf, u32 len);
int e82557_recv(void *buf, u32 cap, u32 *len);
void e82557_get_mac(u8 *mac);
int e82557_state(void);
u32 e82557_rx_free(void);
int e82557_rx_ready(void); /* RUNNING and hardware RU ready; no restart/ack. */
void e82557_tick(void); /* IRQ0, IF=0, at most one SCB issue per tick */
int e82557_loopback(int on);
/* 0 cx(+cna), 1 fr, 2 rnr, 3 swi, 4 other(mdi/dead), 5 tx_dma_done,
 * 6 tx_fail, 7 rx_ok, 8 rx_drop, 9 recover_total, 10 recover_ok, 11 scb_stuck.
 * TCB C is DMA completion, never evidence of wire success (SDM :4648-4663). */
u32 e82557_stat(u32 idx);
extern const struct pci_driver e82557_driver;
#endif
