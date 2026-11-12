/* Startup order: FreeBSD f22d6599 controller/ohci.c:183-293,335-408
 * (BSD-2-Clause); OpenHCI 1.0a §§5,7; USB_DESIGN §4-2. */
#include "drivers/usb/ohci_internal.h"

static void tree_init(struct usb_hc *hc)
{
    struct ohci_dmamem *m = hc->hcpriv;
    u32 i;
    for (i = 0; i < OHCI_TREE_EDS; ++i) {
        struct ohci_ed *ed = &m->tree[i];
        u32 next = i ? m->tree[(i - 1u) / 2u].bus : m->sentinel[0].bus;
        dma_le32_put(&ed->hw->flags, OHCI_ED_SKIP);
        dma_le32_put(&ed->hw->nexted, next);
    }
    for (i = 0; i < OHCI_SENTINEL_EDS; ++i)
        dma_le32_put(&m->sentinel[i].hw->flags, OHCI_ED_SKIP);
    for (i = 0; i < 32; ++i) {
        u32 bit, reversed = 0;
        /* Binary heap parents need reversed leaf bits to give 2/4/...ms,
         * rather than consecutive groups of frames (OHCI §5.2). */
        for (bit = 0; bit < 5; ++bit) reversed = (reversed << 1) | ((i >> bit) & 1u);
        dma_le32_put(&m->hcca->interrupt[i], m->tree[31u + reversed].bus);
    }
    usbp_dma_sync(&hc->dma[0], 0, USB_DMA_PAGE_SIZE, USBP_TO_DEV);
}

void ohci_kill(struct usb_hc *hc)
{
    /* No readback or waiting, even if hardware no longer responds. */
    ohci_wr(hc, OHCI_INTERRUPT_DISABLE, OHCI_INTR_ALL);
    ohci_wr(hc, OHCI_CONTROL, OHCI_CTL_RESET);
    ohci_wr(hc, OHCI_COMMAND_STATUS, OHCI_CMD_HCR);
    usbp_cfg_cmd(&hc->pci, 0, OHCI_BM);
}

static int start_fail(struct usb_hc *hc)
{
    u32 i;
    if (hc->started & OHCI_STARTED_BM) {
        usb_hc_dead(hc, USB_WHY_START);
    } else {
        ohci_wr(hc, OHCI_INTERRUPT_DISABLE, OHCI_INTR_ALL);
        ohci_wr(hc, OHCI_CONTROL, OHCI_CTL_RESET);
        for (i = 0; i < USB_HC_DMA_MAX; ++i) {
            if (hc->dma[i].va) usbp_dma_free(&hc->dma[i]);
            ohci_zero(&hc->dma[i], sizeof(hc->dma[i]));
        }
        if (hc->hcpriv) usbp_mem_free(hc->hcpriv);
        hc->hcpriv = NULL;
        usbp_mmio_unmap(&hc->pci, 0);
        hc->regs = NULL;
        hc->state = HC_OFF;
        hc->started = 0;
    }
    return -USB_E_HC;
}

