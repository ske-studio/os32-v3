#ifndef OS32_GEM_H
#define OS32_GEM_H

#include "types.h"

/* Register offsets are BAR-relative except ERROR_STATUS and RESET, which are bank2-relative. */
#define GEM_CONFIG 0x0004 /* openbsd gemreg.h:44 */
#define GEM_INTMASK 0x0010 /* openbsd gemreg.h:47 */
#define GEM_STATUS_ALIAS 0x001c /* openbsd gemreg.h:49 */
#define GEM_PCI_BANK2_OFFSET 0x1000 /* openbsd gemreg.h:56 */
#define GEM_ERROR_STATUS 0x0000 /* openbsd gemreg.h:59 */
#define GEM_RESET 0x0010 /* openbsd gemreg.h:65 */
#define GEM_CONFIG_BURST_INF 0x000000001 /* openbsd gemreg.h:80 */
#define GEM_CONFIG_TXDMA_LIMIT 0x00000003e /* openbsd gemreg.h:81 */
#define GEM_CONFIG_RXDMA_LIMIT 0x0000007c0 /* openbsd gemreg.h:82 */
#define GEM_CONFIG_RONPAULBIT 0x000000800 /* openbsd gemreg.h:84 */
#define GEM_CONFIG_BUG2FIX 0x000001000 /* openbsd gemreg.h:87 */
#define GEM_INTR_RX_TAG_ERR 0x000000040 /* openbsd gemreg.h:105 */
#define GEM_INTR_TX_MAC 0x000004000 /* openbsd gemreg.h:107 */
#define GEM_INTR_RX_MAC 0x000008000 /* openbsd gemreg.h:108 */
#define GEM_INTR_BERR 0x000040000 /* openbsd gemreg.h:111 */
#define GEM_ERROR_STAT_BADACK 0x000000001 /* openbsd gemreg.h:119 */
#define GEM_ERROR_STAT_DTRTO 0x000000002 /* openbsd gemreg.h:120 */
#define GEM_ERROR_STAT_OTHERS 0x000000004 /* openbsd gemreg.h:121 */
#define GEM_RESET_TX 0x000000001 /* openbsd gemreg.h:130 */
#define GEM_RESET_RX 0x000000002 /* openbsd gemreg.h:131 */
#define GEM_TX_KICK 0x2000 /* openbsd gemreg.h:135 */
#define GEM_TX_CONFIG 0x2004 /* openbsd gemreg.h:136 */
#define GEM_TX_RING_PTR_LO 0x2008 /* openbsd gemreg.h:137 */
#define GEM_TX_RING_PTR_HI 0x200c /* openbsd gemreg.h:138 */
#define GEM_TX_COMPLETION 0x2100 /* openbsd gemreg.h:149 */
#define GEM_TX_CONFIG_TXDMA_EN 0x00000001 /* openbsd gemreg.h:159 */
#define GEM_TX_CONFIG_TXRING_SZ 0x0000001e /* openbsd gemreg.h:160 */
#define GEM_TX_CONFIG_TXFIFO_TH 0x001ffc00 /* openbsd gemreg.h:161 */
#define GEM_RING_SZ_32 (0<<1) /* openbsd gemreg.h:164 */
#define GEM_TX_COMPLETION_MASK 0x00001fff /* openbsd gemreg.h:175 */
#define GEM_RX_CONFIG 0x4000 /* openbsd gemreg.h:178 */
#define GEM_RX_RING_PTR_LO 0x4004 /* openbsd gemreg.h:179 */
#define GEM_RX_RING_PTR_HI 0x4008 /* openbsd gemreg.h:180 */
#define GEM_RX_FIFO_WR_PTR 0x400c /* openbsd gemreg.h:182 */
#define GEM_RX_FIFO_RD_PTR 0x4014 /* openbsd gemreg.h:184 */
#define GEM_RX_PAUSE_THRESH 0x4020 /* openbsd gemreg.h:188 */
#define GEM_RX_KICK 0x4100 /* openbsd gemreg.h:193 */
#define GEM_RX_BLANKING 0x4108 /* openbsd gemreg.h:195 */
#define GEM_RX_FIFO_SIZE 0x4120 /* openbsd gemreg.h:202 */
#define GEM_RX_CONFIG_RXDMA_EN 0x00000001 /* openbsd gemreg.h:205 */
#define GEM_RX_CONFIG_RXRING_SZ 0x0000001e /* openbsd gemreg.h:206 */
#define GEM_RX_CONFIG_FBOFF 0x00001c00 /* openbsd gemreg.h:208 */
#define GEM_RX_CONFIG_CXM_START 0x000fe000 /* openbsd gemreg.h:209 */
#define GEM_RX_CONFIG_FIFO_THRS 0x07000000 /* openbsd gemreg.h:210 */
#define GEM_THRSH_1024 4 /* openbsd gemreg.h:216 */
#define GEM_RX_CONFIG_FIFO_THRS_SHIFT 24 /* openbsd gemreg.h:219 */
#define GEM_RX_CONFIG_FBOFF_SHFT 10 /* openbsd gemreg.h:220 */
#define GEM_RX_PTH_XOFF_THRESH 0x000001ff /* openbsd gemreg.h:224 */
#define GEM_RX_PTH_XON_THRESH 0x07fc0000 /* openbsd gemreg.h:225 */
#define GEM_MAC_SEND_PAUSE_CMD 0x6008 /* openbsd gemreg.h:235 */
#define GEM_MAC_TX_STATUS 0x6010 /* openbsd gemreg.h:236 */
#define GEM_MAC_RX_STATUS 0x6014 /* openbsd gemreg.h:237 */
#define GEM_MAC_TX_MASK 0x6020 /* openbsd gemreg.h:239 */
#define GEM_MAC_RX_MASK 0x6024 /* openbsd gemreg.h:240 */
#define GEM_MAC_CONTROL_MASK 0x6028 /* openbsd gemreg.h:241 */
#define GEM_MAC_TX_CONFIG 0x6030 /* openbsd gemreg.h:242 */
#define GEM_MAC_RX_CONFIG 0x6034 /* openbsd gemreg.h:243 */
#define GEM_MAC_CONTROL_CONFIG 0x6038 /* openbsd gemreg.h:244 */
#define GEM_MAC_XIF_CONFIG 0x603c /* openbsd gemreg.h:245 */
#define GEM_MAC_IPG0 0x6040 /* openbsd gemreg.h:246 */
#define GEM_MAC_IPG1 0x6044 /* openbsd gemreg.h:247 */
#define GEM_MAC_IPG2 0x6048 /* openbsd gemreg.h:248 */
#define GEM_MAC_MAC_MIN_FRAME 0x6050 /* openbsd gemreg.h:250 */
#define GEM_MAC_MAC_MAX_FRAME 0x6054 /* openbsd gemreg.h:251 */
#define GEM_MAC_PREAMBLE_LEN 0x6058 /* openbsd gemreg.h:252 */
#define GEM_MAC_JAM_SIZE 0x605c /* openbsd gemreg.h:253 */
#define GEM_MAC_ATTEMPT_LIMIT 0x6060 /* openbsd gemreg.h:254 */
#define GEM_MAC_CONTROL_TYPE 0x6064 /* openbsd gemreg.h:255 */
#define GEM_MAC_ADR_FLT_MASK1_2 0x60b0 /* openbsd gemreg.h:270 */
#define GEM_MAC_ADR_FLT_MASK0 0x60b4 /* openbsd gemreg.h:271 */
#define GEM_MAC_NORM_COLL_CNT 0x6100 /* openbsd gemreg.h:290 */
#define GEM_MAC_FIRST_COLL_CNT 0x6104 /* openbsd gemreg.h:291 */
#define GEM_MAC_EXCESS_COLL_CNT 0x6108 /* openbsd gemreg.h:292 */
#define GEM_MAC_LATE_COLL_CNT 0x610c /* openbsd gemreg.h:293 */
#define GEM_MAC_DEFER_TMR_CNT 0x6110 /* openbsd gemreg.h:294 */
#define GEM_MAC_PEAK_ATTEMPTS 0x6114 /* openbsd gemreg.h:295 */
#define GEM_MAC_RX_FRAME_COUNT 0x6118 /* openbsd gemreg.h:296 */
#define GEM_MAC_RX_LEN_ERR_CNT 0x611c /* openbsd gemreg.h:297 */
#define GEM_MAC_RX_ALIGN_ERR 0x6120 /* openbsd gemreg.h:298 */
#define GEM_MAC_RX_CRC_ERR_CNT 0x6124 /* openbsd gemreg.h:299 */
#define GEM_MAC_RX_CODE_VIOL 0x6128 /* openbsd gemreg.h:300 */
#define GEM_MAC_RANDOM_SEED 0x6130 /* openbsd gemreg.h:301 */
#define GEM_MAC_MAC_STATE 0x6134 /* openbsd gemreg.h:302 */
#define GEM_MAC_TX_UNDERRUN 0x00000002 /* openbsd gemreg.h:310 */
#define GEM_MAC_TX_PKT_TOO_LONG 0x00000004 /* openbsd gemreg.h:311 */
#define GEM_MAC_RX_DONE 0x00000001 /* openbsd gemreg.h:320 */
#define GEM_MAC_RX_OVERFLOW 0x00000002 /* openbsd gemreg.h:321 */
#define GEM_MAC_RX_FRAME_CNT 0x00000004 /* openbsd gemreg.h:322 */
#define GEM_MAC_XIF_TX_MII_ENA 0x00000001 /* openbsd gemreg.h:335 */
#define GEM_MAC_XIF_GMII_MODE 0x00000008 /* openbsd gemreg.h:338 */
#define GEM_MAC_XIF_LINK_LED 0x00000020 /* openbsd gemreg.h:340 */
#define GEM_MAC_XIF_FDPLX_LED 0x00000040 /* openbsd gemreg.h:341 */
#define GEM_MAC_TX_ENABLE 0x00000001 /* openbsd gemreg.h:348 */
#define GEM_MAC_TX_IGN_CARRIER 0x00000002 /* openbsd gemreg.h:349 */
#define GEM_MAC_TX_IGN_COLLIS 0x00000004 /* openbsd gemreg.h:350 */
#define GEM_MAC_TX_ENA_IPG0 0x00000008 /* openbsd gemreg.h:351 */
#define GEM_MAC_TX_NGU 0x00000010 /* openbsd gemreg.h:352 */
#define GEM_MAC_TX_NGU_LIMIT 0x00000020 /* openbsd gemreg.h:353 */
#define GEM_MAC_RX_ENABLE 0x00000001 /* openbsd gemreg.h:361 */
#define GEM_MAC_RX_STRIP_CRC 0x00000004 /* openbsd gemreg.h:363 */
#define GEM_MAC_RX_PROMISCUOUS 0x00000008 /* openbsd gemreg.h:364 */
#define GEM_MAC_RX_PROMISC_GRP 0x00000010 /* openbsd gemreg.h:365 */
#define GEM_MAC_RX_HASH_FILTER 0x00000020 /* openbsd gemreg.h:366 */
#define GEM_MAC_CC_TX_PAUSE 0x00000001 /* openbsd gemreg.h:376 */
#define GEM_MAC_CC_RX_PAUSE 0x00000002 /* openbsd gemreg.h:377 */
#define GEM_MAC_STATE_OVERFLOW 0x03800000 /* openbsd gemreg.h:381 */
#define GEM_MIF_FRAME 0x620c /* openbsd gemreg.h:388 */
#define GEM_MIF_CONFIG 0x6210 /* openbsd gemreg.h:389 */
#define GEM_MIF_FRAME_DATA 0x0000ffff /* openbsd gemreg.h:395 */
#define GEM_MIF_FRAME_TA0 0x00010000 /* openbsd gemreg.h:396 */
#define GEM_MIF_FRAME_READ 0x60020000 /* openbsd gemreg.h:403 */
#define GEM_MIF_FRAME_WRITE 0x50020000 /* openbsd gemreg.h:404 */
#define GEM_MIF_REG_SHIFT 18 /* openbsd gemreg.h:406 */
#define GEM_MIF_PHY_SHIFT 23 /* openbsd gemreg.h:407 */
#define GEM_MIF_CONFIG_PHY_SEL 0x00000001 /* openbsd gemreg.h:410 */
#define GEM_MIF_CONFIG_POLL_ENA 0x00000002 /* openbsd gemreg.h:411 */
#define GEM_MIF_CONFIG_BB_ENA 0x00000004 /* openbsd gemreg.h:412 */
#define GEM_MIF_CONFIG_MDI0 0x00000100 /* openbsd gemreg.h:414 */
#define GEM_MIF_CONFIG_MDI1 0x00000200 /* openbsd gemreg.h:415 */
#define GEM_MII_CONFIG 0x9010 /* openbsd gemreg.h:435 */
#define GEM_MII_DATAPATH_MODE 0x9050 /* openbsd gemreg.h:438 */
#define GEM_MII_CONFIG_ENABLE 0x00000001 /* openbsd gemreg.h:485 */
#define GEM_MII_DATAPATH_SERIAL 0x00000001 /* openbsd gemreg.h:503 */
#define GEM_MII_DATAPATH_SERDES 0x00000002 /* openbsd gemreg.h:504 */
#define GEM_MII_DATAPATH_MII 0x00000004 /* openbsd gemreg.h:505 */
#define GEM_MII_DATAPATH_MIIOUT 0x00000008 /* openbsd gemreg.h:506 */
#define GEM_TD_BUFSIZE 0x0000000000007fffLL /* openbsd gemreg.h:536 */
#define GEM_TD_END_OF_PACKET 0x0000000040000000LL /* openbsd gemreg.h:542 */
#define GEM_TD_START_OF_PACKET 0x0000000080000000LL /* openbsd gemreg.h:543 */
#define GEM_RD_BUFSIZE 0x000000007fff0000LL /* openbsd gemreg.h:553 */
#define GEM_RD_OWN 0x0000000080000000LL /* openbsd gemreg.h:554 */
#define GEM_RD_BAD_CRC 0x4000000000000000LL /* openbsd gemreg.h:558 */
#define GEM_RD_BUFSHIFT 16 /* openbsd gemreg.h:560 */
#define GEM_MAC_ADDR0 0x6080 /* openbsd gemreg.h:257 */
#define GEM_MAC_ADDR1 0x6084 /* openbsd gemreg.h:258 */
#define GEM_MAC_ADDR2 0x6088 /* openbsd gemreg.h:259 */
#define GEM_MAC_ADDR3 0x608c /* openbsd gemreg.h:260 */
#define GEM_MAC_ADDR4 0x6090 /* openbsd gemreg.h:261 */
#define GEM_MAC_ADDR5 0x6094 /* openbsd gemreg.h:262 */
#define GEM_MAC_ADDR6 0x6098 /* openbsd gemreg.h:263 */
#define GEM_MAC_ADDR7 0x609c /* openbsd gemreg.h:264 */
#define GEM_MAC_ADDR8 0x60a0 /* openbsd gemreg.h:265 */
#define GEM_MAC_ADDR_FILTER0 0x60a4 /* openbsd gemreg.h:267 */
#define GEM_MAC_ADDR_FILTER1 0x60a8 /* openbsd gemreg.h:268 */
#define GEM_MAC_ADDR_FILTER2 0x60ac /* openbsd gemreg.h:269 */
#define GEM_MAC_HASH0 0x60c0 /* openbsd gemreg.h:273 */
#define GEM_MAC_HASH1 0x60c4 /* openbsd gemreg.h:274 */
#define GEM_MAC_HASH2 0x60c8 /* openbsd gemreg.h:275 */
#define GEM_MAC_HASH3 0x60cc /* openbsd gemreg.h:276 */
#define GEM_MAC_HASH4 0x60d0 /* openbsd gemreg.h:277 */
#define GEM_MAC_HASH5 0x60d4 /* openbsd gemreg.h:278 */
#define GEM_MAC_HASH6 0x60d8 /* openbsd gemreg.h:279 */
#define GEM_MAC_HASH7 0x60dc /* openbsd gemreg.h:280 */
#define GEM_MAC_HASH8 0x60e0 /* openbsd gemreg.h:281 */
#define GEM_MAC_HASH9 0x60e4 /* openbsd gemreg.h:282 */
#define GEM_MAC_HASH10 0x60e8 /* openbsd gemreg.h:283 */
#define GEM_MAC_HASH11 0x60ec /* openbsd gemreg.h:284 */
#define GEM_MAC_HASH12 0x60f0 /* openbsd gemreg.h:285 */
#define GEM_MAC_HASH13 0x60f4 /* openbsd gemreg.h:286 */
#define GEM_MAC_HASH14 0x60f8 /* openbsd gemreg.h:287 */
#define GEM_MAC_HASH15 0x60fc /* openbsd gemreg.h:288 */

