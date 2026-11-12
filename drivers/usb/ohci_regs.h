#ifndef OHCI_REGS_H
#define OHCI_REGS_H
#include "endian_dma.h"
/* OpenHCI 1.0a §§4,5,7 (hcir1_0a.pdf); register names/offsets also
 * https://dl.xkwy2018.com/downloads/documents/hcir1_0a.pdf
 * FreeBSD (BSD-2-Clause) controller/ohcireg.h:45-149 and ohci.h:74-167:
 * https://github.com/freebsd/freebsd-src/blob/main/sys/dev/usb/controller/ohcireg.h
 * https://github.com/freebsd/freebsd-src/blob/main/sys/dev/usb/controller/ohci.h
 * No native-endian device fields. */
#define OHCI_REVISION 0x00u
#define OHCI_CONTROL 0x04u
#define OHCI_COMMAND_STATUS 0x08u
#define OHCI_INTERRUPT_STATUS 0x0cu
#define OHCI_INTERRUPT_ENABLE 0x10u
#define OHCI_INTERRUPT_DISABLE 0x14u
#define OHCI_HCCA 0x18u
#define OHCI_PERIOD_CURRENT_ED 0x1cu
#define OHCI_CONTROL_HEAD_ED 0x20u
#define OHCI_CONTROL_CURRENT_ED 0x24u
#define OHCI_BULK_HEAD_ED 0x28u
#define OHCI_BULK_CURRENT_ED 0x2cu
#define OHCI_DONE_HEAD 0x30u
#define OHCI_FM_INTERVAL 0x34u
#define OHCI_FM_REMAINING 0x38u
#define OHCI_FM_NUMBER 0x3cu
#define OHCI_PERIODIC_START 0x40u
#define OHCI_LS_THRESHOLD 0x44u
#define OHCI_RH_DESCRIPTOR_A 0x48u
#define OHCI_RH_DESCRIPTOR_B 0x4cu
#define OHCI_RH_STATUS 0x50u
#define OHCI_RH_PORT_STATUS(p) (0x50u + 4u * (p)) /* ports are 1-based */
#define OHCI_CTL_PLE (1u << 2)
#define OHCI_CTL_IE (1u << 3)
#define OHCI_CTL_CLE (1u << 4)
#define OHCI_CTL_BLE (1u << 5)
#define OHCI_CTL_HCFS_MASK (3u << 6)
#define OHCI_CTL_RESET 0u
#define OHCI_CTL_RESUME (1u << 6)
#define OHCI_CTL_OPERATIONAL (2u << 6)
#define OHCI_CTL_SUSPEND (3u << 6)
#define OHCI_CTL_IR (1u << 8)
#define OHCI_CMD_HCR 1u
#define OHCI_CMD_CLF 2u
#define OHCI_CMD_BLF 4u
#define OHCI_CMD_OCR 8u
#define OHCI_INTR_SO 1u
#define OHCI_INTR_WDH 2u
#define OHCI_INTR_SF 4u
#define OHCI_INTR_RD 8u
#define OHCI_INTR_UE 0x10u
#define OHCI_INTR_FNO 0x20u
#define OHCI_INTR_RHSC 0x40u
#define OHCI_INTR_OC (1u << 30)
#define OHCI_INTR_MIE (1u << 31)
#define OHCI_INTR_ALL 0xc000007fu
#define OHCI_RHA_NDP_MASK 0xffu
#define OHCI_RHA_PSM (1u << 8)
#define OHCI_RHA_NPS (1u << 9)
#define OHCI_RHA_OCPM (1u << 11)
#define OHCI_RHA_NOCP (1u << 12)
#define OHCI_PORT_CCS 1u
#define OHCI_PORT_PES 2u
#define OHCI_PORT_PSS 4u
#define OHCI_PORT_POCI 8u
#define OHCI_PORT_PRS 0x10u
#define OHCI_PORT_PPS 0x100u
#define OHCI_PORT_LSDA 0x200u
#define OHCI_PORT_CSC 0x10000u
#define OHCI_PORT_PESC 0x20000u
#define OHCI_PORT_PSSC 0x40000u
#define OHCI_PORT_OCIC 0x80000u
#define OHCI_PORT_PRSC 0x100000u
#define OHCI_ED_ADDR_MASK 0x7fu
#define OHCI_ED_EP_SHIFT 7u
#define OHCI_ED_DIR_OUT (1u << 11)
#define OHCI_ED_DIR_IN (2u << 11)
#define OHCI_ED_LOW_SPEED (1u << 13)
#define OHCI_ED_SKIP (1u << 14)
#define OHCI_ED_ISO (1u << 15)
#define OHCI_ED_MPS_SHIFT 16u
#define OHCI_HALTED 1u
#define OHCI_TOGGLECARRY 2u
#define OHCI_PTR_MASK 0xfffffff0u
#define OHCI_TD_ROUND (1u << 18)
#define OHCI_TD_SETUP 0u
#define OHCI_TD_OUT (1u << 19)
#define OHCI_TD_IN (2u << 19)
#define OHCI_TD_DI_SHIFT 21u
#define OHCI_TD_TOGGLE_CARRY 0u
#define OHCI_TD_DATA0 (2u << 24)
#define OHCI_TD_DATA1 (3u << 24)
#define OHCI_TD_CC_SHIFT 28u
#define OHCI_CC_NO_ERROR 0u
#define OHCI_CC_STALL 4u
#define OHCI_CC_DATA_UNDERRUN 9u
#define OHCI_CC_NOT_ACCESSED 15u
struct ohci_ed_hw { le32_t flags, tailp, headp, nexted; } __attribute__((aligned(16)));
struct ohci_td_hw { le32_t flags, cbp, nexttd, be; } __attribute__((aligned(16)));
/* Frame number occupies low 16 bits; high 16 bits must be zero. */
struct ohci_hcca {
    le32_t interrupt[32];
    le32_t frame_number, done_head;
    u8 reserved[120];
} __attribute__((aligned(256)));
STATIC_ASSERT(sizeof(struct ohci_ed_hw) == 16, ohci_ed_size);
STATIC_ASSERT(sizeof(struct ohci_td_hw) == 16, ohci_td_size);
STATIC_ASSERT(sizeof(struct ohci_hcca) == 256, ohci_hcca_size);
STATIC_ASSERT(__builtin_offsetof(struct ohci_hcca, done_head) == 0x84, ohci_done_offset);
#endif
