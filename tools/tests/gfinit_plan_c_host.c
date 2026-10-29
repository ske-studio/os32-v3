/* Production lowering/bind/access with synthetic device values. Every C
 * section runs through its real access/control operation, without barriers. */
#define GF_S5_ENTRY c_old_bus_entry
#include "gfinit_bus_host.c"
#undef GF_S5_ENTRY
#include "../../kdebug/gfinit_plan.c"
#include "../../kdebug/gfinit_mode.c"
#include "../../kdebug/gfinit_plan_c.c"
#include "../../kdebug/gfinit_verify.inc"
#include "../../kdebug/recover_profile.c"
static struct gfinit_plan composed;
static u8 frame[GF_MODE_VRAM_BYTES];
static u32 frame_writes, frame_reads, drains, frame_fail_at, frame_abort_at, frame_points;
static bool vram_active, color_active;
static u32 color_count, color_fail_at, color_row, color_events[850][3];
static int color_operation(void *v,struct recover *r,u32 row,u32 value)
{
    (void)v;color_active=true;color_row=row;
    int rc=gfinit_plan_c_color(&device,r,row,value);
    color_active=false;return rc;
}
static int color_memory(bool write,u32 va,u32 width,u32 *value)
{
    u32 pa=mapped_pa+va-MEM_DEVICE_APERTURE_BASE, off=pa-(rawbars[0]&~15U);
    CHECK(width==1 && !irq_state && color_count<850);
    color_events[color_count][0]=write;color_events[color_count][1]=off;color_events[color_count][2]=*value;
    return color_count++==color_fail_at ? -1 : 0;
}
/* Independent pixel oracle: frame > 8x8 checker > eight vertical bands. */
static u32 frame_pixel(u32 off)
{
    u32 x=off%640,y=off/640;
    if(x<8 || x>=632 || y<8 || y>=472)return 4;
    if(x<72 && y<72)return (((x-8)/8+(y-8)/8)&1)?0:4;
    return x/80;
}
static int frame_load(void *v,u32 va,u32 width,u32 *value)
{
    if(color_active)return color_memory(false,va,width,value);
    u32 pa=mapped_pa+va-MEM_DEVICE_APERTURE_BASE,base=rawbars[1]&~15U;
    if(pa>=base && pa-base<sizeof(frame)) {
        u32 off=pa-base;
        CHECK(!irq_state && width==4 && mapped_flags==(PTE_PRESENT|PTE_PCD));
        CHECK(frame_writes==sizeof(frame) && drains==1 && off==frame_reads);
        *value=0;
        for(u32 i=0;i<4;i++)*value|=(u32)frame[off+i]<<(8*i);
        frame_reads+=4;
        return off==frame_fail_at?-1:0;
    }
    if(vram_active && pa==(rawbars[0]&~15U)+GF_PLAN_PFB_AMOUNT) {
        CHECK(!irq_state && width==4 && frame_writes==sizeof(frame) && !frame_reads);
        drains++;*value=0x10000000;return 0;
    }
    return memory_read(v,va,width,value);
}
static int frame_store(void *v,u32 va,u32 width,u32 value)
{
    if(color_active)return color_memory(true,va,width,&value);
    u32 pa=mapped_pa+va-MEM_DEVICE_APERTURE_BASE,base=rawbars[1]&~15U;
    if(pa>=base && pa-base<sizeof(frame)) {
        u32 off=pa-base;
        CHECK(!irq_state && width==4 && mapped_flags==(PTE_PRESENT|PTE_PCD|PTE_RW));
        CHECK(off==frame_writes && !frame_reads);
        for(u32 i=0;i<4;i++) {
            frame[off+i]=(u8)(value>>(8*i));
            CHECK(frame[off+i]==frame_pixel(off+i));
        }
        frame_writes+=4;
        return off==frame_fail_at?-1:0;
    }
    return memory_write(v,va,width,value);
}
static int frame_point(void *v,u32 sid)
{
    (void)v;(void)sid;
    if(vram_active) {
        CHECK(irq_state==1 && pte==0x876540a5 && !device.window.owned && !window_busy);
        frame_points++;
        if(frame_writes==frame_abort_at || frame_reads==frame_abort_at)return 1;
    }
    return 0;
}
static int frame_operation(void *v,struct recover *r,bool write,u32 *bad,u32 *want,u32 *got)
{
    (void)v;vram_active=true;
    int rc=gfinit_plan_c_vram(&device,r,write,bad,want,got);
    vram_active=false;
    return rc;
}
/* Same reservation callbacks as S0; forbid whole-screen operations in RUN. */
static u32 route_value,route_writes,route_reads,route_fault,route_mode_reads,route_mode_writes,route_noise;
static u32 cell_value,cell_calls,cell_writes,cell_fault,cell_runs,cell_trace[8];
static int c_route_read(void *v,u32 *value)
{ (void)v;route_reads++;if(recover_mode_route_active(&core))route_mode_reads++;*value=(route_value | route_noise) ^ (route_fault==1 ? 2U : 0U);return route_fault==3 ? -1 : 0; }
static int c_route_write(void *v,u32 value)
{
    (void)v;
    if(value!=0 && (value!=1 || !recover_mode_route_active(&core)))return -1;
    route_writes++;if(value==1)route_mode_writes++;route_value=value;return route_fault==2 ? -1 : 0;
}
static int cell_read(void *v,u32 *value)
{
    (void)v;CHECK(cell_calls<8);cell_trace[cell_calls++]=0;
    *value=cell_value;
    if((cell_fault==1 && cell_calls==3) || (cell_fault==3 && cell_calls==5))*value^=1;
    return cell_fault==4 && cell_calls==1 ? -1 : 0;
}
static int cell_write(void *v,u32 value)
{
    (void)v;CHECK(cell_calls<8);cell_trace[cell_calls++]=value;
    cell_writes++;cell_value=value;
    return cell_fault==2 && cell_writes==1 ? -1 : 0;
}
static const struct recover_screen_ops cell_ops={.read_cell=cell_read,.write_cell=cell_write};
static int c_cell(void *v,struct recover *r,u32 *actual)
{
    (void)v;CHECK(r->state==REC_RUNNING && (route_value&3)==1);
    cell_runs++;
    int rc=recover_cell_98(r,&cell_ops,0);*actual=!rc;return rc;
}
static void field(const char *s,u32 v){output(s);output("=");number(v);output("\n");}
static void val(u32 v){number(v);output(" ");}
/* Record reads as well as the existing write/delay trace with logging on. */
static int prefix_put(void *v,u32 ch){(void)v;(void)ch;return 0;}
static const struct recover_transport prefix_transport={
    .uart={exec_log_ready,prefix_put,exec_log_wait},.frame_us=1,.max_polls=1048576};
