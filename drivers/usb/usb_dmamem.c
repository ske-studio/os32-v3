/* USB_DESIGN §§3-1,5; OpenHCI 1.0a §4. HCCA and hardware records have
 * fixed offsets; CPU shadows never reside in DMA memory. */
#include "drivers/usb/ohci_internal.h"

static void carve(struct ohci_block *b, struct usb_dma *page, u32 off,
                  u32 size, u16 index)
{
    b->dma.bus = page->bus + off;
    b->dma.va = (u8 *)page->va + off;
    b->dma.size = size;
    b->dma.cookie = page->cookie;
    b->index = index;
    b->used = 0;
}

int usb_dmamem_init(struct usb_hc *hc)
{
    struct ohci_dmamem *m = usbp_mem_alloc(sizeof(*m));
    u32 i, j, off;
    if (!m) return -USB_E_NOMEM;
    ohci_zero(m, sizeof(*m));
    for (i = 0; i < USB_HC_DMA_MAX; ++i) {
        struct usb_dma *d = &hc->dma[i];
        if (usbp_dma_alloc(USB_DMA_PAGE_SIZE, USB_DMA_PAGE_SIZE, d) < 0)
            goto fail;
        if (!ohci_dma_page_ok(d)) { /* A7: independently validate HW2 */
            usbp_dma_free(d);
            ohci_zero(d, sizeof(*d));
            goto fail;
        }
        ohci_zero(d->va, d->size);
    }
    m->hcca = hc->dma[0].va;
    off = sizeof(struct ohci_hcca);
    for (i = 0; i < OHCI_TREE_EDS + OHCI_SENTINEL_EDS; ++i) {
        struct ohci_ed *ed = i < OHCI_TREE_EDS ? &m->tree[i] :
                            &m->sentinel[i - OHCI_TREE_EDS];
        ed->hw = (void *)((u8 *)hc->dma[0].va + off);
        ed->bus = hc->dma[0].bus + off;
        off += sizeof(*ed->hw);
    }
    for (i = 0; i < USB_DMA_BLOCK_MAX; ++i)
        carve(&m->blocks[i], &hc->dma[0], off + i * USB_DMA_BLOCK_SIZE,
              USB_DMA_BLOCK_SIZE, (u16)i);
    off = 0;
    for (i = 0; i < USB_CFG_BUF_MAX; ++i) {
        carve(&m->config[i], &hc->dma[1], off, USB_CFG_MAX, (u16)i);
        off += USB_CFG_MAX;
    }
    for (i = 0; i < USB_CTRL_BUF_MAX; ++i) {
        carve(&m->control[i], &hc->dma[1], off, USB_CTRL_BUF_SIZE, (u16)i);
        off += USB_CTRL_BUF_SIZE;
    }
    for (i = 0; i < USB_CLASS_BUF_MAX; ++i) {
        carve(&m->reports[i], &hc->dma[1], off, USB_CLASS_BUF_SIZE, (u16)i);
        off += USB_CLASS_BUF_SIZE;
    }
    hc->hcpriv = m;
    return 0;
fail:
    for (j = 0; j < i; ++j) {
        usbp_dma_free(&hc->dma[j]);
        ohci_zero(&hc->dma[j], sizeof(hc->dma[j]));
    }
    usbp_mem_free(m);
    return -USB_E_NOMEM;
}

static int take(struct ohci_block *b, u32 count, u32 size, u32 align,
                struct usb_dma *out)
{
    u32 i;
    for (i = 0; i < count; ++i) {
        if (!b[i].used && size <= b[i].dma.size &&
            !(b[i].dma.bus & (align - 1u))) {
            b[i].used = 1;
            *out = b[i].dma;
            ohci_zero(out->va, out->size);
            return 0;
        }
    }
    return -USB_E_NOMEM;
}

int usb_dmamem_alloc(struct usb_hc *hc, u32 size, u32 align, struct usb_dma *out)
{
    struct ohci_dmamem *m = hc->hcpriv;
    if (hc->state == HC_DEAD) return -USB_E_HC;
    if (!m || !size || !align || (align & (align - 1u))) return -USB_E_INVAL;
    if (size <= USB_DMA_BLOCK_SIZE && align <= USB_DMA_BLOCK_SIZE)
        return take(m->blocks, USB_DMA_BLOCK_MAX, size, align, out);
    if (size <= USB_CTRL_BUF_SIZE && align <= USB_CTRL_BUF_SIZE) {
        if (!take(m->control, USB_CTRL_BUF_MAX, size, align, out)) return 0;
        return take(m->reports, USB_CLASS_BUF_MAX, size, align, out);
    }
    if (size <= USB_CFG_MAX && align <= USB_CFG_MAX)
        return take(m->config, USB_CFG_BUF_MAX, size, align, out);
    return -USB_E_NOMEM;
}

void usb_dmamem_free(struct usb_hc *hc, struct usb_dma *d)
{
    struct ohci_dmamem *m = hc->hcpriv;
    struct ohci_block *tables[4];
    const u32 counts[4] = { USB_DMA_BLOCK_MAX, USB_CTRL_BUF_MAX,
                            USB_CLASS_BUF_MAX, USB_CFG_BUF_MAX };
    u32 i, j;
    if (hc->state == HC_DEAD || !m || !d->va) return;
    tables[0] = m->blocks; tables[1] = m->control;
    tables[2] = m->reports; tables[3] = m->config;
    for (i = 0; i < 4; ++i)
        for (j = 0; j < counts[i]; ++j)
            if (tables[i][j].dma.bus == d->bus) {
                tables[i][j].used = 0;
                ohci_zero(d, sizeof(*d));
                return;
            }
}
