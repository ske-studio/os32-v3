/* FreeBSD ohci.c:799-929,939-1060,1123-1189 (BSD-2);
 * USB_DESIGN §4-6-1 D1 and §7-1. Scan works with no WDH interrupt. */
#include "sys/dev/usb/ohci_internal.h"

#ifdef TICK_PROBE
static void record_first(struct irq10_event *event, u32 bits)
{
    if (++event->count != 1) return;
    event->bits = bits;
    if (arch_cpu_has_tsc()) {
        u32 hi, lo;
        __asm__ volatile("rdtsc" : "=a"(lo), "=d"(hi) : : "memory");
        event->first = ((unsigned long long)hi << 32) | lo;
    }
}

u32 ohci_intr_collect(struct usb_hc *hc)
{
    return ohci_diag_collect(hc, 0);
}

u32 ohci_diag_collect(struct usb_hc *hc, unsigned int side)
#else
u32 ohci_intr_collect(struct usb_hc *hc)
#endif
{
    struct ohci_dmamem *m;
    u32 st;
    if (hc->state != HC_RUNNING) return 0;
    st = ohci_rd(hc, OHCI_INTERRUPT_STATUS) & OHCI_WATCH;
    if (!st) return 0;
#ifdef TICK_PROBE
    struct irq10_diag *diag = usb_diag_for(hc);
    if (diag) {
        record_first(&diag->usb[side], st);
        if (diag->collecting) {
            u32 enable = ohci_rd(hc, OHCI_INTERRUPT_ENABLE);
            u32 bits = st & enable & (OHCI_INTR_WDH | OHCI_INTR_RHSC);
            if (bits) {
                struct irq10_window_event *w = &diag->window[side];
                if (!w->event.count) {
                    w->gen = diag->pcm.generation;
                    w->enable = enable;
                    if (side) diag->capture(&diag->service, 1);
                }
                record_first(&w->event, bits);
                w->wdh += !!(bits & OHCI_INTR_WDH);
                w->rhsc += !!(bits & OHCI_INTR_RHSC);
            }
        }
    }
#endif
    m = hc->hcpriv;
    if (st & OHCI_INTR_WDH) {
        ohci_sync(hc, hc->dma[0].bus + 0x84u, 4, USBP_TO_CPU);
        (void)dma_le32_get(&m->hcca->done_head);
        dma_le32_put(&m->hcca->done_head, 0);
        ohci_sync(hc, hc->dma[0].bus + 0x84u, 4, USBP_TO_DEV);
    }
    if (st & OHCI_INTR_RHSC) ohci_wr(hc, OHCI_INTERRUPT_DISABLE, OHCI_INTR_RHSC);
    ohci_wr(hc, OHCI_INTERRUPT_STATUS, st);
    return st;
}

static i8 cc_error(u32 cc)
{
    return cc == OHCI_CC_STALL ? USB_X_STALL : USB_X_IO;
}

static int check(struct usb_hc *hc, struct usb_xfer *x)
{
    struct ohci_xp *p = x->hcpriv;
    struct ohci_td *td;
    u32 head, tail;
    ohci_sync(hc, p->sets[x->curr_set].ed->bus, sizeof(*p->ed), USBP_TO_CPU);
    head = dma_le32_get(&p->ed->headp); tail = dma_le32_get(&p->ed->tailp);
    if (!(head & OHCI_HALTED) && ((head ^ tail) & OHCI_PTR_MASK)) return 0;
    for (td = p->cur; td; td = td->sw_next) {
        u32 cc, cbp;
        int data, shrt;
        ohci_sync(hc, td->bus, sizeof(*td->hw), USBP_TO_CPU);
        cc = OHCI_TD_CC(dma_le32_get(&td->hw->flags));
        cbp = dma_le32_get(&td->hw->cbp);
        data = td->stage == TD_DATA;
        if (data) x->actual += cbp ? cbp - td->start_bus : td->len;
        if (cc && !(cc == OHCI_CC_DATA_UNDERRUN && data)) {
            p->ng = cc_error(cc); break;
        }
        shrt = data && (cc == OHCI_CC_DATA_UNDERRUN || cbp);
        if (shrt) p->short_seen = 1;
        if (td->hw_next_is_end && td->alt_next) {
            p->cur = td->alt_next; p->resumed = 1;
            dma_le32_put(&p->ed->headp, p->cur->bus | (head & OHCI_TOGGLECARRY));
            ohci_sync(hc, p->sets[x->curr_set].ed->bus, sizeof(*p->ed), USBP_TO_DEV);
            ohci_wr(hc, OHCI_COMMAND_STATUS, OHCI_CMD_CLF);
            return 0;
        }
        if (shrt) {
            if (p->status_td && !p->resumed) {
                ohci_sync(hc, p->status_td->bus, sizeof(*td->hw), USBP_TO_CPU);
                cc = OHCI_TD_CC(dma_le32_get(&p->status_td->hw->flags));
                if (cc) p->ng = cc_error(cc);
            }
            break;
        }
        if (td == p->last) break;
    }
    x->error = p->ng ? p->ng : p->short_seen ?
               (x->short_ok ? USB_X_SHORT : USB_X_SHORT_NG) : USB_X_OK;
    p->pipe->toggle = !!(head & OHCI_TOGGLECARRY);
    if (x->len) usbp_dma_sync(&x->buf, 0, x->len, USBP_TO_CPU);
    ohci_xfer_close_done(hc, x);
    return 1;
}

void ohci_scan(struct usb_hc *hc, u32 budget)
{
    struct usb_xfer *first, *x;
    if (hc->state != HC_RUNNING || !hc->active) return;
    x = hc->scan_next ? hc->scan_next : hc->active;
    first = x;
    do {
        struct usb_xfer *next = x->next ? x->next : hc->active;
        if (!budget--) break;
        if (x->state == XS_ACTIVE) check(hc, x);
        x = next;
    } while (x != first);
    hc->scan_next = x;
}