static int prefix_read(void *v,const struct recover_bus_access *a,u32 *value)
{
    int rc=bus_read_test(v,a,value);
    if(!rc)trace_add(actual,&actual_count,REC_READ,a->key,*value);
    return rc;
}
static void prefix_run(bool mode_c)
{
    bus_setup();fixture();
    CHECK(!recover_gf_profile_expand(&profile,&composed));
    CHECK(!gfinit_decode_rom(rom,sizeof(rom),&profile,&decoded));
    CHECK(!(mode_c ? gfinit_plan_build_c(&composed,&decoded,&profile,0x1000000,rom,sizeof(rom)) :
                    gfinit_plan_build(&composed,&decoded,&profile,0x1000000)));
    decoded.plan=composed.plan;CHECK(!contract_bind());
    composed.plan.profile_crc=profile.crc;exec_arm();core.plan=&composed.plan;
    core.token.profile_crc=profile.crc;core.token.profile_version=profile.version;
    core.token.plan_hash=core.bound_hash;core.token.snapshot_crc=snapshot_crc(&core);
    executor_bus.read=prefix_read;executor_bus.write=bus_write_test;
    core.transport=&prefix_transport;exec_log_busy=false;
    for(u32 i=0;i<profile.target_count;i++)values[i]=0;
    for(u32 i=0;i<recover_gf_prepare_plan.node_count;i++) {
        u32 value;
        CHECK(!recover_plan_enter(&core,i));CHECK(!recover_plan_access(&core,&value));CHECK(!recover_plan_leave(&core,0));
    }
    for(u32 main=0;main<GF_INIT_MAINS;main++) {
        execution.main_id=main;CHECK(!gfinit_exec_run(&core,&execution));
    }
    CHECK(core.expected_node==(mode_c?composed.c_node:composed.vram_node));
}
static void prefix_equivalence(void)
{
    static struct trace_item legacy[TRACE_MAX];
    prefix_run(false);u32 count=actual_count,reads=0;
    for(u32 i=0;i<count;i++) {
        legacy[i]=actual[i];
        if(actual[i].dir==REC_READ)reads++;
    }
    CHECK(reads>0);
    prefix_run(true);CHECK(actual_count==count);
    for(u32 i=0;i<count && i<actual_count;i++)
        CHECK(actual[i].dir==legacy[i].dir && actual[i].key==legacy[i].key && actual[i].value==legacy[i].value);
    output("prefix transport trace events ");number(count);output(" reads ");number(reads);output("\n");
    /* C still pre-reads the shared full-mask write in phase 9. The staging
     * barrier is bypassed only here, just as in c_walk's later-row inspection. */
    for(u32 id=composed.c_node;id<composed.plan.node_count;id++) {
        const struct recover_rule *w=core.node_write[id];
        if(!w || profile.targets[w->key].offset!=0x680608 || w->write_mask!=~0U)continue;
        CHECK(w->phase==REC_MODE_C);
        core.expected_node=id;u32 before=actual_count,value=0;
        CHECK(!recover_plan_enter(&core,id));CHECK(!recover_plan_access(&core,&value));
        CHECK(actual_count>=before+2 && actual[before].dir==REC_READ && actual[before].key==w->key);
        CHECK(!recover_plan_leave(&core,0));
        return;
    }
    CHECK(false);
}
static void c_build(void)
{
    bus_setup();fixture();
    CHECK(recover_gf_profile_expand(&profile,&composed)==0);
    CHECK(gfinit_decode_rom(rom,sizeof(rom),&profile,&decoded)==0);
    CHECK(gfinit_plan_build_c(&composed,&decoded,&profile,0x1000000,rom,sizeof(rom))==0);
    /* Synthetic clock stays at zero. These fixture limits let trial_step
     * exercise the existing cleanup; production C budgeting remains 2c. */
    profile.trial_limit=profile.restore_limit=1000;
    profile.max_steps=4096;profile.restore_max_polls=65536;profile.restore_log_bytes=65536;
    decoded.plan=composed.plan;
    CHECK(contract_bind()==0);
}
static void c_arm(void)
{
    for(u32 i=0;i<profile.count;i++)core.saved[i]=rawbars[i];
    core.saved_count=profile.count;
    composed.plan.profile_crc=profile.crc;exec_arm();core.plan=&composed.plan;
    core.token.profile_crc=profile.crc;core.token.profile_version=profile.version;
    core.token.plan_hash=core.bound_hash;core.token.snapshot_crc=snapshot_crc(&core);
    executor_bus.read=bus_read_test;executor_bus.write=bus_write_test;
    for(u32 i=0;i<profile.target_count;i++)values[i]=0;
    for(u32 i=0;i<recover_gf_prepare_plan.node_count;i++) {
        u32 value;
        CHECK(!recover_plan_enter(&core,i));CHECK(!recover_plan_access(&core,&value));CHECK(!recover_plan_leave(&core,0));
    }
    for(u32 main=0;main<GF_INIT_MAINS;main++) {
        execution.main_id=main;CHECK(gfinit_exec_run(&core,&execution)==0);
    }
    CHECK(core.expected_node==composed.c_node);
    executor_bus.mode_wait=exec_delay;executor_bus.mode_vram=frame_operation;executor_bus.mode_color=color_operation;executor_bus.abort_requested=frame_point;
    memory_ops.load=frame_load;memory_ops.store=frame_store;
    executor_bus.route_read=c_route_read;executor_bus.route_write=c_route_write;executor_bus.mode_cell=c_cell;
    route_value=route_writes=route_reads=route_fault=route_mode_reads=route_mode_writes=route_noise=0;
    cell_value=0x1234;cell_calls=cell_writes=cell_fault=cell_runs=0;
    frame_writes=frame_reads=drains=frame_points=0;
    frame_fail_at=frame_abort_at=color_fail_at=RECOVER_NONE;vram_active=color_active=false;color_count=0;
}
static u32 saved_value(const struct recover_target *t,u32 variant)
{
    if(!variant)return 0;
    if(t->space==REC_VGA_INDEXED && t->port==RECOVER_CRTC) {
        switch(t->index) {
        case 0x1c:case 0x33:return 255;
        case 0x3c:return 0x5a;
        case 0x4b:return 0x12;
        case 0x52:return 0xfe;
        }
    }
    if(t->space==REC_BAR_MMIO) {
        switch(t->offset) {
        case 0x60081c:return 0x12345678;
        case 0x600850:return 0x87654321;
        case 0x680524:return 0xabcdef01;
        }
    }
    return 0;
}
static void dump_row(u32 ordinal,u32 value,u32 bound,u32 key)
{
    const struct recover_gf_c_row *row=&recover_gf_c_rows[ordinal];
    u32 kind=row->kind,reg=row->reg,index=row->index,width=row->width;
    if(bound && key!=RECOVER_NONE) {
        const struct recover_target *t=&profile.targets[key];
        struct recover_bus_access a;
        CHECK(t->bar==0 && t->head==0);
        if(t->space==REC_VGA_INDEXED) {
            CHECK(!recover_target_address(t,REC_DATA,&a));
            kind=t->port==0x3d4 ? 1 : t->port==0x3c4 ? 2 : 3;
            CHECK(t->port==0x3d4 || t->port==0x3c4 || t->port==0x3ce);
            reg=a.offset-1;index=t->index;
        } else {
            CHECK(t->space==REC_BAR_MMIO || t->space==REC_VGA_DIRECT);
            CHECK(!recover_target_address(t,REC_WRITE,&a));
            kind=t->space==REC_BAR_MMIO ? 0 : 5;reg=a.offset;index=~0U;
        }
        width=a.width;
    }
    output("row=");val(ordinal);val(kind);val(reg);val(index);
    val(width);val(value);val(row->count);val(bound);output("\n");
}
/* Snapshot only test device/core state, so each CHECK fault uses the exact
 * state produced by the real C writes/captures without repeating VRAM. */
