/* MPC7450UM Rev.5 sections 1.1.3, 2.1, 5.3/5.4:
 * https://www.nxp.com/docs/en/reference-manual/MPC7450UM.pdf
 * Bit encodings cross-checked with NetBSD oea/pte.h and oea/hid.h. */
#ifndef OS32_PPC_OEA_H
#define OS32_PPC_OEA_H
#define PPC_MSR_EE 0x8000
#define PPC_MSR_PR 0x4000
#define PPC_MSR_ME 0x1000
#define PPC_MSR_BE 0x200
#define PPC_MSR_IP 0x40
#define PPC_MSR_IR 0x20
#define PPC_MSR_DR 0x10
#define PPC_MSR_RI 0x2
#define PPC_SPR_SDR1 25
#define PPC_SPR_IBAT0U 528
#define PPC_SPR_IBAT1U 530
#define PPC_SPR_IBAT2U 532
#define PPC_SPR_IBAT3U 534
#define PPC_SPR_DBAT0U 536
#define PPC_SPR_DBAT1U 538
#define PPC_SPR_DBAT2U 540
#define PPC_SPR_DBAT3U 542
#define PPC_HID_TBEN 0x04000000
#define PPC_HID_STEN 0x01000000
#define PPC_SPR_HID0 1008
#define PPC_REV_7450_BTIC_MAX 0x0200
/* MPC7450UM table 2-1: additional upper BAT SPRs, 7455 only. */
#define PPC_SPR_IBAT4U 560
#define PPC_SPR_IBAT5U 562
#define PPC_SPR_IBAT6U 564
#define PPC_SPR_IBAT7U 566
#define PPC_SPR_DBAT4U 568
#define PPC_SPR_DBAT5U 570
#define PPC_SPR_DBAT6U 572
#define PPC_SPR_DBAT7U 574
#define PPC_PVR_7450 0x8000
#define PPC_PVR_7455 0x8001
#define PPC_HID_HIGHBAT 0x00800000
#define PPC_HID_XBSEN 0x100
#define PPC_HID_SGE 0x80
#define PPC_HID_BTIC 0x20
#define PPC_HID_LRSTK 0x10
#define PPC_HID_FOLD 0x08
#define PPC_HID_BHT 0x04
#define PPC_HID_SLEEP 0x00200000
#define PPC_PTE_V 0x80000000
#define PPC_PTE_H 0x40
#define PPC_PTE_RC 0x180
#define PPC_SR_KP 0x20000000
#define PPC_ATTR_RAM_RW 0x12
#define PPC_ATTR_RAM_RO 0x13
#define PPC_ATTR_IO_RW 0x2a
#define PPC_PAGE_SHIFT 12
#define PPC_PAGE_SIZE 4096
#define PPC_PAGE_MASK 0xfffff000
#define PPC_PTEG_SIZE 64
#define PPC_PTEG_ENTRIES 8
#define PPC_SR_COUNT 16
#define PPC_TLB_COUNT 64
#define PPC_CACHE_LINE 32
#define PPC_DEC_FAR 0x7fffffff
#define PPC_DSISR_MISS 0x40000000
#define PPC_DSISR_PROTECT 0x08000000
#define PPC_DSISR_STORE 0x02000000
#define PPC_ISI_MISS 0x40000000
#ifndef __ASSEMBLER__
#include "types.h"
#include "ppc_boot.h"
struct ppc_vector_image;
/* Identify returns 0 for unknown PVR; caller must diagnose and stop.
 * Remaining CPU functions require real mode with interrupts disabled. */
int ppc_cpu_identify(u32 *pvr);
void ppc_cpu_disable_bats(u32 pvr);
void ppc_cpu_load_segments(void);
void ppc_cpu_load_sdr1(u32 sdr1);
void ppc_enable(void);
u32 ppc_irq_save(void);
void ppc_irq_restore(u32 saved_msr);
int ppc_irq_enabled(void);
void ppc_vector_install(const struct ppc_vector_image *image);
void ppc_tlb_flush_all(void);
void ppc_tlb_flush_one(u32 va);
void ppc_sr_sdr1_load(const u32 sr[16], u32 sdr1);
void ppc_enable_translation(void);
void ppc_cache_copy(void *dst, const void *src, u32 size);
u32 ppc_read_pvr(void);
u32 ppc_read_hid0(void);
void ppc_write_hid0(u32 value);
int ppc_read_dec(void);
u32 ppc_read_tb(void);
void ppc_write_dec(u32 value);
void ppc_bat_reset(u32 count);
void ppc_bat_set(u32 index, u32 lower, u32 upper);
void ppc_eieio(void);
void ppc_tlbsync(void);
void ppc_isync(void);
volatile u32 *ppc_hpt_ptr(u32 pa);
#endif
#endif
