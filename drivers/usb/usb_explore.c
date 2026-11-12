/* FB usb_hub.c:707-940,1139-1212: one cooperative depth-first walk. */
#include "drivers/usb/usb_walk.h"
static struct usb_walk *walks;
struct usb_dev **usb_port_child(struct usb_hc *hc, struct usb_dev *hub, u8 port)
{
    return hub ? &usb_hub_sc(hub)->child[port - 1] : &hc->root_child[port - 1];
}
static u8 *restart(struct usb_walk *w)
{
    struct usb_explore *e = &w->hc->explore;
    return e->hub ? &usb_hub_sc(e->hub)->restart[e->port - 1] : &w->hc->root_restart[e->port - 1];
}
int usb_port_op(struct usb_walk *w, u8 op)
{
    struct usb_explore *e = &w->hc->explore;
    u8 req = USB_REQ_CLEAR_FEATURE;
    u16 feature = 0;
    if (!e->hub) {
        int rc = w->hc->ops->rh_port(w->hc, e->port, op, &w->status);
        w->error = rc < 0; return 1;
    }
    switch (op) {
    case USB_RH_STATUS: req = USB_REQ_GET_STATUS; break;
    case USB_RH_SET_ENABLE: req = USB_REQ_SET_FEATURE; feature = USB_PORT_ENABLE; break;
    case USB_RH_CLEAR_ENABLE: feature = USB_PORT_ENABLE; break;
    case USB_RH_SET_POWER: req = USB_REQ_SET_FEATURE; feature = USB_PORT_POWER; break;
    case USB_RH_RESET: req = USB_REQ_SET_FEATURE; feature = USB_PORT_RESET; break;
    case USB_RH_CLEAR_CONNECT: feature = USB_C_PORT_CONNECTION; break;
    case USB_RH_CLEAR_ENABLE_CHANGE: feature = USB_C_PORT_ENABLE; break;
    case USB_RH_CLEAR_RESET: feature = USB_C_PORT_RESET; break;
    case USB_RH_CLEAR_OVER_CURRENT: feature = USB_C_PORT_OVER_CURRENT; break;
    }
    if (!usb_walk_request(w, e->hub, USB_REQ_CLASS | USB_RECIP_OTHER |
                          (op == USB_RH_STATUS ? USB_DIR_IN : 0), req, feature,
                          e->port, op == USB_RH_STATUS ? 4 : 0, &w->scratch)) return 0;
    if (op == USB_RH_STATUS && !w->error) {
        if (w->actual != 4) w->error = USB_X_IO;
        else w->status = le32_rd(w->scratch.va);
    }
    return 1;
}
static void next_port(struct usb_walk *w, int descend)
{
    struct usb_explore *e = &w->hc->explore;
    struct usb_dev *d = *usb_port_child(w->hc, e->hub, e->port);
    w->phase = w->freeing = 0; e->child = 0; e->timeout = 0;
    if (descend && d && usb_hub_sc(d)) { e->hub = d; e->port = 1; }
    else e->port++;
    for (;;) {
        u8 n = e->hub ? usb_hub_sc(e->hub)->nports : w->hc->nports;
        if (e->port <= n) { e->step = EX_P0; return; }
        if (!e->hub) { w->hc->explored = 1; e->step = EX_X9; return; }
        usb_hub_sc(e->hub)->explored = 1;
        e->port = e->hub->port + 1; e->hub = e->hub->parent;
    }
}
static int reset_port(struct usb_walk *w)
{
    struct usb_explore *e = &w->hc->explore;
    if (w->reset_phase == 0) {
        if (!usb_port_op(w, USB_RH_RESET)) return 0;
        if (w->error) return -1;
        w->reset_deadline = usbp_ms() + 50; w->poll_deadline = usbp_ms() + 10; w->reset_phase = 1; return 0;
    }
    if (w->reset_phase == 1) {
        if (!usb_due(usbp_ms(), w->poll_deadline)) return 0;
        if (!usb_port_op(w, USB_RH_STATUS)) return 0;
        if (w->error) return -1;
        w->poll_deadline = usbp_ms() + 10;
        if (!(w->status & (1u << USB_C_PORT_RESET))) {
            return usb_due(usbp_ms(), w->reset_deadline) ? -1 : 0;
        }
        w->reset_phase = 2;
    }
    if (w->reset_phase == 2) {
        if (!usb_port_op(w, USB_RH_CLEAR_RESET)) return 0;
        if (w->error) return -1;
        e->deadline_ms = usbp_ms() + 20; w->reset_phase = 3; return 0;
    }
    if (!usb_due(usbp_ms(), e->deadline_ms)) return 0;
    w->reset_phase = 0; return 1;
}
static struct usb_dev *alloc_device(struct usb_walk *w)
{
    struct usb_hc *hc = w->hc;
    struct usb_explore *e = &hc->explore;
    struct usb_dev *d;
    u32 a, count = 0;
    for (a = 1; a <= USB_DEV_MAX; a++) if (hc->devices[a]) count++;
    if (count >= USB_DEV_COUNT_MAX) return 0;
    for (a = 1; a <= USB_DEV_MAX && hc->devices[a]; a++) {}
    if (a > USB_DEV_MAX) return 0;
    d = usbp_mem_alloc(sizeof(*d));
    if (!d) return 0;
    kmemset(d, 0, sizeof(*d)); d->hc = hc; d->parent = e->hub; d->port = e->port;
    d->speed = (w->status & (1u << 9)) ? USB_SPEED_LOW :
               (w->status & (1u << 10)) ? USB_SPEED_HIGH : USB_SPEED_FULL;
    d->ep0_mps = d->speed == USB_SPEED_HIGH ? 64 : 8;
    d->depth = e->hub ? e->hub->depth + 1 : 0;
    d->state = DEV_ENUM;
    for (u32 i = 0; i < USB_DEV_IFACE_MAX; i++) d->ifs[i].num = 255;
    if (usb_xfer_setup(d, 0, 0, 0, &d->ctrl) < 0) { usbp_mem_free(d); return 0; }
    e->old_addr = a;
    *usb_port_child(hc, e->hub, e->port) = d;
    return d;
}
void usb_dev_return(struct usb_dev *d) { usbp_mem_free(d); }
void usb_explore_step(struct usb_hc *hc)
{
    struct usb_walk *w;
    struct usb_explore *e = &hc->explore;
    int rc;
    if (hc->state != HC_RUNNING || !hc->nports) return;
    for (w = walks; w && w->hc != hc; w = w->next) {}
    if (!w) {
        w = usbp_mem_alloc(sizeof(*w));
        if (!w) return;
        kmemset(w, 0, sizeof(*w)); w->hc = hc;
        if (usb_dmamem_alloc(hc, USB_CTRL_BUF_SIZE, 16, &w->scratch) < 0) { usbp_mem_free(w); return; }
        w->next = walks; walks = w; e->step = EX_X9;
    }
    switch (e->step) {
    case EX_X9:
        if (!hc->needs_explore) return;
        e->step = EX_X0;
        /* fall through */
    case EX_X0:
        hc->needs_explore = 0; e->hub = 0; e->port = 1; w->phase = 0; e->step = EX_P0; return;
    case EX_P0:
        if (w->phase == 0) {
            if (!usb_port_op(w, USB_RH_STATUS)) return;
            if (w->error) { e->step = EX_ERR; return; }
            if (!(e->hub ? usb_hub_sc(e->hub)->explored : hc->explored)) w->status |= 1u << USB_C_PORT_CONNECTION;
            w->phase = 1;
        }
        if (w->phase == 1) {
            u32 status = w->status;
            if ((status & (1u << USB_C_PORT_OVER_CURRENT)) && !usb_port_op(w, USB_RH_CLEAR_OVER_CURRENT)) return;
            w->status = status;
            w->phase = 2;
        }
        if (w->phase == 2) {
            if (w->status & (1u << USB_C_PORT_ENABLE)) {
                u32 status = w->status;
                if (!usb_port_op(w, USB_RH_CLEAR_ENABLE_CHANGE)) return;
                w->status = status;
                if (!(status & (1u << USB_C_PORT_CONNECTION)) && !(status & (1u << USB_PORT_ENABLE)) && *restart(w) < USB_RESTART_MAX) {
                    (*restart(w))++; w->status |= 1u << USB_C_PORT_CONNECTION;
                }
            }
            w->phase = 3;
        }
        if (w->status & (1u << USB_C_PORT_CONNECTION)) { e->step = EX_R0; w->phase = 0; }
        else next_port(w, 1);
        return;
    case EX_R0:
        if (!w->phase) {
            if (!usb_port_op(w, USB_RH_CLEAR_CONNECT)) return;
            e->child = *usb_port_child(hc, e->hub, e->port);
            if (e->child) usb_free_device_begin(e->child);
            w->phase = 1;
        }
        if (e->child && !usb_free_device_step(e->child)) return;
        e->child = 0; w->phase = 0; e->step = EX_R0B; return;
    case EX_R0B:
        if (!usb_port_op(w, USB_RH_STATUS)) return; /* fresh after free */
        if (w->error) { e->step = EX_ERR; return; }
        if (!(w->status & (1u << USB_PORT_CONNECTION))) { next_port(w, 0); return; }
        if (!(w->status & (1u << USB_PORT_POWER))) usbp_log("usb: connected port without power");
        e->deadline_ms = usbp_ms() + 300; e->step = EX_R1; return;
    case EX_R1:
        if (!usb_due(usbp_ms(), e->deadline_ms)) return;
        w->reset_phase = 0; e->step = EX_R2; return;
    case EX_R2:
    case EX_RE1:
        rc = reset_port(w);
        if (!rc) return;
        if (rc < 0) {
            w->reset_phase = 0;
            if (e->step == EX_RE1 && !e->re_retry++) { e->deadline_ms = usbp_ms() + 500; e->step = EX_RE_WAIT; }
            else if (e->step == EX_RE1 && e->ret_step == EX_A2 && e->re_count++ < 2) {
                e->re_retry = 0; e->step = EX_RE1;
            } else if (e->step == EX_RE1 && e->ret_step == EX_A5) e->step = EX_A6;
            else e->step = EX_ERR;
            return;
        }
        e->step = e->step == EX_R2 ? EX_R2B : EX_RE2; return;
    case EX_R2B:
        if (!usb_port_op(w, USB_RH_STATUS)) return; /* fresh after reset */
        if (w->error) { e->step = EX_ERR; return; }
        if ((w->status & (1u << USB_C_PORT_CONNECTION)) || !(w->status & 1)) {
            if (e->timeout++) e->step = EX_ERR;
            else { e->step = EX_R0; w->phase = 0; }
        } else e->step = EX_R3;
        return;
    case EX_R3: e->step = EX_A0; return;
    case EX_A0:
        e->child = alloc_device(w); usb_enum_reset(w);
        e->step = e->child ? EX_A1 : EX_ERR; return;
    case EX_RE2:
        e->child->addr = 0; e->child->ep0_mps = 8; w->phase = 0; e->step = EX_RE3; return;
    case EX_RE_WAIT:
        if (usb_due(usbp_ms(), e->deadline_ms)) { w->reset_phase = 0; e->step = EX_RE1; }
        return;
    case EX_ERR:
        if (e->cfgbuf) { usb_dmamem_free(hc, e->cfgbuf); usbp_mem_free(e->cfgbuf); e->cfgbuf = 0; }
        if (e->child) {
            usb_free_device_begin(e->child);
            if (!usb_free_device_step(e->child)) return;
            e->child = 0;
        }
        if (!usb_port_op(w, USB_RH_CLEAR_ENABLE)) return;
        next_port(w, 0); return;
    default:
        if (usb_enum_step(w)) { *restart(w) = 0; next_port(w, 1); }
        return;
    }
}
