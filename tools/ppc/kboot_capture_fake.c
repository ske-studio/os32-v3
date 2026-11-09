/* Actual kboot_capture.c with an ILP32, libc-free firmware tree fake. */
#include "kboot_info.h"
#include "kstring.h"
static unsigned scenario, ac, sc, mac, expected_stop, wrote;
static unsigned char reg[PM_KBOOT_PROP_BYTES], avail[PM_KBOOT_PROP_BYTES];
static unsigned rn, an;
static void quit(unsigned code) __attribute__((noreturn));
static void quit(unsigned code)
{
    __asm__ volatile("int $0x80" : : "a"(1), "b"(code) : "memory");
    __builtin_unreachable();
}
#define REQUIRE(x) do { if (!(x)) quit(__LINE__%200+20); } while (0)
static void put(unsigned char *p, unsigned v)
{
    p[0]=v>>24; p[1]=v>>16; p[2]=v>>8; p[3]=v;
}
static void tuple(unsigned char *p, unsigned a, unsigned s, unsigned start, unsigned size)
{
    for (unsigned i=0; i<(a+s)*4; i++) p[i]=0;
    put(p+(a-1)*4,start); put(p+(a+s-1)*4,size);
}
static int prop(int node, const char *name, unsigned char *p)
{
    unsigned n=0;
    if (node==-17) node=1;
    const char *str=0;
    if ((node==1 || node==5) && !kstrcmp(name,"ranges")) return 0;
    if ((node==4 || node==9 || node==5) && !kstrcmp(name,"#address-cells")) {
        put(p,node==4?3:1); return 4;
    }
    if ((node==4 || node==9 || node==5) && !kstrcmp(name,"#size-cells")) {
        put(p,node==4?2:1); return 4;
    }
    if (node==1 && !kstrcmp(name,"#address-cells")) { put(p,ac); return 4; }
    if (node==1 && !kstrcmp(name,"#size-cells")) { put(p,sc); return 4; }
    if (node==1 && !kstrcmp(name,"compatible") && mac) str="MacRISC2";
    if (node==2 && !kstrcmp(name,"reg")) { for (n=0;n<rn;n++) p[n]=reg[n]; return rn; }
    if (node==2 && !kstrcmp(name,"available")) { for (n=0;n<an;n++) p[n]=avail[n]; return an; }
    if ((node==3 || node==10) && !kstrcmp(name,"device_type")) str="cpu";
    if ((node==10 || (node==3 && scenario!=11 && scenario!=23)) && !kstrcmp(name,"timebase-frequency")) {
        put(p,scenario==12?0:25000000); return scenario==13?3:4;
    }
    if (node==5 && !kstrcmp(name,"name")) str="escc";
    if (node==6 && !kstrcmp(name,"name")) str="ch-a";
    if (node==7 && scenario!=19 && scenario!=20 && !kstrcmp(name,"device_type")) str="open-pic";
    if (node==9 && !kstrcmp(name,"compatible")) str="Keylargo";
    if (node==5 && !kstrcmp(name,"compatible")) str="escc";
    if (node==7 && !kstrcmp(name,"compatible")) {
        const char *list=scenario==20?"other\0openpic":"other\0chrp,open-pic";
        unsigned bytes=scenario==20?14:20;
        for (n=0;n<bytes;n++) p[n]=list[n];
        return bytes;
    }
    /* Synthetic binding-valid fixture, not a claim about mac99's OF tree.
     * OF PCI binding 2.1 sections 2.2.1, 3.1: PCI addresses are 3 cells,
     * size is 2. NetBSD macppc/macppc/interrupts.c:91-103 and
     * dev/zs.c:239-255: macio assigned base plus child reg. */
    const unsigned *v=0;
    unsigned count=0;
    if (node==4 && !kstrcmp(name,"reg")) {
        tuple(p,ac,sc,0x80000000,0x1000); return (ac+sc)*4;
    }
    if (node==4 && !kstrcmp(name,"ranges")) {
        for (n=0;n<(3+ac+2)*4;n++) p[n]=0;
        put(p,0x02000000); put(p+8,0x80000000);
        put(p+(3+ac-1)*4,0x80000000);
        put(p+(3+ac+1)*4,0x10000000);
        return (3+ac+2)*4;
    }
    static const unsigned macio_reg[]={0x00008000,0,0,0,0};
    static const unsigned assigned[]={0x82008010,0,0x80000000,0,0x80000};
    static const unsigned ranges[]={0,0x02000000,0,0x80000000,0x80000};
    static const unsigned escc_reg[]={0x13000,0x100};
    static const unsigned channel_reg[]={0x13020,1,0x13030,1,0x13040,1};
    static const unsigned mpic_reg[]={0x40000,0x40000};
    if (!kstrcmp(name,"reg")) {
        if (node==9) { v=macio_reg; count=5; }
        if (node==5) { v=escc_reg; count=2; }
        if (node==6) { v=channel_reg; count=6; }
        if (node==7) { v=mpic_reg; count=2; }
    }
    if (node==9 && !kstrcmp(name,"assigned-addresses")) { v=assigned; count=5; }
    if (!kstrcmp(name,"ranges")) {
        if (node==9) { v=ranges; count=5; }
    }
    if (v) {
        for (n=0;n<count;n++) put(p+4*n,v[n]);
        return scenario==14?(int)(count*4-1):(int)(count*4);
    }
    if (!str) return -1;
    do { p[n]=(unsigned char)str[n]; } while (str[n++]);
    return n;
}
void ofw_init(ppc_ci ci) { (void)ci; }
int ofw_setup(struct ofw_env *e) { e->out=9; return 0; }
int ofw_finddevice(const char *s) { return !kstrcmp(s,PM_ROOT)?(scenario==18?-17:1):!kstrcmp(s,"/cpus")?8:2; }
int ofw_parent(int n)
{
    if (n==-17) n=1;
    return n==1?0:n==6?5:n==5||n==7?9:n==9?4:n==3 && scenario!=22?8:scenario==18?-17:1;
}
int ofw_child(int n) { return n==1||n==-17?2:n==4?9:n==9?5:n==5?6:n==8 && scenario!=22?3:0; }
int ofw_peer(int n) { return n==2?(scenario==22?3:8):n==8?4:n==3 && scenario==22?4:n==4 && scenario==23?10:n==5?7:0; }
int ofw_package_to_path(int n, char *p, unsigned cap)
{
    if (n==-17) n=1;
    (void)cap; p[0]='/'; p[1]='0'+n; return 2;
}
int ofw_getprop(int n, const char *s, void *p, unsigned cap)
{
    (void)cap; return prop(n,s,p);
}
int ofw_call(const char *s, unsigned ni, unsigned no, cell *args)
{
    unsigned char temp[PM_KBOOT_PROP_BYTES];
    REQUIRE(!kstrcmp(s,"getproplen") && ni==2 && no==1);
    int n=prop((int)args[0],ppc_cell_ptr(args[1]),temp);
    if (scenario==15 && (int)args[0]==7) n=PM_KBOOT_PROP_BYTES+1;
    args[2]=(cell)n; return 0;
}
int ofw_write(int ih, const void *p, unsigned n)
{
    REQUIRE(ih==9 && n>10 && !kstrncmp(p,"FAIL:KBOOT:",11)); wrote=1; return n;
}
void ppc_stop(void) { quit(expected_stop && wrote?0:2); }
static void test(void)
{
    ac=1+(scenario==1 || scenario==3 || scenario==4 || scenario==6);
    sc=1+(scenario==2 || scenario==3 || scenario==4 || scenario==7);
    mac=scenario==4;
    tuple(reg,ac,sc,0,0x8000000); rn=(ac+sc)*4;
    unsigned aa=mac?1:ac, ss=mac?1:sc, stride=(aa+ss)*4;
    /* Unsorted overlap + adjacency, followed by a real hole. */
    tuple(avail,aa,ss,0x4000,0x1000);
    tuple(avail+stride,aa,ss,0x1000,0x2000);
    tuple(avail+2*stride,aa,ss,0x2000,0x2000);
    tuple(avail+3*stride,aa,ss,0x8000,0x1000); an=4*stride;
    expected_stop=(scenario>=5 && scenario<=17 && scenario!=9) || scenario==22 || scenario==23;
    if (scenario==5) tuple(reg,ac,sc,0xfffff000,0x2000);
    if (scenario==6) put(reg,1);
    if (scenario==7) put(reg+ac*4,1);
    if (scenario==8) rn--;
    if (scenario==9 || scenario==10) {
        unsigned count=PM_REGION_COUNT+(scenario==10);
        rn=count*8;
        for (unsigned i=0;i<count;i++) tuple(reg+i*8,1,1,i*0x2000,0x1000);
    }
    if (scenario==16) ac=3;
    if (scenario==17) tuple(reg,1,1,0,0);
    const struct pm_kboot_info *p=pm_kboot_capture(0);
    REQUIRE(!expected_stop);
    REQUIRE(p->ram.count==(scenario==9?PM_REGION_COUNT:1));
    REQUIRE(p->available.count==2);
    REQUIRE(p->available.v[0].start==0x1000 && p->available.v[0].size==0x4000);
    REQUIRE(p->available.v[1].start==0x8000 && p->available.v[1].size==0x1000);
    REQUIRE(p->tb_hz==25000000);
    REQUIRE(p->escc.count==5 && p->mpic.count==4);
    REQUIRE(p->escc.machine==PM_KBOOT_MACHINE_KEYLARGO &&
            p->mpic.machine==PM_KBOOT_MACHINE_KEYLARGO);
    REQUIRE(p->escc.v[0].node==6 && p->mpic.v[0].node==7);
    /* Compare every saved raw property and cell count to the firmware data. */
    const struct pm_kboot_device *devs[]={&p->escc,&p->mpic};
    const char *names[]={"reg","assigned-addresses","ranges","compatible"};
    for (unsigned d=0;d<2;d++) for (unsigned i=0;i<devs[d]->count;i++) {
        const struct pm_kboot_node *v=&devs[d]->v[i];
        unsigned char buf[PM_KBOOT_PROP_BYTES];
        const struct pm_kboot_property *props[]={&v->reg,&v->assigned,&v->ranges,&v->compatible};
        for (unsigned j=0;j<4;j++) {
            int n=prop(v->node,names[j],buf);
            REQUIRE(props[j]->present==(unsigned)(n!=-1));
            REQUIRE(props[j]->bytes==(n==-1?0:(unsigned)n));
            for (int k=0;k<n;k++) REQUIRE(props[j]->data[k]==buf[k]);
        }
        int n=prop(v->node,"#address-cells",buf);
        REQUIRE(v->ac==(n==-1?2:buf[3]));
        n=prop(v->node,"#size-cells",buf);
        REQUIRE(v->sc==(n==-1?1:buf[3]));
    }
    REQUIRE(p->mpic.v[1].node==9 && p->mpic.v[2].node==4);
    REQUIRE(p->mpic.v[0].reg.bytes==8 && p->mpic.v[0].reg.data[1]==4);
    REQUIRE(p->escc.v[0].path[1]=='6');
    for (unsigned i=0;i<sizeof(reg);i++) reg[i]=avail[i]=0;
    REQUIRE(p->ram.v[0].size==(scenario==9?0x1000:0x8000000));
    REQUIRE(p->mpic.v[0].reg.data[1]==4);
    quit(0);
}
void entry(unsigned *stack)
{
    scenario=0;
    if (stack[0]>1) {
        const char *p=(const char *)stack[2];
        while (*p) scenario=scenario*10+(*p++-'0');
    }
    test();
}
__asm__(".global _start\n_start:\n mov %esp,%eax\n push %eax\n call entry\n");
