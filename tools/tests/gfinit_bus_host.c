/* Production decoder/rules/executor/accessor; only PCI/PTE and memory are fake. */
#define GF_S3_ENTRY s3_entry
#include "gfinit_exec_host.c"
#undef GF_S3_ENTRY
#define KDEBUG_WINDOW_HOST 1
#include "../../kdebug/kdebug_window.c"
#include "../../kdebug/gfinit_bus.c"
static u32 pci_id, pci_status, pci_writes, cfg_width, cfg_offset;
static u32 pte, mapped_pa, mapped_flags, irq_state, maps, restores, fault;
static struct gfinit_bus device;
static const struct recover_bus_access *active;
static u32 pci_read32(u32 bus,u32 dev,u32 fn,u32 off)
{
    CHECK(bus==0 && dev==13 && fn==0);
    if(off==0)return pci_id;
    if(off==4)return (pci_status<<16)|rawcommand;
    if(off==8)return 0xa1;
    if(off==0x2c)return 0x127010fc;
    if(off>=0x10 && off<=0x18)return rawbars[(off-0x10)/4];
    return 0;
}
static u16 pci_read16(u32 bus,u32 dev,u32 fn,u32 off)
{return (u16)(pci_read32(bus,dev,fn,off&~3U)>>((off&2)*8));}
static void pci_write32(u32 bus,u32 dev,u32 fn,u32 off,u32 value)
{
    CHECK(bus==0 && dev==13 && fn==0);pci_writes++;cfg_width=4;cfg_offset=off;
    if(off==4){rawcommand=value&65535;pci_status&=~(value>>16);}
    if(off>=0x10 && off<=0x18)rawbars[(off-0x10)/4]=value;
}
static int pci_write16(u32 bus,u32 dev,u32 fn,u32 off,u16 value)
{
    CHECK(bus==0 && dev==13 && fn==0);pci_writes++;cfg_width=2;cfg_offset=off;
    CHECK(off==4);rawcommand=value;return fault==4?-1:0;
}
static const struct gfinit_pci_ops pci_ops={pci_read32,pci_read16,pci_write32,pci_write16};
u32 recover_test_irq_save(void){u32 saved=irq_state;irq_state=0;return saved;}
void recover_test_irq_restore(u32 f){irq_state=f;}
u32 paging_pte_raw(u32 va){CHECK(va==MEM_DEVICE_APERTURE_BASE);return pte;}
int paging_map_phys(u32 va,u32 pa,u32 n,u32 flags)
{CHECK(va==MEM_DEVICE_APERTURE_BASE && n==1 && !irq_state);maps++;mapped_pa=pa;mapped_flags=flags;pte=pa|flags;return fault==1?-1:0;}
int paging_pte_restore(u32 va,u32 raw)
{CHECK(va==MEM_DEVICE_APERTURE_BASE && !irq_state);restores++;pte=raw;return fault==2?-1:0;}
static void physical(u32 va,u32 width,bool write)
{
    CHECK(active && !irq_state && width==active->width);
    u32 offset=active->offset;
    const struct recover_target *t=&profile.targets[active->key];
    if(!write && t->space==REC_VGA_DIRECT && t->port==0x3c2)offset+=0x3cc-0x3c2;
    CHECK(mapped_pa+(va-MEM_DEVICE_APERTURE_BASE)==(rawbars[active->bar]&~15U)+offset);
    CHECK(mapped_flags==(PTE_PRESENT|PTE_PCD|(write?PTE_RW:0)));
}
static int memory_read(void *v,u32 va,u32 width,u32 *out)
{(void)v;physical(va,width,false);return fault==3?-1:exec_bus_read(0,active,out);}
static int memory_write(void *v,u32 va,u32 width,u32 value)
{
    (void)v;physical(va,width,true);
    if(fault==3)return -1;
    int rc=exec_bus_write(0,active,value);
    const struct recover_target *t=&profile.targets[active->key];
    if(active->dir_substep==REC_DATA && t->port==RECOVER_CRTC && t->index==RECOVER_CR1F) {
        CHECK(value==RECOVER_CR1F_UNLOCK || value==RECOVER_CR1F_LOCK);
        values[active->key]=value==RECOVER_CR1F_UNLOCK?RECOVER_CR1F_UNLOCKED:0;
    }
    return rc;
}
static struct recover_window_ops memory_ops;
static int bus_read_test(void *v,const struct recover_bus_access *a,u32 *out)
{(void)v;active=a;return gfinit_bus_read(&device,a,out);}
static int bus_write_test(void *v,const struct recover_bus_access *a,u32 value)
{(void)v;active=a;return gfinit_bus_write(&device,a,value);}
static void bus_setup(void)
{
    setup();profile.layouts=gf_fixture_profile.layouts;profile.layout_count=gf_fixture_profile.layout_count;
    profile.expected_bdf=RECOVER_GF_BDF;profile.expected_device=RECOVER_GF_DEVICE;ident.bdf=RECOVER_GF_BDF;
    pci_id=RECOVER_GF_DEVICE;pci_status=0xa5a5;pci_writes=maps=restores=fault=0;
    pte=0x876540a5;irq_state=1;window_failed=window_busy=false;
    gfinit_bus_init(&device,&profile,&pci_ops);
    CHECK(gfinit_bus_probe(&device)==0 && !pci_writes);
    memory_ops=*device.window.ops;memory_ops.load=memory_read;memory_ops.store=memory_write;device.window.ops=&memory_ops;
}
static struct recover_bus_access address(u32 key,u32 dir)
{
    struct recover_bus_access a;
    CHECK(recover_target_address(&profile.targets[key],dir,&a)==0);a.key=key;return a;
}
static void accessor_tests(void)
{
    for(u32 key=0;key<gf_fixture_profile.target_count;key++) {
        bus_setup();
        const struct recover_target *t=&profile.targets[key];
        if(t->space==REC_PCI_CFG)continue;
        struct recover_bus_access a=address(key,t->space==REC_VGA_INDEXED?REC_SELECT:REC_READ);
        u32 value=0;CHECK(bus_read_test(0,&a,&value)==0);
        CHECK(maps==1 && restores==1 && pte==0x876540a5 && irq_state==1);
        if(t->space==REC_VGA_INDEXED) {
            CHECK(a.offset==(t->port==0x3d4?0x601000U:0xc0000U)+t->head*0x2000+t->port);
            CHECK(bus_write_test(0,&a,t->index)==0);
            a=address(key,REC_DATA);CHECK(bus_write_test(0,&a,t->index==RECOVER_CR1F?RECOVER_CR1F_UNLOCK:0x5a)==0);
        }
        a.offset++;u32 before=maps;CHECK(bus_read_test(0,&a,&value)<0 && maps==before);
    }
    bus_setup();struct recover_bus_access command=address(6,REC_WRITE);
    CHECK(bus_write_test(0,&command,0x102)==0 && cfg_width==2 && cfg_offset==4 && pci_status==0xa5a5);
    for(u32 i=0;i<3;i++) {
        struct recover_bus_access bar=address(7+i,REC_WRITE);u32 saved=rawbars[i],value;
        CHECK(bus_write_test(0,&bar,~0U)==0 && rawbars[i]==~0U && cfg_width==4);
        bar.dir_substep=REC_READ;CHECK(bus_read_test(0,&bar,&value)==0 && value==~0U);
        bar.dir_substep=REC_WRITE;CHECK(bus_write_test(0,&bar,saved)==0 && rawbars[i]==saved);
    }
    for(u32 bad=0;bad<5;bad++) {
        bus_setup();struct recover_bus_access a=address(4,REC_READ);u32 value=0;
        if(bad==0)pci_id=~0U;
        if(bad==1)rawcommand=0;
        if(bad==2)rawbars[0]^=0x1000;
        if(bad==3)rawbars[1]^=0x1000;
        if(bad==4)a.space=REC_PC98_IO;
        CHECK(bus_read_test(0,&a,&value)<0 && !maps);
    }
    bus_setup();pci_id=~0U;
    struct recover_bus_access cfg=address(6,REC_READ);u32 absent=0;
    CHECK(bus_read_test(0,&cfg,&absent)<0 && !pci_writes);
    cfg.dir_substep=REC_WRITE;CHECK(bus_write_test(0,&cfg,0x102)<0 && !pci_writes);
    for(u32 type=1;type<=4;type+=3) {
        bus_setup();device.ready=false;rawbars[0]|=type;
        CHECK(gfinit_bus_probe(&device)<0 && !device.ready);
    }
    bus_setup();
    struct recover_target legacy_targets[RECOVER_TARGET_MAX];
    for(u32 i=0;i<profile.target_count;i++)legacy_targets[i]=profile.targets[i];
    profile.targets=legacy_targets;legacy_targets[4].offset=0;
    rawbars[0]=device.bars[0]=0xa0000;
    struct recover_bus_access legacy=address(4,REC_READ);u32 legacy_value=0;
    CHECK(bus_read_test(0,&legacy,&legacy_value)<0 && !maps);
    for(u32 bad=1;bad<=3;bad++) {
        bus_setup();fault=bad;struct recover_bus_access a=address(4,REC_READ);u32 value=0;
        CHECK(bus_read_test(0,&a,&value)<0 && restores==1 && irq_state==1 && !window_busy);
        CHECK(recover_window_cleanup(&device.window)<0 && restores==1);
        if(bad==2) {
            struct recover_window fresh;recover_window_hal_init(&fresh);
            CHECK(kdebug_window_begin(&fresh,0x24000000,4,false)<0 && maps==1);
        }
    }
    bus_setup();CHECK(kdebug_window_begin(&device.window,0x24000000,4,false)==0);
    struct recover_window other;recover_window_hal_init(&other);
    CHECK(kdebug_window_begin(&other,0x25000000,4,true)<0 && maps==1 && !irq_state);
    CHECK(recover_window_cleanup(&device.window)==0 && irq_state==1);
    output("bus: all descriptor addresses, head aliases, Command16/status, BAR probe, raw mismatch, PTE faults/busy\n");
}
static struct recover_plan_entry combined_entries[6];
static struct recover_plan_dep combined_deps[2];
static void cr1f_two_visit_test(u32 first, u32 initial_lock)
{
    u32 selects=0, data=0;
    for(u32 i=first;i<actual_count;i++) {
        const struct recover_target *t=&profile.targets[actual[i].key];
        if(t->space!=REC_VGA_INDEXED || t->port!=RECOVER_CRTC ||
           t->index!=RECOVER_CR1F)continue;
        if(actual[i].dir==REC_SELECT)selects++;
        if(actual[i].dir==REC_DATA) {
            CHECK(actual[i].value==(data==0 || initial_lock==RECOVER_CR1F_UNLOCKED ?
                RECOVER_CR1F_UNLOCK:RECOVER_CR1F_LOCK));
            data++;
        }
    }
    /* k=0 unlock and k=3 restore both visit CR1F. Each visit performs
     * SELECT and DATA, each with write/readback BEGIN/END: 16 REC rows. */
    CHECK(selects==2 && data==2);
    output("bus: CR1F two visits, SELECT/DATA twice, 16 access REC rows\n");
}
static void production_plan(u32 initial_lock)
{
    bus_setup();fixture();
    CHECK(gfinit_decode_rom(rom,sizeof(rom),&profile,&decoded)==0 && decoded.plan.unresolved_count==0);
    const struct recover_plan *prep=&recover_gf_prepare_plan;
    u32 nn=prep->node_count, na=prep->arg_count;
    for(u32 i=decoded.plan.node_count;i>0;i--) {
        struct recover_plan_node n=decoded.nodes[i-1];n.arg_first+=na;
        for(u32 j=0;j<2;j++)if(n.next[j]!=RECOVER_NONE)n.next[j]+=nn;
        decoded.nodes[i-1+nn]=n;
    }
    for(u32 i=decoded.plan.arg_count;i>0;i--)decoded.args[i-1+na]=decoded.args[i-1];
    for(u32 i=0;i<nn;i++)decoded.nodes[i]=prep->nodes[i];
    decoded.nodes[nn-1].next[0]=decoded.nodes[nn-1].next[1]=nn;
    for(u32 i=0;i<na;i++)decoded.args[i]=prep->args[i];
    for(u32 i=0;i<decoded.plan.capture_count;i++) {
        decoded.captures[i].producer_node+=nn;decoded.captures[i].consumer_node+=nn;
    }
    combined_entries[0]=prep->entries[0];
    for(u32 i=0;i<5;i++){combined_entries[i+1]=decoded.entries[i];combined_entries[i+1].first+=nn;}
    decoded.plan.entries=combined_entries;decoded.plan.entry_count=6;
    decoded.plan.node_count+=nn;decoded.plan.arg_count+=na;
    for(u32 i=nn;i<decoded.plan.node_count;i++) {
        const struct recover_rule *w=rule_by_id(&profile,decoded.nodes[i].rule_id);
        if(w && (w->flags&REC_N1)) {
            combined_deps[0]=(struct recover_plan_dep){i,0,0};
            combined_deps[1]=(struct recover_plan_dep){i,1,1};
        }
    }
    decoded.plan.deps=combined_deps;decoded.plan.dep_count=2;
    for(u32 i=0;i<decoded.plan.node_count;i++)if(!node_ok(&profile,&decoded.plan,i)) {
        output("bad production node ");number(i);output(" pc ");number(decoded.nodes[i].pc);output("\n");
    }
    CHECK(contract_bind()==0);exec_arm();
    executor_bus.read=bus_read_test;executor_bus.write=bus_write_test;
    /* Guard values and retained locks are readable; reference interpreter
     * receives the same deterministic initial register contents. */
    for(u32 i=0;i<profile.target_count;i++)reference_values[i]=values[i]=0;
    values[11]=initial_lock;
    ref_head=ref_skip=0;
    for(u32 i=0;i<nn;i++) {
        u32 value;
        CHECK(recover_plan_enter(&core,i)==0);
        CHECK(recover_plan_access(&core,&value)==0);
        CHECK(recover_plan_leave(&core,0)==0);
    }
    u32 total=0;
    for(u32 main=0;main<5;main++) {
        for(u32 i=0;i<profile.target_count;i++)reference_values[i]=values[i];
        ref_head=ref_skip=actual_count=expected_count=0;
        reference(decoded.nodes[combined_entries[main+1].first].pc,0);
        execution.main_id=main;
        int rc=gfinit_exec_run(&core,&execution);
        if(rc){output("production pc/reason ");number(execution.fail_pc);output("/");number(core.reason);output("\n");}
        CHECK(rc==0);if(rc)break;
        compare_trace();total+=actual_count;
    }
    CHECK(total>100 && !core.capture_valid && pte==0x876540a5 && irq_state==1);
    core.state=REC_RESTORING;core.auth_phase=REC_RESTORE;core.in_opcode=false;core.restore_tick=0;
    CHECK(!cleanup_budget_prepare(&core));
    CHECK(core.local_saved_count>0);
    u32 saved_count=core.local_saved_count;
    struct recover_local_saved saved[RECOVER_MAX_REGS];
    for(u32 i=0;i<saved_count;i++)saved[i]=core.local_saved[i];
    u32 cleanup_first=actual_count;
    CHECK(gfinit_bus_local_cleanup(&device,&core)==0 && !core.local_saved_count);
    cr1f_two_visit_test(cleanup_first,initial_lock);
    const u32 cleanup_keys[]={11,12,10,11};
    const u32 cleanup_values[]={RECOVER_CR1F_UNLOCK,0,0,
        initial_lock==RECOVER_CR1F_UNLOCKED?RECOVER_CR1F_UNLOCK:RECOVER_CR1F_LOCK};
    CHECK(actual_count>=cleanup_first+9);
    for(u32 i=0;i<4;i++) {
        CHECK(actual[cleanup_first+2*i].dir==REC_SELECT && actual[cleanup_first+2*i].key==cleanup_keys[i]);
        CHECK(actual[cleanup_first+2*i+1].dir==REC_DATA && actual[cleanup_first+2*i+1].key==cleanup_keys[i]);
        CHECK(actual[cleanup_first+2*i+1].value==cleanup_values[i]);
    }
    CHECK(actual[cleanup_first+8].dir==REC_SELECT);
    for(u32 i=0;i<saved_count;i++)if(saved[i].dir_substep==REC_DATA)CHECK(values[saved[i].key]==saved[i].value);
    u32 count=actual_count;CHECK(gfinit_bus_local_cleanup(&device,&core)==0 && actual_count==count);
    output("bus: local CR11/lock/owner and original indices restored once, COLD data not replayed\n");
    output("bus: full production plan through S3, COPY/capture permissions, no unresolved rows\n");
}
/* Independently fake the live PCI configuration used for exclusions. */
#define GFINIT_RESOURCES_HOST 1
#include "../../kdebug/gfinit_resources.c"
#include "gfinit_resources_ra266.h"
static u32 resource_cfg[32][8][16];
static u32 resource_writes, resource_extra_reads;
void pci_cfg_write32(u32 bus,u32 dev,u32 fn,u32 reg,u32 value)
{(void)bus;(void)dev;(void)fn;(void)reg;(void)value;resource_writes++;}
int pci_cfg_write16(u32 bus,u32 dev,u32 fn,u32 reg,u16 value)
{(void)bus;(void)dev;(void)fn;(void)reg;(void)value;resource_writes++;return 0;}
static int resource_run(struct recover_range *ranges,u32 *n)
{int rc=gfinit_resources(ranges,n);CHECK(resource_writes==0);return rc;}

