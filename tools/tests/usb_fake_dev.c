/* Minimal HC contract model: no OHCI implementation linked into core tests. */
#include "drivers/usb/usb_walk.h"
#include "drivers/usb/ohci_regs.h"
#include "endian_dma.h"
static void failure(const char *text, u32 len)
{
    __asm__ volatile("int $0x80" : : "a"(4), "b"(2), "c"(text), "d"(len) : "memory");
}
#define REQUIRE(c) do { if (!(c)) { failure("FAIL: " #c "\n", sizeof("FAIL: " #c "\n")-1); failed = 1; } } while (0)
static int failed;
static u32 now, frame, freeze, inject, gone, callbacks, attached, detached, released;
static u32 allocation_count, free_count, dma_count, dma_free_count;
static u32 fail_attach, busy_attach, address_early, overlap, reset_count;
static u8 heap[2 * 1024 * 1024];
static u32 heap_used;
struct allocation { void *ptr; u32 size; u8 freed; };
static struct allocation allocations[4096];
static u32 allocations_n;
struct node {
    u8 kind, addr, conn, enabled, reset_fail, desc_fail, setaddr_fail, config_fail;
    u8 re_fail, resets, reset_change, connect_change, enable_change, power;
    u32 addressed_at, gets, resets_at;
    u16 total;
};
static struct node nodes[8];
struct fake_xp { u32 entered, held[2]; u8 linked; };
static struct usb_hc hc;
static struct usb_dev direct;
static struct usb_xfer *pending_at_deadline;
static u32 free_at_frame, swap_on_reset;
void *kmemset(void *p, int v, u32 n) { u8 *b = p; while (n--) *b++ = v; return p; }
void *kmemcpy(void *p, const void *s, u32 n) { u8 *b = p; const u8 *a = s; while (n--) *b++ = *a++; return p; }
void *usbp_mem_alloc(u32 n)
{
    void *p;
    n = (n + 15) & ~15u;
    if (heap_used + n > sizeof(heap) || allocations_n == 4096) return 0;
    p = heap + heap_used; heap_used += n;
    allocations[allocations_n++] = (struct allocation){p,n,0}; allocation_count++;
    kmemset(p, 0, n); return p;
}
void usbp_mem_free(void *p)
{
    if (!p) return;
    for (u32 i = 0; i < allocations_n; i++) if (allocations[i].ptr == p) {
        REQUIRE(!allocations[i].freed); allocations[i].freed = 1;
        kmemset(p, 0xdd, allocations[i].size); free_count++; return;
    }
    REQUIRE(0);
}
int usb_dmamem_alloc(struct usb_hc *h, u32 n, u32 align, struct usb_dma *d)
{
    (void)h; (void)align;
    d->va = usbp_mem_alloc(n); if (!d->va) return -USB_E_NOMEM;
    d->size = n; d->bus = (u32)d->va; d->cookie = ++dma_count; return 0;
}
void usb_dmamem_free(struct usb_hc *h, struct usb_dma *d)
{
    REQUIRE(h->state != HC_DEAD); dma_free_count++; usbp_mem_free(d->va); kmemset(d,0,sizeof(*d));
}
void usbp_dma_sync(struct usb_dma *d, u32 off, u32 len, int dir)
{ REQUIRE(off <= d->size && len <= d->size - off); (void)dir; }
u32 usbp_ms(void) { return now; }
u32 usbp_irq_save(void) { return 0; }
void usbp_irq_restore(u32 f) { (void)f; }
void usbp_log(const char *fmt, ...) { (void)fmt; }
void usbp_input_release(int kind, u8 src) { (void)kind; (void)src; }
static struct node *node_for(struct usb_dev *d)
{
    if (d == &direct) return &nodes[0];
    return d->parent ? &nodes[3 + d->port] : &nodes[d->port - 1];
}
static u32 port_status(struct node *n)
{
    return n->conn | ((u32)n->enabled << 1) | ((u32)n->power << 8) |
        ((u32)n->connect_change << 16) | ((u32)n->enable_change << 17) |
        ((u32)n->reset_change << 20);
}
static void port_act(struct node *n, u8 op)
{
    switch (op) {
    case USB_RH_RESET:
        reset_count++; n->resets++; n->addr = 0; n->enabled = 1;
        n->reset_change = !n->reset_fail && !(n->re_fail && n->resets == 2); n->resets_at = now;
        for (u32 i=0;i<8;i++) if (&nodes[i] != n && nodes[i].conn && nodes[i].enabled && !nodes[i].addr) overlap++;
        break;
    case USB_RH_CLEAR_CONNECT: n->connect_change = 0; break;
    case USB_RH_CLEAR_RESET: n->reset_change = 0; break;
    case USB_RH_CLEAR_ENABLE: n->enabled = 0; break;
    case USB_RH_SET_ENABLE: n->enabled = 1; break;
    case USB_RH_CLEAR_ENABLE_CHANGE: n->enable_change = 0; break;
    case USB_RH_SET_POWER: n->power = 1; break;
    }
}
static int fake_rh(struct usb_hc *h, u8 p, u8 op, u32 *s)
{
    (void)h; REQUIRE(p && p <= 3);
    if (swap_on_reset && op==USB_RH_STATUS && nodes[p-1].resets==1 && !nodes[p-1].reset_change && now-nodes[p-1].resets_at>=30) { nodes[p-1].connect_change=1;swap_on_reset=0; }
    port_act(&nodes[p-1],op); *s=port_status(&nodes[p-1]); return 0;
}
static u16 fake_frame(struct usb_hc *h) { REQUIRE(h->state == HC_RUNNING); return frame; }
static int fake_start(struct usb_hc *h) { h->state=HC_RUNNING; return 0; }
static void fake_kill(struct usb_hc *h) { (void)h; }
static u32 fake_intr(struct usb_hc *h)
{
    (void)h; now++; if (!freeze) frame++;
    if (h == &hc && inject && now >= inject) { inject=0; return OHCI_INTR_UE; }
    return 0;
}
static int fake_alloc(struct usb_hc *h, struct usb_xfer *x)
{ (void)h; x->hcpriv=usbp_mem_alloc(sizeof(struct fake_xp)); return x->hcpriv ? 0 : -USB_E_NOMEM; }
static void fake_free(struct usb_hc *h, struct usb_xfer *x)
{
    struct fake_xp *p=x->hcpriv;
    REQUIRE(h->state==HC_RUNNING);
    REQUIRE(frame >= p->held[0] && frame >= p->held[1]);
    free_at_frame=frame; usbp_mem_free(p); x->hcpriv=0;
}
static int fake_enter(struct usb_hc *h, struct usb_xfer *x)
{
    struct fake_xp *p=x->hcpriv;
    REQUIRE(h->state==HC_RUNNING); REQUIRE(frame >= p->held[x->curr_set]);
    p->linked=1; p->entered=now; p->held[x->curr_set]=frame+2; return 0;
}
static void fake_close(struct usb_hc *h, struct usb_xfer *x)
{ struct fake_xp *p=x->hcpriv; REQUIRE(h->state==HC_RUNNING); p->linked=0; }
static void configuration(struct node *n, u8 *b, u32 len)
{
    u32 total=n->total ? n->total : n->kind==3 ? 59 : 34;
    const u8 cfg[]={9,2,0,0,1,1,0,0x80,50,9,4,0,0,1,3,1,1,0,9,0x21,0x11,1,0,1,0x22,63,0,7,5,0x81,3,8,0,10};
    kmemset(b,0,len); kmemcpy(b,cfg,len<sizeof(cfg)?len:sizeof(cfg)); le16_wr(b+2,total);
    if (n->kind==2 && len>=18) b[16]=2;
    if (n->kind==4 && len>=18) { b[14]=9; b[15]=b[16]=0; }
    if (n->kind==3 && len>=59) {
        const u8 second[]={9,4,1,0,1,3,1,2,0,9,0x21,0x11,1,0,1,0x22,63,0,7,5,0x82,3,8,0,10};
        b[4]=2; kmemcpy(b+34,second,sizeof(second));
    }
    if (total>59 && len>34) {
        u32 off=34;
        while (off<len) { u32 nbytes=len-off; if(nbytes>255)nbytes=255; if(nbytes<2){b[off]=2;break;} b[off]=nbytes;b[off+1]=0x30;off+=nbytes; }
    }
    if (n->kind==5 && len>9) b[9]=1;
    if (n->kind==6 && len==512) b[289]=255;
    if (n->kind==7 && len>=34) b[33]=0;
}
static void execute_control(struct usb_xfer *x)
{
    struct node *n=node_for(x->dev);
    u8 req=x->setup.request, type=x->setup.type;
    u16 value=le16_rd(x->setup.value), index=le16_rd(x->setup.index);
    u8 *b=x->buf.va;
    if (!n->conn) { x->error=USB_X_TIMEOUT; return; }
    if (req==USB_REQ_SET_ADDRESS) {
        n->addr=value; n->addressed_at=now;
        x->error=n->setaddr_fail?USB_X_TIMEOUT:USB_X_OK; return;
    }
    if (x->dev->addr!=n->addr || !n->addr) { x->error=USB_X_IO; return; }
    if (now-n->addressed_at<10) { address_early++; x->error=USB_X_IO; return; }
    if ((type & USB_REQ_CLASS) && (type & USB_RECIP_OTHER)) {
        struct node *child=&nodes[3+index];
        REQUIRE(index>=1 && index<=4);
        if (req==USB_REQ_GET_STATUS) { le32_wr(b,port_status(child)); x->actual=4; }
        else {
            u8 op=USB_RH_CLEAR_CONNECT;
            if (req==USB_REQ_SET_FEATURE && value==USB_PORT_POWER) op=USB_RH_SET_POWER;
            else if (req==USB_REQ_SET_FEATURE && value==USB_PORT_RESET) op=USB_RH_RESET;
            else if (value==USB_PORT_ENABLE) op=USB_RH_CLEAR_ENABLE;
            else if (value==USB_C_PORT_ENABLE) op=USB_RH_CLEAR_ENABLE_CHANGE;
            else if (value==USB_C_PORT_RESET) op=USB_RH_CLEAR_RESET;
            port_act(child,op);
        }
    } else if (req==USB_REQ_GET_DESCRIPTOR) {
        if ((value>>8)==USB_DESC_DEVICE) {
            const u8 desc[]={18,1,0,2,0,0,0,8,0x34,0x12,0x78,0x56,0,1,0,0,0,1};
            n->gets++;
            if (n->desc_fail) { n->desc_fail--; x->error=USB_X_STALL; return; }
            kmemcpy(b,desc,x->len); x->actual=x->len;
        } else if ((value>>8)==USB_DESC_CONFIG) { configuration(n,b,x->len); x->actual=x->len; }
        else if ((value>>8)==USB_DESC_HUB) {
            const u8 desc[]={9,0x29,4,0,0,1,0,0,0xff};
            kmemcpy(b,desc,sizeof(desc)); x->actual=sizeof(desc); x->error=USB_X_SHORT;
        }
    } else if (req==USB_REQ_SET_CONFIGURATION && n->config_fail) x->error=USB_X_STALL;
}
static void fake_scan(struct usb_hc *h, u32 budget)
{
    struct usb_xfer *x=h->active;
    if (pending_at_deadline && now >= 20) pending_at_deadline->state=XS_CB_PENDING;
    while (x && budget--) {
        struct usb_xfer *next=x->next;
        struct fake_xp *p=x->hcpriv;
        if (x->state==XS_ACTIVE && p->linked && p->entered<now && x->pipe.type==USB_EP_CONTROL) {
            x->actual=0; x->error=USB_X_OK; execute_control(x); fake_close(h,x); usb_xfer_notify(x);
        }
        x=next;
    }
}
static const struct usb_hc_ops ops={fake_start,fake_kill,fake_intr,fake_scan,fake_alloc,fake_free,fake_enter,fake_close,fake_frame,fake_rh};
void usb_hc_dead(struct usb_hc *h, u32 why)
{
    if(h->state==HC_DEAD)return;
    h->state=HC_DEAD;h->dead_why=why;h->ops->kill(h);
    for(u32 a=1;a<=USB_DEV_MAX;a++)if(h->devices[a])
        for(u32 i=0;i<USB_DEV_IFACE_MAX;i++) {
            struct usb_iface *f=&h->devices[a]->ifs[i];
            if(f->cls)f->cls->gone(h->devices[a],f);
        }
}
struct class_sc { struct usb_xfer x; u32 attempts; u8 started; };
static int class_match(struct usb_dev *d, struct usb_iface *f)
{
    for(u32 off=0;off+9<=d->cfg_len;off+=d->cfg[off]) {
        u8 *p=d->cfg+off;
        if(!p[0])break;
        if(p[1]==4 && p[2]==f->num && !p[3])return p[5]==3;
    }
    return 0;
}
static void class_done(struct usb_xfer *x) { (void)x;callbacks++; }
static i32 context_rc;
static void context_done(struct usb_xfer *x) { x->dev->state=DEV_DETACHED; context_rc=usb_xfer_sync(x,100); }
static u32 cancelled_done, successful_done;
static void owner_done(struct usb_ctrl_req *r)
{
    REQUIRE(usb_busy);
    if(r->error==USB_X_CANCELLED)cancelled_done++;
    else { REQUIRE(r->error==0 && r->actual==18);successful_done++; }
}
static void restart_done(struct usb_xfer *x) { callbacks++; if(callbacks==1)REQUIRE(usb_xfer_start(x)==0); }
static void class_detach(struct usb_dev *d, struct usb_iface *f)
{
    struct class_sc *s=f->sc; detached++;
    if(s && s->x.open)usb_xfer_unsetup_begin(&s->x);
    usb_ctrl_cancel_owner(d,f);
}
static int class_attach(struct usb_dev *d, struct usb_iface *f)
{
    struct class_sc *s=f->sc;
    u8 ep[]={7,5,0x81,3,8,0,10};
    if(!s) { s=usbp_mem_alloc(sizeof(*s));f->sc=s; }
    if(s->attempts++<busy_attach)return -USB_E_BUSY;
    if(fail_attach && f->num==1) { if(fail_attach==2)node_for(d)->conn=0; class_detach(d,f);return -USB_E_INVAL; }
    if(!s->started) {
        if(usb_xfer_setup(d,f,ep,64,&s->x)<0)return -USB_E_NOMEM;
        s->x.len=8;s->x.done=class_done;s->started=1;
        REQUIRE(usb_xfer_start(&s->x)==0);attached++;
    }
    return 0;
}
static void class_release(struct usb_dev *d, struct usb_iface *f)
{ (void)d;released++;usbp_mem_free(f->sc);f->sc=0; }
static void class_gone(struct usb_dev *d, struct usb_iface *f) { (void)d;(void)f;gone++; }
const struct usb_class hid_kbd_class={class_match,class_attach,class_detach,class_gone,0,class_release};
static int never_match(struct usb_dev *d, struct usb_iface *f) { (void)d;(void)f;return 0; }
const struct usb_class hid_mouse_class={never_match,0,0,0,0,0};
static void pump(u32 n) { while(n-- && !failed)usb_service(); }
static u32 devices(void) { u32 n=0;for(u32 a=1;a<=USB_DEV_MAX;a++)if(hc.devices[a])n++;return n; }
static void initialize(void)
{
    hc.ops=&ops;hc.state=HC_RUNNING;hc.nports=3;
    for(u32 i=0;i<8;i++){nodes[i].kind=1;nodes[i].power=1;}
    REQUIRE(usb_hc_register(&hc)==0);
}
static void plug(u32 p,u8 kind) { nodes[p].kind=kind;nodes[p].conn=1;nodes[p].connect_change=1;hc.needs_explore=1; }
static int scenario(int c)
{
    initialize();
    if(c==1) { /* A4/F7/F8s/F11/F8a: concurrent arrival, SET_ADDRESS first. */
        plug(0,1);plug(1,2);plug(2,3);nodes[0].setaddr_fail=1;
        pump(2400);REQUIRE(devices()==3 && attached==4);REQUIRE(!address_early && !overlap);
        REQUIRE(hc.root_child[0] && hc.root_child[2]);
    } else if(c==2) { /* A6/F9x: unlink and final DMA delay before release. */
        pump(10);u32 baseline=allocation_count-free_count;u32 dma_base=dma_count-dma_free_count;
        plug(0,1);pump(800);REQUIRE(attached==1);
        nodes[0].conn=0;nodes[0].connect_change=1;hc.needs_explore=1;
        pump(300);REQUIRE(devices()==0 && detached==1 && released==1);REQUIRE(hc.root_child[0]==0);
        REQUIRE(free_at_frame!=0);REQUIRE(allocation_count-free_count==baseline && dma_count-dma_free_count==dma_base);
    } else if(c==3) { /* F8: reset timeout must disable before next port. */
        plug(0,1);plug(1,2);nodes[0].reset_fail=1;pump(1700);
        REQUIRE(!nodes[0].enabled && devices()==1 && hc.root_child[1]);
    } else if(c==4) { /* F11a, resumable attach. */
        plug(0,3);fail_attach=1;busy_attach=5;pump(1000);
        REQUIRE(devices()==1 && attached==1 && released==1 && hc.root_child[0]->ctrl.open);
    } else if(c==5) { /* F14: hub children returned before hub itself. */
        pump(10);u32 baseline=allocation_count-free_count;
        plug(0,4);plug(4,1);plug(5,2);pump(2500);
        REQUIRE(devices()==3 && attached==2);
        nodes[0].conn=0;nodes[0].connect_change=1;hc.needs_explore=1;pump(500);
        REQUIRE(devices()==0 && detached==2 && released==2 && allocation_count-free_count==baseline);
    } else if(c==6) { /* F8l: full config and boundary validation. */
        plug(0,1);nodes[0].total=600;pump(1300);
        REQUIRE(devices()==1 && attached==1 && hc.root_child[0]->cfg_len==512);
        REQUIRE(le16_rd(hc.root_child[0]->cfg+2)==512);
    } else if(c==7) { /* F8c: two SET_CONFIGURATION failures are ignored. */
        plug(0,1);nodes[0].config_fail=1;pump(1600);
        REQUIRE(devices()==1 && attached==0 && !hc.root_child[0]->cfg);
    } else if(c==8) { /* G1: UE suppresses pending completions and release. */
        plug(0,1);pump(800);u32 f=free_count;inject=now+1;pump(20);
        REQUIRE(hc.state==HC_DEAD && gone==1 && callbacks==0 && free_count==f);
        pump(20);REQUIRE(gone==1 && free_count==f);
    } else if(c==9) { /* G1(h): synchronous wait keeps the caller's storage. */
        struct usb_xfer x;u8 ep[]={7,5,0x81,3,8,0,10};
        direct.hc=&hc;direct.ep0_mps=8;
        REQUIRE(usb_xfer_setup(&direct,0,ep,64,&x)==0);x.len=8;inject=now+5;
        REQUIRE(usb_xfer_sync(&x,100)==-USB_E_HC && x.dev==&direct && x.hcpriv);
    } else if(c==10) { /* F8r: descriptor fallback and re-enumeration. */
        plug(0,1);nodes[0].desc_fail=4;pump(1600);REQUIRE(devices()==1 && attached==1 && nodes[0].resets>=2);
    } else if(c==11) { /* F8l malformed descriptor boundary: no class. */
        plug(0,5);pump(1600);REQUIRE(devices()==1 && !hc.root_child[0]->cfg && attached==0);
    } else if(c==12) { /* G1(g): deadline failure discards another pending done. */
        struct usb_xfer a,b;u8 ep[]={7,5,0x81,3,8,0,10};direct.hc=&hc;direct.ep0_mps=8;
        REQUIRE(usb_xfer_setup(&direct,0,ep,64,&a)==0);REQUIRE(usb_xfer_setup(&direct,0,ep,64,&b)==0);
        a.done=class_done;b.done=class_done;a.len=b.len=8;REQUIRE(usb_xfer_start(&a)==0);REQUIRE(usb_xfer_start(&b)==0);
        usb_xfer_stop(&a);pending_at_deadline=&b;freeze=1;pump(25);
        REQUIRE(hc.state==HC_DEAD && callbacks==0);
    } else if(c==13) { /* F9d: each stop independently waits two frames. */
        struct usb_xfer x;u8 ep[]={7,5,0x81,3,8,0,10};direct.hc=&hc;direct.ep0_mps=8;
        REQUIRE(usb_xfer_setup(&direct,0,ep,64,&x)==0);x.len=8;x.done=class_done;
        REQUIRE(usb_xfer_start(&x)==0);usb_xfer_stop(&x);pump(1);REQUIRE(callbacks==0);pump(1);REQUIRE(callbacks==1);
        REQUIRE(usb_xfer_start(&x)==0);usb_xfer_stop(&x);pump(1);REQUIRE(callbacks==1);pump(1);REQUIRE(callbacks==2);
    } else if(c==14) { /* F6r: immediate restart uses the other DMA set. */
        struct usb_xfer x;direct.hc=&hc;direct.ep0_mps=8;direct.addr=1;nodes[0].conn=1;nodes[0].addr=1;
        now=frame=100;
        REQUIRE(usb_xfer_setup(&direct,0,0,0,&x)==0);x.done=restart_done;
        x.setup.request=USB_REQ_GET_CONFIGURATION;
        REQUIRE(usb_xfer_start(&x)==0);pump(2);REQUIRE(callbacks==2);
    } else if(c==15) { /* F8e: disappearance at second iface frees the whole dev. */
        plug(0,3);fail_attach=2;pump(1200);REQUIRE(devices()==0 && attached==1 && released==2 && detached==2);
    } else if(c==16 || c==17) { /* F8g/F8h inner retry includes RE1 failure. */
        plug(0,1);nodes[0].desc_fail=c==16?8:4;nodes[0].re_fail=c==17;pump(2100);
        REQUIRE(devices()==1 && attached==1 && nodes[0].resets==3 && nodes[0].resets_at>=800);
    } else if(c==18) { /* F8l: truncation in the middle of a descriptor. */
        plug(0,6);nodes[0].total=600;pump(1800);REQUIRE(devices()==1 && !hc.root_child[0]->cfg);
    } else if(c==19) { /* F8p: disabled port without connect change reattaches. */
        plug(0,1);pump(800);nodes[0].enabled=0;nodes[0].enable_change=1;hc.needs_explore=1;pump(1000);
        REQUIRE(attached==2 && detached==1 && devices()==1);
    } else if(c==20) { /* F4m: RHSC during port 3 never rewinds ports 1/2. */
        plug(0,1);plug(1,2);plug(2,1);pump(1100);hc.needs_explore=1;pump(1400);
        REQUIRE(devices()==3 && nodes[0].resets==1 && nodes[1].resets==1);
    } else if(c==21) { /* F8v: disconnect while old child is being freed. */
        plug(0,1);pump(800);nodes[0].connect_change=1;hc.needs_explore=1;pump(5);nodes[0].conn=0;pump(800);
        REQUIRE(devices()==0 && nodes[0].resets==1);
    } else if(c==22) { /* F8v: fresh status after reset triggers one repeat. */
        plug(0,1);swap_on_reset=1;pump(1600);REQUIRE(devices()==1 && nodes[0].resets==2 && attached==1);
    } else if(c==23) { /* F8l: retrieve 300 bytes, not only nine. */
        plug(0,1);nodes[0].total=300;pump(1000);REQUIRE(devices()==1 && hc.root_child[0]->cfg_len==300);
    } else if(c==24) { /* F8r: both outer re-enumeration attempts. */
        plug(0,1);nodes[0].desc_fail=12;pump(2600);REQUIRE(devices()==1 && nodes[0].resets==4);
    } else if(c==25 || c==26) { /* G1(a,b): fault while enumeration owns memory. */
        plug(0,1);u32 limit=1500;
        while(limit-- && hc.explore.step!=(c==25?EX_R2:EX_A4))pump(1);
        REQUIRE(limit>0);u32 f=free_count;inject=now+1;pump(30);
        REQUIRE(hc.state==HC_DEAD && free_count==f && callbacks==0);
    } else if(c==27) { /* G1(d): unsetup memory remains allocated on failure. */
        plug(0,1);pump(800);struct usb_dev *d=hc.root_child[0];struct class_sc *s=d->ifs[0].sc;
        usb_xfer_unsetup_begin(&s->x);u32 f=free_count;inject=now+1;pump(20);
        REQUIRE(hc.state==HC_DEAD && free_count==f && gone==1 && callbacks==0);
    } else if(c==28) { /* Endpoint error drops only its interface. */
        plug(0,7);pump(1000);REQUIRE(devices()==1 && hc.root_child[0]->cfg && attached==0);
    } else if(c==29) { /* Sync is rejected from a completion callback. */
        struct usb_xfer x;direct.hc=&hc;direct.ep0_mps=8;direct.addr=1;nodes[0].conn=1;nodes[0].addr=1;now=frame=100;
        REQUIRE(usb_xfer_setup(&direct,0,0,0,&x)==0);x.done=context_done;x.setup.request=USB_REQ_GET_CONFIGURATION;
        REQUIRE(usb_xfer_start(&x)==0);pump(2);REQUIRE(context_rc==-USB_E_CTX);
    } else if(c==30) { /* ep0 serializes owners, including queued cancellation. */
        struct usb_iface a={0},b={0};struct usb_dma buf;struct usb_setup s={0};
        direct.hc=&hc;direct.ep0_mps=8;direct.addr=1;nodes[0].conn=1;nodes[0].addr=1;now=frame=100;
        REQUIRE(usb_xfer_setup(&direct,0,0,0,&direct.ctrl)==0);
        REQUIRE(usb_dmamem_alloc(&hc,64,16,&buf)==0);
        s.type=USB_DIR_IN;s.request=USB_REQ_GET_DESCRIPTOR;le16_wr(s.value,USB_DESC_DEVICE<<8);le16_wr(s.length,18);
        REQUIRE(usb_ctrl_async(&direct,&a,&s,&buf,0,owner_done,0)==0);
        REQUIRE(usb_ctrl_async(&direct,&b,&s,&buf,0,owner_done,0)==0);
        REQUIRE(usb_ctrl_async(&direct,&a,&s,&buf,0,owner_done,0)==0);
        usb_ctrl_cancel_owner(&direct,&a);REQUIRE(cancelled_done==0 && !usb_class_unsetup_done(&direct,&a));
        pump(10);REQUIRE(cancelled_done==2 && successful_done==1 && usb_class_unsetup_done(&direct,&a));
    } else if(c==31) { /* A second controller keeps running after HC death. */
        struct usb_hc second={0};second.ops=&ops;second.state=HC_RUNNING;
        REQUIRE(usb_hc_register(&second)==0);inject=now+2;pump(20);u32 old=frame;pump(20);
        REQUIRE(hc.state==HC_DEAD && second.state==HC_RUNNING && frame>old);
    } else return 2;
    return failed;
}
int main(int argc,char **argv)
{
    int c=0;
    if(argc!=2)return 2;
    for(char *p=argv[1];*p;p++)c=c*10+*p-'0';
    return scenario(c);
}

__asm__(".global _start\n_start:\nmov %esp,%eax\nand $-16,%esp\nsub $8,%esp\nlea 4(%eax),%edx\npush %edx\npush (%eax)\ncall main\nmov %eax,%ebx\nmov $1,%eax\nint $0x80\n");
