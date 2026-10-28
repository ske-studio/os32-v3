/* Production lowering/bind/access with synthetic device values. The walk
 * below explicitly bypasses staging barriers ONLY to inspect later writes;
 * separate tests prove recover refuses every barrier in normal execution. */
#define GF_S5_ENTRY c_old_bus_entry
#include "gfinit_bus_host.c"
#undef GF_S5_ENTRY
#include "../../kdebug/gfinit_plan.c"
#include "../../kdebug/gfinit_mode.c"
#include "../../kdebug/gfinit_plan_c.c"
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
    executor_bus.mode_vram=frame_operation;executor_bus.mode_color=color_operation;executor_bus.abort_requested=frame_point;
    memory_ops.load=frame_load;memory_ops.store=frame_store;
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
static void c_walk(u32 variant,bool fp_on)
{
    c_build();c_arm();
    output("variant=");number(variant);output("\n");
    for(u32 i=0;i<profile.target_count;i++)values[i]=saved_value(&profile.targets[i],variant);
    u32 rows=0, sections=0, pending=0, waits=0, data_writes=0, saved_count=core.local_saved_count;
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
            const u32 first_row[]={0,0,2,15,15,26,46,120,190,205,206};
            CHECK(sections<sizeof(first_row)/sizeof(*first_row) && rows==first_row[sections]);
            CHECK(a[0].value==sections++ && a[1].value==GF_MODE_C);
        }
        if(l->flags & REC_LAYOUT_UNBOUND) {
            /* Explicit test-only bypass. Do not call a production executor. */
            if(n->substep==GF_C_WAIT) {
                CHECK((a[0].value==50 && rows==2) ||
                      (a[0].value==10 && (rows==6 || rows==201)));
                waits+=a[0].value;
            }
            else {
                pending++;
                if(a[0].value==GF_C_PENDING_ROW) {
                    const struct recover_gf_c_row *row=&recover_gf_c_rows[rows];
                    const u32 payload[]={row->id,row->key,row->phase,row->kind,row->reg,
                        row->index,row->width,row->expr,row->source,row->keep,row->bits,
                        row->verify,row->count,row->flags};
                    for(u32 j=0;j<sizeof(payload)/sizeof(*payload);j++)CHECK(a[j+1].value==payload[j]);
                    u32 v=row->bits;
                    if(row->expr==GF_C_STATE)v=((const u8 *)&mode)[row->source];
                    dump_row(rows++,v,0,RECOVER_NONE);
                } else {
                    CHECK((a[0].value==GF_C_PENDING_CHECK && sections==1 && rows==0) ||
                          (a[0].value==GF_C_PENDING_CELL && sections==10 && rows==205) ||
                          (a[0].value==GF_C_PENDING_EXIT && sections==11 && rows==206));
                }
            }
            core.expected_node=n->next[0];continue;
        }
        /* Restore row 34 must see the now disconnected C040 if incorrectly
         * lowered to LIVE: do not hide that defect by resetting the fake GPU. */
        if(n->expr_id==GF_C_EXPR_LIVE && n->rule_id!=578+34)
            values[a[0].value]=0xa5a55a5aU & width_mask(profile.targets[a[0].value].width);
        CHECK(!recover_plan_enter(&core,id));
        if(l->flags & REC_LAYOUT_CHECK_EXPR) {
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
    CHECK(rows==206 && sections==GF_C_SECTION_COUNT && pending+3==composed.c_unbound);
    CHECK(waits==(fp_on?70U:20U));
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
static void barriers(void)
{
    c_build();
    for(u32 i=composed.c_node;i<composed.plan.node_count;i++) {
        if(!(core.node_layout[i]->flags & REC_LAYOUT_UNBOUND))continue;
        /* No dependency/identity shortcut can turn a pending node into NOP. */
        core.state=REC_RUNNING;core.valid=true;core.entered=false;core.expected_node=i;
        core.plan=&composed.plan;core.profile=&profile;core.head=0;
        core.in_call=true;core.in_opcode=false;core.budget=100000;
        u32 before=actual_count;
        CHECK(recover_plan_enter(&core,i)<0 && core.state!=REC_RUNNING);
        CHECK(actual_count==before);
        /* Cleanup detaches: restore only the test fixture's bind state. */
        c_build();
    }
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
            if(frame_damage==6)frame_fail_at=1280;
            if(frame_damage==8)frame_abort_at=1280;
        }
    }
    return 0;
}
static void frame_cases(void)
{
    for(frame_damage=0;frame_damage<=8;frame_damage++) {
        c_build();c_arm();
        /* Only this test bypasses the still-unbound entry/Q/P waits. The
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
void _start(void)
{
    gf_fixture_init();prefix_equivalence();contracts();barriers();frame_cases();color_cases();c_walk(0,true);c_walk(1,true);c_walk(1,false);
    output(failures?"C FAIL\n":"C PASS\n");
    __asm__ volatile("int $0x80"::"a"(1),"b"(failures?1:0):"memory");for(;;){}
}
