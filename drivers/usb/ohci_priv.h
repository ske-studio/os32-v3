#ifndef OHCI_PRIV_H
#define OHCI_PRIV_H
#include "drivers/usb/usb_core.h"
#include "drivers/usb/ohci_regs.h"
#define OHCI_TREE_EDS 63u
#define OHCI_SENTINEL_EDS 3u
#define OHCI_FIXED_BYTES 1312u
/* Hardware records stay exactly 16 bytes; CPU-only shadows are ordinary
 * memory. No pointer or CPU metadata is embedded in a device descriptor. */
enum ohci_td_stage { TD_SETUP, TD_DATA, TD_STATUS };
struct ohci_block { struct usb_dma dma; u16 index; u8 used; };
struct ohci_td {
    struct ohci_td_hw *hw;
    u32 bus, len, start_bus;
    struct ohci_td *sw_next, *alt_next;
    u8 stage, hw_next_is_end;
};
struct ohci_ed {
    struct ohci_ed_hw *hw;
    u32 bus;
    struct ohci_ed *sw_next, *sw_prev;
};
struct ohci_dma_set {
    struct ohci_ed *ed;
    struct ohci_td *first, *last, *status_td;
    struct ohci_block setup;
};
/* xfer owns both sets until unsetup's final DMA delay. */
struct ohci_xp {
    struct ohci_dma_set sets[USB_DMA_SETS];
    struct ohci_ed_hw *ed;
    struct usb_pipe *pipe;
    struct ohci_td *cur, *last, *status_td;
    u8 short_seen, resumed;
    i8 ng;
};
struct ohci_dmamem {
    struct ohci_hcca *hcca;
    struct ohci_ed tree[OHCI_TREE_EDS], sentinel[OHCI_SENTINEL_EDS];
    struct ohci_block blocks[USB_DMA_BLOCK_MAX];
    struct ohci_block control[USB_CTRL_BUF_MAX];
    struct ohci_block reports[USB_CLASS_BUF_MAX];
    struct ohci_block config[USB_CFG_BUF_MAX];
};
STATIC_ASSERT(OHCI_FIXED_BYTES + USB_DMA_BLOCK_MAX * USB_DMA_BLOCK_SIZE ==
              USB_DMA_PAGE_SIZE, ohci_page_a_budget);
STATIC_ASSERT(USB_CFG_MAX * USB_CFG_BUF_MAX +
              USB_CTRL_BUF_SIZE * USB_CTRL_BUF_MAX +
              USB_CLASS_BUF_SIZE * USB_CLASS_BUF_MAX <= USB_DMA_PAGE_SIZE,
              ohci_page_b_budget);
int ohci_start(struct usb_hc *hc);
void ohci_kill(struct usb_hc *hc);
u32 ohci_intr_collect(struct usb_hc *hc);
void ohci_scan(struct usb_hc *hc, u32 budget);
int ohci_xfer_alloc(struct usb_hc *hc, struct usb_xfer *x);
void ohci_xfer_free(struct usb_hc *hc, struct usb_xfer *x);
int ohci_xfer_enter(struct usb_hc *hc, struct usb_xfer *x);
void ohci_xfer_close(struct usb_hc *hc, struct usb_xfer *x);
void ohci_xfer_close_done(struct usb_hc *hc, struct usb_xfer *x);
u16 ohci_frame(struct usb_hc *hc);
int ohci_rh_port(struct usb_hc *hc, u8 port, u8 op, u32 *st);
int ehci_quiesce(struct usb_hc *hc, u32 *why);
extern const struct usb_hc_ops ohci_ops;
#endif
