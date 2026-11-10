/* OF PCI cells: docs/ppc/open-firmware.md, PCI section. */
#include "lan_boot.h"
#include "ofw.h"
#include "kstring.h"

static u8 prop[512];
static int error;
static unsigned visited, found;
static int candidate;
static u32 be32(const u8 *p)
{
    return ((u32)p[0]<<24)|((u32)p[1]<<16)|((u32)p[2]<<8)|p[3];
}
static int get(int node, const char *name)
{
    cell a[3]={(cell)node,ppc_ptr_cell(name),0};
    if (ofw_call("getproplen",2,1,a)) { error=LAN_EIO; return -1; }
    int n=(int)a[2];
    if (n < -1 || n > (int)sizeof(prop)) { error=LAN_ERANGE; return -1; }
    if (n>=0 && ofw_getprop(node,name,prop,sizeof(prop))!=n) {
        error=LAN_EIO; return -1;
    }
    return n;
}
static int compat(int node, const char *value)
{
    int n=get(node,"compatible"), hit=0;
    unsigned len=kstrlen(value);
    for (int i=0;i<n;) {
        int end=i;
        while (end<n && prop[end]) end++;
        if (end==n) { error=LAN_EINVAL; return 0; }
        if ((unsigned)(end-i)==len && !kstrncmp((char *)prop+i,value,len)) hit=1;
        i=end+1;
    }
    return hit;
}
static u32 scalar(int node, const char *name, int *present)
{
    int n=get(node,name);
    *present=n>=0;
    if (n==-1) return 0;
    if (n!=4) { error=LAN_EINVAL; return 0; }
    return be32(prop);
}
static void walk(int node, unsigned depth)
{
    while (node && node!=-1 && !error) {
        if (++visited>1024 || depth>=8) { error=LAN_ERANGE; return; }
        int yes=compat(node,"gmac"), a,b;
        u32 vendor=scalar(node,"vendor-id",&a), device=scalar(node,"device-id",&b);
        if (yes || (a && b && vendor==0x106b && device==0x21)) {
            candidate=node; found++;
        }
        walk(ofw_child(node),depth+1);
        node=ofw_peer(node);
    }
}
static int hex(int c)
{
    if (c>='0' && c<='9') return c-'0';
    if (c>='a' && c<='f') return c-'a'+10;
    if (c>='A' && c<='F') return c-'A'+10;
    return -1;
}
static void bootargs(struct pm_gmac_boot *b)
{
    int chosen=ofw_finddevice("/chosen");
    if (!chosen || chosen==-1) return;
    int n=get(chosen,"bootargs");
    for (int i=0;i<n;) {
        while (i<n && (prop[i]==' ' || prop[i]=='\t' || !prop[i])) i++;
        int start=i;
        while (i<n && prop[i] && prop[i]!=' ' && prop[i]!='\t') i++;
        int len=i-start, off=0, count=0, peer=0;
        if (len==46 && !kstrncmp((char *)prop+start,"os32-lan-boot=",14)) { off=14; count=16; }
        if (len==31 && !kstrncmp((char *)prop+start,"os32-lan-peer=",14)) { off=14; count=6; peer=1; }
        u8 val[16]; int valid=count!=0;
        for (int j=0;j<count;j++) {
            int p=start+off+j*(peer?3:2), h=hex(prop[p]), l=hex(prop[p+1]);
            if (h<0 || l<0 || (peer && j<5 && prop[p+2]!=':')) valid=0;
            val[j]=(u8)(((unsigned)h<<4)|(unsigned)l);
        }
        if (valid && peer) { for (int j=0;j<6;j++) b->peer_mac[j]=val[j]; b->override_flags|=PM_GMAC_PEER_OVERRIDE; }
        if (valid && !peer) { for (int j=0;j<16;j++) b->boot_id[j]=val[j]; b->override_flags|=PM_GMAC_BOOT_ID_OVERRIDE; }
    }
}
void pm_gmac_boot_id(struct pm_gmac_boot *b, u64 tb, u32 pvr)
{
    if (b->override_flags & PM_GMAC_BOOT_ID_OVERRIDE) return;
    for (unsigned i=0;i<8;i++) b->boot_id[i]=(u8)(tb>>(56-8*i));
    for (unsigned i=0;i<6;i++) b->boot_id[8+i]=b->mac[i];
    b->boot_id[14]=(u8)(pvr>>8); b->boot_id[15]=(u8)pvr;
}
int pm_gmac_capture(struct pm_gmac_boot *out)
{
    if (!out) return LAN_EINVAL;
    struct pm_gmac_boot b={0};
    error=0; visited=found=0; candidate=0;
    int root=ofw_finddevice("/");
    if (!root || root==-1) { error=LAN_ENODEV; goto fail; }
    walk(root,0);
    if (error) goto fail;
    if (!found) { error=LAN_ENODEV; goto fail; }
    if (found!=1) { error=LAN_EUNSUPPORTED; goto fail; }
    b.node=(u32)candidate;
    int bridge=ofw_parent(candidate), parent=ofw_parent(bridge), present;
    if (!bridge || bridge==-1 || !parent || parent==-1 || !compat(bridge,"uni-north")) {
        if (!error) error=LAN_EUNSUPPORTED;
        goto fail;
    }
    b.root_node=(u32)bridge;
    if (scalar(bridge,"#address-cells",&present)!=3 || !present ||
        scalar(bridge,"#size-cells",&present)!=2 || !present) { error=LAN_EINVAL; goto fail; }
    u32 ac=scalar(parent,"#address-cells",&present); if (!present) ac=2;
    u32 sc=scalar(parent,"#size-cells",&present); if (!present) sc=1;
    if (error || ac<1 || ac>2 || sc<1 || sc>2) { error=LAN_EINVAL; goto fail; }
    int n=get(bridge,"reg");
    if (n<(int)((ac+sc)*4) || n%(int)((ac+sc)*4)) { error=LAN_EINVAL; goto fail; }
    if (ac==2 && be32(prop)) { error=LAN_ERANGE; goto fail; }
    b.root_pa=be32(prop+(ac-1)*4);
    n=get(bridge,"bus-range");
    if (n!=8) { error=LAN_EINVAL; goto fail; }
    b.bus_first=be32(prop); b.bus_last=be32(prop+4);
    if (b.bus_first>b.bus_last || b.bus_last>255) { error=LAN_ERANGE; goto fail; }
    n=get(candidate,"reg");
    if (n<20 || n%20) { error=LAN_EINVAL; goto fail; }
    b.bdf=(be32(prop)>>8)&0xffff;
    int a,d; u32 vendor=scalar(candidate,"vendor-id",&a), device=scalar(candidate,"device-id",&d);
    if (error || !a || !d || vendor>0xffff || device>0xffff) { error=LAN_EINVAL; goto fail; }
    if (vendor!=0x106b || device!=0x21) { error=LAN_EUNSUPPORTED; goto fail; }
    b.vendor_device=(device<<16)|vendor;
    n=get(candidate,"assigned-addresses");
    if (n<=0 || n%20) { error=LAN_EINVAL; goto fail; }
    int bars=0;
    for (int i=0;i<n;i+=20) {
        u32 hi=be32(prop+i);
        if ((hi&255)==0x10) {
            if ((hi&0x03000000)!=0x02000000 || be32(prop+i+4) || be32(prop+i+12)) { error=LAN_ERANGE; goto fail; }
            bars++; b.bar_reg=0x10; b.bar_bus=be32(prop+i+8); b.bar_bytes=be32(prop+i+16);
        }
    }
    if (bars!=1 || !b.bar_bytes || b.bar_bytes>0xffffffffu-b.bar_bus) { error=LAN_ERANGE; goto fail; }
    n=get(bridge,"ranges");
    unsigned stride=(3+ac+2)*4;
    if (n<=0 || (unsigned)n%stride) { error=LAN_EINVAL; goto fail; }
    b.nwin=(unsigned)n/stride;
    if (b.nwin>8) { error=LAN_ERANGE; goto fail; }
    int matches=0;
    for (unsigned i=0;i<b.nwin;i++) {
        u8 *p=prop+i*stride;
        struct pm_pci_window *w=&b.win[i];
        w->ss=(be32(p)>>24)&3; w->pci_base=be32(p+8);
        w->cpu_base=be32(p+(3+ac-1)*4); w->bytes=be32(p+(3+ac+1)*4);
        if (be32(p+4) || (ac==2 && be32(p+12)) || be32(p+(3+ac)*4) ||
            !w->bytes || w->bytes>0xffffffffu-w->pci_base || w->bytes>0xffffffffu-w->cpu_base) { error=LAN_ERANGE; goto fail; }
        for (unsigned j=0;j<i;j++) {
            struct pm_pci_window *v=&b.win[j];
            if ((v->ss==w->ss && w->pci_base<v->pci_base+v->bytes && v->pci_base<w->pci_base+w->bytes) ||
                (w->cpu_base<v->cpu_base+v->bytes && v->cpu_base<w->cpu_base+w->bytes)) { error=LAN_ERANGE; goto fail; }
        }
        if (w->ss==2 && b.bar_bus>=w->pci_base && b.bar_bus-w->pci_base<=w->bytes &&
            b.bar_bytes<=w->bytes-(b.bar_bus-w->pci_base)) {
            b.bar_pa=w->cpu_base+(b.bar_bus-w->pci_base); matches++;
        }
    }
    if (matches!=1) { error=LAN_ERANGE; goto fail; }
    n=get(candidate,"local-mac-address");
    if (n!=6) { error=LAN_EINVAL; goto fail; }
    unsigned nonzero=0;
    for (int i=0;i<6;i++) { b.mac[i]=prop[i]; nonzero|=prop[i]; b.peer_mac[i]=0xff; }
    if (!nonzero || (b.mac[0]&1)) { error=LAN_EINVAL; goto fail; }
    /* NetBSD macppc/pci/uninorth.c:179-180 supplies pci_bus_dma_tag. */
    b.dma_bytes=0x1000000; b.profile=1;
    bootargs(&b);
    if (error) goto fail;
    *out=b; return LAN_OK;
fail:
    *out=(struct pm_gmac_boot){0}; return error;
}
