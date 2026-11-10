#include "lan_boot.h"
#include "ofw.h"
#include "kstring.h"
static unsigned scenario;
static void put(u8 *p,u32 v) { p[0]=v>>24; p[1]=v>>16; p[2]=v>>8; p[3]=v; }
static int property(int node,const char *name,u8 *p)
{
    const char *s=0; unsigned n=0;
    if (node==1 && !kstrcmp(name,"#address-cells")) { put(p,scenario==21?2:1); return 4; }
    if (node==1 && !kstrcmp(name,"#size-cells")) { put(p,scenario==16?2:1); return 4; }
    if (node==2 && !kstrcmp(name,"compatible")) s=scenario==3?"u3":"uni-north";
    if ((node==3 || (node==4 && scenario==2)) && !kstrcmp(name,"compatible")) s=node==3 && scenario!=1?"gmac":"other";
    if ((node==3 || (node==4 && scenario==2)) && !kstrcmp(name,"vendor-id")) { put(p,node==3 && scenario==1?0:0x106b); return 4; }
    if ((node==3 || (node==4 && scenario==2)) && !kstrcmp(name,"device-id")) { put(p,scenario==13?0x22:0x21); return 4; }
    if (node==2 && !kstrcmp(name,"#address-cells")) { put(p,3); return scenario==4?3:4; }
    if (node==2 && !kstrcmp(name,"#size-cells")) { put(p,2); return 4; }
    if (node==2 && !kstrcmp(name,"reg") && scenario==21) { put(p,0); put(p+4,0xf2000000); put(p+8,0x2000000); return 12; }
    if (node==2 && !kstrcmp(name,"reg")) { put(p,0xf2000000); put(p+4,scenario==17?0x20000000:scenario==16?1:0x2000000); if (scenario==16) { put(p+8,0x2000000); return 12; } return 8; }
    if (node==2 && !kstrcmp(name,"bus-range")) { put(p,0); put(p+4,0); return 8; }
    if (node==2 && !kstrcmp(name,"ranges")) {
        unsigned count=scenario==6?9:2;
        if (scenario==21) { for (unsigned i=0;i<56;i++) p[i]=0; put(p,0x01000000); put(p+16,0xf2000000); put(p+24,0x800000); put(p+28,0x02000000); put(p+36,0x80000000); put(p+40,1); put(p+44,0x80000000); put(p+52,0x10000000); return 56; }
        for (unsigned i=0;i<count;i++) {
            u8 *q=p+i*24; for (unsigned j=0;j<24;j++) q[j]=0;
            put(q,i?0x02000000:0x01000000); put(q+8,i?0x80000000+(i-1)*0x1000000:0);
            put(q+12,i?0x80000000+(i-1)*0x1000000:0xf2000000);
            put(q+20,i?0x10000000:0x800000);
            if (scenario==7 && i==1) { put(q,0x01000000); put(q+8,0); }
            if (scenario==8 && i==1) put(q+4,1);
            if (scenario==18 && i==1) put(q+16,1);
            if (scenario==19 && i==1) put(q+8,0xf8000000);
            if (scenario==20 && i==1) put(q+12,0xf8000000);
        }
        return count*24;
    }
    if (node==3 && !kstrcmp(name,"reg")) { for (n=0;n<40;n++) p[n]=0; put(p,0x7800); put(p+20,0x02007810); put(p+36,0x200000); return 40; }
    if (node==3 && !kstrcmp(name,"assigned-addresses")) { for (n=0;n<40;n++) p[n]=0; if (scenario==14) { put(p,0x02007814); put(p+8,0x83000000); put(p+16,0x1000); } unsigned off=scenario==14?20:0; put(p+off,0x02007810); put(p+off+8,scenario==5?0xa0000000:scenario==19?0xf8200000:0x82200000); put(p+off+16,0x200000); return off+20; }
    if (node==3 && !kstrcmp(name,"local-mac-address")) { const u8 mac[]={0x52,0x54,0,0x12,0x34,0x56}; for (n=0;n<6;n++) p[n]=scenario==9?0:mac[n]; if (scenario==10) p[0]|=1; return 6; }
    if (node==5 && !kstrcmp(name,"bootargs")) {
        if (scenario==11) s="os32-lan-boot=00112233445566778899aabbccddeeff os32-lan-peer=02:11:22:33:44:55";
        if (scenario==12) s="os32-lan-boot=00112233445566778899aabbccddeezz os32-lan-peer=02:11:22:33:44:xx";
    }
    if (!s) return -1;
    do { p[n]=s[n]; } while (s[n++]); return n;
}
int ofw_finddevice(const char *s) { return !kstrcmp(s,"/")?1:!kstrcmp(s,"/chosen")?5:0; }
int ofw_parent(int n) { return n==3||n==4?2:n==2?(scenario==15?6:1):n==6?1:0; }
int ofw_child(int n) { if (scenario==22 || scenario==23) { if (n==3) return 100; if (n>=100 && n<(scenario==22?105:106)) return n+1; } return n==1?2:n==2?3:0; }
int ofw_peer(int n) { if (scenario==24 || scenario==25) { if (n==2) return 100; if (n>=100 && n<(scenario==24?1121:1122)) return n+1; } return n==3 && scenario==2?4:0; }
int ofw_getprop(int n,const char *s,void *p,unsigned cap) { (void)cap; return property(n,s,p); }
int ofw_call(const char *s,unsigned ni,unsigned no,cell *a) { u8 p[512]; (void)s; (void)ni; (void)no; a[2]=(cell)property((int)a[0],ppc_cell_ptr(a[1]),p); return 0; }
static void quit(unsigned code) __attribute__((noreturn));
static void quit(unsigned code) { __asm__ volatile("int $0x80" : : "a"(1), "b"(code) : "memory"); __builtin_unreachable(); }
static void diagnostic(unsigned line) {
    const char *msg=line==1001?"H:A1 capture\n":line==1002?"H:A1 peer\n":"H:A1 value\n";
    unsigned len=kstrlen(msg);
    __asm__ volatile("int $0x80" : : "a"(4), "b"(1), "c"(msg), "d"(len) : "memory");
}
#define REQUIRE(x) do { if (!(x)) { diagnostic(__LINE__); quit(1); } } while (0)
static void mark(void) { const char msg[]="reached=A1\n"; __asm__ volatile("int $0x80" : : "a"(4), "b"(1), "c"(msg), "d"(sizeof(msg)-1) : "memory"); }
void entry(unsigned *stack)
{
    const char *p=(const char *)stack[2]; while (*p) scenario=scenario*10+(*p++-'0');
    struct pm_gmac_boot b; mark(); int rc=pm_gmac_capture(&b);
    const int expected[]={0,LAN_ENODEV,LAN_EUNSUPPORTED,LAN_EUNSUPPORTED,LAN_EINVAL,LAN_ERANGE,LAN_ERANGE,LAN_ERANGE,LAN_ERANGE,LAN_EINVAL,LAN_EINVAL,0,0,LAN_EUNSUPPORTED,0,LAN_EUNSUPPORTED,LAN_ERANGE,LAN_ERANGE,LAN_ERANGE,LAN_ERANGE,LAN_ERANGE,LAN_ERANGE,0,LAN_ERANGE,0,LAN_ERANGE};
    if (rc!=expected[scenario]) { diagnostic(1001); quit(1); }
    if (rc) { const u8 *v=(const u8 *)&b; for (unsigned i=0;i<sizeof(b);i++) REQUIRE(!v[i]); quit(0); }
    REQUIRE(b.bar_pa==0x82200000 && b.bar_bytes==0x200000 && b.root_pa==0xf2000000 && b.nwin==2 && !b.bus_first && !b.bus_last);
    REQUIRE(b.mac[0]==0x52 && b.mac[5]==0x56);
    if (b.peer_mac[0]!=(scenario==11?2:255)) { diagnostic(1002); quit(1); }
    REQUIRE(b.profile==1 && b.override_flags==(scenario==11?3u:0u));
    pm_gmac_boot_id(&b,0x0123456789abcdefULL,0x12345678);
    const u8 generated[]={1,0x23,0x45,0x67,0x89,0xab,0xcd,0xef,0x52,0x54,0,0x12,0x34,0x56,0x56,0x78};
    for (unsigned i=0;i<16;i++) REQUIRE(b.boot_id[i]==(scenario==11?i*0x11:generated[i]));
    quit(0);
}
__asm__(".global _start\n_start:\n mov %esp,%eax\n push %eax\n call entry\n");
