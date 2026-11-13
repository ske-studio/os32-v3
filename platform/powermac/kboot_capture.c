/* OF memory model: OpenBSD macppc/ofw_machdep.c:136-153;
 * NetBSD powerpc/ofw_machdep.c:201,264 (separate reg/available).
 * Cell decoding and MacRISC exception follow mmu_boot.c:158,245-249.
 * OF-PPC binding 8.2.1: use the existing client-interface wrapper. */
#include "kboot_info.h"
#include "kstring.h"
#if KBOOT_SH
#include "usb_boot.h"
#endif
static struct pm_kboot_info info;
static unsigned char property[PM_KBOOT_PROP_BYTES];
static int output;
static unsigned visited;
static void fail(const char *why)
{
    ofw_write(output,why,kstrlen(why));
    ppc_stop();
}
static u32 be32(const unsigned char *p)
{
    return ((u32)p[0]<<24)|((u32)p[1]<<16)|((u32)p[2]<<8)|p[3];
}
/* getproplen first: getprop alone may silently truncate at the supplied cap. */
static int read_property(int node, const char *name)
{
    cell args[3]={ (cell)node, ppc_ptr_cell(name), 0 };
    if (ofw_call("getproplen",2,1,args)) fail("FAIL:KBOOT:PROPLEN\r\n");
    int n=(int)args[2];
    if (n==-1) return -1;
    if (n<0 || n>(int)sizeof(property)) fail("FAIL:KBOOT:PROPERTY-LIMIT\r\n");
    if (ofw_getprop(node,name,property,sizeof(property))!=n)
        fail("FAIL:KBOOT:PROPERTY-LENGTH\r\n");
    return n;
}
static unsigned cells(int node, const char *name, unsigned fallback,
                      unsigned minimum, unsigned maximum)
{
    int n=read_property(node,name);
    if (n==-1) return fallback;
    if (n!=4 || be32(property)<minimum || be32(property)>maximum)
        fail("FAIL:KBOOT:CELLS\r\n");
    return be32(property);
}
static int named(int node, const char *key, const char *value)
{
    int n=read_property(node,key);
    unsigned len=kstrlen(value);
    return (n==(int)len || n==(int)len+1) &&
        !kstrncmp((const char *)property,value,len) &&
        (n==(int)len || property[len]==0);
}
/* OF compatible is a NUL-separated string list, not a single name.
 * MPIC identities: NetBSD macppc/macppc/interrupts.c:58-88. */
