/* Freestanding BE-property fixtures and native-u32 HPT, qemu-i386. */
#include "ppc_pmap.h"
#include "ppc_frame.h"
#include "ofw.h"
#include "mmu_fixture.h"
#include "mmu_boot.c"
#include "pmap.c"
#include "cpu_setup.c"
#include "decrementer.c"
static struct {
    u32 before;
    volatile u32 words[262144];
    u32 after;
} guarded;
#define table guarded.words
static u32 events[128], nevents, pvr, hid, batcount;
__asm__(".globl pm_image_start,pm_image_end\n.set pm_image_start,0x20000\n.set pm_image_end,0x40000\n");
static void exit_code(int rc) __attribute__((noreturn));
static void exit_code(int rc)
{
    __asm__ volatile("int $0x80" : : "a"(1), "b"(rc) : "memory");
    __builtin_unreachable();
}
#define CHECK(x) do { if (!(x)) exit_code(__LINE__); } while (0)
static void event(u32 e) { if (nevents<128) events[nevents++]=e; }
void ppc_stop(void) { exit_code(99); }
void ppc_sync(void) { event(1); }
void ppc_eieio(void) { event(2); }
void ppc_tlbsync(void) { event(3); }
void ppc_isync(void) { event(4); }
void ppc_tlb_flush_one(u32 va) { CHECK(va==PM_TEST_ALIAS); event(5); }
void ppc_tlb_flush_all(void) { event(6); }
void ppc_sr_sdr1_load(const u32 sr[16],u32 sdr)
{
    CHECK(sr[0]==0x20000010 && sr[15]==0x2000001f);
    CHECK(sdr==0x0010000f); event(7);
}
volatile u32 *ppc_hpt_ptr(u32 pa) { CHECK(pa==0x100000); return table; }
u32 ppc_read_pvr(void) { return pvr; }
u32 ppc_read_hid0(void) { return hid; }
void ppc_write_hid0(u32 v) { hid=v; event(0x10000000 | v); }
void ppc_bat_reset(u32 count) { batcount=count; event(0x20000000 | count); }
void ppc_bat_set(u32 index,u32 lo,u32 hi) { CHECK(index==0 && lo==0x12 && hi==0x1ffe); event(8); }
/* Unused OF path in parser/allocator fixture; any accidental CI fails. */
void ofw_init(ppc_ci ci) { (void)ci; exit_code(98); }
int ofw_setup(struct ofw_env *e) { (void)e; exit_code(98); }
int pm_escc_prepare(const struct ofw_env *e,struct pm_escc *s) { (void)e; (void)s; exit_code(98); }
int ofw_getprop(int p,const char *n,void *b,unsigned c) { (void)p;(void)n;(void)b;(void)c; exit_code(98); }
int ofw_finddevice(const char *p) { (void)p; exit_code(98); }
int ofw_child(int p) { (void)p; exit_code(98); }
int ofw_peer(int p) { (void)p; exit_code(98); }
int ofw_write(int h,const void *b,unsigned n) { (void)h;(void)b;(void)n; exit_code(98); }
int pm_escc_drain(const struct pm_escc *s) { (void)s; exit_code(98); }
void ppc_takeover(const struct pm_boot_info *i) { (void)i; exit_code(98); }
static int dec;
static u32 dec_write, dec_writes, dumps;
int ppc_read_dec(void) { return dec; }
u32 ppc_read_tb(void) { return 0x1234; }
void ppc_write_dec(u32 v) { dec_write=v; dec_writes++; }
void pm_mmu_dump(const char *tag,const struct ppc_frame *f) { (void)tag; (void)f; dumps++; }
static void test_dec(void)
{
    struct ppc_frame f={0};
    ppc_dec_interrupt(&f); CHECK(dec_writes==0);
    ppc_dec_init(100000); CHECK(dec_write==1000 && dec_writes==1);
    dec=-2500; ppc_dec_interrupt(&f); CHECK(dec_write==500 && count==1);
    dec=123; ppc_dec_interrupt(&f); CHECK(dec_write==123 && count==2);
    ppc_dec_interrupt(&f); ppc_dec_interrupt(&f);
    CHECK(count==4 && dec_writes==5 && dumps==4);
}
static void cell_at(unsigned i,u32 v)
{
    for (unsigned j=0;j<4;j++) property[i*4+j]=(unsigned char)(v>>(24-j*8));
}
static void test_memory(void)
{
    /* 2+2: 128MiB, zero length, >32bit PA, >32bit size, wrapping end. */
    for (unsigned i=0;i<sizeof(mmu_memory_cells)/sizeof(mmu_memory_cells[0]);i++) cell_at(i,mmu_memory_cells[i]);
    CHECK(regions(&ram,2,2,sizeof(mmu_memory_cells))==0 && ram.count==1);
    CHECK(ram.v[0].start==0 && ram.v[0].size==0x08000000);
    CHECK(regions(&ram,1,1,0)==-1);
    ram.count=1; ram.v[0]=(struct pm_region){0,0x08000000};
    avail.count=2; avail.v[0]=(struct pm_region){0,0x1000};
    avail.v[1]=(struct pm_region){0x123456,0x600000};
    CHECK(hpt_allocate()==0 && info.hpt_pa==0x200000 && info.hpt_mask==0x3fff);
    avail.count=1; CHECK(hpt_allocate()==-1);
    avail.v[0]=(struct pm_region){0xfff80000,0x10000};
    CHECK(hpt_allocate()==-1);
    ram.v[0].size=0x400000;
    avail.v[0]=(struct pm_region){0,0x400000};
    CHECK(hpt_allocate()==0 && info.hpt_pa==0x40000 && info.hpt_mask==0x7ff);
}
static void test_cpu(void)
{
    nevents=0;
    pvr=0x80000200; hid=0; ppc_cpu_setup();
    CHECK(batcount==4 && hid==0x9c);
    CHECK(nevents==sizeof(mmu_cpu_7450)/sizeof(u32));
    for (unsigned j=0;j<nevents;j++) CHECK(events[j]==mmu_cpu_7450[j]);
    pvr=0x80000201; hid=0; ppc_cpu_setup(); CHECK(hid==0xbc);
    nevents=0;
    pvr=0x80010200; hid=0; ppc_cpu_setup();
    CHECK(batcount==8 && hid==0x8001bc);
    CHECK(nevents==sizeof(mmu_cpu_7455)/sizeof(u32));
    for (unsigned j=0;j<nevents;j++) CHECK(events[j]==mmu_cpu_7455[j]);
}
static void test_pmap(void)
{
    struct pm_boot_info i={.hpt_pa=0x100000,.hpt_mask=0x3fff};
    guarded.before=0x12345678; guarded.after=0x87654321;
    for (unsigned j=0;j<262144;j++) table[j]=0xa5a5a5a5;
    ppc_pmap_bootstrap(&i);
    for (unsigned j=0;j<262144;j++) CHECK(table[j]==0);
    CHECK(guarded.before==0x12345678 && guarded.after==0x87654321);
    ppc_select_context(ppc_current_context());
    CHECK(events[nevents-2]==7 && events[nevents-1]==6);
    nevents=0;
    CHECK(ppc_pmap_enter(PM_TEST_ALIAS,0x300000,PPC_ATTR_RAM_RW)==0);
    CHECK(nevents==3 && events[0]==2 && events[1]==3 && events[2]==1);
    /* Independently calculated: VSID 0x14, hash 0x14, hi=0x80000a00. */
    CHECK(table[0x140]==0x80000a00 && table[0x141]==0x300012);
    table[0x141]|=0x180;
    nevents=0;
    CHECK(ppc_pmap_remove(PM_TEST_ALIAS)==0 && table[0x140]==0xa00);
    CHECK(nevents==7);
    for (unsigned j=0;j<7;j++) CHECK(events[j]==mmu_unset_events[j]);
    CHECK(mapping[0].rc==0x180);
    ppc_pmap_bootstrap(&i);
    for (unsigned j=0;j<8;j++) table[0x140+j*2]=PPC_PTE_V;
    CHECK(ppc_pmap_enter(PM_TEST_ALIAS,0x300000,PPC_ATTR_RAM_RW)==0);
    CHECK(table[0x3feb0]==0x80000a40 && table[0x3feb1]==0x300012);
    for (unsigned j=1;j<8;j++) table[0x3feb0+j*2]=PPC_PTE_V;
    CHECK(ppc_pmap_enter(PM_TEST_ALIAS,0x400000,PPC_ATTR_RAM_RW)==-1);
    CHECK(mapping[0].pa==0x300000 && table[0x3feb0]==0x80000a40 && table[0x3feb1]==0x300012);
    for (unsigned j=0;j<8;j++) CHECK(table[0x140+j*2]==PPC_PTE_V);
}
void _start(void)
{
    test_memory(); test_cpu(); test_pmap(); test_dec(); exit_code(0);
}
