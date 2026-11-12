/* USB_DESIGN §11: real HCD/core/classes; hardware-level device responses. */
#define USB_STACK
#include "usb_fake_ohci.c"
#include "drivers/usb/usb_walk.h"
static u8 address[2][7], selected[2], setup_bytes[2][128][8];
static u8 report[7][2][8], ready[7][2], sources[2];
static u32 hub_ports[4], keys, releases, mice, callbacks, irq_depth;
static u8 key_sc[32], key_down[32], key_src[32];
static i16 mouse_dx, mouse_dy;
static u32 sync_fault, sync_round, gone[4], hub_reports;
static struct usb_dev probe;
static struct usb_xfer bulk, extra, survivor;
static struct usb_dev other;
static u32 survivor_done;
static const u8 intr_ep[] = {7,5,0x81,3,8,0,1};
static const u8 bulk_ep[] = {7,5,0x81,2,8,0,1};
void *kmemset(void *p, int v, u32 n) { u8 *b=p; while(n--) *b++=(u8)v; return p; }
void *kmemcpy(void *p, const void *s, u32 n)
{ u8 *b=p; const u8 *a=s; while(n--) *b++=*a++; return p; }
static void tick(void);
u32 usbp_irq_save(void)
{
    /* sync has no delay hook: advance hardware at each foreground entry. */
    if (!irq_depth && sync_fault) {
        frame(1); frame(0);
        if (++sync_round == sync_fault) rput(0, OHCI_INTERRUPT_STATUS, OHCI_INTR_UE);
    }
    return irq_depth++;
}
void usbp_irq_restore(u32 f) { irq_depth=f; }
int usbp_input_src_alloc(int kind) { return ++sources[kind]; }
void usbp_input_key(u8 src, u8 sc, int down)
{
    if (!irq_depth || keys >= 32) { bad_dma=1; return; }
    key_src[keys]=src; key_sc[keys]=sc; key_down[keys++]=(u8)down;
}
void usbp_input_key_repeat(u8 src, u8 sc) { (void)src; (void)sc; }
void usbp_input_mouse(u8 src, i16 dx, i16 dy, u8 b)
{ (void)src; (void)b; ++mice; mouse_dx=dx; mouse_dy=dy; }
void usbp_input_release(int kind, u8 src) { (void)kind; (void)src; ++releases; }
/* Physical IDs: root kbd=1 mouse=2 composite=3 hub=4; hub children=5,6. */
static u32 device(u32 h, u32 a)
{
    if (!a) return selected[h];
    for (u32 i=1;i<=6;i++) if (address[h][i]==a) return i;
    return 0;
}
static u32 configuration(u32 id, u8 *b)
{
    u32 count=id==3 ? 2:1, n=9;
    const u8 cfg[]={9,2,0,0,1,1,0,0x80,50};
    kmemcpy(b,cfg,9); b[4]=(u8)count;
    for (u32 i=0;i<count;i++) {
        u8 iface[]={9,4,0,0,1,3,1,1,0};
        u8 ep[]={7,5,0x81,3,8,0,1};
        iface[2]=(u8)i;
        iface[7]=(id==2 || id==6 || (id==3 && i)) ? 2:1;
        if(id==4) { iface[5]=9; iface[6]=iface[7]=0; ep[4]=2; }
        ep[2]+=(u8)i;
        kmemcpy(b+n,iface,9); n+=9; kmemcpy(b+n,ep,7); n+=7;
    }
    le16_wr(b+2,(u16)n); return n;
}
static int stack_td(u32 h, u32 ef, u32 tf, u32 bus, u32 len)
{
    u32 a=ef&127u, ep=(ef>>7)&15u, id=device(h,a), actual=len;
    u8 *s=setup_bytes[h][a], response[64];
    if (!ep && (tf&(3u<<19))==0) {
        kmemcpy(s,bus_va(bus),8);
        if(s[1]==USB_REQ_SET_ADDRESS) {
            u32 addr=le16_rd(s+2);
            if (!id || addr>127) { bad_dma=1; return -1; }
            for(u32 i=1;i<=6;i++) if(i!=id && address[h][i]==addr) bad_dma=1;
            address[h][id]=(u8)addr;
        }
        return (int)len;
    }
    if(!ep && len && (tf&(3u<<19))==OHCI_TD_IN) {
        zero(response,sizeof(response));
        if(s[1]==USB_REQ_GET_DESCRIPTOR) {
            if(s[3]==USB_DESC_DEVICE) {
                const u8 d[]={18,1,0,2,0,0,0,8,1,0,1,0,0,1,0,0,0,1};
                kmemcpy(response,d,18); actual=18;
            } else if(s[3]==USB_DESC_CONFIG) actual=configuration(id,response);
            else if(s[3]==USB_DESC_HUB) {
                const u8 d[]={9,0x29,4,0,0,1,0,0,0xff};
                kmemcpy(response,d,9); actual=9;
            }
        } else if(s[1]==USB_REQ_GET_STATUS && id==4) {
            u32 p=le16_rd(s+4);
            if(p && p<=4) le32_wr(response,hub_ports[p-1]);
            actual=4;
        }
        if(actual>len) actual=len;
        kmemcpy(bus_va(bus),response,actual); return (int)actual;
    }
    if(!ep && !len && id==4) {
        u32 p=le16_rd(s+4), f=le16_rd(s+2);
        if(p && p<=4) {
            if(s[1]==USB_REQ_SET_FEATURE) {
                hub_ports[p-1]|=1u<<f;
                if(f==USB_PORT_RESET) {
                    selected[h]=(u8)(p+4); address[h][p+4]=0;
                    hub_ports[p-1]&=~(1u<<USB_PORT_RESET);
                    hub_ports[p-1]|=(1u<<USB_C_PORT_RESET)|(1u<<USB_PORT_ENABLE);
                }
            } else if(s[1]==USB_REQ_CLEAR_FEATURE) hub_ports[p-1]&=~(1u<<f);
        }
    }
    if(ep) {
        u32 which_ep=ep==2 ? 1:0;
        if(!id || !ready[id][which_ep]) return -1;
        ready[id][which_ep]=0;
        if(id==4) ++hub_reports;
        kmemcpy(bus_va(bus),report[id][which_ep],len);
    }
    return (int)len;
}
static void tick(void)
{
    /* Select the physical address-zero responder on root reset. */
    for(u32 h=0;h<2;h++) for(u32 p=1;p<=hcs[h].nports;p++)
        if(rget(h,OHCI_RH_PORT_STATUS(p)) & (1u<<USB_C_PORT_RESET)) {
            selected[h]=(u8)(h ? 4:p); address[h][selected[h]]=0;
        }
    frame(1); frame(0); usb_service();
}
static void pump(u32 n) { while(n-- && !failure) tick(); }
static void start(void)
{
    /* Reference standalone-only model controls; shared platform remains exact. */
    (void)event_no_response; (void)event_td; (void)event_cc; (void)event_len; (void)event_status_cc;
    (void)status_count; (void)data_bytes; (void)gone_count; (void)notifications;
    page_limit=NPAGES;
    for(u32 h=0;h<2;h++) {
        hcs[h].regs=regs[h]; hcs[h].ops=&ohci_ops;
        rput(h,OHCI_FM_INTERVAL,11999); rput(h,OHCI_RH_DESCRIPTOR_A,h ? 1:3);
        REQUIRE(!ohci_start(&hcs[h])); REQUIRE(!usb_hc_register(&hcs[h]));
    }
}
static u32 dev_count(u32 h)
{ u32 n=0; for(u32 a=1;a<=127;a++) if(hcs[h].devices[a]) n++; return n; }
static u32 live_mem(void) { return alloc_count-mem_frees; }
static u32 blocks(u32 h)
{
    struct ohci_dmamem *m=hcs[h].hcpriv; u32 n=0;
    struct ohci_block *tables[]={m->blocks,m->control,m->reports,m->config};
    const u32 counts[]={USB_DMA_BLOCK_MAX,USB_CTRL_BUF_MAX,USB_CLASS_BUF_MAX,USB_CFG_BUF_MAX};
    for(u32 t=0;t<4;t++) for(u32 i=0;i<counts[t];i++) n+=tables[t][i].used;
    return n;
}
static void connect_all(void)
{
    for(u32 p=1;p<=3;p++) rput(0,OHCI_RH_PORT_STATUS(p),OHCI_PORT_CCS|OHCI_PORT_CSC|OHCI_PORT_PPS);
    rput(1,OHCI_RH_PORT_STATUS(1),OHCI_PORT_CCS|OHCI_PORT_CSC|OHCI_PORT_PPS);
    hub_ports[0]=hub_ports[1]=1u|(1u<<USB_C_PORT_CONNECTION);
    pump(3200);
}
static struct usb_dev *root(u32 h,u32 p) { return hcs[h].root_child[p-1]; }
/* Preconfigured topology isolates class and fault paths from enumeration. */
static struct usb_dev *seed(u32 h, u32 id, struct usb_dev *parent, u32 port)
{
    struct usb_dev *d=usbp_mem_alloc(sizeof(*d));
    struct usb_dma capacity;
    u8 cfg[64]; u32 len=configuration(id,cfg);
    if(!d) { failure=1; return 0; }
    zero(d,sizeof(*d)); d->hc=&hcs[h]; d->port=(u8)port; d->parent=parent;
    d->addr=(u8)id; d->ep0_mps=8; d->state=DEV_CONFIGURED;
    if(usb_xfer_setup(d,0,0,64,&d->ctrl)) { failure=1; return d; }
    capacity=d->ctrl.buf; usb_dmamem_free(&hcs[h],&capacity);
    zero(&d->ctrl.buf,sizeof(d->ctrl.buf));
    d->cfg=usbp_mem_alloc(len); d->cfg_len=(u16)len; kmemcpy(d->cfg,cfg,len);
    if(usb_cfg_parse(d)) failure=1;
    hcs[h].devices[id]=d; address[h][id]=(u8)id;
    *usb_port_child(&hcs[h],parent,(u8)port)=d;
    return d;
}
static void attach(struct usb_dev *d)
{
    struct usb_walk w;
    zero(&w,sizeof(w)); w.hc=d->hc;
    d->hc->explore.child=d; d->hc->explore.step=EX_A6;
    for(u32 i=0;i<100;i++) {
        if(usb_enum_step(&w)) break;
        frame(1); frame(0);
        /* Actual service transfer paths, with explorer paused by its caller. */
        d->hc->ops->scan(d->hc,32); usb_xfer_service(d->hc);
        usb_xfer_callbacks(d->hc); usb_req_callbacks(d->hc);
    }
    d->hc->explore.child=0; d->hc->explore.step=EX_X9;
}
static void seed_all(void)
{
    struct usb_dev *hub;
    for(u32 p=1;p<=3;p++) {
        struct usb_dev *d=seed(0,p,0,p); attach(d);
        rput(0,OHCI_RH_PORT_STATUS(p),OHCI_PORT_CCS|OHCI_PORT_PES|OHCI_PORT_PPS);
    }
    hub=seed(1,4,0,1); attach(hub);
    rput(1,OHCI_RH_PORT_STATUS(1),OHCI_PORT_CCS|OHCI_PORT_PES|OHCI_PORT_PPS);
    for(u32 p=1;p<=2;p++) {
        struct usb_dev *d=seed(1,p+4,hub,p); attach(d);
        hub_ports[p-1]=1u|(1u<<USB_PORT_ENABLE)|(1u<<USB_PORT_POWER);
    }
    usb_hub_sc(hub)->explored=1;
}
static void topology(u32 natural)
{
    struct usb_dev *d; struct usb_hub_sc *hub;
    u32 base, bytes, rel;
    start(); pump(80); base=live_mem(); bytes=blocks(0)+blocks(1);
    if(natural) connect_all(); else seed_all();
    if(natural) {
        struct usb_dev *pending=root(0,1);
        struct ohci_xp *xp=pending ? pending->ctrl.hcpriv:0;
        say("natural enumeration: devices="); number(dev_count(0)+dev_count(1));
        say(" ep0 TDs/set="); number(xp ? (u32)(xp->sets[0].last-xp->sets[0].first+1):0);
        say(" (DATA needs 3)\n");
    }
    REQUIRE(!bad_dma && dev_count(0)==3 && dev_count(1)==3);
    d=root(0,3); REQUIRE(d && d->ifs[0].attached && d->ifs[1].attached);
    REQUIRE(d->ifs[0].cls==&hid_kbd_class && d->ifs[1].cls==&hid_mouse_class);
    REQUIRE(root(0,1)->ifs[0].attached && root(0,2)->ifs[0].attached);
    hub=usb_hub_sc(root(1,1)); REQUIRE(hub && hub->nports==4 && hub->child[0] && hub->child[1]);
    REQUIRE(hub->intr.state==XS_ACTIVE);
    hcs[1].needs_explore=0; ready[4][0]=1; frame(1);
    ohci_scan(&hcs[1],32); usb_xfer_callbacks(&hcs[1]);
    REQUIRE(hub_reports==1 && hub->intr.state==XS_ACTIVE && hcs[1].needs_explore);
    /* A5: both shifts -> one shift, then release + A. Modifier goes first. */
    report[1][0][0]=0x22; report[1][0][2]=4; ready[1][0]=1; pump(2);
    REQUIRE(keys==2 && key_sc[0]==0x70 && key_down[0] && key_sc[1]==0x1d && key_down[1]);
    report[1][0][0]=0x20; ready[1][0]=1; pump(2); REQUIRE(keys==2);
    report[1][0][0]=0; report[1][0][2]=5; ready[1][0]=1; pump(2);
    REQUIRE(keys==5 && key_sc[2]==0x70 && !key_down[2] && key_src[0]==key_src[2]);
    report[3][0][2]=4; ready[3][0]=1;
    report[3][1][1]=0xfe; report[3][1][2]=3; ready[3][1]=1; pump(2);
    REQUIRE(keys==6 && mice==1 && mouse_dx==-2 && mouse_dy==3);
    rel=releases;
    for(u32 p=1;p<=3;p++) rput(0,OHCI_RH_PORT_STATUS(p),OHCI_PORT_CSC);
    rput(1,OHCI_RH_PORT_STATUS(1),OHCI_PORT_CSC); pump(1600);
    REQUIRE(!dev_count(0) && !dev_count(1) && releases==rel+6);
    REQUIRE(live_mem()==base && blocks(0)+blocks(1)==bytes && !bad_dma);
    for(u32 h=0;h<2;h++) for(u32 p=0;p<3;p++) REQUIRE(!hcs[h].root_child[p]);
}
static void done(struct usb_xfer *x) { (void)x; ++callbacks; }
static void synthetic(void)
{
    probe.hc=&hcs[0]; probe.addr=7; probe.ep0_mps=8; probe.state=DEV_CONFIGURED;
    REQUIRE(!usb_xfer_setup(&probe,0,intr_ep,8,&bulk)); bulk.len=8; bulk.done=done;
}
static void restart_done(struct usb_xfer *x)
{ ++callbacks; if(usb_xfer_start(x)) bad_dma=1; }
static void delays(void)
{
    struct ohci_xp *p; u32 oldset;
    start(); pump(80); synthetic();
    REQUIRE(!usb_xfer_start(&bulk)); p=bulk.hcpriv; oldset=bulk.curr_set;
    /* Hardware completion, then retain the old set during callback restart. */
    address[0][1]=7; ready[1][0]=1; frame(0);
    retain(p->sets[oldset].ed->bus); retain(p->sets[oldset].first->bus); hold_active=1; bulk.done=restart_done;
    usb_service(); REQUIRE(callbacks==1 && bulk.curr_set!=oldset && !bad_dma);
    hold_active=0; zero(retained,sizeof(retained)); bulk.done=done;
    for(u32 i=0;i<2;i++) {
        frame_begin(0); usb_xfer_stop(&bulk);
        REQUIRE(bulk.state==XS_DMA_DELAY && bulk.did_dma_delay);
        usb_service(); REQUIRE(callbacks==i+1);
        frame_end(0); usb_service(); REQUIRE(callbacks==i+1);
        tick(); REQUIRE(callbacks==i+2);
        if(!i) REQUIRE(!usb_xfer_start(&bulk));
    }
    usb_xfer_unsetup_begin(&bulk); pump(4); REQUIRE(usb_xfer_unsetup_done(&bulk) && !bad_dma);
}
static void gone_probe(struct usb_dev *d,struct usb_iface *f)
{ (void)d; if(f->attached) bad_dma=1; ++gone[f->num]; }
static const struct usb_class watcher={.gone=gone_probe};
static void alive_done(struct usb_xfer *x) { (void)x; ++survivor_done; }
static void fault(u32 mode)
{
    u32 frees, rel, w, rd, q, state; struct usb_dev *d;
    start(); pump(80);
    other.hc=&hcs[1]; other.addr=9; other.state=DEV_CONFIGURED;
    REQUIRE(!usb_xfer_setup(&other,0,intr_ep,8,&survivor));
    survivor.len=8; survivor.done=alive_done; address[1][2]=9;
    REQUIRE(!usb_xfer_start(&survivor));
    if(mode==0 || mode==1) {
        rput(0,OHCI_RH_PORT_STATUS(1),OHCI_PORT_CCS|OHCI_PORT_CSC|OHCI_PORT_PPS);
        for(u32 i=0;i<1400;i++) {
            tick(); d=root(0,1);
            if((mode==0 && hcs[0].explore.step==EX_R2) ||
               (mode==1 && hcs[0].explore.step==EX_A4 && d->ctrl.state==XS_ACTIVE) ||
               (mode==8 && d && d->ifs[0].sc && !d->ifs[0].attached)) break;
        }
        REQUIRE(mode==0 ? hcs[0].explore.step==EX_R2 :
                mode==1 ? hcs[0].explore.step==EX_A4 : root(0,1)->ifs[0].sc!=0);
    }
    if(mode==8) {
        struct usb_dev *pending=seed(0,1,0,1);
        struct usb_walk w;
        zero(&w,sizeof(w)); w.hc=&hcs[0];
        hcs[0].explore.child=pending; hcs[0].explore.step=EX_A6;
        REQUIRE(!usb_enum_step(&w) && pending->ifs[0].sc && !pending->ifs[0].attached);
    }
    synthetic();
    for(u32 i=0;i<4;i++) { probe.ifs[i].cls=&watcher; probe.ifs[i].num=(u8)i; probe.ifs[i].attached=1; }
    hcs[0].devices[7]=&probe;
    REQUIRE(!usb_xfer_setup(&probe,0,bulk_ep,8,&extra)); extra.len=8; extra.done=done;
    REQUIRE(!usb_xfer_start(&extra));
    if(mode!=4 && mode!=7) REQUIRE(!usb_xfer_start(&bulk));
    if(mode==3) { usb_xfer_unsetup_begin(&bulk); pump(3); REQUIRE(bulk.unsetup==UNSETUP_DELAY); callbacks=0; }
    if(mode==5 || mode==6) usb_xfer_stop(&extra);
    frees=mem_frees; rel=releases; q=quarantines;
    if(mode==4 || mode==7) {
        sync_fault=mode==4 ? 2:4; sync_round=0;
        REQUIRE(usb_xfer_sync(&bulk,100)==-USB_E_HC); sync_fault=0;
    } else if(mode==5 || mode==6) {
        if(mode==6) {
            address[0][1]=7; ready[1][0]=1; frame(0); /* scan reserves done */
        }
        no_clock=1; now_us+=21000; frame(1); usb_service();
    } else { frame_begin(0); rput(0,OHCI_INTERRUPT_STATUS,OHCI_INTR_UE); usb_service(); frame_end(0); }
    REQUIRE(hcs[0].state==HC_DEAD && quarantines==q+2 && !callbacks && mem_frees==frees);
    REQUIRE(extra.hcpriv && bulk.hcpriv && probe.state==DEV_DETACHED);
    for(u32 i=0;i<4;i++) REQUIRE(gone[i]==1 && !probe.ifs[i].attached);
    REQUIRE(releases==rel+(mode==8 ? 1u:0u));
    w=controller_writes[0]; rd=controller_reads[0]; state=mem_frees;
    usb_hc_dead(&hcs[0],USB_WHY_UE); usb_service();
    REQUIRE(!usb_busy && mem_frees==state && controller_writes[0]==w && controller_reads[0]==rd);
    /* Other HC is polled; direct dead-HC operations cause no MMIO. */
    w=writes; rd=actual_reads; ohci_intr_collect(&hcs[0]); ohci_scan(&hcs[0],32);
    REQUIRE(usb_xfer_sync(&bulk,1)==-USB_E_HC);
    REQUIRE(writes==w && actual_reads==rd && !callbacks && !bad_dma);
    REQUIRE(hcs[1].state==HC_RUNNING); state=ohci_frame(&hcs[1]); frame(1);
    REQUIRE(ohci_frame(&hcs[1])==(u16)(state+1));
    ready[2][0]=1; frame(1); usb_service();
    REQUIRE(survivor_done==1 && survivor.state==XS_IDLE && !bad_dma);
}
int main(int argc,char **argv)
{
    u32 mode=0;
    if(argc>1) for(char *s=argv[1];*s;s++) mode=mode*10u+(u32)(*s-'0');
    if(mode==0 || mode==11) topology(mode==11); else if(mode==1) delays(); else fault(mode-2);
    if(!failure) say("usb stack OK\n");
    return failure;
}
#ifdef __i386__
__asm__(".global _start\n_start:\nmov %esp,%eax\nand $-16,%esp\nsub $8,%esp\nlea 4(%eax),%edx\npush %edx\npush (%eax)\ncall main\nmov %eax,%ebx\nmov $1,%eax\nint $0x80\n");

#endif
