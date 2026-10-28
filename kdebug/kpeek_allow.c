/* ======================================================================== */
/*  KPEEK_ALLOW.C — peek -m の許可リストと BAR の確かめ (TASK_KPEEK §2-5)    */
/* ======================================================================== */
#include "kpeek_allow.h"
#include "os32_kapi_shared.h"
#include "drivers/pci.h"

#define NV_ROW(off, len, w) \
    { KPEEK_NV_VENDOR, KPEEK_NV44A_DEVICE, KPEEK_NV_BAR_MMIO, (w), 0, (off), (len) }
#define NV_VGA(base, port) NV_ROW((base) + (port), KPEEK_VGA_BYTE_LEN, KPEEK_W1)

const KpeekAllowRow kpeek_allow_rows[] = {
#if GF_VPLL_PROBE
    NV_ROW(KPEEK_NV_VPLL_CONTROL_OFF, KPEEK_NV_VPLL_CONTROL_LEN, KPEEK_W4),
    /* nouveau nvkm/subdev/clk/nv40.c reads these PLL registers normally (MIT). */
    NV_ROW(KPEEK_NV_CORE_PLL_OFF, KPEEK_NV_PLL_LEN, KPEEK_W4),
    NV_ROW(KPEEK_NV_MEM_PLL_OFF, KPEEK_NV_PLL_LEN, KPEEK_W4),
#endif
    NV_ROW(KPEEK_NV_PMC_OFF,      KPEEK_NV_PMC_LEN,      KPEEK_W4),
    NV_ROW(KPEEK_NV_PBUS_OFF,     KPEEK_NV_PBUS_LEN,     KPEEK_W4),
    NV_ROW(KPEEK_NV_PFB_OFF,      KPEEK_NV_PFB_LEN,      KPEEK_W4),
    NV_ROW(KPEEK_NV_PSTRAPS_OFF,  KPEEK_NV_PSTRAPS_LEN,  KPEEK_W4),
    NV_ROW(KPEEK_NV_PCRTC0_OFF,   KPEEK_NV_PCRTC_LEN,    KPEEK_W4),
    NV_ROW(KPEEK_NV_PCRTC1_OFF,   KPEEK_NV_PCRTC_LEN,    KPEEK_W4),
    NV_ROW(KPEEK_NV_PRAMDAC0_OFF, KPEEK_NV_PRAMDAC_LEN,  KPEEK_W4),
    NV_ROW(KPEEK_NV_PRAMDAC1_OFF, KPEEK_NV_PRAMDAC_LEN,  KPEEK_W4),
    NV_VGA(KPEEK_NV_PRMVIO0_OFF, KPEEK_VGA_MISC_W),
    NV_VGA(KPEEK_NV_PRMVIO0_OFF, KPEEK_VGA_SRX),
    NV_VGA(KPEEK_NV_PRMVIO0_OFF, KPEEK_VGA_SR),
    NV_VGA(KPEEK_NV_PRMVIO0_OFF, KPEEK_VGA_MISC_R),
    NV_VGA(KPEEK_NV_PRMVIO0_OFF, KPEEK_VGA_GRX),
    NV_VGA(KPEEK_NV_PRMVIO0_OFF, KPEEK_VGA_GX),
    NV_VGA(KPEEK_NV_PRMCIO0_OFF, KPEEK_VGA_CRX),
    NV_VGA(KPEEK_NV_PRMCIO0_OFF, KPEEK_VGA_CR),
    NV_ROW(KPEEK_NV_PROM_OFF,     KPEEK_NV_PROM_LEN,     KPEEK_W1),
};
const u32 kpeek_allow_count = sizeof(kpeek_allow_rows) / sizeof(kpeek_allow_rows[0]);

/* 行の BAR の基準番地。I/O BAR・未割当・Memory 無効なら 0 を返し、
 * その行は使わない (K2)。 */
static u32 row_base(const KpeekAllowRow *r)
{
    struct pci_dev d;
    u32 bar;
    int idx = pci_find(r->vendor, r->device);
    if (idx < 0 || r->bar >= PCI_CFG_BAR_COUNT) return 0;
    if (pci_get((u32)idx, &d) != 0) return 0;
    bar = d.bar[r->bar];
    if (bar & KPEEK_BAR_IO_BIT) return 0;
    if ((bar & KPEEK_BAR_ADDR_MASK) == 0) return 0;
    if (!(d.command & PCI_CMD_MEM_ENABLE)) return 0;
    return bar & KPEEK_BAR_ADDR_MASK;
}

u32 kpeek_allow_check(u32 addr, u32 len, u32 width)
{
    u32 i;
    for (i = 0; i < kpeek_allow_count; i++) {
        const KpeekAllowRow *r = &kpeek_allow_rows[i];
        u32 base, rel;
        if (!(r->width_mask & width)) continue;
        base = row_base(r);
        if (!base || addr < base) continue;
        rel = addr - base;
        if (rel < r->off) continue;
        rel -= r->off;
        if (rel >= r->len || len > r->len - rel) continue;
        return KDEBUG_PEEK_OK;
    }
    return KDEBUG_PEEK_DENY;
}