struct check_fixture {
    struct recover r;
    u32 values[RECOVER_TARGET_MAX], indices[RECOVER_TARGET_MAX], valid[RECOVER_BITS_WORDS];
};
static struct check_fixture entry_fixture, exit_fixture;
static void save_checks(struct check_fixture *f)
{
    f->r=core;
    for(u32 i=0;i<RECOVER_TARGET_MAX;i++){f->values[i]=values[i];f->indices[i]=indices[i];}
    for(u32 i=0;i<RECOVER_BITS_WORDS;i++)f->valid[i]=valid[i];
}
static void c_walk(u32 variant,bool fp_on)
{
    c_build();c_arm();
    output("variant=");number(variant);output("\n");
    for(u32 i=0;i<profile.target_count;i++)values[i]=saved_value(&profile.targets[i],variant);
    u32 rows=0, sections=0, pending=0, waits=0, data_writes=0, saved_count=core.local_saved_count;
    u32 wait_before=wait_total;
    u32 fp_key=key_for(&profile,0x680848,0,0,0);
    values[fp_key]=fp_on?0x10000000:0x20000022;
    struct gfinit_pll_limits limits;struct gfinit_mode mode;const struct gfinit_mode_saved empty={0};
    CHECK(!gfinit_pll_parse(rom,sizeof(rom),GF_INIT_PLL_TABLE,GF_PLL_VIDEO,&limits));
    CHECK(!gfinit_mode_calc(&limits,&empty,&mode));
    output("pll=");const u32 *pll=(const u32 *)&mode.pll;
    for(u32 i=0;i<sizeof(mode.pll)/sizeof(u32);i++)val(pll[i]);
    output("\n");
    while(core.expected_node!=RECOVER_NONE) {
        u32 id=core.expected_node, value=0,edge=0;
        const struct recover_plan_node *n=&composed.plan.nodes[id];
        const struct recover_opcode_layout *l=core.node_layout[id];
        const struct recover_plan_arg *a=&composed.plan.args[n->arg_first];
        if(n->substep==GF_C_SECTION) {
            if(a[0].value==GF_C_CHECK_IN)save_checks(&entry_fixture);
            if(a[0].value==GF_C_CHECK_OUT)save_checks(&exit_fixture);
            const u32 first_row[]={0,0,2,15,15,26,46,120,190,205,206};
            CHECK(sections<sizeof(first_row)/sizeof(*first_row) && rows==first_row[sections]);
            CHECK(a[0].value==sections++ && a[1].value==GF_MODE_C);
        }
        /* Restore row 34 must see the now disconnected C040 if incorrectly
         * lowered to LIVE: do not hide that defect by resetting the fake GPU. */
        if(n->expr_id==GF_C_EXPR_LIVE && n->rule_id!=578+34) {
            const struct recover_target *t=&profile.targets[a[0].value];
            /* Preserve the just-loaded CR11 for its final lock RMW. */
            if(!(t->space==REC_VGA_INDEXED && t->port==RECOVER_CRTC &&
                 t->index==0x11 && a[4].value==0x80))
                values[a[0].value]=0xa5a55a5aU & width_mask(t->width);
        }
        CHECK(!recover_plan_enter(&core,id));
        if(l->flags & REC_LAYOUT_MODE_ROUTE) {
            CHECK(sections==10 && rows==205 && !cell_runs);
            CHECK(!gfinit_plan_c_control(&composed,&core));
            dump_row(rows++,route_value,1,RECOVER_NONE);
        } else if(l->flags & (REC_LAYOUT_MODE_CHECK | REC_LAYOUT_MODE_WAIT)) {
            if(n->substep==GF_C_WAIT) {
                CHECK((a[0].value==50 && rows==2) ||
                      (a[0].value==10 && (rows==6 || rows==201)));
                waits+=a[0].value;
            }
            int rc=gfinit_plan_c_control(&composed,&core);
            if(rc) {output("control failure sub/key/slot/actual ");val(n->substep);val(a[REC_CHECK_KEY].value);val(a[REC_CHECK_SLOT].value);val(composed.c_checks[a[REC_CHECK_SLOT].value].actual);output("\n");}
            CHECK(!rc);
        } else if(l->flags & REC_LAYOUT_CHECK_EXPR) {
            CHECK(!recover_plan_check(&core,&edge));
            CHECK(edge==(u32)fp_on);
            if(!edge)rows++; /* conditional Q FP_OFF and its wait are skipped */
        } else if(l->flags & REC_LAYOUT_MODE_VRAM) {
            CHECK(sections==4 && rows==15);
            CHECK(!recover_plan_access(&core,&value));
        } else if(l->flags & REC_LAYOUT_MODE_COLOR) {
            u32 count = a[0].value==GF_MODE_ROW_DAC ? 3 : 1;
            CHECK(a[0].value==rows);
            CHECK(!recover_plan_access(&core,&value));
            for (u32 j=0;j<count;j++) {
                const struct recover_gf_c_row *row=&recover_gf_c_rows[rows];
                u32 v=row->expr==GF_C_STATE ? ((const u8 *)&mode)[row->source] : row->bits;
                dump_row(rows++,v,1,RECOVER_NONE);
            }
        } else if(l->dir_substep!=RECOVER_NONE) {
            CHECK(!recover_plan_access(&core,&value));
            if(n->substep>=REC_GF_C_RULE_FIRST) {
                CHECK(rows<recover_gf_c_row_count && n->rule_id==recover_gf_c_rows[rows].id);
                CHECK(core.node_write[id]->verify_mask==recover_gf_c_rows[rows].verify);
                dump_row(rows++,value,1,a[0].value);data_writes++;
            }
        }
        CHECK(!recover_plan_leave(&core,edge));
        if(core.state!=REC_RUNNING)break;
    }
    CHECK(rows==206 && sections==GF_C_SECTION_COUNT && pending==composed.c_unbound);
    CHECK(!pending && route_writes==1 && route_value==1 && cell_runs==1);
    CHECK(cell_calls==5 && cell_writes==2 && cell_value==0x1234);
    CHECK(composed.c_checks[94].valid && composed.c_checks[94].actual==1);
    CHECK(composed.c_checks[95].valid && composed.c_checks[95].actual==1);
    CHECK(waits==(fp_on?70U:20U));
    CHECK(wait_total-wait_before==waits*1000);
    CHECK(data_writes==(fp_on?178U:177U));
    CHECK(frame_writes==sizeof(frame) && frame_reads==sizeof(frame) && drains==1);
    /* Independent physical-byte oracle: gate-off, full DAC, AR pairs, gates. */
    static const u8 colors[8][3]={{0,0,0},{255,0,0},{0,255,0},{0,0,255},
        {255,255,255},{0,255,255},{255,0,255},{255,255,0}};
    u32 at=0;
#define COLOR_EVENT(w,o,v) do { CHECK(color_events[at][0]==(w) && color_events[at][1]==(o) && color_events[at][2]==(v)); at++; } while(0)
    COLOR_EVENT(0,GF_MODE_AR_STATUS,0);COLOR_EVENT(1,GF_MODE_AR_PORT,0);
    COLOR_EVENT(1,GF_MODE_DAC_MASK,255);COLOR_EVENT(1,GF_MODE_DAC_INDEX,0);
    for(u32 i=0;i<256;i++)for(u32 c=0;c<3;c++)
        COLOR_EVENT(1,GF_MODE_DAC_DATA,i<8?colors[i][c]:0);
    for(u32 i=0;i<21;i++) {
        COLOR_EVENT(0,GF_MODE_AR_STATUS,0);COLOR_EVENT(1,GF_MODE_AR_PORT,i);
        COLOR_EVENT(1,GF_MODE_AR_PORT,i<16?i:i==16?1:i==18?15:0);
    }
    for(u32 i=0;i<2;i++) {COLOR_EVENT(0,GF_MODE_AR_STATUS,0);COLOR_EVENT(1,GF_MODE_AR_PORT,32);}
    CHECK(color_count==at);
#undef COLOR_EVENT
    /* C data on shared RW keys must not add local DATA restoration. */
    for(u32 i=saved_count;i<core.local_saved_count;i++)CHECK(core.local_saved[i].dir_substep==REC_SELECT);
    CHECK(core.capture_valid==0);
    field("nodes",composed.plan.node_count);field("args",composed.plan.arg_count);
    field("targets",profile.target_count);field("rules",profile.rule_count);field("layouts",profile.layout_count);
    field("entries",composed.plan.entry_count);field("captures",composed.plan.capture_count);
    field("pending",composed.c_unbound);field("c_nodes",composed.plan.node_count-composed.c_node);
    field("prefix_nodes",composed.c_node);
    field("plan_bytes",sizeof(composed));field("decode_bytes",sizeof(decoded));
}
static void contracts(void)
{
    const u32 fp_values[]={0,0x20000022,1,16,0x10000000};
    for(u32 i=0;i<sizeof(fp_values)/sizeof(*fp_values);i++) {
        u32 enabled;
        CHECK(!c_fp_on(&fp_values[i],1,&enabled) && enabled==(i>=2));
    }
    c_build();u32 crc=profile.crc,hash=core.bound_hash;
    CHECK(composed.mode_flags==GF_MODE_C && composed.vram_node==RECOVER_NONE);
    CHECK(profile.targets[key_for(&profile,0x680510,0,0,0)].read_safe); /* appended C read permission */
    core.plan=0;decoded.args[decoded.nodes[composed.c_node].arg_first+1].value=0;
    CHECK(contract_bind()==0 && core.bound_hash!=hash);
    c_build();u32 id=composed.c_node;
    while(core.node_layout[id]->dir_substep!=REC_DATA)id++;
    core.plan=0;
    /* Keep reachability, remove the SELECT permission (not the DATA node). */
    decoded.nodes[id-1].substep=GF_C_SECTION;
    decoded.nodes[id-1].rule_id=RECOVER_NONE;
    CHECK(contract_bind()<0);
    c_build();core.plan=0;decoded.captures[35].producer_node=decoded.captures[35].consumer_node;
    CHECK(contract_bind()<0);
    c_build();core.plan=0;decoded.captures[35].read_ordinal++;
    CHECK(contract_bind()<0);
    bus_setup();fixture();CHECK(!recover_gf_profile_expand(&profile,&composed));
    CHECK(!gfinit_decode_rom(rom,sizeof(rom),&profile,&decoded));
    CHECK(!gfinit_plan_build(&composed,&decoded,&profile,0x1000000));
    CHECK(!composed.mode_flags && composed.c_node==RECOVER_NONE && profile.crc!=crc);
    for(u32 i=0;i<composed.plan.node_count;i++)CHECK(composed.plan.nodes[i].phase!=REC_MODE_C);
    CHECK(composed.plan.entry_count==7 && composed.plan.entries[6].phase==REC_VRAM_TEST);
}
static char frame_log[RECOVER_ROW_MAX], frame_records[4][RECOVER_ROW_MAX];
static u32 frame_log_used, frame_record_count, frame_damage;
static bool frame_contains(const char *s,const char *part)
{
    for(u32 i=0;s[i];i++) {
        u32 j=0;while(part[j] && s[i+j]==part[j])j++;
        if(!part[j])return true;
    }
    return false;
}
static int frame_put(void *v,u32 ch)
{
    (void)v;
    CHECK(irq_state==1 && !device.window.owned);
    if(frame_log_used+1>=sizeof(frame_log))return -1;
    frame_log[frame_log_used++]=(char)ch;frame_log[frame_log_used]=0;
    if(ch=='\n') {
        if(frame_contains(frame_log,"phase=gf_bus") && frame_contains(frame_log," len=")) {
            CHECK(frame_record_count<4);
            if(frame_record_count<4)for(u32 i=0;i<=frame_log_used;i++)
                frame_records[frame_record_count][i]=frame_log[i];
            frame_record_count++;
        }
        frame_log_used=0;
    }
    return 0;
}
static const struct recover_transport frame_transport={
    .uart={exec_log_ready,frame_put,exec_log_wait},.frame_us=1,.max_polls=1048576};