static const char *resource_reason;
static u32 resource_dev, resource_fn, resource_reg, resource_raw;
void __cdecl kprintf(u8 attr, const char *fmt, ...)
{
    __builtin_va_list ap;
    (void)attr; (void)fmt;
    __builtin_va_start(ap, fmt);
    resource_reason=__builtin_va_arg(ap,const char *);
    resource_dev=__builtin_va_arg(ap,u32);resource_fn=__builtin_va_arg(ap,u32);
    resource_reg=__builtin_va_arg(ap,u32);resource_raw=__builtin_va_arg(ap,u32);
    __builtin_va_end(ap);
}
u32 pci_cfg_read32(u32 bus,u32 dev,u32 fn,u32 reg)
{CHECK(bus==0 && dev<32 && fn<8 && reg<64);if(fn)resource_extra_reads++;return resource_cfg[dev][fn][reg/4];}
u16 pci_cfg_read16(u32 bus,u32 dev,u32 fn,u32 reg)
{return (u16)(pci_cfg_read32(bus,dev,fn,reg&~3U)>>((reg&2)*8));}
static void resource_setup(void)
{
    CHECK(resource_writes==0);resource_writes=0;resource_reason=0;resource_extra_reads=0;
    for(u32 d=0;d<32;d++)for(u32 f=0;f<8;f++)for(u32 r=0;r<16;r++)resource_cfg[d][f][r]=r?0:~0U;
}
static void resource_tests(void)
{
    struct recover_range ranges[RECOVER_RANGE_MAX];u32 n=123;
    resource_setup();CHECK(resource_run(ranges,&n)<0 && n==0);
    resource_cfg[13][0][0]=GF_RES_GPU_ID;
    CHECK(resource_run(ranges,&n)==0 && n==0);
    resource_cfg[2][0][0]=0x12348086;
    resource_cfg[2][0][3]=PCI_HDR_MULTIFUNCTION<<16;
    resource_cfg[2][7][0]=0x12298086;resource_cfg[2][7][1]=2;
    resource_cfg[2][7][4]=0x20410000;
    CHECK(resource_run(ranges,&n)<0 && n==0); /* Known ID, incorrect prefetch attribute. */
    CHECK(resource_reason && resource_reason[0]=='m' && resource_dev==2 && resource_fn==7 &&
          resource_reg==PCI_CFG_BAR0 && resource_raw==0x20410000);
    resource_cfg[2][7][1]=0;CHECK(resource_run(ranges,&n)==0);
    resource_cfg[2][7][1]=2;resource_cfg[2][7][4]=0x6001;
    CHECK(resource_run(ranges,&n)==0);
    resource_cfg[2][7][12]=1;CHECK(resource_run(ranges,&n)<0);
    resource_cfg[2][7][12]=0;
    resource_cfg[2][7][3]=1U<<16;
    resource_cfg[2][7][8]=0x21f02000; /* [20000000,22000000) */
    resource_cfg[2][7][9]=0x31f13001; /* 64-bit prefetch, zero upper halves */
    CHECK(resource_run(ranges,&n)==0 && n==2);
    CHECK(ranges[0].base==0x20000000 && ranges[0].len==0x2000000);
    CHECK(ranges[1].base==0x30000000 && ranges[1].len==0x2000000);
    resource_cfg[2][7][10]=1;
    CHECK(resource_run(ranges,&n)<0 && n==0);
    resource_cfg[2][7][10]=0;resource_cfg[2][7][9]=0x31f03001;
    CHECK(resource_run(ranges,&n)<0 && n==0);
    resource_cfg[2][7][9]=0x0000fff0; /* disabled */
    CHECK(resource_run(ranges,&n)==0 && n==1);
    resource_cfg[2][7][8]=0xfff00000; /* 4 GiB length cannot fit u32 */
    CHECK(resource_run(ranges,&n)<0 && n==0);
    resource_cfg[2][7][8]=0x21f02000;
    n=RECOVER_RANGE_MAX-1U;
    CHECK(resource_window(ranges,&n,2,7,PCI_CFG_MEM_WINDOW)==0 && n==RECOVER_RANGE_MAX);
    CHECK(ranges[RECOVER_RANGE_MAX-1U].base==0x20000000 &&
          ranges[RECOVER_RANGE_MAX-1U].len==0x2000000);
    CHECK(resource_window(ranges,&n,2,7,PCI_CFG_MEM_WINDOW)<0 && n==RECOVER_RANGE_MAX);
    output("resources: live scan, unknown BAR reject, disabled/IO skip, bridge windows and bounds PASS\n");
}

