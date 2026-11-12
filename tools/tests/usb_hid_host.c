#include "drivers/usb/usb_core.h"
/* No libc (CI has no 32-bit libc/crt), as tick_probe_host.c: a zeroing bump
 * pool, local mem*, and exit through the i386 Linux syscall. */
static unsigned char heap[65536] __attribute__((aligned(16)));
static unsigned heap_used;
static void *calloc(unsigned n, unsigned size)
{
    unsigned bytes = (n * size + 15u) & ~15u, i;
    unsigned char *p;
    if (bytes > sizeof(heap) - heap_used) return 0;
    p = heap + heap_used; heap_used += bytes;
    for (i = 0; i < bytes; ++i) p[i] = 0;
    return p;
}
static void free(void *p) { (void)p; }
void *memcpy(void *d, const void *s, unsigned n)
{
    unsigned char *a = d; const unsigned char *b = s;
    while (n--) *a++ = *b++;
    return d;
}
void *memset(void *d, int c, unsigned n)
{
    unsigned char *a = d;
    while (n--) *a++ = (unsigned char)c;
    return d;
}
static void sys_exit(int code)
{
    __asm__ volatile("int $0x80" : : "a"(1), "b"(code));
    for (;;) {}
}
static void abort(void) { sys_exit(99); }
u8 hid_kbd_sc(u8 usage);
#define REQUIRE(c) do { if (!(c)) return __LINE__; } while (0)
struct event { int kind, src, a, b, c; };
static struct event events[128];
static unsigned ne, frees, stops, starts, cancelled;
static u32 clock_ms;
static struct usb_ctrl_req req;
static int pending, fail_setup, fail_start;
static struct usb_xfer *xf[2];
static struct usb_setup setups[8];
static unsigned ns;
static void record(int kind, int src, int a, int b, int c)
{
    events[ne++] = (struct event){kind, src, a, b, c};
}
void *usbp_mem_alloc(u32 n) { return calloc(1, n); }
void usbp_mem_free(void *p) { ++frees; free(p); }
int usbp_input_src_alloc(int kind) { return kind + 1; }
void usbp_input_key(u8 src, u8 sc, int down) { record(0, src, sc, down, 0); }
void usbp_input_key_repeat(u8 src, u8 sc) { record(1, src, sc, 0, 0); }
void usbp_input_mouse(u8 src, i16 dx, i16 dy, u8 b) { record(2, src, dx, dy, b); }
void usbp_input_release(int kind, u8 src) { record(3, src, kind, 0, 0); }
u32 usbp_ms(void) { return clock_ms; }
int usb_ctrl_async(struct usb_dev *d, struct usb_iface *f, const struct usb_setup *s,
                   struct usb_dma *b, u32 off, void (*done)(struct usb_ctrl_req *), void *ctx)
{
    (void)b; (void)off;
    if (pending) abort();
    setups[ns++] = *s;
    req = (struct usb_ctrl_req){ .dev=d, .owner=f, .done=done, .ctx=ctx };
    pending = 1;
    return 0;
}
static void control_done(void) { pending = 0; req.error = USB_X_STALL; req.done(&req); }
void usb_ctrl_cancel_owner(struct usb_dev *d, struct usb_iface *f)
{
    (void)d; ++cancelled;
    if (pending && req.owner == f) { pending=0; req.error=USB_X_CANCELLED; req.done(&req); }
}
int usb_xfer_setup(struct usb_dev *d, struct usb_iface *f, const u8 *ep, u32 n, struct usb_xfer *x)
{
    if (!ep || fail_setup) return -USB_E_NOMEM;
    *x = (struct usb_xfer){.dev=d, .owner=f};
    x->buf.va=calloc(1,n); x->buf.size=n;
    xf[f->num]=x;
    return 0;
}
int usb_xfer_start(struct usb_xfer *x) { (void)x; ++starts; return fail_start ? -USB_E_HC : 0; }
void usb_xfer_unsetup_begin(struct usb_xfer *x) { ++stops; x->unsetup=UNSETUP_WAIT; }
static void finish_unsetup(struct usb_xfer *x) { free(x->buf.va); x->unsetup=UNSETUP_DONE; }
static void report(unsigned iface, const u8 *r, unsigned n, int error)
{
    struct usb_xfer *x=xf[iface];
    memcpy(x->buf.va,r,n); x->actual=n; x->error=error;
    if (error == USB_X_OK && n < x->len)
        x->error = x->short_ok ? USB_X_SHORT : USB_X_SHORT_NG;
    x->done(x);
}
static u8 cfg[] = {
    9,2,41,0,2,1,0,0x80,50,
    9,4,0,0,1,3,1,1,0, 7,5,0x81,3,8,0,10,
    9,4,1,0,1,3,1,2,0, 7,5,0x82,3,3,0,10
};
static int attach(struct usb_dev *d, unsigned i, const struct usb_class *c)
{
    struct usb_iface *f=&d->ifs[i];
    REQUIRE(c->match(d,f));
    REQUIRE(c->attach(d,f)==-USB_E_BUSY);
    REQUIRE(c->attach(d,f)==-USB_E_BUSY && ns==i*2+1);
    REQUIRE(setups[ns-1].type==0x21 && setups[ns-1].request==11);
    REQUIRE(le16_rd(setups[ns-1].index)==i && le16_rd(setups[ns-1].value)==0);
    control_done();
    if (i==0) {
        REQUIRE(c->attach(d,f)==-USB_E_BUSY);
        REQUIRE(setups[ns-1].request==10 && le16_rd(setups[ns-1].length)==0);
        control_done();
    }
    REQUIRE(c->attach(d,f)==0);
    return 0;
}
static int run(void)
{
    struct usb_dev d={.cfg=cfg,.cfg_len=sizeof(cfg)};
    struct usb_iface *k=&d.ifs[0], *m=&d.ifs[1];
    u8 r[8]={0}, mouse[3]={0xfd,0x80,0x7f};
    unsigned n;
    int rc;
    REQUIRE(hid_kbd_sc(0x2f)==0x1a && hid_kbd_sc(0x30)==0x1b && hid_kbd_sc(0x31)==0x28);
    REQUIRE(hid_kbd_sc(0x87)==0x33 && hid_kbd_sc(0x89)==0x0d);
    REQUIRE(hid_kbd_sc(0x88)==0x72 && hid_kbd_sc(0x8a)==0x35 && hid_kbd_sc(0x8b)==0x51);
    REQUIRE(hid_kbd_sc(0x44)==0x52 && hid_kbd_sc(0x45)==0x53 && hid_kbd_sc(0xe3)==0xff);
    REQUIRE(hid_kbd_sc(0x47)==0xff);
    m->num=1;
    REQUIRE(!hid_mouse_class.match(&d,k) && !hid_kbd_class.match(&d,m));
    rc=attach(&d,0,&hid_kbd_class); REQUIRE(!rc);
    rc=attach(&d,1,&hid_mouse_class); REQUIRE(!rc);
    REQUIRE(starts==2 && xf[0]->len==8 && xf[1]->len==3);
    r[0]=0x22; report(0,r,8,USB_X_OK);
    REQUIRE(ne==1 && events[0].a==0x70 && events[0].b==1);
    r[0]=0x20; report(0,r,8,USB_X_OK); REQUIRE(ne==1);
    r[0]=0; report(0,r,8,USB_X_OK); REQUIRE(ne==2 && events[1].b==0);
    r[2]=4; clock_ms=100; report(0,r,8,USB_X_OK);
    REQUIRE(ne==3 && events[2].a==0x1d && events[2].b==1);
    report(0,r,8,USB_X_OK); REQUIRE(ne==3);
    hid_kbd_class.tick(&d,k,599); REQUIRE(ne==3);
    hid_kbd_class.tick(&d,k,600); REQUIRE(ne==4 && events[3].kind==1);
    hid_kbd_class.tick(&d,k,629); REQUIRE(ne==4);
    hid_kbd_class.tick(&d,k,630); REQUIRE(ne==5);
    r[2]=1; report(0,r,8,USB_X_OK); REQUIRE(ne==5); /* rollover preserves state */
    n=starts;
    r[2]=0; report(0,r,7,USB_X_OK); REQUIRE(ne==5 && starts==n+1);
    report(0,r,8,USB_X_OK); REQUIRE(ne==6 && events[5].b==0);
    hid_kbd_class.tick(&d,k,1000); REQUIRE(ne==6);
    n=starts;
    report(1,mouse,2,USB_X_OK); REQUIRE(ne==6 && starts==n+1);
    report(1,mouse,3,USB_X_OK);
    REQUIRE(ne==7 && events[6].kind==2 && events[6].src==2 &&
            events[6].a==-128 && events[6].b==127 && events[6].c==5);
    n=ne;
    r[0]=2; r[2]=4; report(0,r,8,USB_X_OK);
    REQUIRE(ne==n+2 && events[n].a==0x70 && events[n].b==1 &&
            events[n+1].a==0x1d && events[n+1].b==1);
    n=ne;
    r[0]=0; r[2]=5; report(0,r,8,USB_X_OK);
    REQUIRE(ne==n+3 && events[n].a==0x70 && events[n].b==0 &&
            events[n+1].a==0x1d && events[n+1].b==0 &&
            events[n+2].a==0x2d && events[n+2].b==1);
    n=ne;
    hid_kbd_class.detach(&d,k); hid_kbd_class.detach(&d,k);
    REQUIRE(ne==n+1 && events[n].kind==3 && events[n].a==USBP_INPUT_KBD);
    hid_kbd_class.tick(&d,k,2000); REQUIRE(ne==n+1 && !frees && stops==1);
    finish_unsetup(xf[0]); hid_kbd_class.release(&d,k); REQUIRE(frees==1 && !k->sc);
    n=ne; hid_mouse_class.gone(&d,m); hid_mouse_class.gone(&d,m);
    REQUIRE(ne==n+1 && events[n].a==USBP_INPUT_MOUSE && frees==1 && stops==1);
    /* Simulated reboot cleanup: gone itself retains sc and DMA. */
    free(xf[1]->buf.va); free(m->sc); m->sc=0;
    ns=0; pending=0;
    REQUIRE(hid_kbd_class.attach(&d,k)==-USB_E_BUSY);
    hid_kbd_class.detach(&d,k); REQUIRE(!pending && cancelled==2);
    hid_kbd_class.release(&d,k);
    REQUIRE(hid_kbd_class.attach(&d,k)==-USB_E_BUSY); control_done();
    REQUIRE(hid_kbd_class.attach(&d,k)==-USB_E_BUSY); control_done();
    fail_setup=1; REQUIRE(hid_kbd_class.attach(&d,k)==-USB_E_NOMEM);
    hid_kbd_class.release(&d,k); fail_setup=0;
    REQUIRE(hid_kbd_class.attach(&d,k)==-USB_E_BUSY); control_done();
    REQUIRE(hid_kbd_class.attach(&d,k)==-USB_E_BUSY); control_done();
    fail_start=1; REQUIRE(hid_kbd_class.attach(&d,k)==-USB_E_HC);
    REQUIRE(xf[0]->unsetup==UNSETUP_WAIT);
    finish_unsetup(xf[0]); hid_kbd_class.release(&d,k);
    return 0;
}

void _start(void) { sys_exit(run()); }
