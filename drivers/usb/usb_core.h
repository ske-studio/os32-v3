#ifndef USB_CORE_H
#define USB_CORE_H
#include "usb_platform.h"
#include "drivers/usb/usb_desc.h"
/* USB_DESIGN §3-1: device count is separate from the 7-bit address table. */
#define USB_DEV_COUNT_MAX 16u
#define USB_DEV_MAX 127u
#define USB_DEV_IFACE_MAX 4u
#define USB_HUB_PORT_MAX 8u
#define USB_RH_PORTS_MAX 15u
#define USB_HUB_DEPTH_MAX 5u
#define USB_IFACE_EP_MAX 4u
#define USB_HC_DMA_MAX 2u
#define USB_DMA_PAGE_SIZE 4096u
#define USB_DMA_SETS 2u
#define USB_DMA_BLOCK_SIZE 16u
#define USB_DMA_BLOCK_MAX 174u
#define USB_CTRL_BUF_SIZE 64u
#define USB_CTRL_BUF_MAX 8u
#define USB_CFG_MAX 512u
#define USB_CFG_BUF_MAX 2u
#define USB_CLASS_BUF_SIZE 64u
#define USB_CLASS_BUF_MAX (USB_KBD_MAX + USB_MOUSE_MAX)
#define USB_KBD_MAX 4u
#define USB_MOUSE_MAX 4u
#define USB_RESTART_MAX 5u
#define USB_DMA_DELAY_FRAMES 2u
#define USB_DMA_DELAY_MS 20u
struct usb_hc;
struct usb_dev;
struct usb_xfer;
struct usb_iface;
struct usb_ctrl_req;
struct usb_class;
enum usb_speed { USB_SPEED_LOW, USB_SPEED_FULL, USB_SPEED_HIGH };
enum usb_error { USB_E_NOMEM = 1, USB_E_HC, USB_E_CTX, USB_E_INVAL, USB_E_BUSY };
enum usb_xfer_error {
    USB_X_OK, USB_X_SHORT, USB_X_SHORT_NG, USB_X_STALL, USB_X_TIMEOUT,
    USB_X_CANCELLED, USB_X_IO, USB_X_HC
};
enum usb_hc_state { HC_OFF, HC_STARTING, HC_RUNNING, HC_DEAD };
enum usb_dev_state { DEV_ENUM, DEV_CONFIGURED, DEV_DETACHED };
enum usb_xfer_state { XS_IDLE, XS_ACTIVE, XS_DMA_DELAY, XS_CB_PENDING };
enum usb_unsetup_state { UNSETUP_NONE, UNSETUP_WAIT, UNSETUP_DELAY, UNSETUP_DONE };
enum usb_dead_why { USB_WHY_UE = 1, USB_WHY_CLOCK, USB_WHY_DMA_DELAY,
    USB_WHY_UNSETUP, USB_WHY_START, USB_WHY_ABORT };
enum usb_rh_op { USB_RH_STATUS, USB_RH_SET_ENABLE, USB_RH_CLEAR_ENABLE,
    USB_RH_SET_POWER, USB_RH_RESET, USB_RH_CLEAR_CONNECT,
    USB_RH_CLEAR_ENABLE_CHANGE, USB_RH_CLEAR_RESET, USB_RH_CLEAR_OVER_CURRENT };
/* One cooperative exploration per HC; waits resume at a deadline. */
enum usb_explore_state { EX_X0, EX_P0, EX_R0, EX_R0B, EX_R1, EX_R2, EX_R2B, EX_R3,
    EX_A0, EX_A1, EX_A2, EX_A4, EX_A5, EX_A6, EX_RE1, EX_RE2, EX_RE3, EX_RE4, EX_RE_WAIT, EX_ERR, EX_X9 };
