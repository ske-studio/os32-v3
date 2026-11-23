/* HWOP_COMMON_DESIGN §2.3: retain every PCI bridge and assignment after OF detach.
 * Actual kboot_capture.c with an ILP32, libc-free firmware tree fake. */
#include "kboot_info.h"
#include "kstring.h"
static unsigned bad_gmac_bridge;
static unsigned scenario, ac, sc, mac, expected_stop, wrote, lan_fail;
static void emit(const char *s) { __asm__ volatile("int $0x80" : : "a"(4), "b"(1), "c"(s), "d"(kstrlen(s)) : "memory"); }
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
static int has_parent_bus(void) { return scenario==45 || scenario==48; }

static int prop(int node, const char *name, unsigned char *p)
{
    unsigned n=0;
    if (has_parent_bus() && node == 19) {
        if (!kstrcmp(name,"#address-cells") || !kstrcmp(name,"#size-cells")) {
            put(p,1); return 4;
        }
        if (!kstrcmp(name,"ranges")) {
            put(p,0x80000000); put(p+4,0x60000000); put(p+8,0x70000000);
            if (scenario==48) {
                put(p+12,0x80000800); put(p+16,0x70000800); put(p+20,0x800);
                return 24;
            }
            return 12;
        }
        return -1;
    }
    if (scenario >= 42 && node >= 20) {
        const unsigned mem = 0x02000000;
        if (node==20 && !kstrcmp(name,"compatible")) { kmemcpy(p,"uni-north",10); return scenario==46?9:10; }
        if (node==20 && !kstrcmp(name,"reg")) { put(p,0xe0000000); put(p+4,0x2000000); return 8; }
        if (node <= 22) {
            if (node==21 && !kstrcmp(name,"class-code")) { put(p,0x060400); return 4; }
            if (node==21 && !kstrcmp(name,"device_type")) return -1;
            if (scenario==47 && node==21 && !kstrcmp(name,"ranges")) return -1;
            if (!kstrcmp(name,"device_type")) { kmemcpy(p,"pci",4); return 4; }
            if (!kstrcmp(name,"#address-cells")) { put(p,3); return 4; }
            if (!kstrcmp(name,"#size-cells")) { put(p,node==21?1:2); return 4; }
            if (!kstrcmp(name,"bus-range")) { put(p,node-20); put(p+4,3); return 8; }
            if (!kstrcmp(name,"ranges")) {
                unsigned child = node==20?0x3000:node==21?0x2000:0x1000;
                unsigned v[]={mem,0,child,mem,0,child+0x1000,0,0x1000};
                unsigned count=8;
                if (node==20) { v[3]=0x80000000; v[4]=0; v[5]=0x1000; count=6; }
                if (node==21) { v[1]=1; v[6]=0x1000; count=7; }
                if (node==22) v[4]=1;
                for (unsigned i=0;i<count;i++) put(p+i*4,v[i]);
                return count*4;
            }
        }
        if (node==40) {
            if (!kstrcmp(name,"vendor-id")) { put(p,0x106b); return 4; }
            if (!kstrcmp(name,"device-id")) { put(p,0x003f); return 4; }
            if (!kstrcmp(name,"class-code")) { put(p,0x0c0310); return 4; }
            if (!kstrcmp(name,"compatible")) { kmemcpy(p,"usb-ohci",9); return 9; }
            if (!kstrcmp(name,"assigned-addresses")) {
                unsigned v[]={mem|0x20110,0,0x1100,0,0x100};
                for (unsigned i=0;i<5;i++) put(p+i*4,v[i]);
                return 20;
            }
        }
        return -1;
    }
    if (node==-17) node=1;
    if (scenario==24 && (node==11 || node==12)) {
        if (!kstrcmp(name,"compatible")) { const char *s=node==11?"uni-north":"gmac"; do { p[n]=s[n]; } while (s[n++]); return n; }
        if (node==11 && !kstrcmp(name,"#address-cells")) { put(p,3); return 4; }
        if (node==11 && !kstrcmp(name,"#size-cells")) { put(p,2); return 4; }
        if (node==11 && !kstrcmp(name,"reg")) { put(p,0xf2000000); put(p+4,0x2000000); return 8; }
        if (node==11 && !kstrcmp(name,"bus-range")) { put(p,0); put(p+4,0); return bad_gmac_bridge ? 4 : 8; }
        if (node==11 && !kstrcmp(name,"ranges")) { for (n=0;n<48;n++) p[n]=0; put(p,0x01000000); put(p+12,0xf2000000); put(p+20,0x800000); put(p+24,0x02000000); put(p+32,0x80000000); put(p+36,0x80000000); put(p+44,0x10000000); return 48; }
        if (node==12 && !kstrcmp(name,"vendor-id")) { put(p,0x106b); return 4; }
        if (node==12 && !kstrcmp(name,"device-id")) { put(p,0x21); return 4; }
        if (node==12 && !kstrcmp(name,"reg")) { for (n=0;n<40;n++) p[n]=0; put(p,0x7800); put(p+20,0x02007810); put(p+36,0x200000); return 40; }
        if (node==12 && !kstrcmp(name,"assigned-addresses")) { for (n=0;n<20;n++) p[n]=0; put(p,0x02007810); put(p+8,0x82200000); put(p+16,0x200000); return 20; }
        if (node==12 && !kstrcmp(name,"local-mac-address")) { const unsigned char m[]={0x52,0x54,0,0x12,0x34,0x56}; for (n=0;n<6;n++) p[n]=m[n]; return 6; }
        return -1;
    }
    const char *str=0;
    if (node==9 && !kstrcmp(name,"name")) str="mac-io";
    if (node==13 || node==14) {
        if (!kstrcmp(name,"name"))
            str=node==14 || scenario==26 ? "via-pmu" :
                scenario==32 ? "via-cuda-extra" : "via-cuda";
        if (!kstrcmp(name,"compatible")) str="via-pmu"; /* diagnostic only */
        if (!kstrcmp(name,"reg")) {
            put(p,0x16020);
            put(p+4,0x2000);
            if (scenario==29) { put(p+8,0); return 12; }
            return 8;
        }
    }
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
        put(p+(3+ac+1)*4,scenario==30 ? 0x17000 : 0x10000000);
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
    if (node==9 && !kstrcmp(name,"assigned-addresses")) {
        v=assigned; count=5;
        if (scenario==31) {
            for (n=0;n<5;n++) put(p+4*n,assigned[n]);
            put(p+4,1);
            return 20;
        }
    }
    if (node==9 && !kstrcmp(name,"ranges") && scenario>=36 && scenario<=41) {
        for (n=0;n<5;n++) put(p+4*n,ranges[n]);
        /* Test matching tokens as well as the wrong BDF and wrong BAR. */
        unsigned token = scenario==36 || scenario==39 ? 0x8010 :
                         scenario==37 || scenario==40 ? 0x8810 : 0x8014;
        put(p+4, (scenario>=39 ? 0x82000000 : 0x02000000) | token);
        put(p+12, scenario>=39 ? 0x80000000 : 0);
        return 20;
    }
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
    if (has_parent_bus() && n==20) return 19;
    if (scenario>=42 && n>=20) return n==20?1:n<=22?n-1:22;
    if (n==-17) n=1;
    return n==12?11:n==11?1:n==1?0:n==6?5:n==5||n==7||n==13||n==14?9:n==9?4:n==3 && scenario!=22?8:scenario==18?-17:1;
}
int ofw_child(int n) { if (has_parent_bus() && n==19) return 20; if (scenario>=42 && n>=20) return n<=22?n+1:0; if (n==13 && scenario==34) return -1; return n==11?12:n==1||n==-17?2:n==4?9:n==9?5:n==5?6:n==8 && scenario!=22?3:0; }
int ofw_peer(int n) { if (scenario==46 && n==8) return 20; if (scenario==46 && n==20) return 4; if (scenario==46 && n==4) return 0; if (scenario>=42 && n==4) return has_parent_bus()?19:20; if (scenario>=42 && n>=23) return n<(scenario==43?140:40)?n+1:0; if (n==13 && scenario==35) return -1; return n==2?(scenario==22?3:8):n==8?4:n==3 && scenario==22?4:n==4 && scenario==23?10:n==4 && scenario==24?11:n==5?7:n==7 && scenario>=26 && scenario!=33?13:n==13 && scenario==28?14:0; }
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
    REQUIRE(ih==9 && n>10 && !kstrncmp(p,"FAIL:KBOOT:",11)); wrote=1; if (!kstrncmp(p,"FAIL:KBOOT:LAN",14)) lan_fail=1; return n;
}
void ppc_stop(void) { if (lan_fail) { emit("H:A1 fatal-lan\n"); quit(41); } quit(expected_stop && wrote?0:2); }
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
    /* G4 PowerMac3,5: empty DIMM banks are listed with size 0 and skipped. */
    if (scenario==25) { tuple(reg+8,1,1,0x8000000,0); tuple(reg+16,1,1,0x8000000,0); rn=24; }
    emit("reached=A1\n");
    const struct pm_kboot_info *p=pm_kboot_capture(0);
    REQUIRE(!expected_stop);
    if (bad_gmac_bridge) {
        REQUIRE(p->pci.gmac_known && p->gmac_rc==LAN_EINVAL && !p->gmac.node);
        quit(0);
    }
    int expected_gmac_rc = scenario==24 ? LAN_OK : scenario==46 ? LAN_EINVAL : LAN_ENODEV;
    if (p->gmac_rc!=expected_gmac_rc) { emit("H:A1 gmac-rc\n"); quit(42); }
    if (scenario==24 && !(p->gmac.bar_pa==0x82200000 && p->gmac.mac[0]==0x52 && p->gmac.mac[5]==0x56)) { emit("H:A1 gmac-data\n"); quit(43); }
    if (scenario>=42) {
        const struct pm_pci_capture *c=&p->pci;
        if (scenario==48) {
            REQUIRE(c->bridges==3 && c->bridge[0].id==20);
            if (!c->bridge[0].incomplete) { emit("partial overlap accepted\n"); quit(46); }
            quit(0);
        }
        if (scenario==47) {
            REQUIRE(c->bridges==3 && c->bridge[1].parent==0 &&
                    c->bridge[1].incomplete && c->bridge[0].incomplete);
            quit(0);
        }
        REQUIRE(c->bridges==3 && c->ranges==3 && c->assigned==1);
        if (scenario==46) { REQUIRE(!c->bridge[0].supported && c->bridge[0].incomplete); quit(0); }
        REQUIRE(c->bridge[0].supported && c->bridge[0].id==20 && c->bridge[0].parent==-1 &&
                c->bridge[0].cfg_pa==(scenario==45?0xc0000000:0xe0000000) && c->bridge[0].first==0 && c->bridge[0].last==3);
        const struct pm_of_assignment *a=&c->assignment[0];
        const struct pm_of_function *f=&c->function[a->function];
        REQUIRE(f->vendor==0x106b && f->device==0x3f && f->class_code==0x0c0310);
        REQUIRE(!kstrcmp(f->compatible,"usb-ohci") && a->hi==0x02020110 && a->bytes==0x100);
        REQUIRE(c->range[1].base==0x100002000ULL && c->range[2].parent_base==0x100002000ULL);
        unsigned long long pa=a->base;
        for (int bridge=f->bridge; bridge>=0; bridge=c->bridge[bridge].parent) {
            const struct pm_of_range *r=&c->range[bridge];
            if (r->bridge!=bridge || pa<r->base || pa-r->base>=r->bytes) {
                emit("OHCI parent interval mismatch\n"); quit(45);
            }
            pa=r->parent_base+(pa-r->base);
        }
        REQUIRE(pa==(scenario==45?0x60000100:0x80000100));
        REQUIRE(c->bridge[0].incomplete==(scenario==43));
        if (scenario==43) REQUIRE(c->functions==PM_PCI_FUNCTIONS);
        quit(0);
    }
    if (scenario>=26) {
        const int expected[]={0,0,PM_VIA_AMBIGUOUS,PM_VIA_BAD_PROPERTY,
                              PM_VIA_BAD_ADDRESS,PM_VIA_BAD_ADDRESS,
                              PM_VIA_MISSING,PM_VIA_MISSING,
                              PM_VIA_BAD_PROPERTY,PM_VIA_BAD_PROPERTY,
                              0,PM_VIA_BAD_ADDRESS,PM_VIA_BAD_ADDRESS,
                              0,PM_VIA_BAD_ADDRESS,PM_VIA_BAD_ADDRESS};
        if (p->via.capture_rc!=expected[scenario-26] ||
            (p->via.capture_rc && (p->via.pa || p->via.bytes)) ||
            (!p->via.capture_rc &&
             (p->via.pa!=0x80016020 || p->via.bytes!=0x2000 ||
              p->via.kind!=(scenario==26?PM_VIA_PMU:PM_VIA_CUDA) ||
              kstrcmp(p->via.compatible,"via-pmu")))) {
            emit("H:A1 via\n"); quit(44);
        }
        quit(0);
    }
    REQUIRE(p->via.capture_rc==PM_VIA_MISSING);
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
    if (scenario==44) { scenario=24; bad_gmac_bridge=1; }
    test();
}
__asm__(".global _start\n_start:\n mov %esp,%eax\n push %eax\n call entry\n");