#define GEM_MMIO_SPAN 0x10000
#define GEM_RING_COUNT 32
#define GEM_RX_PUBLISHED 28
/* Descriptors are little-endian bytes, never cast to a C structure. */
#define GEM_DESC_FLAGS_OFFSET 0
#define GEM_DESC_ADDR_OFFSET 8
#define GEM_DESC_SIZE 16
_Static_assert(GEM_DESC_FLAGS_OFFSET == 0, "descriptor flags offset");
_Static_assert(GEM_DESC_ADDR_OFFSET == 8, "descriptor address offset");
_Static_assert(GEM_DESC_SIZE == 16, "descriptor size");

enum { GEM_OK = 0, GEM_AGAIN = 1, GEM_EINVAL = -1, GEM_ENODEV = -2,
       GEM_ERANGE = -3, GEM_ETIME = -4, GEM_EIO = -5,
       GEM_EUNSUPPORTED = -6, GEM_EBUSY = -7 };
enum { GEM_STATE_OFF, GEM_STATE_RESETTING, GEM_STATE_QUARANTINED, GEM_STATE_READY };
enum dma_sync { DMA_PREWRITE, DMA_PREREAD, DMA_POSTREAD, DMA_POSTWRITE };
struct gem_dma { void *cpu; u32 pa, bus, bytes; };
struct gem_io {
    void *ctx;
    u32 (*read32)(void *, u32);
    void (*write32)(void *, u32, u32);
    unsigned long long (*tb)(void *);
    u32 tb_hz;
    int (*sync)(void *, u32, enum dma_sync);
};
struct gem {
    struct gem_io io; /* Platform-provided register, clock and DMA access. */
    struct gem_dma dma; /* Pool retained until DMA is known to be stopped. */
    u32 state, step, cursor, polls; /* Bounded state-machine progress. */
    u32 error_status, tx_status, rx_status; /* Captured fault registers. */
    u32 phy_addr, phy_port, phy_id1, phy_id2; /* Selected PHY and MIF port. */
    u32 phy_step, phy_polls;
    unsigned long long phy_started;
    u32 tx_head, tx_tail, tx_pending, rx_head; /* Ring ownership indices. */
    u32 rx_watch_wr, rx_watch_rd, rx_watching; /* Overflow watchdog snapshot. */
    unsigned long long started, tx_started, rx_watch_started; /* TB deadlines. */
    unsigned long long tx_packets, rx_packets, rx_drops, uncertain_tx; /* Counters. */
};
int gem_reset_step(struct gem *, unsigned long long now);
int gem_mac_init(struct gem *, const u8 mac[6]);
int gem_mif_init(struct gem *);
int gem_phy_step(struct gem *, unsigned long long now);
int gem_ring_prepare(struct gem *, struct gem_dma *);
int gem_ring_publish(struct gem *);
int gem_mac_enable_step(struct gem *, unsigned long long now);
int gem_tx(struct gem *, const void *frame, u32 bytes);
int gem_rx(struct gem *, void *frame, u32 cap, u32 *bytes);
int gem_poll(struct gem *, unsigned long long now, u32 tx_budget);
#endif
