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