static int frame_step(struct recover *r,void *v)
{
    (void)v;
    for(u32 direction=0;direction<2;direction++) {
        u32 id=r->expected_node,value=0;
        if(recover_plan_enter(r,id) || recover_plan_access(r,&value) || recover_plan_leave(r,0))return -1;
        if(!direction) {
            if(frame_damage==1)frame[0]^=1;
            if(frame_damage==2)frame[640*23+65]^=1;
            if(frame_damage==3)frame[sizeof(frame)-1]^=1;
            if(frame_damage==4) {frame[17]^=1;frame[sizeof(frame)-1]^=2;}
            if(frame_damage==6) {frame[0]^=1;frame_fail_at=1280;}
            if(frame_damage==8)frame_abort_at=1280;
        }
    }
    return 0;
}
static void frame_cases(void)
{
    for(frame_damage=0;frame_damage<=8;frame_damage++) {
        c_build();c_arm();
        /* Only this test bypasses the entry and Q/P (this test starts at VRAM). The
         * actual two-node section, accessors, trial_step and cleanup run. */
        u32 first=composed.c_node;
        while(composed.plan.nodes[first].substep!=GF_C_VRAM_WRITE)first++;
        CHECK(composed.plan.nodes[first+1].substep==GF_C_VRAM_COMPARE);
        CHECK(composed.plan.nodes[first-1].substep==GF_C_SECTION);
        CHECK(composed.plan.args[composed.plan.nodes[first-1].arg_first].value==GF_C_VRAM);
        CHECK(composed.plan.nodes[first+2].substep==GF_C_SECTION);
        CHECK(composed.plan.args[composed.plan.nodes[first+2].arg_first].value==GF_C_SD);
        core.expected_node=first;core.state=REC_PAUSED;core.in_call=core.in_opcode=false;
        core.transport=&frame_transport;exec_log_busy=false;
        frame_log_used=frame_record_count=0;
        if(frame_damage==5)frame_fail_at=1280;
        if(frame_damage==7)frame_abort_at=1280;
        u32 before_maps=maps,before_restores=restores;
        CHECK(core.enabled && core.valid && token_matches(&core,&core.token) && live_matches(&core));
        int rc=trial_step(&core,&core.token,true,frame_step,0);
        CHECK((rc!=0)==(frame_damage!=0));
        CHECK(maps-before_maps==restores-before_restores);
        CHECK(pte==0x876540a5 && irq_state==1 && !device.window.owned && !window_busy);
        CHECK(frame_points>=3);
        if(frame_damage<=4) {
            CHECK(frame_writes==307200 && frame_reads==307200 && drains==1);
            CHECK(frame_record_count==4);
            for(u32 i=0;i<4;i++) {
                CHECK(frame_contains(frame_records[i]," bar=1 off=0 len=307200"));
                CHECK(frame_contains(frame_records[i],i<2?" dir=1":" dir=0"));
                CHECK(frame_contains(frame_records[i],(i&1)?"event=END":"event=BEGIN"));
            }
            if(frame_damage) {
                const char *bad[]={"","bad_off=0 expected=4 actual=5",
                    "bad_off=14785 expected=4 actual=5","bad_off=307199 expected=4 actual=5",
                    "bad_off=17 expected=4 actual=5"};
                CHECK(frame_contains(frame_records[3],bad[frame_damage]));
                CHECK(frame_contains(frame_records[3],"rc=-1"));
                CHECK(core.reason==REC_VERIFY && core.state==REC_FAILED && cleanup_calls==1);
            } else {
                CHECK(core.state==REC_PAUSED && core.expected_node==first+2 && !cleanup_calls);
                CHECK(frame_contains(frame_records[3],"bad_off=4294967295") && frame_contains(frame_records[3],"rc=0"));
                CHECK(frame_points>=2*480);
            }
        } else {
            CHECK(core.state!=REC_RUNNING && core.state!=REC_PAUSED && cleanup_calls==1);
            CHECK(frame_record_count<=4);
            if(frame_damage==5 || frame_damage==6) {
                CHECK(core.reason==REC_BUS);
                CHECK(frame_contains(frame_records[frame_record_count-1],"rc=-1"));
            } else CHECK(core.reason==REC_ABORT);
            if(frame_damage==5)CHECK(frame_writes==1284 && !frame_reads && !drains);
            if(frame_damage==6)CHECK(frame_reads==1284);
            if(frame_damage==7)CHECK(frame_writes==1280 && !frame_reads && !drains);
            if(frame_damage==8)CHECK(frame_reads==1280);
        }
        output("VRAM case=");number(frame_damage);output(" writes=");number(frame_writes);
        output(" reads=");number(frame_reads);output(" gf_bus_rows=");number(frame_record_count);output("\n");
        if(frame_damage<=4)output(frame_records[3]);
    }
    /* A typed operation cannot become a successful NOP. */
    c_build();c_arm();u32 id=composed.c_node;
    while(composed.plan.nodes[id].substep!=GF_C_VRAM_WRITE)id++;
    core.expected_node=id;CHECK(!recover_plan_enter(&core,id));
    CHECK(recover_plan_leave(&core,0)<0 && cleanup_calls==1 && !frame_writes);
    c_build();core.plan=0;
    decoded.args[decoded.nodes[id].arg_first+2].value--;
    CHECK(contract_bind()<0);
    output("VRAM fixed range, required execution, full row order/compare, first mismatch, bus failure and safe-point abort PASS\n");
}
static u32 color_log_rows, color_begin, color_end;
static char color_log[RECOVER_ROW_MAX];
static u32 color_log_used;
static int color_put(void *v,u32 ch)
{
    (void)v;
    CHECK(irq_state==1 && !device.window.owned);
    if(color_log_used+1>=sizeof(color_log))return -1;
    color_log[color_log_used++]=(char)ch;color_log[color_log_used]=0;
    if(ch=='\n') {
        if(frame_contains(color_log,"phase=gf_bus")) {
            color_log_rows++;
            if(frame_contains(color_log,"event=BEGIN"))color_begin++;
            if(frame_contains(color_log,"event=END"))color_end++;
            CHECK(frame_contains(color_log," bar=0 "));
            if(color_row==GF_MODE_ROW_DAC) CHECK(frame_contains(color_log," len=768 "));
        }
        color_log_used=0;
    }
    return 0;
}
static const struct recover_transport color_transport={
    .uart={exec_log_ready,color_put,exec_log_wait},.frame_us=1,.max_polls=1048576};
