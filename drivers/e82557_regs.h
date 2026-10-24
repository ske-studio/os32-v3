/* Intel 8255x SDM §4.1, §6.3.4, Tables 21–23. [C4] */
#ifndef E82557_REGS_H
#define E82557_REGS_H
#include "net_eth.h"
#include "memmap.h"
#include "kprintf.h"
#include "pci_decode.h"
#define E82557_VENDOR          0x8086
#define E82557_DEVICE          0x1229
#define E82557_IO_BAR          1
#define E82557_CSR_LEN         0x20u
#define E82557_PORT_LIMIT      0x10000u
#define E82557_COMMAND_IO      PCI_CMD_IO_ENABLE
/* SCB byte/word/dword layout, SDM Tables 13, 14, 18, 19. */
#define E82557_SCB_STATUS      0
#define E82557_SCB_ACK         1
#define E82557_SCB_COMMAND     2
#define E82557_SCB_INTCTL      3
#define E82557_SCB_POINTER     4
#define E82557_PORT            8
#define E82557_SCB_M           0x01
#define E82557_CUS_MASK        0x00C0u
#define E82557_RUS_MASK        0x003Cu
#define E82557_STAT_MASK       0xFF00u
#define E82557_PORT_SOFTWARE   0u
#define E82557_PORT_SELECTIVE  2u
#define E82557_CU_START        1u
#define E82557_CU_BASE         6u
#define E82557_RU_START        1u
#define E82557_RU_BASE         6u
#define E82557_RESET_US        20u
#define E82557_SCB_WAIT_US     100u
#define E82557_SCB_POLL_US     10u
#define E82557_SCB_CUC_SHIFT   4
#define E82557_CUS_SHIFT       6
#define E82557_RUS_SHIFT       2
#define E82557_STAT_SHIFT      8
#define E82557_RUS_NORES       2u
/* DMA layout and simplified CB/TCB, SDM Figures 14, 16, 17, 19, 25. */
#define E82557_COMMAND_BM      PCI_CMD_BUS_MASTER
#define E82557_DMA_SIZE        16384u
#define E82557_DMA_ALIGN       16u
#define E82557_DMA_LIMIT       0xFFFFFFFFu
#define E82557_CB_OFF          0u
#define E82557_CB_SIZE         32u
#define E82557_CB_HEADER       8u
#define E82557_CFG_LEN         22u
#define E82557_TCB_OFF         64u
#define E82557_TCB_HEADER      16u
#define E82557_TCB_SIZE        1552u
#define E82557_RFD_OFF         1616u
#define E82557_RFD_STRIDE      1552u
#define E82557_RFD_COUNT       8u
#define E82557_DMA_END         14032u
#define E82557_FRAME_MAX       ETH_FRAME_MAX
#define E82557_HELLO_LEN       ETH_ZLEN
#define E82557_HELLO_MARK_LEN  14u
#define E82557_HELLO_TYPE      OS32_ETHERTYPE
#define E82557_CB_IA           1u
#define E82557_CB_CONFIG       2u
#define E82557_CB_TX           4u
#define E82557_CB_EL           0x8000u
#define E82557_CB_S            0x4000u
#define E82557_CB_I            0x2000u
#define E82557_CB_C            0x8000u
#define E82557_CB_OK           0x2000u
#define E82557_TCB_EOF         0x8000u
#define E82557_TBD_NONE        0xFFFFFFFFu
#define E82557_TX_THRESH       0x10u
#define E82557_CB_WAIT_MS      20u
#define E82557_CB_POLL_US      1000u
#define E82557_STOP_ATTEMPTS   2u
#define E82557_CFG_NORMAL     0
#define E82557_CFG_LOOPBACK    1
#define E82557_EE_CTRL         0x0E
#define E82557_EE_SK           0x01
#define E82557_EE_CS           0x02
#define E82557_EE_DI           0x04
#define E82557_EE_DO           0x08
#define E82557_EE_READ         6u  /* start 1 + READ 10 */
#define E82557_EE_PREFIX_BITS  3
#define E82557_EE_DATA_BITS    16
#define E82557_EE_WIDTH6       6
#define E82557_EE_WIDTH8       8
#define E82557_EE_MAX_ADDR     8
#define E82557_EE_STEP_US      4u
#define E82557_EE_HIGH_US      1u
#define E82557_EE_LOW_US       3u
#define E82557_EE_PERIOD_US    4u
#define E82557_EE_CS_LOW_US    1u
#define E82557_MAC_LEN         ETH_ALEN
#define E82557_LOG_ATTR        KLOG_ATTR
#define E82557_MAC_WORDS       (E82557_MAC_LEN / 2)
#define E82557_DESC_CMD        2
#define E82557_DESC_LINK       4
#define E82557_DESC_TBD        8
#define E82557_RFD_RBD         8
#define E82557_DESC_COUNT      12
#define E82557_TCB_THRESH_OFF  14
#define E82557_RFD_SIZE_OFF    14
#define E82557_CFG_LB_BYTE     10
#define E82557_CFG_LB_VALUE    0x6E
#define E82557_MDI_OP_SHIFT    26
#define E82557_MDI_PHY_SHIFT   21
#define E82557_MDI_REG_SHIFT   16
/* MDI (SDM Tables 24/26) and clause 22 PHY registers. */
#define E82557_MDI             0x10u
#define E82557_MDI_READ        2u
#define E82557_MDI_WRITE       1u
#define E82557_MDI_READY       (1u << 28)
#define E82557_MDI_WAIT_US     2000u
#define E82557_MDI_POLL_US     10u
#define E82557_PHY_MASK        31u
#define E82557_PHY_BMCR        0u
#define E82557_PHY_BMSR        1u
#define E82557_PHY_ID1         2u
#define E82557_PHY_ID2         3u
/* DP83840A datasheet 4.9-4.15; FreeBSD nsphyreg.h / nsphy.c. */
#define E82557_NSC_ID1         0x2000u
#define E82557_NSC_ID2         0x5C00u
#define E82557_NSC_ID2_MASK    0xFFF0u
#define E82557_NSC_PCR         0x17u
#define E82557_NSC_PAR         0x19u
#define E82557_NSC_PCR_SET     0x0562u
#define E82557_NSC_PCR_VERIFY  0x0062u
#define E82557_EE_COMPAT       3u
#define E82557_EE_PHY          6u
#define E82557_BMCR_AN         0x1000u
#define E82557_BMSR_LINK       0x0004u
#define E82557_BMSR_AN         0x0020u
#define E82557_PHY_POLL_TICKS  10u
#define E82557_LINK_WAIT_TICKS 300u
#define E82557_TICK_MS         TICK_MS
/* SDM Figure 25 / Table 14; FreeBSD if_fxpreg.h:77,85-92,381-386. */
#define E82557_RFD_HEADER      16u
#define E82557_RFD_SIZE        1536u
#define E82557_RFD_C           0x8000u
#define E82557_RFD_OK          0x2000u
#define E82557_RFD_RNR         0x0200u
#define E82557_RFD_EL          0x8000u
#define E82557_RFD_COUNT_MASK  0x3FFFu
#define E82557_RUS_READY       4u
#define E82557_SCB_SI          0x02u
#define E82557_STAT_CX         0x8000u
#define E82557_STAT_FR         0x4000u
#define E82557_STAT_CNA        0x2000u
#define E82557_STAT_RNR        0x1000u
#define E82557_STAT_MDI        0x0800u
#define E82557_STAT_SWI        0x0400u
#define E82557_LB_WAIT_MS      50u
#define E82557_SWI_WAIT_MS     10u
#define E82557_SWI_POLL_US     100u
#define E82557_TX_WAIT_TICKS   50u
#define E82557_RECOVER_MAX     10u
#define E82557_RECOVER_RESETS  3u
#endif
