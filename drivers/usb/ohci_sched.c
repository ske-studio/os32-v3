/* FreeBSD f22d6599 ohci.c:655-686,1240-1560,2341-2497 (BSD-2).
 * USB_DESIGN §4-6: a transfer owns two sets until delayed unsetup. */
#include "drivers/usb/ohci_internal.h"

void ohci_xfer_free(struct usb_hc *hc, struct usb_xfer *x)
{
    struct ohci_xp *p = x->hcpriv;
    u32 s;
    if (!p || hc->state == HC_DEAD) return;
    for (s = 0; s < USB_DMA_SETS; ++s) {
        struct ohci_dma_set *set = &p->sets[s];
        if (set->first) {
            struct ohci_td *td;
            for (td = set->first; td <= set->last; ++td)
                if (td->hw) ohci_free_record(hc, td->hw, td->bus);
            usbp_mem_free(set->first);
        }
        if (set->ed) {
            if (set->ed->hw) ohci_free_record(hc, set->ed->hw, set->ed->bus);
            usbp_mem_free(set->ed);
        }
        if (set->setup.used) usb_dmamem_free(hc, &set->setup.dma);
    }
    usbp_mem_free(p);
    x->hcpriv = NULL;
}

int ohci_xfer_alloc(struct usb_hc *hc, struct usb_xfer *x)
{
    struct ohci_xp *p;
    u32 s, i, n;
    int control = x->pipe.type == USB_EP_CONTROL;
    if (hc->state != HC_RUNNING) return -USB_E_HC;
    if (x->pipe.type != USB_EP_CONTROL && x->pipe.type != USB_EP_INTERRUPT &&
        x->pipe.type != USB_EP_BULK) return -USB_E_INVAL;
    if (control && x->buf.size > USB_CFG_MAX) return -USB_E_INVAL;
    n = control ? (x->buf.size ? 3u : 2u) :
        (x->buf.size ? ((x->buf.bus & (USB_DMA_PAGE_SIZE - 1u)) +
                       x->buf.size + USB_DMA_PAGE_SIZE - 1u) / USB_DMA_PAGE_SIZE : 1u);
    if (n > USB_DMA_BLOCK_MAX / USB_DMA_SETS) return -USB_E_NOMEM;
    p = usbp_mem_alloc(sizeof(*p));
    if (!p) return -USB_E_NOMEM;
    ohci_zero(p, sizeof(*p));
    p->pipe = &x->pipe;
    x->hcpriv = p;
    for (s = 0; s < USB_DMA_SETS; ++s) {
        struct ohci_dma_set *set = &p->sets[s];
        struct usb_dma d;
        set->ed = usbp_mem_alloc(sizeof(*set->ed));
        if (!set->ed) goto fail;
        ohci_zero(set->ed, sizeof(*set->ed));
        if (usb_dmamem_alloc(hc, USB_DMA_BLOCK_SIZE, USB_DMA_BLOCK_SIZE, &d) < 0)
            goto fail;
        set->ed->hw = d.va; set->ed->bus = d.bus;
        set->first = usbp_mem_alloc(n * sizeof(*set->first));
        if (!set->first) goto fail;
        ohci_zero(set->first, n * sizeof(*set->first));
        set->last = set->first + n - 1u;
        for (i = 0; i < n; ++i) {
            if (usb_dmamem_alloc(hc, USB_DMA_BLOCK_SIZE, USB_DMA_BLOCK_SIZE, &d) < 0)
                goto fail;
            set->first[i].hw = d.va;
            set->first[i].bus = d.bus;
        }
        if (control) {
            if (usb_dmamem_alloc(hc, sizeof(x->setup), USB_DMA_BLOCK_SIZE,
                                &set->setup.dma) < 0) goto fail;
            set->setup.used = 1;
            set->status_td = set->last;
        }
    }
    return 0;
fail:
    ohci_xfer_free(hc, x);
    return -USB_E_NOMEM;
}

static void td_init(struct ohci_td *td, u32 flags, u32 bus, u32 len, u8 stage)
{
    td->len = len; td->start_bus = bus; td->stage = stage;
    td->alt_next = NULL; td->hw_next_is_end = 0;
    dma_le32_put(&td->hw->flags, flags | (OHCI_CC_NOT_ACCESSED << OHCI_TD_CC_SHIFT));
    dma_le32_put(&td->hw->cbp, len ? bus : 0);
    dma_le32_put(&td->hw->be, len ? bus + len - 1u : 0);
    dma_le32_put(&td->hw->nexttd, 0);
}

static struct ohci_ed *anchor(struct usb_hc *hc, const struct usb_xfer *x)
{
    struct ohci_dmamem *m = hc->hcpriv;
    u32 interval = 1;
    if (x->pipe.type == USB_EP_CONTROL) return &m->sentinel[1];
    if (x->pipe.type == USB_EP_BULK) return &m->sentinel[2];
    while (interval < 32 && interval * 2u <= x->pipe.interval) interval *= 2u;
    return &m->tree[interval - 1u];
}

