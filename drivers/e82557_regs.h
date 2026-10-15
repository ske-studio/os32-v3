/* Intel 8255x SDM §4.1, §6.3.4, Tables 21–23. [C4] */
#ifndef E82557_REGS_H
#define E82557_REGS_H
#define E82557_VENDOR          0x8086
#define E82557_DEVICE          0x1229
#define E82557_IO_BAR          1
#define E82557_CSR_LEN         0x20u
#define E82557_PORT_LIMIT      0x10000u
#define E82557_COMMAND_IO      1
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
#define E82557_MAC_LEN         6
#define E82557_LOG_ATTR        0x07
#endif
