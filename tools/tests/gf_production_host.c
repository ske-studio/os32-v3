/* KDEBUG=0 production path. Only PCI, MMIO, RAM, PTE and the clock are fake.
 * The real decoder, runner, bus, owner, service and required CHECK execute.
 * No recover executor/kpeek calls survive section GC in the linked test. */
#define GF_S2_ENTRY unused_decoder_tests
#include "gfinit_host.c"
#include "hwdev/gf/production.h"
#include "kernel/pgalloc.h"
#include "kernel/paging.h"
#include "include/gf_production.h"
#include "kernel/sysconfig.c"
#include "gfinit_resources_ra266.h"

struct prod_reg { u32 off,value; };
static struct prod_reg prod_regs[4096];
static u32 prod_reg_count,prod_index[6],prod_vga[6][256];
static struct gf_production prod;
static struct gf_production_resources prod_boot;
static struct gf_production_scratch prod_scratch __attribute__((aligned(PAGE_SIZE)));
static u8 prod_rom[GF_ROM_MAX],prod_rgb[VIDEO_PALETTE_BYTES];
static u32 prod_cfg[32][8][64];
static u32 prod_now,prod_clock_step,prod_clock_fail,prod_clock_calls;
static u32 prod_mmio_calls,prod_cmd_writes,prod_other_cfg_writes,prod_reads16,prod_live_pages;
static u32 prod_alloc_fail,prod_map_fail,prod_ram_fail,prod_route_fail,prod_rom_changed;
static u32 prod_mismatch,prod_fail_section,prod_fail_main,prod_fail_a2,prod_a2_writes;
static u32 prod_deadline_section;
static u32 prod_stop_fail,prod_stop_writes,prod_route_calls,prod_redraws,prod_pfb;
static u32 prod_trace[8192],prod_trace_count,prod_main_mask;
static bool prod_a2_done,prod_stopping,prod_faulted;
static u32 prod_logs;
void kprintf(u8 attr,const char *format,...)
{ (void)attr;(void)format;prod_logs++; }
#define PROD_ROUTE 0xfacU
#define PROD_REDRAW 0xa8000U
static void prod_event(u32 e)
{ CHECK(prod_trace_count<8192);if (prod_trace_count<8192)prod_trace[prod_trace_count++]=e; }
static int prod_port(u32 off)
{
    for(u32 h=0;h<2;h++) {
        if(off==0x6013d4+h*0x2000)return h*3;
        if(off==0xc03c4+h*0x2000)return h*3+1;
        if(off==0xc03ceU+h*0x2000)return h*3+2;
    }
    return -1;
}
static u32 *prod_reg(u32 off)
{
    for(u32 i=0;i<prod_reg_count;i++)if(prod_regs[i].off==off)return &prod_regs[i].value;
    CHECK(prod_reg_count<4096);
    prod_regs[prod_reg_count]=(struct prod_reg){off,0};
    return &prod_regs[prod_reg_count++].value;
}
int gf_production_mmio(struct gf_production *p,u32 bar,u32 off,u32 width,bool write,u32 *v)
{
    CHECK(p==&prod && bar==0 && (width==1 || width==2 || width==4));
    CHECK(write || (off!=0x6013c0 && off!=0x6813c9));
    prod_mmio_calls++;
    if(off>=KPEEK_NV_PROM_OFF && off-KPEEK_NV_PROM_OFF<GF_ROM_MAX) {
        CHECK(!write && prod_cmd_writes==1);
        *v=prod_rom[off-KPEEK_NV_PROM_OFF];
        if(prod_rom_changed && prod_mmio_calls>GF_ROM_MAX)*v^=1;
        return 0;
    }
    if(prod.display.init && prod_a2_done && prod.display.section==GF_DISPLAY_INIT) {
        u32 main=prod.display.init->main_id;
        prod_main_mask|=1U<<main;
        if(!prod_faulted && prod_fail_main==main+1) {prod_faulted=true;return -1;}
    }
    if(!prod_faulted && prod_fail_section==prod.display.section) {prod_faulted=true;return -1;}
    if(write) {
        if(p->display.section==GF_DISPLAY_STOP &&
           !(width==4 && (off==0x1850 || off==0x1854 || off==0x140))) prod_event(off);
        if(p->display.section==GF_DISPLAY_STOP && width==4 && (off==0x1850 || off==0x1854 || off==0x140)) {
            prod_stop_writes++;prod_event(off);
            if(prod_stop_writes==prod_stop_fail)return -1;
        } else if(!prod_a2_done) {
            prod_a2_writes++;
            if(!prod_faulted && prod_a2_writes==prod_fail_a2) {prod_faulted=true;return -1;}
            prod_event(off);
        }
    }
    int select=prod_port(off),data=prod_port(off-1);
    if(write) {
        if(select>=0)prod_index[select]=*v;
        else if(data>=0) {
            prod_vga[data][prod_index[data]]=*v;
            if(data%3==0 && prod_index[data]==0x1f)prod_vga[data][0x1f]=*v==0x57?3:0;
            if(!prod_a2_done && data==0 && prod_index[data]==0x11)prod_a2_done=true;
        } else *prod_reg(off)=*v;
    } else {
        if(select>=0)*v=prod_index[select];
        else if(data>=0)*v=prod_vga[data][prod_index[data]];
        else *v=*prod_reg(off==0xc03cc?0xc03c2:off);
        if(off==GF_PRODUCTION_PFB_AMOUNT)*v=prod_pfb;
    }
    return 0;
}
u32 pci_cfg_read32(u32 b,u32 d,u32 f,u32 o)
{
    CHECK(b==0 && d<32 && f<8 && o<256 && !(o&3));
    if(d==13 && (o==RECOVER_CFG50 || o==RECOVER_CFG54))
        return *prod_reg(o==RECOVER_CFG50?0x1850:0x1854);
    return prod_cfg[d][f][o/4];
}
u16 pci_cfg_read16(u32 b,u32 d,u32 f,u32 o)
{ prod_reads16++;return (u16)(pci_cfg_read32(b,d,f,o&~3U)>>((o&2)*8)); }
void pci_cfg_write32(u32 b,u32 d,u32 f,u32 o,u32 v)
{(void)b;(void)d;(void)f;(void)o;(void)v;prod_other_cfg_writes++;}
int pci_cfg_write16(u32 b,u32 d,u32 f,u32 o,u16 v)
{
    CHECK(!b && d==13 && !f && o==4 && v==0x102);
    prod_event(4);prod_cmd_writes++;
    prod_cfg[d][f][o/4]=(prod_cfg[d][f][o/4]&0xffff0000U)|(prod_mismatch?0x106:v);
    return 0;
}
void *kmemset(void *p,int value,u32 n)
{u8 *b=p;while(n--)*b++=(u8)value;return p;}
int kstrcmp(const char *a,const char *b)
{while(*a && *a==*b){a++;b++;}return (u8)*a-(u8)*b;}
u32 pgalloc_alloc_phys(u32 owner,int pages)
{
    CHECK(owner==LEDGER_OWNER_KERNEL && pages==(int)(PAGE_ALIGN_UP(sizeof(prod_scratch))/PAGE_SIZE));
    if(prod_alloc_fail)return 0;
    CHECK(!prod_live_pages);prod_live_pages=pages;return V2P(&prod_scratch);
}
int pgalloc_free_n_owner(u32 owner,u32 first,int pages)
{
    CHECK(owner==LEDGER_OWNER_KERNEL && first==V2P(&prod_scratch)/PAGE_SIZE && pages==(int)prod_live_pages);
    prod_live_pages=0;return 1;
}
int pgalloc_range_has_ram(u32 first,u32 end)
{CHECK(first<end);return prod_ram_fail;}
struct ledger_region ledger_regions[LEDGER_MAX_REGIONS];
u32 ledger_region_count;
u32 paging_pte_raw(u32 va)
{return prod_map_fail?0:(va|PTE_PRESENT|PTE_RW|PTE_PCD);}
int sys_time_now(u32 *lo,u32 *hi)
{
    if(prod.display.init && prod.display.section==prod_deadline_section)
        prod_now=prod.display.init->started_us+GF_INIT_EXEC_US;
    if(prod.display.init && prod_a2_done && prod.display.section==GF_DISPLAY_INIT) {
        u32 main=prod.display.init->main_id;
        prod_main_mask|=1U<<main;
        if(!prod_faulted && prod_fail_main==main+1) {prod_faulted=true;return -1;}
    }
    if(++prod_clock_calls==prod_clock_fail)return -1;
    prod_now+=prod_clock_step;*lo=prod_now;*hi=0;return 0;
}
static int prod_route(void *v)
{CHECK(v==&prod);prod_event(PROD_ROUTE);prod_route_calls++;return prod_route_fail?-1:0;}
static int prod_return(void *v)
{
    struct gf_production *p=v;
    prod_stopping=true;
    int stopped=gf_production_quiesce(p);
    int routed=prod_route(v); /* still attempted on identity/stop failure */
    int safe=gf_production_decode_safe(p);
    if(!routed && !safe) {prod_redraws++;prod_event(PROD_REDRAW);}
    prod_stopping=false;
    return stopped || routed || safe ? -1 : 0;
}
void platform_reset(void)
{
    CHECK(prod_trace_count && prod_trace[prod_trace_count-1]==PROD_REDRAW);
    output(failures?"PRODUCTION FAIL\n":"PRODUCTION PASS reboot callback before reset\n");
    __asm__ volatile("int $0x80"::"a"(1),"b"(failures?1:0));__builtin_unreachable();
}
static void prod_reset(void)
{
    kmemset(&prod,0,sizeof(prod));kmemset(prod_cfg,0xff,sizeof(prod_cfg));
    kmemset(prod_vga,0,sizeof(prod_vga));kmemset(prod_index,0,sizeof(prod_index));
    prod_reg_count=prod_trace_count=prod_main_mask=0;
    prod_now=prod_clock_calls=prod_mmio_calls=prod_cmd_writes=prod_other_cfg_writes=prod_reads16=0;
    prod_clock_step=1;prod_clock_fail=prod_live_pages=prod_alloc_fail=prod_map_fail=prod_ram_fail=0;
    prod_route_fail=prod_rom_changed=prod_mismatch=prod_fail_main=0;
    prod_fail_section=prod_deadline_section=RECOVER_NONE;
    prod_fail_a2=prod_a2_writes=prod_stop_fail=prod_stop_writes=prod_route_calls=prod_redraws=0;
    prod_pfb=0x10000000;prod_a2_done=prod_stopping=prod_faulted=false;
    for(u32 i=0;i<sizeof(resource_ra266)/sizeof(resource_ra266[0]);i++) {
        const struct resource_ra266_function *f=&resource_ra266[i];
        for(u32 j=0;j<16;j++)prod_cfg[f->dev][f->fn][j]=f->cfg[j];
    }
    /* Native maps and complete decode reservations furnished by P5 at boot. */
    ledger_region_count=3;
    const u32 bases[]={0x24000000,0x30000000,0x25000000};
    const u32 lengths[]={0x1000000,0x10000000,0x1000000};
    for(u32 i=0;i<3;i++)ledger_regions[i]=(struct ledger_region){
        .first=bases[i]/PAGE_SIZE,.end=(bases[i]+lengths[i])/PAGE_SIZE,
        .type=LEDGER_R_DEVICE,.owner=LEDGER_OWNER_GFX,.cache=LEDGER_CACHE_UC};
    fixture();kmemset(prod_rom,0,sizeof(prod_rom));
    for(u32 i=0;i<sizeof(rom);i++)prod_rom[i]=rom[i];
    prod_rom[0]=0x55;prod_rom[1]=0xaa;prod_rom[2]=128;prod_rom[0x18]=0x40;
    prod_rom[0x40]='P';prod_rom[0x41]='C';prod_rom[0x42]='I';prod_rom[0x43]='R';
    prod_rom[0x44]=0xde;prod_rom[0x45]=0x10;prod_rom[0x46]=0x21;prod_rom[0x47]=2;
    const u8 bit[]={0xff,0xb8,'B','I','T',0,0,0,12,6,1,0,'i',2,3,0,0x20,1};
    for(u32 i=0;i<sizeof(bit);i++)prod_rom[0x100+i]=bit[i];
    prod_rom[0x122]=0x44;
    u8 sum=0;for(u32 i=0;i<65536;i++)sum+=prod_rom[i];prod_rom[65535]-=sum;
    prod_vga[0][0x11]=0x80;prod_index[0]=0x22;
    *prod_reg(0x1850)=*prod_reg(0x1854)=1;
    CHECK(!gf_production_identify(&prod_boot));
    CHECK(!gf_production_prepare(&prod,&prod_boot,prod_route,&prod));
    CHECK(!prod_cmd_writes && !prod_mmio_calls && !prod_route_calls);
}
static int prod_start(void)
{return video_start(&prod.video,1,prod_rgb);}
static void prod_order(void)
{
    /* Independent startup prefix; CR11 RMW finishes A2, then main0..4. */
    const u32 prefix[]={PROD_ROUTE,4,0x1854,0x1850,0x140,0x200,
                        0x6013d4,0x6013d5,0x6013d4,0x6013d5,0x6013d4,0x6013d5};
    CHECK(prod_trace_count>=sizeof(prefix)/sizeof(prefix[0]));
    for(u32 i=0;i<sizeof(prefix)/sizeof(prefix[0]);i++)CHECK(prod_trace[i]==prefix[i]);
    CHECK(prod_main_mask==31 && prod_a2_done && prod.display.baseline_valid);
}
static void prod_exit_order(void)
{
    /* P3 performs cleanup/quiesce on failure. P5 calls stop (idempotent) and
     * route afterwards. All three writes precede route and safe 98 redraw. */
    u32 last_route=0;
    for(u32 i=0;i<prod_trace_count;i++)if(prod_trace[i]==PROD_ROUTE)last_route=i;
    CHECK(last_route>=3);
    CHECK(prod_trace[last_route-3]==0x1850 && prod_trace[last_route-2]==0x1854 && prod_trace[last_route-1]==0x140);
    CHECK(prod_trace[prod_trace_count-1]==PROD_REDRAW);
}
static void prod_failure(void)
{
    prod_stopping=true;
    int rc=prod_start();
    if(rc!=-1) {
        output("failure injection missed: section/a2/main ");
        number(prod_fail_section);output("/");number(prod_fail_a2);output("/");number(prod_fail_main);output("\n");
    }
    CHECK(rc==-1);prod_stopping=false;
    CHECK(!prod.display.active && !prod.display.reusable && !prod_live_pages && !prod.scratch);
    u32 io=prod_mmio_calls,commands=prod_cmd_writes;
    CHECK(prod_start()==-1 && prod_mmio_calls==io && prod_cmd_writes==commands);
    (void)prod_return(&prod);
    CHECK(!prod_other_cfg_writes && prod_cmd_writes<=1);
}
static void prod_selection_tests(void)
{
    CHECK(!gf_production_quiesce(&prod) && gf_production_quiesce(0)==-1);
    const char *gfx[]={"geforce","geforce","geforce","pc98","auto","unknown",0};
    const int gui[]={0,1,2,0,0,0,0};
    for(u32 i=0;i<7;i++) {
        gf_configured=0;gf_production_configure(gfx[i],gui[i]);
        CHECK(gf_production_selected()==(i==0 || i==2));
        gf_production_configure("auto",0);CHECK(gf_production_selected()==(i==0 || i==2));
    }
    output("PASS selection: GUI=0/default or !=1, GUI=1 refusal, immutable boot choice\n");
}
static void prod_cases(void)
{
    prod_reset();CHECK(!prod_start());prod_order();
    CHECK(prod_cmd_writes==1 && !prod_other_cfg_writes && prod_reads16 && !prod_live_pages);
    CHECK((prod_cfg[13][0][1]&0xffff)==0x102 && (prod_cfg[13][0][1]>>16)==0x02b0);
    CHECK(!prod_return(&prod));prod_exit_order();
    CHECK(prod_index[0]==0x22 && prod_vga[0][0x11]==0x80 && prod_vga[0][0x1f]==0);
    u32 reads=prod_mmio_calls;
    CHECK(!gf_production_quiesce(&prod) && prod_mmio_calls==reads);
    CHECK(!prod_start() && prod_cmd_writes==1);CHECK(!prod_return(&prod));
    output("PASS real startup/main/baseline/mode/CHECK, WORD Command 0106->0102 once, no BAR writes, restart without devinit\n");
    prod_reset();prod_mismatch=1;prod_failure();CHECK(prod_cmd_writes==1 && !prod_mmio_calls && !prod_a2_done && !prod_redraws);
    prod_reset();prod_route_fail=1;prod_failure();CHECK(!prod_cmd_writes && !prod_mmio_calls);
    prod_reset();prod_alloc_fail=1;prod_failure();CHECK(!prod_a2_done);prod_exit_order();
    prod_reset();prod_rom[0]=0;prod_failure();CHECK(!prod_a2_done);prod_exit_order();
    prod_reset();prod_rom_changed=1;prod_failure();CHECK(!prod_a2_done);prod_exit_order();
    prod_reset();prod_rom[0xdeb0]^=1;prod_rom[65535]^=1;prod_failure();CHECK(!prod_a2_done);prod_exit_order();
    for(u32 i=1;i<=10;i++) {prod_reset();prod_fail_a2=i;prod_failure();prod_exit_order();}
    for(u32 i=1;i<=5;i++) {prod_reset();prod_fail_main=i;prod_failure();prod_exit_order();}
    const u32 sections[]={GF_DISPLAY_BASELINE,GF_LOAD_L2,GF_DISPLAY_CHECK};
    for(u32 i=0;i<sizeof(sections)/sizeof(sections[0]);i++) {
        prod_reset();prod_fail_section=sections[i];prod_failure();prod_exit_order();
    }
    prod_reset();prod_pfb=0x30000000;prod_failure();prod_exit_order();
    prod_reset();prod_clock_step=GF_INIT_EXEC_US;prod_failure();CHECK(!prod_a2_done);prod_exit_order();
    prod_reset();prod_deadline_section=GF_DISPLAY_BASELINE;prod_failure();
    CHECK(prod_a2_done && prod_main_mask==31 && !prod.display.baseline_valid);prod_exit_order();
    prod_reset();prod_clock_fail=1;prod_failure();prod_exit_order();
    output("PASS Command mismatch, route/ROM/alloc/A2/main/baseline/PLL/CHECK/PFB/clock/deadline failures and 98 exit order\n");
    for(u32 fault=1;fault<=3;fault++) {
        prod_reset();CHECK(!prod_start());prod_stop_fail=fault;
        CHECK(prod_return(&prod)==-1 && prod_stop_writes==3 && !prod.display.reusable);
        CHECK(prod_redraws==1);prod_exit_order();
    }
    prod_reset();CHECK(!prod_start());prod_fail_section=GF_DISPLAY_STOP;
    CHECK(prod_return(&prod)==-1 && prod_stop_writes==3 && prod_redraws==1);
    prod_exit_order();
    for(u32 fault=0;fault<6;fault++) {
        prod_reset();CHECK(!prod_start());
        if(fault<3)prod_cfg[13][0][4+fault]^=0x1000000;
        else if(fault==3)prod_cfg[13][0][0]^=1;
        else if(fault==4)prod_cfg[13][0][11]^=1;
        else prod_cfg[13][0][2]^=1;
        reads=prod_mmio_calls;
        CHECK(prod_return(&prod)==-1 && prod.revoked && prod_mmio_calls==reads && !prod_redraws);
        CHECK(prod_route_calls==2 && prod_start()==-1);
    }
    output("PASS cleanup failures attempt all quiesce writes; ID/subsys/revision/each BAR change denies MMIO and still attempts route00\n");
    prod_reset();u32 value;
    CHECK(gf_production_read(&prod,0,1,4,&value)==-1);
    CHECK(gf_production_read(&prod,1,GF_MODE_VRAM_BYTES,1,&value)==-1);
    CHECK(gf_production_read(&prod,2,0,4,&value)==-1);
    CHECK(gf_production_read(&prod,0,GF_PRODUCTION_BAR0_BYTES,4,&value)==-1);
    prod_cfg[13][0][0]^=1;CHECK(gf_production_identify(&prod_boot)==-1 && !prod_boot.bars[0].valid);
    prod_reset();prod_ram_fail=1;CHECK(gf_production_identify(&prod_boot)==-1);
    prod_reset();prod_cfg[13][0][5]=prod_cfg[13][0][4]|8;CHECK(gf_production_identify(&prod_boot)==-1);
    prod_reset();prod_cfg[13][0][4]=0x20000000;CHECK(gf_production_identify(&prod_boot)==-1);
    /* astra P4 counterexample: another device's enabled expansion ROM makes
     * the resource gate reject. Production prints nothing (no 98 text VRAM
     * before decode is proven safe); the reject is kept in the failure record. */
    prod_reset();prod_logs=0;prod_cfg[11][0][12]|=1U;
    CHECK(gf_production_identify(&prod_boot)==-1 && !prod_logs);
    CHECK(prod_boot.reject.reason && !kstrcmp(prod_boot.reject.reason,"rom_length") &&
          prod_boot.reject.dev==11 && !prod_boot.reject.fn &&
          prod_boot.reject.reg==PCI_CFG_ROM_ENDPOINT && prod_boot.reject.raw==prod_cfg[11][0][12]);
    CHECK(prod_start()==-1 && !prod_logs && !prod_cmd_writes && !prod_mmio_calls && prod_route_calls==1);
    CHECK(prod.reject.reason && !kstrcmp(prod.reject.reason,"rom_length") && prod.reject.dev==11);
    CHECK(prod.display.section==GF_DISPLAY_INIT && prod.display.last_rc==-1 &&
          prod.display.target==((11U<<3)<<8 | PCI_CFG_ROM_ENDPOINT) && !prod.display.reusable);
    prod_cfg[11][0][12]&=~1U;CHECK(!gf_production_identify(&prod_boot) && !prod_boot.reject.reason);
    output("PASS resource reject (other device ROM enabled): no console output, reason/BDF/reg kept in failure record\n");
    prod_reset();prod_map_fail=1;CHECK(gf_production_mapped(&prod)==-1);
    prod_reset();ledger_region_count=0;CHECK(gf_production_mapped(&prod)==-1);
    output("PASS resource/overlap/RAM/reservation/mapping/width/range gates\n");
    prod_reset();prod_clock_step=0;
    CHECK(gf_production_wait(&prod,1)==-1); /* finite stopped-clock path */
    prod_clock_step=1;CHECK(!gf_production_wait(&prod,1000));
    CHECK(gf_production_wait(&prod,1001)==-1);
    prod_reset();prod_now=U32_MAX-16;
    CHECK(!prod_start() && !prod_return(&prod));
    output("PASS finite wait, unavailable/frozen clock, elapsed wrap and baseline deadline\n");
}
#ifdef GF_HAL_TEST
#include "gf_production_hal_host.inc"
#endif
void _start(void)
{
#ifdef GF_HAL_TEST
    prod_hal_cases();
#endif
    prod_selection_tests();prod_cases();
    prod_reset();CHECK(!prod_start());
    gf_production_set_return(prod_return,&prod);
    sys_reboot();__builtin_unreachable();
}