static bool resource_text(const char *a,const char *b)
{
    if(!a)return false;
    while(*a && *a==*b){a++;b++;}
    return *a==*b;
}
static void resource_snapshot(void)
{
    resource_setup();
    for(u32 i=0;i<ARRAY_SIZE(resource_ra266);i++)
        for(u32 r=0;r<16;r++)
            resource_cfg[resource_ra266[i].dev][resource_ra266[i].fn][r]=resource_ra266[i].cfg[r];
}
static void resource_expect(const char *reason,u32 dev,u32 fn,u32 reg,u32 raw)
{
    struct recover_range ranges[RECOVER_RANGE_MAX];u32 n=123;
    CHECK(resource_run(ranges,&n)<0 && n==0);
    if(!resource_text(resource_reason,reason)){output("expected ");output(reason);output(" got ");output(resource_reason?resource_reason:"(none)");output("\n");}
    CHECK(resource_text(resource_reason,reason));
    CHECK(resource_dev==dev && resource_fn==fn && resource_reg==reg && resource_raw==raw);
}
static void resource_barlen_tests(void)
{
    struct recover_range ranges[RECOVER_RANGE_MAX];u32 n=0;
    const struct recover_range expected[]={
        {0x20000000,0x400000},{0x20400000,0x10000},{0x20800000,0x800000},
        {0x20410000,0x1000},{0x20500000,0x100000}};
    resource_snapshot();CHECK(resource_run(ranges,&n)==0 && n==ARRAY_SIZE(expected));
    for(u32 i=0;i<n;i++)CHECK(ranges[i].base==expected[i].base && ranges[i].len==expected[i].len);
    /* Ra266 single-function Trident aliases fn0 at every higher function.
     * Both passes must skip those reads and preserve all BARLEN ranges. */
    for(u32 f=1;f<PCI_FN_COUNT;f++)for(u32 r=0;r<16;r++)
        resource_cfg[8][f][r]=resource_cfg[8][0][r];
    resource_extra_reads=0;
    CHECK(resource_run(ranges,&n)==0 && n==ARRAY_SIZE(expected));
    CHECK(resource_extra_reads==0);
    for(u32 i=0;i<n;i++)CHECK(ranges[i].base==expected[i].base && ranges[i].len==expected[i].len);
    /* With bit7 set fn1 is real, and overlapping BARs still reject. */
    resource_cfg[8][0][3]|=PCI_HDR_MULTIFUNCTION<<16;
    resource_expect("memory_bar_alias",8,1,PCI_CFG_BAR0,0x20000000);
    /* An absent fn0 skips even a responding higher function. */
    resource_snapshot();resource_cfg[2][1][0]=0x12348086;
    resource_cfg[2][1][1]=2;resource_cfg[2][1][4]=0x20000000;
    resource_extra_reads=0;
    CHECK(resource_run(ranges,&n)==0 && n==ARRAY_SIZE(expected));
    CHECK(resource_extra_reads==0);
    output("resources: single-function aliases skipped, multifunction overlap rejected, absent fn0 skipped PASS\n");
    resource_snapshot();
    resource_cfg[8][0][1]=0; /* B2: only Command changes. */
    CHECK(resource_run(ranges,&n)==0 && n==2);
    for(u32 i=0;i<n;i++)CHECK(ranges[i].base==expected[i+3].base && ranges[i].len==expected[i+3].len);
    struct resource_bounds b;
    CHECK(resource_collect_bounds(&b)==0 && b.count==2);
    resource_snapshot();CHECK(resource_collect_bounds(&b)==0 && b.count==5);
    for(u32 i=0;i<b.count;i++)CHECK(b.base[i]!=0x21000000 && b.base[i]!=0x22000000 && b.base[i]!=0x24000000);
    /* Disabled ROM and disabled device must never shrink the bound (C1). */
    resource_cfg[8][0][12]=0x20800800;
    resource_cfg[7][0][1]=0;resource_cfg[7][0][4]=0x20801000;
    CHECK(resource_run(ranges,&n)==0 && ranges[2].len==0x800000);
    resource_snapshot();resource_cfg[11][0][0]=0x12348086;
    resource_expect("memory_bar_length",11,0,0x10,0x20410008);
    resource_snapshot();resource_cfg[8][0][2]=0xd2;
    resource_expect("memory_bar_length",8,0,0x10,0x20000000);
    resource_snapshot();resource_cfg[8][0][7]=0x21000000;
    resource_expect("memory_bar_length",8,0,0x1c,0x21000000);
    resource_snapshot();resource_cfg[8][0][4]|=8;
    resource_expect("memory_bar_attr",8,0,0x10,0x20000008);
    for(u32 type=0;type<=6;type+=2){
        resource_snapshot();resource_cfg[11][0][4]=0x20410000|type;
        resource_expect(type?"memory_bar_type":"memory_bar_attr",11,0,0x10,0x20410000|type);
    }
    resource_snapshot();resource_cfg[11][0][7]=resource_cfg[11][0][6];resource_cfg[11][0][6]=0;
    resource_expect("memory_bar_length",11,0,0x1c,0x20500000);
    resource_snapshot();resource_cfg[11][0][6]=0x20400000;
    resource_expect("memory_bar_alias",11,0,0x18,0x20400000);
    resource_snapshot();resource_cfg[8][0][5]=resource_cfg[8][0][4];
    resource_expect("memory_bar_alias",8,0,0x14,0x20000000);
    /* C1 replaces the old ROM-bound contradiction with a live memory BAR. */
    resource_snapshot();resource_cfg[8][0][6]=0x20580000;
    resource_expect("memory_bar_bound",11,0,0x18,0x20500000);
    resource_snapshot();resource_cfg[11][0][6]=0x20580000;
    resource_expect("memory_bar_range",11,0,0x18,0x20580000);
    resource_setup();resource_cfg[13][0][0]=GF_RES_GPU_ID;
    resource_cfg[8][0][0]=GF_RES_TRIDENT_ID;resource_cfg[8][0][1]=2;
    resource_cfg[8][0][2]=0xd3;resource_cfg[8][0][4]=0x20000000;
    CHECK(resource_run(ranges,&n)==0 && n==1 && ranges[0].len==0x20000000);
    resource_cfg[8][0][4]=0xfffffff0;
    CHECK(resource_run(ranges,&n)==0 && n==1 && ranges[0].len==16);
    /* A 64-bit BAR's high half is not a second candidate. */
    resource_cfg[8][0][4]=0x20000004;resource_cfg[8][0][5]=0x20000000;
    CHECK(resource_collect_bounds(&b)==0 && b.count==0);
    resource_expect("memory_bar_type",8,0,0x10,0x20000004);
    resource_setup();u32 assigned=0;
    for(u32 d=0;d<32;d++)for(u32 f=0;f<8;f++){
        if(d==13 && f==0)continue;
        resource_cfg[d][f][0]=0x12348086;resource_cfg[d][f][1]=2;
        resource_cfg[d][0][3]=PCI_HDR_MULTIFUNCTION<<16;
        resource_cfg[d][f][4]=(++assigned)*0x1000;
    }
    resource_cfg[13][0][0]=GF_RES_GPU_ID;
    resource_cfg[0][0][5]=(++assigned)*0x1000;
    CHECK(resource_collect_bounds(&b)==0 && b.count==RECOVER_RANGE_MAX);
    resource_cfg[0][0][6]=(++assigned)*0x1000;
    CHECK(resource_collect_bounds(&b)<0 && resource_text(resource_reason,"memory_bar_count"));
    const u32 bad_len[]={0,3,0x2000};
    for(u32 i=0;i<ARRAY_SIZE(bad_len);i++){
        n=0;CHECK(resource_append(ranges,&n,1,0,0,0x1008,bad_len[i],0x10000,true)<0 && n==0);
    }
    n=0;CHECK(resource_append(ranges,&n,1,0,0,0xfffff008,0x1000,0x1000,true)==0 && n==1);
    n=0;CHECK(resource_append(ranges,&n,1,0,0,0xfffff008,0x2000,0x2000,false)<0 && n==0);
    n=0;CHECK(resource_append(ranges,&n,1,0,0,8,0x1000,0x1000,true)<0 && n==0);
    n=RECOVER_RANGE_MAX;CHECK(resource_append(ranges,&n,1,0,0,0x1008,0x1000,0x1000,true)<0);
    /* Table invariants: unique ID:BAR and nonzero, power-of-two exact lengths. */
    for(u32 i=0;i<ARRAY_SIZE(resource_bar_specs);i++){
        const struct resource_bar_spec *a=&resource_bar_specs[i];
        CHECK(a->len && !(a->len&(a->len-1)) && a->bar<PCI_CFG_BAR_COUNT);
        for(u32 j=0;j<i;j++)CHECK(a->id!=resource_bar_specs[j].id || a->bar!=resource_bar_specs[j].bar);
    }
    CHECK(resource_writes==0);
    output("resources: BARLEN baseline/B2, C1-C3, exact lengths, bounds, aliases, trace and zero writes PASS\n");
}

