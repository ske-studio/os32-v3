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
    CHECK(!gfinit_exec_prepare(&core,&execution));
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
    const struct gf_mode_row route={.kind=10,.reg=0xfac,.index=~0U,.width=1,.count=1};
    const struct gf_mode_row *row=ordinal==gf_mode_row_count ? &route : &gf_mode_rows[ordinal];
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
                const struct gf_mode_row *row=&gf_mode_rows[rows];
                u32 v=row->expr==GF_C_STATE ? ((const u8 *)&mode)[row->source] : row->bits;
                dump_row(rows++,v,1,RECOVER_NONE);
            }
        } else if(l->dir_substep!=RECOVER_NONE) {
            CHECK(!recover_plan_access(&core,&value));
            if(n->substep>=REC_GF_C_RULE_FIRST) {
                CHECK(rows<gf_mode_row_count && n->rule_id==REC_GF_C_RULE_FIRST+rows);
                CHECK(core.node_write[id]->verify_mask==gf_mode_rows[rows].verify);
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