static int color_step(struct recover *r,void *v)
{
    (void)v;u32 id=r->expected_node,value=0;
    return recover_plan_enter(r,id) || recover_plan_access(r,&value) || recover_plan_leave(r,0) ? -1 : 0;
}
static void color_cases(void)
{
    static const u32 rows[]={GF_MODE_ROW_DAC,GF_MODE_ROW_DAC,GF_MODE_ROW_DAC,
        GF_MODE_ROW_DAC,GF_MODE_ROW_AR_FIRST,GF_MODE_ROW_AR_FIRST,
        GF_MODE_ROW_AR_FIRST,GF_MODE_ROW_GATE_ON,GF_MODE_ROW_DAC};
    static const u32 failures_at[]={RECOVER_NONE,0,1,400,0,1,2,1,769};
    for(u32 c=0;c<sizeof(rows)/sizeof(*rows);c++) {
        c_build();c_arm();u32 id=composed.c_node;
        while(composed.plan.nodes[id].substep!=GF_C_COLOR ||
              composed.plan.args[composed.plan.nodes[id].arg_first].value!=rows[c])id++;
        core.expected_node=id;core.state=REC_PAUSED;core.in_call=core.in_opcode=false;
        core.transport=&color_transport;color_row=rows[c];color_fail_at=failures_at[c];
        color_log_rows=color_begin=color_end=color_log_used=0;exec_log_busy=false;
        u32 old_maps=maps,old_restores=restores;
        int rc=trial_step(&core,&core.token,true,color_step,0);
        CHECK((rc!=0)==(c!=0));
        CHECK(color_log_rows==2 && color_begin==1 && color_end==1);
        CHECK(maps-old_maps==restores-old_restores && pte==0x876540a5 && irq_state==1 && !device.window.owned);
        if(c)CHECK(cleanup_calls==1 && core.state!=REC_RUNNING && color_count==failures_at[c]+1);
        else CHECK(color_count==770);
        output("COLOR case=");number(c);output(" accesses=");number(color_count);
        output(" gf_bus_rows=");number(color_log_rows);output("\n");
    }
    c_build();c_arm();u32 id=composed.c_node;
    while(composed.plan.nodes[id].substep!=GF_C_COLOR)id++;
    core.expected_node=id;CHECK(!recover_plan_enter(&core,id));
    CHECK(recover_plan_leave(&core,0)<0 && cleanup_calls==1);
    output("COLOR order values log and bus failure cleanup PASS\n");
}
static u32 check_damage_slot, check_damage_mask, check_record_count, check_line_used;
static u32 wait_begin,wait_end,wait_ms;
static char route_record[RECOVER_ROW_MAX];
static char check_line[RECOVER_ROW_MAX], check_records[GF_C_CHECK_MAX][RECOVER_ROW_MAX];
static bool damaged_check(void)
{
    if(!core.entered || !core.plan)return false;
    const struct recover_plan_node *n=&core.plan->nodes[core.cursor];
    return (n->substep==GF_C_CHECK || n->substep==GF_C_CHECK_SAVED) &&
        core.plan->args[n->arg_first+REC_CHECK_SLOT].value==check_damage_slot;
}
static int check_read(void *v,const struct recover_bus_access *a,u32 *value)
{
    int rc=bus_read_test(v,a,value);
    if(!rc && damaged_check())*value^=check_damage_mask;
    return rc;
}
static int check_identity(void *v,struct recover_identity *id)
{
    int rc=identity(v,id);
    if(!rc && damaged_check())id->device^=1;
    return rc;
}
static int check_route(void *v,u32 *value)
{ (void)v;*value=(core.expected_node>=exit_fixture.r.expected_node?1U:0U) ^ (damaged_check()?check_damage_mask:0U);return 0; }
static int check_cell(void *v,struct recover *r,u32 *actual)
{ (void)v;(void)r;*actual=1U ^ (damaged_check()?check_damage_mask:0U);return 0; }
static int check_put(void *v,u32 ch)
{
    (void)v;CHECK(irq_state==1 && !device.window.owned);
    CHECK(check_line_used+1<sizeof(check_line));
    check_line[check_line_used++]=(char)ch;check_line[check_line_used]=0;
    if(ch=='\n') {
        if(frame_contains(check_line,"phase=wait event=BEGIN")) {
            wait_begin++;
            CHECK(frame_contains(check_line,wait_ms==50?" addr=50\n":" addr=10\n"));
        }
        if(frame_contains(check_line,"phase=wait event=END"))wait_end++;
        if(frame_contains(check_line,"phase=route event=END") && frame_contains(check_line," mask="))
            for(u32 i=0;i<=check_line_used;i++)route_record[i]=check_line[i];
        if(frame_contains(check_line,"phase=check event=END")) {
            CHECK(check_record_count<GF_C_CHECK_MAX);
            for(u32 i=0;i<=check_line_used;i++)check_records[check_record_count][i]=check_line[i];
            check_record_count++;
        }
        check_line_used=0;
    }
    return 0;
}
static const struct recover_transport check_transport={
    .uart={exec_log_ready,check_put,exec_log_wait},.frame_us=1,.max_polls=1048576};