static struct recover_range probe_ranges[RECOVER_RANGE_MAX];
static u32 probe_range_count;
static int resource_size_operation(struct recover *r,void *arg)
{
    (void)arg;
    for(u32 i=0;i<plan.node_count;i++) {
        const struct recover_plan_node *node=&plan.nodes[i];
        const struct recover_opcode_layout *l=layout_for(&profile,node);
        u32 key=plan.args[node->arg_first].value,value=0;
        if(recover_plan_enter(r,i))return -1;
        if(node->rule_id!=RECOVER_NONE){
            const struct recover_rule *w=rule_by_id(&profile,node->rule_id);
            if(!evaluate(r,node,w,0,&value))return -1;
        }
        if(recover_access(r,i,l->dir_substep,key,gf_fixture_profile.targets[key].width,&value))return -1;
        if(l->dir_substep==REC_READ && recover_bar_probe(r,key-7,r->bars[key-7].raw,
                value,probe_ranges,probe_range_count,0,0))return -1;
        if(recover_plan_leave(r,0))return -1;
    }
    return 0;
}
static void resource_probe(bool collision)
{
    CHECK(resource_run(probe_ranges,&probe_range_count)==0);
    setup();
    /* Separate synthetic BAR2 so BAR0 upper adjacency can be tested. */
    rawbars[2]=values[9]=0x26000000;core.cache.bar[2].raw=rawbars[2];
    profile.layouts=gf_fixture_profile.layouts;profile.layout_count=gf_fixture_profile.layout_count;
    profile.crc=recover_profile_crc(&profile);
    plan=recover_gf_size_bars_plan;plan.profile_crc=profile.crc;
    CHECK(recover_bind_plan(&core,&profile,&plan,&buffers,&hash)==0 && !writes);
    CHECK(trial_begin(&core,&profile,hash,1000)==0);
    int rc=trial_step(&core,&core.token,true,resource_size_operation,0);
    if((rc<0)!=collision){output("probe expected collision=");number(collision);output(" reason/key=");number(core.reason);output("/");number(core.first_key);output("\n");}
    CHECK((rc<0)==collision);
    if(collision){
        CHECK(core.failed && core.reason==REC_CONTRACT && core.state==REC_FAILED && cleanup_calls==1);
        CHECK(!core.cache.bar[0].valid && !core.measurement.bar[0].valid);
    }else{
        CHECK(trial_end(&core,&core.token,REC_NORMAL)==0 && core.cache.bar[0].valid);
    }
    /* Failure uses the existing recover cleanup, which does not replay an
     * incomplete BAR measurement. Successful measurement restores all BARs. */
    if(!collision)CHECK(rawcommand==0x102 && rawbars[0]==0x24000000 &&
        rawbars[1]==0x30000008 && rawbars[2]==0x26000000);
    CHECK(resource_writes==0);
}
static void resource_probe_tests(void)
{
    /* All GPU masks come from write_fake's synthetic ff000000, never dump data. */
    resource_snapshot();resource_probe(false);
    resource_snapshot();resource_cfg[11][0][6]=0x24000000;resource_probe(true);
    /* Exclusion strictly inside the 16 MiB GPU BAR, then at either boundary. */
    resource_snapshot();resource_cfg[11][0][6]=0x24800000;resource_probe(true);
    resource_snapshot();resource_cfg[11][0][6]=0x23f00000;resource_probe(false);
    resource_snapshot();resource_cfg[11][0][6]=0x25000000;resource_probe(false);
    resource_setup();resource_cfg[13][0][0]=GF_RES_GPU_ID;
    resource_cfg[8][0][0]=GF_RES_TRIDENT_ID;resource_cfg[8][0][1]=2;
    resource_cfg[8][0][2]=0xd3;resource_cfg[8][0][4]=0x20000000;
    resource_probe(true); /* Lone Trident bound is 512 MiB and reaches the GPU. */
    struct resource_bounds b={{0x11800},1};u32 n=0;
    CHECK(resource_cap(&b,0x10000)==0x1800);
    CHECK(resource_append(probe_ranges,&n,1,0,0,0x10000,0x1800,0x1800,false)==0);
    output("resources: real SIZE_BARS/probe, containment/adjacency, access_error cleanup and restoration PASS\n");
}

#ifndef GF_S5_ENTRY
#define GF_S5_ENTRY _start
#endif
void GF_S5_ENTRY(void)
{
    gf_fixture_init();
    resource_tests();resource_barlen_tests();resource_probe_tests();accessor_tests();production_plan(0);production_plan(RECOVER_CR1F_UNLOCKED);
    output(failures?"GF S5 FAIL\n":"GF S5 PASS\n");
    __asm__ volatile("int $0x80"::"a"(1),"b"(failures?1:0):"memory");for(;;){}
}