static int compatible(int node, const char *value)
{
    int n=read_property(node,"compatible"), found=0;
    unsigned len=kstrlen(value);
    for (int p=0; p<n;) {
        int end=p;
        while (end<n && property[end]) end++;
        if (end==n) fail("FAIL:KBOOT:COMPATIBLE\r\n");
        if ((unsigned)(end-p)==len &&
            !kstrncmp((const char *)property+p,value,len)) found=1;
        p=end+1;
    }
    return found;
}
static void compatible_copy(int node, struct pm_kboot_property *dest)
{
    /* Validate all list entries before retaining the bounded BSS copy. */
    (void)compatible(node,"");
    int n=read_property(node,"compatible");
    dest->present=n!=-1;
    dest->bytes=n==-1?0:(unsigned)n;
    for (int i=0; i<n; i++) dest->data[i]=property[i];
}
static int macrisc(int root)
{
    int n=read_property(root,"compatible");
    for (int p=0; p<n;) {
        int end=p;
        while (end<n && property[end]) end++;
        if (end==n) fail("FAIL:KBOOT:COMPATIBLE\r\n");
        int len=end-p;
        if ((len==7 || (len==8 && property[p+7]>='2' && property[p+7]<='4')) &&
            !kstrncmp((const char *)property+p,"MacRISC",7)) return 1;
        p=end+1;
    }
    return 0;
}
static void memory(int node, const char *name, unsigned ac, unsigned sc,
                   struct pm_regions *out)
{
    int n=read_property(node,name);
    unsigned stride=(ac+sc)*PM_CELL_BYTES;
    if (n<=0 || (unsigned)n%stride) fail("FAIL:KBOOT:MEMORY-LENGTH\r\n");
    unsigned count=(unsigned)n/stride;
    if (count>PM_REGION_COUNT) fail("FAIL:KBOOT:REGION-LIMIT\r\n");
    out->count=0;
    for (unsigned pos=0; pos<(unsigned)n; pos+=stride) {
        const unsigned char *p=property+pos;
        if ((ac==2 && be32(p)) || (sc==2 && be32(p+ac*4)))
            fail("FAIL:KBOOT:MEMORY-32BIT\r\n");
        u32 start=be32(p+(ac-1)*4), size=be32(p+(ac+sc-1)*4);
        /* Apple OF lists empty DIMM banks with size 0 (G4 PowerMac3,5); skip them like mmu_boot. */
        if (!size) continue;
        if (size>0xffffffffUL-start) fail("FAIL:KBOOT:MEMORY-OVERFLOW\r\n");
        unsigned i=out->count++;
        while (i && out->v[i-1].start>start) {
            out->v[i]=out->v[i-1]; i--;
        }
        out->v[i]=(struct pm_region){start,size};
    }
    if (!out->count) fail("FAIL:KBOOT:MEMORY-EMPTY\r\n");
    unsigned kept=0;
    for (unsigned i=0; i<out->count; i++) {
        struct pm_region r=out->v[i];
        if (kept && r.start<=out->v[kept-1].start+out->v[kept-1].size) {
            struct pm_region *last=&out->v[kept-1];
            if (r.start+r.size>last->start+last->size) last->size=r.start+r.size-last->start;
        } else out->v[kept++]=r;
    }
    out->count=kept;
}
static void raw(int node, const char *name, struct pm_kboot_property *dest)
{
    int n=read_property(node,name);
    dest->present=n!=-1;
    if (n==-1) { dest->bytes=0; return; }
    if (n%4) fail("FAIL:KBOOT:RAW-LENGTH\r\n");
    dest->bytes=(unsigned)n;
    for (int i=0; i<n; i++) dest->data[i]=property[i];
}
static void device(int node, struct pm_kboot_device *dest)
{
    dest->count=0;
    dest->machine=PM_KBOOT_MACHINE_UNKNOWN;
    while (node!=0 && node!=-1) {
        if (dest->count>=PM_KBOOT_ANCESTORS) fail("FAIL:KBOOT:ANCESTOR-LIMIT\r\n");
        struct pm_kboot_node *v=&dest->v[dest->count++];
        v->node=node; v->parent=ofw_parent(node);
        int n=ofw_package_to_path(node,v->path,sizeof(v->path));
        if (n<=0 || n>=(int)sizeof(v->path)) fail("FAIL:KBOOT:PATH\r\n");
        v->path[n]=0;
        v->ac=cells(node,"#address-cells",2,0,3);
        v->sc=cells(node,"#size-cells",1,0,2);
        raw(node,"reg",&v->reg);
        raw(node,"assigned-addresses",&v->assigned);
        raw(node,"ranges",&v->ranges);
        compatible_copy(node,&v->compatible);
        if (compatible(node,"Keylargo") || compatible(node,"AAPL,Keylargo") ||
            compatible(node,"K2-Keylargo")) dest->machine=PM_KBOOT_MACHINE_KEYLARGO;
        node=v->parent;
    }
}
/* Bounded preorder walk: also bounds malformed peer/child cycles. */
static void tree(int node, unsigned depth)
{
    while (node!=0 && node!=-1) {
        if (++visited>PM_KBOOT_TREE_LIMIT || depth>=PM_KBOOT_ANCESTORS)
            fail("FAIL:KBOOT:TREE-LIMIT\r\n");
        if (!info.tb_hz && ofw_parent(node)==ofw_finddevice("/cpus") &&
            named(node,"device_type","cpu")) {
            int n=read_property(node,"timebase-frequency");
            if (n!=-1) {
                if (n!=4 || !be32(property)) fail("FAIL:KBOOT:TIMEBASE\r\n");
                info.tb_hz=be32(property);
            }
        }
        if (!info.escc.count && named(node,"name","ch-a") &&
            named(ofw_parent(node),"name","escc")) device(node,&info.escc);
        if (!info.mpic.count && (named(node,"device_type","open-pic") ||
            named(node,"name","open-pic") ||
            compatible(node,"chrp,open-pic") || compatible(node,"open-pic") ||
            compatible(node,"openpic"))) device(node,&info.mpic);
        tree(ofw_child(node),depth+1);
        node=ofw_peer(node);
    }
}
const struct pm_kboot_info *pm_kboot_capture(ppc_ci ci)
{
    struct ofw_env env;
    info=(struct pm_kboot_info){0};
    visited=0; output=-1;
    ofw_init(ci);
    if (ofw_setup(&env)) fail("FAIL:KBOOT:SETUP\r\n");
    output=env.out;
    int root=ofw_finddevice(PM_ROOT), mem=ofw_finddevice(PM_MEMORY);
    if (root==0 || root==-1 || mem==0 || mem==-1) fail("FAIL:KBOOT:MEMORY\r\n");
    int parent=ofw_parent(mem);
    if (parent==0 || parent==-1) fail("FAIL:KBOOT:MEMORY-PARENT\r\n");
    unsigned ac=cells(parent,"#address-cells",2,1,2), sc=cells(parent,"#size-cells",1,1,2);
    memory(mem,"reg",ac,sc,&info.ram);
    int mac=macrisc(root);
    memory(mem,"available",mac?1:ac,mac?1:sc,&info.available);
    tree(ofw_child(root),0);
    if (!info.tb_hz || !info.escc.count || !info.mpic.count)
        fail("FAIL:KBOOT:REQUIRED-NODE\r\n");
    info.gmac_rc = pm_gmac_capture(&info.gmac);
#if KBOOT_SH
    pm_usb_capture();
#endif
    return &info;
}

/* Reuse the exact B1 decoder for the last firmware-owned available snapshot.
 * No claims/maps: image is already PT_LOAD-owned, other storage is untouched. */
void pm_kboot_refresh_available(struct pm_kboot_info *dest)
{
    int root=ofw_finddevice(PM_ROOT), mem=ofw_finddevice(PM_MEMORY);
    if (root==0 || root==-1 || mem==0 || mem==-1)
        fail("FAIL:KBOOT:MEMORY\r\n");
    int parent=ofw_parent(mem);
    if (parent==0 || parent==-1) fail("FAIL:KBOOT:MEMORY-PARENT\r\n");
    unsigned ac=cells(parent,"#address-cells",2,1,2);
    unsigned sc=cells(parent,"#size-cells",1,1,2);
    int mac=macrisc(root);
    memory(mem,"available",mac?1:ac,mac?1:sc,&dest->available);
}
