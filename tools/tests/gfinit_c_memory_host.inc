static char frame_log[RECOVER_ROW_MAX], frame_records[4][RECOVER_ROW_MAX];
static u32 frame_log_used, frame_record_count, frame_damage;
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