int ohci_start(struct usb_hc *hc)
{
    u32 i, fi, desca;
    struct ohci_dmamem *m;
    if (hc->state == HC_DEAD) return -USB_E_HC;
    hc->state = HC_STARTING;
    hc->started = 0;
    if (usb_dmamem_init(hc) < 0) {
        start_fail(hc);
        return -USB_E_NOMEM;
    }
    m = hc->hcpriv;
    tree_init(hc);
    if (ohci_rd(hc, OHCI_CONTROL) & OHCI_CTL_IR) {
        ohci_wr(hc, OHCI_COMMAND_STATUS, OHCI_CMD_OCR);
        for (i = 0; i < 100; ++i) {
            usbp_udelay(1000);
            if (!(ohci_rd(hc, OHCI_CONTROL) & OHCI_CTL_IR)) break;
        }
    }
    ohci_wr(hc, OHCI_CONTROL, OHCI_CTL_RESET);
    usbp_udelay(100000);
    fi = OHCI_FMI_FI(ohci_rd(hc, OHCI_FM_INTERVAL));
    ohci_wr(hc, OHCI_COMMAND_STATUS, OHCI_CMD_HCR);
    for (i = 0; i < 10; ++i) {
        usbp_udelay(10);
        if (!(ohci_rd(hc, OHCI_COMMAND_STATUS) & OHCI_CMD_HCR)) break;
    }
    if (i == 10) return start_fail(hc);
    ohci_wr(hc, OHCI_HCCA, hc->dma[0].bus);
    ohci_wr(hc, OHCI_CONTROL_HEAD_ED, m->sentinel[1].bus);
    ohci_wr(hc, OHCI_BULK_HEAD_ED, m->sentinel[2].bus);
    if (ohci_rd(hc, OHCI_HCCA) != hc->dma[0].bus ||
        ohci_rd(hc, OHCI_CONTROL_HEAD_ED) != m->sentinel[1].bus ||
        ohci_rd(hc, OHCI_BULK_HEAD_ED) != m->sentinel[2].bus)
        return start_fail(hc);
    ohci_wr(hc, OHCI_INTERRUPT_DISABLE, OHCI_INTR_ALL);
    ohci_wr(hc, OHCI_INTERRUPT_ENABLE,
            OHCI_WATCH | (hc->has_irq ? OHCI_INTR_MIE : 0));
    hc->started |= OHCI_STARTED_BM; /* a failed readback may follow a successful BM write */
    if (usbp_cfg_cmd(&hc->pci, OHCI_BM, 0) < 0) return start_fail(hc);
    ohci_wr(hc, OHCI_CONTROL, OHCI_CTL_CBSR_1_4 | OHCI_CTL_PLE |
            OHCI_CTL_IE | OHCI_CTL_CLE | OHCI_CTL_BLE | OHCI_CTL_OPERATIONAL);
    ohci_wr(hc, OHCI_FM_INTERVAL, ((ohci_rd(hc, OHCI_FM_INTERVAL) ^
            0x80000000u) & 0x80000000u) | (((fi - 210u) * 6u / 7u) << 16) | fi);
    ohci_wr(hc, OHCI_PERIODIC_START, fi * 9u / 10u);
    desca = ohci_rd(hc, OHCI_RH_DESCRIPTOR_A);
    ohci_wr(hc, OHCI_RH_DESCRIPTOR_A, desca | OHCI_RHA_NOCP);
    ohci_wr(hc, OHCI_RH_STATUS, OHCI_RHS_LPSC);
    usbp_udelay(5000); /* FreeBSD OHCI_ENABLE_POWER_DELAY = 5ms */
    ohci_wr(hc, OHCI_RH_DESCRIPTOR_A, desca);
    hc->nports = 0;
    for (i = 0; i < 10 && !hc->nports; ++i) {
        usbp_udelay(5000); /* OHCI_READ_DESC_DELAY */
        hc->nports = (u8)(ohci_rd(hc, OHCI_RH_DESCRIPTOR_A) & OHCI_RHA_NDP_MASK);
    }
    if (!hc->nports || hc->nports > USB_RH_PORTS_MAX) return start_fail(hc);
    hc->stop_frame = ohci_frame(hc);
    hc->stop_deadline_ms = usbp_ms() + USB_DMA_DELAY_MS;
    hc->state = HC_RUNNING;
    return 0;
}

u16 ohci_frame(struct usb_hc *hc)
{
    if (hc->state == HC_DEAD) return (u16)hc->stop_frame;
    return (u16)ohci_rd(hc, OHCI_FM_NUMBER);
}

const struct usb_hc_ops ohci_ops = {
    .start = ohci_start, .kill = ohci_kill, .intr_collect = ohci_intr_collect,
    .scan = ohci_scan, .xfer_alloc = ohci_xfer_alloc, .xfer_free = ohci_xfer_free,
    .xfer_enter = ohci_xfer_enter, .xfer_close = ohci_xfer_close,
    .frame = ohci_frame, .rh_port = ohci_rh_port
};