int ohci_xfer_enter(struct usb_hc *hc, struct usb_xfer *x)
{
    struct ohci_xp *p = x->hcpriv;
    struct ohci_dma_set *set;
    struct ohci_ed *ed, *prev;
    struct ohci_td *td, *first, *last;
    u32 flags, i, remaining, bus, n = 0;
    int ctrl = x->pipe.type == USB_EP_CONTROL;
    int in = ctrl ? !!(x->setup.type & USB_DIR_IN) : !!(x->pipe.ep & USB_DIR_IN);
    if (hc->state != HC_RUNNING) return -USB_E_HC;
    if (!p || x->len > x->buf.size || !x->pipe.mps) return -USB_E_INVAL;
    if (ctrl && le16_rd(x->setup.length) != x->len) return -USB_E_INVAL;
    set = &p->sets[x->curr_set]; /* core flips curr_set on each start (§4-6-2) */
    ed = set->ed; first = set->first;
    if (ctrl) {
        const u8 *src = (const u8 *)&x->setup;
        u8 *dst = set->setup.dma.va;
        for (i = 0; i < sizeof(x->setup); ++i) dst[i] = src[i];
        ohci_sync(hc, set->setup.dma.bus, sizeof(x->setup), USBP_TO_DEV);
        td_init(first, OHCI_TD_SETUP | OHCI_TD_DATA0,
                set->setup.dma.bus, sizeof(x->setup), TD_SETUP);
        if (x->len) {
            td_init(first + 1, (in ? OHCI_TD_IN | OHCI_TD_ROUND : OHCI_TD_OUT) |
                    OHCI_TD_DATA1, x->buf.bus, x->len, TD_DATA);
            n = 1;
        }
        last = set->status_td;
        td_init(last, (in && x->len ? OHCI_TD_OUT : OHCI_TD_IN) |
                OHCI_TD_DATA1, 0, 0, TD_STATUS);
        first->sw_next = n ? first + 1 : last;
        if (n) (first + 1)->sw_next = last;
        last->sw_next = NULL;
    } else {
        remaining = x->len; bus = x->buf.bus;
        do {
            u32 len = USB_DMA_PAGE_SIZE - (bus & (USB_DMA_PAGE_SIZE - 1u));
            if (len > remaining) len = remaining;
            td = first + n++;
            td_init(td, in ? OHCI_TD_IN : OHCI_TD_OUT, bus, len, TD_DATA);
            bus += len; remaining -= len;
            td->sw_next = remaining ? td + 1 : NULL;
        } while (remaining);
        last = first + n - 1u;
        if (in) dma_le32_put(&last->hw->flags,
                            dma_le32_get(&last->hw->flags) | OHCI_TD_ROUND);
    }
    for (td = first; td; td = td->sw_next) {
        u32 next = td->sw_next ? td->sw_next->bus : 0;
        if (ctrl && x->len && td == first + 1 && x->short_ok) {
            next = 0;
            td->hw_next_is_end = 1;
        }
        /* STATUS has an alt pointer too, as FB's segment builder does;
         * only hw_next_is_end marks a continuation point. */
        if (ctrl && x->short_ok && td->stage != TD_SETUP) td->alt_next = last;
        dma_le32_put(&td->hw->nexttd, next);
        ohci_sync(hc, td->bus, sizeof(*td->hw), USBP_TO_DEV);
    }
    if (x->len) usbp_dma_sync(&x->buf, 0, x->len, USBP_TO_DEV);
    p->ed = ed->hw; p->cur = first; p->last = last;
    p->status_td = ctrl ? last : NULL;
    p->short_seen = 0; p->resumed = 0; p->ng = 0;
    flags = x->dev->addr | ((x->pipe.ep & USB_EP_NUM_MASK) << OHCI_ED_EP_SHIFT) |
            ((u32)x->pipe.mps << OHCI_ED_MPS_SHIFT);
    if (x->dev->speed == USB_SPEED_LOW) flags |= OHCI_ED_LOW_SPEED;
    dma_le32_put(&ed->hw->flags, flags);
    dma_le32_put(&ed->hw->tailp, 0);
    dma_le32_put(&ed->hw->headp, first->bus | (x->pipe.toggle ? OHCI_TOGGLECARRY : 0));
    prev = anchor(hc, x);
    ed->sw_prev = prev; ed->sw_next = prev->sw_next;
    if (ed->sw_next) ed->sw_next->sw_prev = ed;
    dma_le32_put(&ed->hw->nexted, dma_le32_get(&prev->hw->nexted));
    ohci_sync(hc, ed->bus, sizeof(*ed->hw), USBP_TO_DEV);
    prev->sw_next = ed;
    dma_le32_put(&prev->hw->nexted, ed->bus);
    ohci_sync(hc, prev->bus, sizeof(*prev->hw), USBP_TO_DEV);
    if (ctrl || x->pipe.type == USB_EP_BULK)
        ohci_wr(hc, OHCI_COMMAND_STATUS, ctrl ? OHCI_CMD_CLF : OHCI_CMD_BLF);
    return 0;
}
