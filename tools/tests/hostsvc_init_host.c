/* I1: execute the real boot helper with the real LAN MAC parser/initializer. */
#include "fs/serialfs/serialfs.h"
#include "fs/serialfs/sfs_lan.h"
#include "net/nic.h"
#include "init/sysconfig.h"
#include "include/config.h"
#include "lib/kstring.h"
#include "kprintf.h"
#include "init/boot_platform.h"
#define CHECK(x) do { if (!(x)) fail(__LINE__); } while (0)
static void fail(int line) { __asm__ volatile("int $0x80"::"a"(1),"b"(line % 250 + 1)); __builtin_unreachable(); }
volatile u32 tick_count;
static int mode, loaded, starts, unbinds, mounts, initialized, parsed, step, halted;
static int boot_legacy_started;
static struct SfsPersistentConfig saved;
static const u8 expected[6] = {2, 0xAB, 0xcd, 0x10, 0x20, 0x30};
static int same(const char *a,const char *b) { while(*a && *a==*b) {a++; b++;} return *a==*b; }
int sysconfig_get_int(const char *path,const char *key,int def)
{
    CHECK(same(path,SYS_NETCD_CFG) && same(key,"SFS_LAN") && def==0);
    return mode!=0 && mode!=2;
}
int sysconfig_get_str(const char *path,const char *key,char *out,int cap)
{
    CHECK(same(path,SYS_NETCD_CFG) && same(key,"SFS_LAN_PEER") && cap>=18);
    const char *value=mode==4 ? "02:ab:cd:10:20:xx" : "02:AB:cd:10:20:30";
    if(mode==0 || mode==1) {out[0]=0; return -1;}
    int n=0; do {out[n]=value[n];} while(value[n++]); return n-1;
}
static int memory_boot_dma_available(void) { return 1; }
void boot_platform_run(enum boot_point point,const struct boot_context *boot)
{ (void)boot; CHECK((point==BOOT_PCI_RESOURCES && step++==0) ||
                     (point==BOOT_NIC_LEGACY && step++==3)); }
static int lan_module_load(void) { CHECK(step++==1); loaded=mode!=5; return loaded?0:-1; }
static void lan_module_pci_bind_fallback(void) { CHECK(step++==2); }
void net_unbind(void) { CHECK(parsed && step==4); unbinds++; }
int net_start(enum NetNicId id,const u8 peer[6])
{
    CHECK(id==NET_NIC_AUTO && unbinds==1 && !mounts && !initialized);
    if (starts++) {
        CHECK(starts==2 && (mode==6 || mode==8) && !peer);
        return mode==8 ? -1 : 0;
    }
    if(mode==1) CHECK(!peer);
    else {CHECK(peer); for(int i=0;i<6;i++) CHECK(peer[i]==expected[i]);}
    return mode==6 || mode==8 ? -1 : 0;
}
int net_mac(u8 mac[6]) { for(int i=0;i<6;i++) mac[i]=(u8)(i+1); return 0; }
void body_lan_init(SfsLan *,const SfsLanNic *,const SfsIo *,void *);
int body_lan_peer(SfsLan *,const char *);
void sfs_lan_init(SfsLan *lan,const SfsLanNic *nic,const SfsIo *clock,void *ctx)
{ CHECK(starts==1 && !mounts); initialized++; body_lan_init(lan,nic,clock,ctx); }
int sfs_lan_set_peer(SfsLan *lan,const char *peer)
{ parsed++; return loaded ? body_lan_peer(lan,peer) : -1; }
const SfsIo sfs_lan_io = {0};
int sfs_persistent_start(const struct SfsPersistentConfig *cfg)
{
    CHECK(starts==1 && initialized==1 && cfg->lan_enabled && cfg->io==&sfs_lan_io);
    SfsLan *lan=cfg->io_ctx;
    CHECK(lan->mac[0]==1 && lan->clock && !lan->clock_ctx);
    CHECK(lan->peer_fixed==(mode!=1));
    if(mode!=1) for(int i=0;i<6;i++) CHECK(lan->peer[i]==expected[i]);
    saved=*cfg; mounts++;
    return mode==7 ? -1 : 0;
}
void kprintf(u8 attr,const char *fmt,...) { (void)attr; (void)fmt; }
static void host_halt(void) { halted++; }
#define _halt host_halt
#define _irq_enabled() 1
#include "hostsvc_init_source.inc"
static int suite(void)
{
    struct boot_context boot={0};
    kernel_boot_lan(&boot);
    CHECK(step==3 || step==4);
    if(mode==0 || mode==4 || mode==5) {CHECK(!starts && !mounts && !unbinds); return 0;}
    if(mode==6 || mode==8) {CHECK(starts==2 && unbinds==1 && !mounts && !initialized); return 0;}
    CHECK(starts==1 && unbinds==1);
    if(mode==2) {CHECK(!mounts); return 0;}
    CHECK(mounts==1 && initialized==1 && saved.io_ctx==&boot_sfs_lan);
    /* Use borrowed objects after the entire boot helper's stack has returned. */
    SfsLan *lan=saved.io_ctx;
    tick_count=1234;
    CHECK(lan->clock->now(lan->clock_ctx)==1234);
    CHECK(lan->clock->can_wait(lan->clock_ctx));
    lan->clock->idle(lan->clock_ctx);
    CHECK(halted==1 && boot_legacy_started);
    return 0;
}
void _start(void)
{
    u8 input=0;
    __asm__ volatile("int $0x80"::"a"(3),"b"(0),"c"(&input),"d"(1):"memory");
    mode=input;
    int rc=suite();
    __asm__ volatile("int $0x80"::"a"(1),"b"(rc):"memory");
    __builtin_unreachable();
}