enum usb_free_state { FD_IDLE, FD_D0, FD_D1, FD_D2, FD_D3, FD_D4, FD_D5 };
struct usb_pipe { u8 ep, type, interval, toggle; u16 mps; };
struct usb_xfer {
    struct usb_dev *dev;
    struct usb_iface *owner;
    struct usb_pipe pipe;
    /* CONTROL data direction is bmRequestType (setup.type) bit 7;
     * length is wLength (LE setup.length).
     * xfer_enter copies these eight bytes into the SETUP TD buffer. */
    struct usb_setup setup;
    struct usb_dma buf;
    u32 len, actual;
    i8 error;
    u8 state, short_ok, did_dma_delay, open, curr_set, unsetup;
    u16 dma_delay_frame;
    u32 dma_delay_deadline_ms, deadline_ms;
    u16 unsetup_frame;
    u32 unsetup_deadline_ms;
    struct usb_xfer *unsetup_next;
    void *hcpriv;
    void (*done)(struct usb_xfer *);
    void *ctx;
    struct usb_xfer *next;
};
struct usb_ctrl_req {
    struct usb_dev *dev;
    struct usb_iface *owner;
    struct usb_setup setup;
    struct usb_dma *buf;
    u32 off, actual;
    i8 error;
    void (*done)(struct usb_ctrl_req *);
    void *ctx;
    struct usb_ctrl_req *next;
};
struct usb_explore {
    struct usb_dev *hub;
    u8 port, step, ret_step, timeout, re_count, re_retry;
    u32 deadline_ms;
    u8 err, old_addr;
    struct usb_dev *child;
    struct usb_dma *cfgbuf;
    struct usb_ctrl_req *req;
};
struct usb_hc_ops {
    int (*start)(struct usb_hc *);
    void (*kill)(struct usb_hc *);
    u32 (*intr_collect)(struct usb_hc *);
    void (*scan)(struct usb_hc *, u32 budget);
    int (*xfer_alloc)(struct usb_hc *, struct usb_xfer *);
    void (*xfer_free)(struct usb_hc *, struct usb_xfer *);
    int (*xfer_enter)(struct usb_hc *, struct usb_xfer *);
    void (*xfer_close)(struct usb_hc *, struct usb_xfer *);
    u16 (*frame)(struct usb_hc *);
    int (*rh_port)(struct usb_hc *, u8 port, u8 op, u32 *st);
};
struct usb_hc {
    const struct usb_hc_ops *ops;
    volatile void *regs;
    void *hcpriv; /* HCD-owned state, e.g. ohci_dmamem. */
    struct usb_pci_fn pci;
    u8 nports, state, has_irq, started, needs_explore;
    u32 dead_why;
    struct usb_xfer *unsetup_list;
    u32 fault_why, stop_frame, stop_deadline_ms;
    volatile u32 isr_pending;
    struct usb_xfer *active, *scan_next;
    struct usb_dev *devices[USB_DEV_MAX + 1];
    /* Root ports are 1-based: index = port - 1. */
    struct usb_dev *root_child[USB_RH_PORTS_MAX];
    u8 root_restart[USB_RH_PORTS_MAX];
    u8 explored; /* First root-port scan has completed (§4-4-1 P0). */
    struct usb_explore explore;
    struct usb_dma dma[USB_HC_DMA_MAX];
};
struct usb_iface { u8 num, alt, src; const struct usb_class *cls; void *sc; };
struct usb_free_ctx { u8 step, iface, child_port; };
struct usb_dev {
    struct usb_hc *hc;
    struct usb_dev *parent;
    u8 port, addr, speed, state, depth, ep0_mps;
    u8 ddesc[USB_DEVICE_DESC_LEN];
    u16 cfg_len;
    u8 *cfg;
    struct usb_xfer ctrl;
    struct usb_ctrl_req *ctrl_q;
    struct usb_iface ifs[USB_DEV_IFACE_MAX];
    struct usb_free_ctx freeing; /* FD_IDLE when inactive. */
    u8 tt_hub_addr, tt_port;
};
/* detach begins unsetup and cancels owner control requests. gone only releases
 * upper-layer state; it must not free sc or touch xfers after HC death. */
struct usb_class {
    int (*match)(struct usb_dev *, struct usb_iface *);
    int (*attach)(struct usb_dev *, struct usb_iface *);
    void (*detach)(struct usb_dev *, struct usb_iface *);
    void (*gone)(struct usb_dev *, struct usb_iface *);
    void (*tick)(struct usb_dev *, struct usb_iface *, u32 now_ms); /* NULL allowed */
};
/* Nonblocking except sync (foreground only). Reject submissions to detached
 * devices or non-running HCs. drained includes notification; unsetup_done
 * additionally includes the DMA delay and HCD xfer_free. */
int usb_xfer_setup(struct usb_dev *, struct usb_iface *owner, const u8 *epd,
                   u32 bufsize, struct usb_xfer *x);
int usb_xfer_start(struct usb_xfer *x);
void usb_xfer_stop(struct usb_xfer *x);
int usb_xfer_drained(const struct usb_xfer *x);
void usb_xfer_unsetup_begin(struct usb_xfer *x);
int usb_xfer_unsetup_done(const struct usb_xfer *x);
void usb_xfer_unsetup_step(struct usb_xfer *x);
void usb_xfer_notify(struct usb_xfer *x);
int usb_ctrl_async(struct usb_dev *, struct usb_iface *owner, const struct usb_setup *s,
                   struct usb_dma *b, u32 off, void (*done)(struct usb_ctrl_req *), void *ctx);
void usb_ctrl_cancel_owner(struct usb_dev *, struct usb_iface *owner);
int usb_hc_register(struct usb_hc *hc); /* Add to usb_service targets (1E/1P -> 1C). */
void usb_service(void);
extern const struct usb_class *const usb_classes[]; /* NULL-terminated; defined in 1C. */
extern const struct usb_class hid_kbd_class, hid_mouse_class;
void usb_class_tick(u32 now_ms);
int usb_dmamem_init(struct usb_hc *hc);
int usb_dmamem_alloc(struct usb_hc *hc, u32 size, u32 align, struct usb_dma *out);
void usb_dmamem_free(struct usb_hc *hc, struct usb_dma *d);
int usb_xfer_sync(struct usb_xfer *x, u32 timeout_ms);
void usb_explore_step(struct usb_hc *hc);
void usb_free_device_begin(struct usb_dev *);
int usb_free_device_step(struct usb_dev *);
int usb_class_unsetup_done(struct usb_dev *, struct usb_iface *);
/* Kill writes only; quarantine DMA AND ordinary memory until reboot. Never
 * notify queued done callbacks or continue servicing this HC after death. */
void usb_hc_dead(struct usb_hc *, u32 why);
#endif