static void restore_checks(const struct check_fixture *f)
{
    core=f->r;
    for(u32 i=0;i<RECOVER_TARGET_MAX;i++){values[i]=f->values[i];indices[i]=f->indices[i];}
    for(u32 i=0;i<RECOVER_BITS_WORDS;i++)valid[i]=f->valid[i];
    core.state=REC_PAUSED;core.in_call=core.in_opcode=false;core.transport=&check_transport;
    executor_bus.read=check_read;executor_bus.identity=check_identity;executor_bus.route_read=check_route;executor_bus.mode_cell=check_cell;
    cleanup_calls=actual_count=check_record_count=check_line_used=0;exec_log_busy=false;
    for(u32 i=0;i<GF_C_CHECK_MAX;i++)composed.c_checks[i]=(struct gfinit_c_check_result){0};
}
static int checks_step(struct recover *r,void *v)
{
    (void)v;
    while(r->expected_node!=RECOVER_NONE) {
        u32 id=r->expected_node,value=0;
        const struct recover_plan_node *n=&composed.plan.nodes[id];
        const struct recover_opcode_layout *l=r->node_layout[id];
        const struct recover_plan_arg *a=&composed.plan.args[n->arg_first];
        /* Exactly the entry or exit section. */
        if((l->flags & REC_LAYOUT_UNBOUND) ||
           (n->substep==GF_C_SECTION && a[0].value==GF_C_Q))return 0;
        if(recover_plan_enter(r,id))return -1;
        if(l->flags & REC_LAYOUT_MODE_CHECK) {
            if(gfinit_plan_c_control(&composed,r))return -1;
        } else if(l->dir_substep!=RECOVER_NONE) {
            if(recover_plan_access(r,&value))return -1;
        } else CHECK(n->substep==GF_C_SECTION);
        if(recover_plan_leave(r,0))return -1;
    }
    return 0;
}
static u32 check_number(const char *line,const char *key)
{
    for(u32 i=0;line[i];i++) {
        u32 j=0;while(key[j] && line[i+j]==key[j])j++;
        if(key[j])continue;
        u32 v=0;while(line[i+j]>='0' && line[i+j]<='9')v=v*10+line[i+j++]-'0';
        return v;
    }
    CHECK(false);return 0;
}
/* These masks/values are the design contract, independent of plan metadata
 * used to enumerate fault injection below. */
