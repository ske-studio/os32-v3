/* FB usb_device.c:1906-2039; usb_request.c:1352-1390,2056-2250. */
#include "drivers/usb/usb_walk.h"
static void request_done(struct usb_ctrl_req *r)
{
    struct usb_walk *w = r->ctx;
    w->pending = 0; w->complete = 1; w->error = r->error; w->actual = r->actual;
    w->hc->explore.req = 0;
}
int usb_walk_request(struct usb_walk *w, struct usb_dev *d, u8 type, u8 req,
                     u16 value, u16 index, u16 len, struct usb_dma *buf)
{
    struct usb_setup s;
    int rc;
    if (w->pending) return 0;
    if (w->complete) { w->complete = 0; w->resource_deadline = 0; return 1; }
    kmemset(&s, 0, sizeof(s)); s.type = type; s.request = req;
    le16_wr(s.value, value); le16_wr(s.index, index); le16_wr(s.length, len);
    w->pending = 1;
    rc = usb_ctrl_async(d, 0, &s, buf, 0, request_done, w);
    if (rc < 0) {
        w->pending = 0;
        if (rc == -USB_E_NOMEM) {
            if (!w->resource_deadline) w->resource_deadline = usbp_ms() + USB_REQUEST_TIMEOUT_MS;
            if (!usb_due(usbp_ms(), w->resource_deadline)) return 0;
        }
        w->resource_deadline = 0;
        w->error = USB_X_IO; w->actual = 0; return 1;
    }
    return 0;
}
void usb_enum_reset(struct usb_walk *w)
{
    w->phase = w->desc_phase = w->desc_retry = w->config_retry = 0;
    w->iface = w->attach_failed = w->error = 0;
}
static int valid_mps(struct usb_dev *d, u8 mps)
{
    if (d->speed == USB_SPEED_LOW) return mps == 8;
    if (d->speed == USB_SPEED_HIGH) return mps == 64;
    return mps == 8 || mps == 16 || mps == 32 || mps == 64;
}
/* Return 0 while waiting, 1 success, -1 failure. */
static int device_desc(struct usb_walk *w)
{
    struct usb_dev *d = w->hc->explore.child;
    u16 len = (d->speed == USB_SPEED_FULL && w->desc_phase == 0) ? 8 : 18;
    u8 *b = w->scratch.va;
    if (!usb_walk_request(w, d, USB_DIR_IN, USB_REQ_GET_DESCRIPTOR,
                          USB_DESC_DEVICE << 8, 0, len, &w->scratch)) return 0;
    if (!w->error && w->actual == len && b[1] == USB_DESC_DEVICE &&
        valid_mps(d, b[7]) && (len == 8 || b[0] >= 18)) {
        d->ep0_mps = b[7];
        if (len == 8) { w->desc_phase = 1; return 0; }
        kmemcpy(d->ddesc, b, 18); w->desc_phase = w->desc_retry = 0; return 1;
    }
    if (d->speed == USB_SPEED_FULL && w->desc_phase == 0) {
        w->desc_phase = 2; d->ep0_mps = 8; return 0;
    }
    if (d->speed == USB_SPEED_FULL && w->desc_phase >= 2 && w->desc_phase < 4) {
        w->desc_phase++; d->ep0_mps = (w->desc_phase == 3) ? 16 : 32; return 0;
    }
    if (d->speed != USB_SPEED_FULL && !w->desc_retry++) return 0;
    w->desc_phase = w->desc_retry = 0; return -1;
}
static void cfg_drop(struct usb_walk *w)
{
    struct usb_explore *e = &w->hc->explore;
    struct usb_dev *d = e->child;
    if (e->cfgbuf) { usb_dmamem_free(w->hc, e->cfgbuf); usbp_mem_free(e->cfgbuf); e->cfgbuf = 0; }
    if (d->cfg) { usbp_mem_free(d->cfg); d->cfg = 0; d->cfg_len = 0; }
    kmemset(d->ifs, 0, sizeof(d->ifs));
    for (u32 i = 0; i < USB_DEV_IFACE_MAX; i++) d->ifs[i].num = 255;
}
static void cfg_failed(struct usb_walk *w)
{
    struct usb_explore *e = &w->hc->explore;
    cfg_drop(w); w->phase = 0; w->resource_deadline = 0;
    if (!w->config_retry++) {
        e->ret_step = EX_A5; e->re_retry = 0; e->step = EX_RE1; w->reset_phase = 0;
    } else e->step = EX_A6;
}
int usb_enum_step(struct usb_walk *w)
{
    struct usb_hc *hc = w->hc;
    struct usb_explore *e = &hc->explore;
    struct usb_dev *d = e->child;
    int rc;
    switch (e->step) {
    case EX_A1:
    case EX_RE3:
        if (w->phase == 0) {
            if (!usb_walk_request(w, d, 0, USB_REQ_SET_ADDRESS, e->old_addr, 0, 0, 0)) return 0;
            /* FB ignores SET_ADDRESS failure, including lost STATUS. */
            d->addr = e->old_addr; e->deadline_ms = usbp_ms() + 10; w->phase = 1;
            return 0;
        }
        if (!usb_due(usbp_ms(), e->deadline_ms)) return 0;
        w->phase = 0; e->step = (e->step == EX_A1) ? EX_A2 : EX_RE4; return 0;
    case EX_A2:
    case EX_RE4:
        rc = device_desc(w);
        if (!rc) return 0;
        if (rc > 0) { e->step = EX_A4; w->phase = 0; return 0; }
        if (e->step == EX_A2) {
            e->ret_step = EX_A2; e->re_count = 1; e->re_retry = 0;
            e->step = EX_RE1; w->reset_phase = 0; /* A2 recovery */
        } else {
            if (!e->re_retry++) { e->deadline_ms = usbp_ms() + 500; e->step = EX_RE_WAIT; }
            else if (e->ret_step == EX_A2 && e->re_count++ < 2) {
                e->re_retry = 0; e->step = EX_RE1; w->reset_phase = 0;
            } else if (e->ret_step == EX_A5) { cfg_drop(w); e->step = EX_A6; }
            else e->step = EX_ERR;
        }
        return 0;
    case EX_A4:
        if (!w->phase) {
            if (d->cfg) { usbp_mem_free(d->cfg); d->cfg = 0; d->cfg_len = 0; }
            if (!e->cfgbuf) {
                e->cfgbuf = usbp_mem_alloc(sizeof(*e->cfgbuf));
                if (!e->cfgbuf) {
                    if (!w->resource_deadline) w->resource_deadline = usbp_ms() + USB_REQUEST_TIMEOUT_MS;
                    if (usb_due(usbp_ms(), w->resource_deadline)) cfg_failed(w);
                    return 0;
                }
                if (usb_dmamem_alloc(hc, USB_CFG_MAX, USB_CFG_MAX, e->cfgbuf) < 0) {
                    usbp_mem_free(e->cfgbuf); e->cfgbuf = 0;
                    if (!w->resource_deadline) w->resource_deadline = usbp_ms() + USB_REQUEST_TIMEOUT_MS;
                    if (usb_due(usbp_ms(), w->resource_deadline)) cfg_failed(w);
                    return 0;
                }
            }
            if (!usb_walk_request(w, d, USB_DIR_IN, USB_REQ_GET_DESCRIPTOR,
                                  USB_DESC_CONFIG << 8, 0, 9, e->cfgbuf)) return 0;
            if (w->error || w->actual != 9 || ((u8 *)e->cfgbuf->va)[1] != USB_DESC_CONFIG) { cfg_failed(w); return 0; }
            w->cfg_length = le16_rd((u8 *)e->cfgbuf->va + 2);
            if (w->cfg_length < 9) { cfg_failed(w); return 0; }
            if (w->cfg_length > USB_CFG_MAX) w->cfg_length = USB_CFG_MAX;
            w->phase = 1; return 0;
        }
        if (!usb_walk_request(w, d, USB_DIR_IN, USB_REQ_GET_DESCRIPTOR,
                              USB_DESC_CONFIG << 8, 0, w->cfg_length, e->cfgbuf)) return 0;
        if (w->error || w->actual != w->cfg_length) { cfg_failed(w); return 0; }
        le16_wr((u8 *)e->cfgbuf->va + 2, w->cfg_length);
        d->cfg = usbp_mem_alloc(w->cfg_length);
        if (!d->cfg) { cfg_drop(w); e->step = EX_ERR; return 0; }
        d->cfg_len = w->cfg_length; kmemcpy(d->cfg, e->cfgbuf->va, d->cfg_len);
        usb_dmamem_free(hc, e->cfgbuf); usbp_mem_free(e->cfgbuf); e->cfgbuf = 0;
        if (usb_cfg_parse(d) < 0) { cfg_failed(w); return 0; }
        w->phase = 0; e->step = EX_A5; return 0;
    case EX_A5:
        if (!usb_walk_request(w, d, 0, USB_REQ_SET_CONFIGURATION, d->cfg[5], 0, 0, 0)) return 0;
        if (w->error) cfg_failed(w);
        else { e->step = EX_A6; w->iface = w->phase = 0; }
        return 0;
    case EX_A6:
        hc->devices[d->addr] = d;
        d->state = DEV_CONFIGURED;
        if (!d->cfg || w->iface == USB_DEV_IFACE_MAX) return 1;
        {
            struct usb_iface *f = &d->ifs[w->iface];
            if (f->num == 255) { w->iface++; return 0; }
            if (w->attach_failed == 2) {
                if (!usb_port_op(w, USB_RH_STATUS)) return 0;
                w->attach_failed = 0;
                if (w->error || !(w->status & 1)) e->step = EX_ERR;
                else w->iface++;
                return 0;
            }
            if (w->attach_failed) {
                f->attached = 0;
                if (!usb_class_unsetup_done(d, f)) return 0;
                if (f->cls->release) f->cls->release(d, f);
                f->cls = 0; f->sc = 0; w->attach_failed = 2; return 0;
            }
            if (!f->cls) {
                for (u32 i = 0; usb_classes[i]; i++)
                    if (usb_classes[i]->match(d, f)) { f->cls = usb_classes[i]; break; }
                if (!f->cls) { w->iface++; return 0; }
            }
            rc = f->cls->attach(d, f);
            if (rc == -USB_E_BUSY) return 0;
            if (rc < 0) { w->attach_failed = 1; return 0; }
            f->attached = 1;
            w->iface++; return 0;
        }
    default: return 0;
    }
}