static void check_contracts(void)
{
    u32 seen=0;
    for(u32 id=exit_fixture.r.expected_node;id<composed.plan.node_count;id++) {
        const struct recover_plan_node *n=&composed.plan.nodes[id];
        if(!(exit_fixture.r.node_layout[id]->flags & REC_LAYOUT_MODE_CHECK))continue;
        const struct recover_plan_arg *a=&composed.plan.args[n->arg_first];
        if(a[REC_CHECK_SOURCE].value!=REC_CHECK_TARGET)continue;
        const struct recover_target *t=&profile.targets[a[REC_CHECK_KEY].value];
        u32 mask=a[REC_CHECK_MASK].value,expected=a[REC_CHECK_EXPECTED].value;
        if(t->space==REC_BAR_MMIO) switch(t->offset) {
        case 0x68050c:CHECK(mask==0xefffffffU && expected==0x10000204);seen|=1;break;
        case 0x680510:CHECK(mask==0);seen|=2;break;
        case 0x680508:CHECK(mask==0x0007ffff);seen|=4;break;
        case 0x680578:CHECK(mask==0x8000ffff);seen|=8;break;
        case 0x680580:CHECK(mask==0x100 && expected==0x100);seen|=16;break;
        case 0x680524:CHECK(mask==~0U && a[REC_CHECK_EXPECTED].kind==REC_ARG_SAVED && a[REC_CHECK_KEEP].value==0xfffaffff);seen|=32;break;
        case 0xc040:CHECK(mask==~0U && a[REC_CHECK_EXPECTED].kind==REC_ARG_SAVED && a[REC_CHECK_KEEP].value==~0U);seen|=64;break;
        case 0x600804:CHECK(mask==3 && expected==2);seen|=128;break;
        case 0x600800:CHECK(mask==~0U && expected==0);seen|=256;break;
        case 0x68052c:CHECK(mask==0x101 && expected==1);seen|=512;break;
        case 0x680608:CHECK(mask==0x100000 && expected==0x100000);seen|=1024;break;
        }
        if(t->space==REC_VGA_INDEXED) {
            CHECK(mask==(t->port==0x3d4 && t->index==0x1a ? 0xd7U : 255U));
            if(t->port==0x3d4 && t->index==0x11)CHECK(expected==0xab);
            if(t->port==0x3d4 && t->index==0x17)CHECK(expected==0xc3);
            if(t->port==0x3c4 && t->index==1)CHECK(expected==1);
        }
    }
    CHECK(seen==2047 && composed.c_check_count==66);
}
static void check_cases(void)
{
    check_contracts();
    u32 total=0;
    for(u32 section=0;section<2;section++) {
        const struct check_fixture *f=section?&exit_fixture:&entry_fixture;
        u32 nodes[GF_C_CHECK_MAX],count=0;
        for(u32 id=f->r.expected_node;id<composed.plan.node_count;id++) {
            const struct recover_plan_node *n=&composed.plan.nodes[id];
            const struct recover_plan_arg *a=&composed.plan.args[n->arg_first];
            if(f->r.node_layout[id]->flags & REC_LAYOUT_UNBOUND)break;
            if(n->substep==GF_C_SECTION && a[0].value==GF_C_Q)break;
            if(f->r.node_layout[id]->flags & REC_LAYOUT_MODE_CHECK)nodes[count++]=id;
        }
        CHECK(count>0);
        /* Baseline, every compared item wrong in isolation, and every mask's
         * first excluded bit (including read-only 510 and TEST sense). */
        for(u32 pass=0;pass<3;pass++)for(u32 c=0;c<=count;c++) {
            if(pass && !c)continue;
            restore_checks(f);check_damage_slot=RECOVER_NONE;check_damage_mask=0;
            bool bad=false;
            if(c) {
                const struct recover_plan_arg *a=&composed.plan.args[composed.plan.nodes[nodes[c-1]].arg_first];
                u32 mask=a[REC_CHECK_MASK].value;
                if(pass) {
                    if(a[REC_CHECK_SOURCE].value!=REC_CHECK_TARGET || mask==~0U)continue;
                    const struct recover_target *t=&profile.targets[a[REC_CHECK_KEY].value];
                    u32 excluded=~mask & width_mask(t->width);
                    if(!excluded)continue;
                    if(pass==2) {
                        if(t->space!=REC_BAR_MMIO || t->offset!=0x680608)continue;
                        check_damage_mask=0xf0000000; /* sense bits, not bit20 */
                    } else check_damage_mask=excluded & (0U-excluded);
                } else {
                    check_damage_mask=mask ? mask & (0U-mask) : ~0U;
                    bad=mask!=0;
                }
                check_damage_slot=a[REC_CHECK_SLOT].value;
            }
            int rc=trial_step(&core,&core.token,true,checks_step,0);
            CHECK((rc!=0)==bad && cleanup_calls==(u32)bad);
            if(bad) {
                CHECK(core.state==REC_FAILED && core.reason==REC_VERIFY);
                CHECK(check_record_count==c && composed.c_checks[check_damage_slot].valid);
                CHECK(frame_contains(check_records[c-1]," rc=-1"));
            } else {
                CHECK(core.state==REC_PAUSED && check_record_count==count);
                CHECK(frame_contains(check_records[count-1]," rc=0"));
            }
            for(u32 i=0;i<check_record_count;i++) {
                const struct recover_plan_arg *a=&composed.plan.args[composed.plan.nodes[nodes[i]].arg_first];
                u32 slot=a[REC_CHECK_SLOT].value;
                CHECK(composed.c_checks[slot].valid);
                CHECK(check_number(check_records[i]," mask=")==a[REC_CHECK_MASK].value);
                CHECK(check_number(check_records[i]," actual=")==composed.c_checks[slot].actual);
            }
            if(!section)CHECK(!composed.c_checks[94].valid && !composed.c_checks[95].valid);
            if(section && !bad)CHECK(composed.c_checks[94].valid && composed.c_checks[95].valid);
            output(section?"CHECK exit case=":"CHECK entry case=");number(c);
            output(" excluded=");number(pass);output(" records=");number(check_record_count);output("\n");
            if(c && !pass)output(check_records[c-1]);
        }
        total+=count;
    }
    CHECK(total==composed.c_check_count);
    output("CHECK entry/exit match, each mismatch, masks, retained values, REC and failure exit PASS\n");
}
static u32 wait_calls,wait_fault;
static int wait_delay(void *v,u32 us)
{
    (void)v;CHECK(us==1000 && irq_state==1 && !device.window.owned);
    wait_calls++;return wait_fault==1 && wait_calls==3 ? -1 : 0;
}
static int wait_abort(void *v,u32 sid)
{ (void)v;(void)sid;return wait_fault==2 && wait_calls==2; }
static int wait_step(struct recover *r,void *v)
{
    (void)v;u32 id=r->expected_node;
    if(recover_plan_enter(r,id) || gfinit_plan_c_control(&composed,r))return -1;
    return recover_plan_leave(r,0);
}
static void wait_cases(void)
{
    for(u32 which=0;which<3;which++)for(u32 fault_kind=0;fault_kind<3;fault_kind++) {
        c_build();c_arm();u32 id=composed.c_node,seen=0;
        while(id<composed.plan.node_count) {
            if(composed.plan.nodes[id].substep==GF_C_WAIT && seen++==which)break;
            id++;
        }
        CHECK(id<composed.plan.node_count);
        wait_ms=composed.plan.args[composed.plan.nodes[id].arg_first].value;
        CHECK(wait_ms==(which?10U:50U));
        core.expected_node=id;core.state=REC_PAUSED;core.in_call=core.in_opcode=false;
        core.transport=&check_transport;executor_bus.abort_requested=wait_abort;executor_bus.mode_wait=wait_delay;
        wait_calls=wait_begin=wait_end=check_line_used=0;wait_fault=fault_kind;
        int rc=trial_step(&core,&core.token,true,wait_step,0);
        CHECK((rc!=0)==(fault_kind!=0) && cleanup_calls==(u32)(fault_kind!=0));
        CHECK(wait_begin==1 && wait_end==(fault_kind==2?0U:1U));
        if(!fault_kind)CHECK(core.state==REC_PAUSED && wait_calls==wait_ms && core.expected_node==id+1);
        if(fault_kind==1)CHECK(core.state==REC_FAILED && core.reason==REC_BUS && wait_calls==3);
        if(fault_kind==2)CHECK(core.reason==REC_ABORT && wait_calls==2);
        output("WAIT ordinal=");number(which);output(" ms=");number(wait_ms);
        output(" fault=");number(fault_kind);output(" calls=");number(wait_calls);output("\n");
    }
    /* Neither a check nor a wait may be left as a successful NOP. */
    for(u32 type=0;type<2;type++) {
        c_build();c_arm();u32 id=composed.c_node;
        while(composed.plan.nodes[id].substep!=(type?GF_C_WAIT:GF_C_CHECK))id++;
        core.expected_node=id;CHECK(!recover_plan_enter(&core,id));
        CHECK(recover_plan_leave(&core,0)<0 && cleanup_calls==1 && core.reason==REC_CONTRACT);
    }
    output("WAIT lengths positions, conditional FP, REC, callback failure and abort PASS\n");
}
static int route_cell_step(struct recover *r,void *v)
{
    u32 count=(u32)(uptr)v;
    for(u32 i=0;i<count;i++) {
        u32 id=r->expected_node;
        if(recover_plan_enter(r,id) || gfinit_plan_c_control(&composed,r) || recover_plan_leave(r,0))return -1;
    }
    return 0;
}
static void route_cell_cases(void)
{
    for(u32 fault_kind=0;fault_kind<4;fault_kind++) {
        c_build();c_arm();u32 id=composed.c_node;
        while(composed.plan.nodes[id].substep!=GF_C_ROUTE_SET)id++;
        CHECK(!c_route_write(0,0) && c_route_write(0,1)<0 && route_writes==1);
        CHECK(!recover_plan_enter(&core,composed.c_node));
        CHECK(c_route_write(0,1)<0 && route_writes==1);
        CHECK(!recover_plan_leave(&core,0));
        route_writes=0;route_fault=fault_kind;route_noise=0x80;
        core.transport=&check_transport;check_line_used=0;route_record[0]=0;exec_log_busy=false;
        core.expected_node=id;core.state=REC_PAUSED;core.in_call=core.in_opcode=false;
        CHECK(!recover_mode_route_active(&core));
        int rc=trial_step(&core,&core.token,true,route_cell_step,(void *)(uptr)1);
        CHECK((rc!=0)==(fault_kind!=0) && cleanup_calls==(u32)(fault_kind!=0));
        CHECK(route_mode_writes==1 && route_mode_reads==(fault_kind==2?0U:1U) && !cell_runs);
        CHECK(frame_contains(route_record," mask=3 expected=1 actual="));
        CHECK(frame_contains(route_record,fault_kind ? " rc=-1" : " actual=129 rc=0"));
        if(!fault_kind)CHECK(core.state==REC_PAUSED && (route_value&3)==1);
        if(fault_kind==1)CHECK(core.reason==REC_VERIFY);
        if(fault_kind>=2)CHECK(core.reason==REC_BUS);
    }
    for(u32 fault_kind=0;fault_kind<5;fault_kind++) {
        c_build();c_arm();u32 id=composed.c_node;
        while(composed.plan.nodes[id].substep!=GF_C_CHECK ||
              composed.plan.args[composed.plan.nodes[id].arg_first+REC_CHECK_SLOT].value!=94)id++;
        const struct recover_plan_arg *a=&composed.plan.args[composed.plan.nodes[id].arg_first];
        CHECK(a[REC_CHECK_SOURCE].value==REC_CHECK_ROUTE && a[REC_CHECK_MASK].value==3 && a[REC_CHECK_EXPECTED].value==1);
        a=&composed.plan.args[composed.plan.nodes[id+1].arg_first];
        CHECK(a[REC_CHECK_SOURCE].value==REC_CHECK_CELL && a[REC_CHECK_SLOT].value==95);
        route_value=0x81;cell_fault=fault_kind;
        core.expected_node=id;core.state=REC_PAUSED;core.in_call=core.in_opcode=false;
        int rc=trial_step(&core,&core.token,true,route_cell_step,(void *)(uptr)2);
        CHECK((rc!=0)==(fault_kind!=0) && cleanup_calls==(u32)(fault_kind!=0));
        CHECK(cell_runs==1 && composed.c_checks[94].valid && composed.c_checks[94].actual==0x81);
        CHECK(cell_value==0x1234 && cell_writes==(fault_kind==4?0U:2U));
        CHECK(cell_calls==(fault_kind==4?1U:fault_kind==2?4U:5U));
        if(fault_kind!=4)CHECK(cell_trace[cell_calls-2]==0x1234);
        if(!fault_kind)CHECK(composed.c_checks[95].valid && composed.c_checks[95].actual==1);
        else CHECK(core.state==REC_FAILED);
    }
    /* Generic I/O targets retain the original prohibition, and only the
     * fixed phase/value/mask route node binds. */
    struct recover_target t={.space=REC_PC98_IO,.offset=0xfac,.index=RECOVER_NONE,.width=1,.access=REC_RW,.read_safe=1};
    struct recover_bus_access access;CHECK(recover_target_address(&t,REC_WRITE,&access)<0);
    for(u32 bad=0;bad<3;bad++) {
        c_build();u32 id=composed.c_node;while(decoded.nodes[id].substep!=GF_C_ROUTE_SET)id++;
        if(bad==0)decoded.nodes[id].phase=REC_RESTORE;
        else decoded.args[decoded.nodes[id].arg_first+bad-1].value=bad==1?0U:1U;
        core.plan=0;CHECK(contract_bind()<0);
    }
    c_build();c_arm();u32 id=composed.c_node;while(composed.plan.nodes[id].substep!=GF_C_ROUTE_SET)id++;
    core.expected_node=id;CHECK(!recover_plan_enter(&core,id));
    CHECK(recover_plan_leave(&core,0)<0 && cleanup_calls==1);
    output("ROUTE fixed 01 mask03 match/mismatch/bus failures, exclusive operation and slots94/95 PASS\n");
    output("CELL RUN match/mismatch/test-write failure restore once, no HAL restore/redraw PASS\n");
}
static u32 policy_bad_key, policy_reads, policy_used, policy_bus_fault;
static char policy_line[RECOVER_ROW_MAX], policy_end[RECOVER_ROW_MAX];
static int policy_put(void *v,u32 ch)
{
    (void)v;CHECK(policy_used+1<sizeof(policy_line));
    policy_line[policy_used++]=(char)ch;policy_line[policy_used]=0;
    if(ch=='\n') {
        if(frame_contains(policy_line,"phase=gf_bus event=END"))
            for(u32 i=0;i<=policy_used;i++)policy_end[i]=policy_line[i];
        policy_used=0;
    }
    return 0;
}
static const struct recover_transport policy_transport={
    .uart={exec_log_ready,policy_put,exec_log_wait},.frame_us=1,.max_polls=1048576};
static int policy_read(void *v,const struct recover_bus_access *a,u32 *value)
{
    int rc=bus_read_test(v,a,value);
    if(a->key==policy_bad_key && ++policy_reads>1){if(policy_bus_fault)return -1;*value^=1;}
    return rc;
}
static int policy_step(struct recover *r,void *v)
{ u32 value=0;(void)v;return recover_plan_access(r,&value); }
static void verify_policy_cases(void)
{
    c_build();u32 hash=core.bound_hash,crc=profile.crc;
    field("default_hash",hash);field("default_crc",crc);
    CHECK(!gfinit_verify_policy(&composed,&profile,0,0,0));
    core.plan=0;CHECK(!contract_bind() && hash!=core.bound_hash && crc!=profile.crc);
    hash=core.bound_hash;crc=profile.crc;
    CHECK(!gfinit_verify_policy(&composed,&profile,GF_VERIFY_REQUIRED,0,0));
    core.plan=0;CHECK(!contract_bind() && hash==core.bound_hash && crc==profile.crc);
    u32 recorded=0,required=0;
    for(u32 i=0;i<profile.rule_count;i++) {
        const struct recover_rule *w=&profile.rules[i];
        if(w->phase!=REC_MODE_C) {CHECK(!(w->flags&REC_VERIFY_RECORD));continue;}
        const struct recover_target *t=&profile.targets[w->key];
        if(w->substep==GF_C_SELECT)CHECK(!(w->flags&REC_VERIFY_RECORD));
        else if(t->space==REC_BAR_MMIO && (t->offset==0x68050c || t->offset==0x680524 ||
                t->offset==0x680580 || t->offset==0xc040 || t->offset==0x600800 ||
                t->offset==0x600804 || t->offset==0x68052c || t->offset==0x680608)) {
            CHECK(!(w->flags&REC_VERIFY_RECORD));required++;
        }
        if(w->flags&REC_VERIFY_RECORD)recorded++;
    }
    CHECK(recorded>0 && required>=8);
    const struct gfinit_no_verify indexed[]={{GF_VERIFY_CR,0x1c},{GF_VERIFY_SR,1}};
    CHECK(!gfinit_verify_policy(&composed,&profile,0,indexed,2));
    for(u32 i=0;i<profile.rule_count;i++) {
        const struct recover_rule *w=&profile.rules[i];
        const struct recover_target *t=&profile.targets[w->key];
        if(w->phase==REC_MODE_C && w->substep!=GF_C_SELECT && t->space==REC_VGA_INDEXED &&
           ((t->port==RECOVER_CRTC && t->index==0x1c) || (t->port==RECOVER_SEQ && t->index==1)))
            CHECK(w->flags&REC_VERIFY_RECORD);
    }
    const struct gfinit_no_verify missing={GF_VERIFY_MMIO,0x123456};
    CHECK(gfinit_verify_policy(&composed,&profile,0,&missing,1)<0);
    /* Required write mismatch still fails; optional/explicit mismatch closes
     * with rc=0. Use the real binder/access path, corrupt only the readback. */
    for(u32 mode=0;mode<4;mode++) {
        c_build();
        struct gfinit_no_verify reg={GF_VERIFY_MMIO,(mode==0 || mode==3)?0x680a24:0x68050c};
        CHECK(!gfinit_verify_policy(&composed,&profile,mode!=2?GF_VERIFY_REQUIRED:0,
                                    mode==2?&reg:0,mode==2?1:0));
        core.plan=0;CHECK(!contract_bind());c_arm();
        u32 id=composed.c_node;
        while(id<composed.plan.node_count) {
            const struct recover_rule *w=core.node_write[id];
            if(w && w->substep!=GF_C_SELECT && profile.targets[w->key].offset==reg.reg)break;
            id++;
        }
        CHECK(id<composed.plan.node_count);policy_bad_key=core.node_write[id]->key;policy_reads=0;policy_bus_fault=mode==3;
        executor_bus.read=policy_read;core.expected_node=id;
        core.transport=&policy_transport;policy_used=0;policy_end[0]=0;exec_log_busy=false;
        CHECK(!recover_plan_enter(&core,id));
        int rc=policy_step(&core,0);
        output("VERIFY execution mode/rc/reads/state/reason ");val(mode);val((u32)rc);val(policy_reads);val(core.state);val(core.reason);output("\n");
        CHECK((rc!=0)==(mode==1 || mode==3));
        if(mode==3)CHECK(core.reason==REC_BUS);
        else if(mode==1)CHECK(core.reason==REC_VERIFY);
        else {
            CHECK(core.state==REC_RUNNING && !core.failed && policy_reads>=2);
            CHECK(frame_contains(policy_end," mismatch=1 space=1 port=0 index=4294967295 rc=0"));
            output(policy_end);
        }
    }
    output("VERIFY default hash/CRC, required targets, explicit override, unmatched selector and mismatch execution PASS\n");
}
#include "gf_stage_host.inc"
#include "gfinit_sequence_host.inc"
#ifndef GF_C_ENTRY
#define GF_C_ENTRY _start
#endif
void GF_C_ENTRY(void)
{
    gf_fixture_init();
#ifdef HWDEV_SEQUENCE_IMAGE
    sequence_owner_cases();
#endif
    sequence_cases();verify_policy_cases();prefix_equivalence();contracts();frame_cases();color_cases();c_walk(0,true);c_walk(1,true);c_walk(1,false);check_cases();wait_cases();route_cell_cases();stage_record();stage_gates();stage_s0_failures();stage_local_locks();
    output(failures?"C FAIL\n":"C PASS\n");
    __asm__ volatile("int $0x80"::"a"(1),"b"(failures?1:0):"memory");for(;;){}
}
