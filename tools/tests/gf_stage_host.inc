/* T1 acceptance using real state, builder, binder, execution and S0. */
#include "../../kdebug/gf_stage_state.c"
static struct gf_stage_state stage_fixture;
static void stage_baseline(void)
{
    c_build();c_arm();
    stage_fixture=(struct gf_stage_state){0};
    CHECK(!gfinit_plan_baseline(&composed,&profile,&stage_fixture,rom,sizeof(rom)));
    values[stage_fixture.keys[4]]=0xfe; /* CR52: independent non-cumulative oracle */
    values[stage_fixture.keys[GF_STAGE_GATE]]=0x12345678;
    CHECK(!gf_stage_capture(&core,&stage_fixture));
    CHECK(stage_fixture.initialized && stage_fixture.values[4]==0xfe);
    CHECK(stage_fixture.values[GF_STAGE_GATE]==0x12345678);
    values[stage_fixture.keys[4]]=2;
    values[stage_fixture.keys[GF_STAGE_GATE]]=0x12305678; /* PLL interrupted */
    CHECK(!gf_stage_capture(&core,&stage_fixture));
    CHECK(stage_fixture.values[4]==0xfe && stage_fixture.values[GF_STAGE_GATE]==0x12345678);
    core.gf_stage=&stage_fixture;
    CHECK(!trial_end(&core,&core.token,REC_ABORT));
    CHECK(stage_fixture.initialized && stage_fixture.s0_ok && core.s0_verified && !core.plan);
    u32 sid=core.token.session;
    CHECK(!recover_restore_session(&core));
    CHECK(core.token.session==sid && stage_fixture.initialized && stage_fixture.s0_ok);
    CHECK(!gf_stage_entry(&core));
    core.failed=true;core.state=REC_FAILED;core.reason=REC_VERIFY;
    CHECK(recover_restore_session(&core)<0 && core.failed);
    CHECK(stage_fixture.initialized && stage_fixture.s0_ok && core.s0_verified);
    for(u32 bad=0;bad<5;bad++) {
        u32 command=rawcommand,bar=rawbars[2],pmc=values[REC_GF_T_PMC_ENABLE];
        if(bad==0)rawcommand^=1;
        if(bad==1)rawbars[2]^=0x1000;
        if(bad==2)values[REC_GF_T_PMC_ENABLE]^=1;
        if(bad==3)values[REC_GF_T_PBUS1854]=1;
        if(bad==4)route_value=1;
        CHECK(gf_stage_entry(&core)<0);
        rawcommand=command;rawbars[2]=bar;values[REC_GF_T_PMC_ENABLE]=pmc;
        values[REC_GF_T_PBUS1854]=route_value=0;
    }
}
static void stage_rebuild(void)
{
    CHECK(!recover_gf_profile_expand(&profile,&composed));
    CHECK(!gfinit_plan_rebuild_c(&composed,&decoded,&profile,&stage_fixture));
    profile.expected_bdf=RECOVER_GF_BDF;profile.expected_device=RECOVER_GF_DEVICE;
    profile.trial_limit=profile.opcode_limit=60*PIT_HZ;
    profile.restore_limit=180*PIT_HZ;profile.restore_max_polls=262144;
    profile.restore_log_bytes=131072;profile.max_steps=4096;profile.max_polls=65536;
    decoded.plan=composed.plan;
    CHECK(composed.c_node==0 && composed.plan.entry_count==1 && !composed.plan.capture_count);
    CHECK(composed.plan.nodes[0].phase==REC_MODE_C && profile.cold_required);
    core.c_only=true;core.gf_stage=&stage_fixture;stage_fixture.entry_ok=true;
}
static void stage_gates(void)
{
    stage_baseline();stage_rebuild();
    struct recover original_owner=*recover_owner();
    core.state=REC_FAILED;core.failed=core.cold_latched=true;
    /* All three gates must independently require the same E. No early clear. */
    for(u32 bad=0;bad<5;bad++) {
        bool *flag=bad==0?&core.c_only:bad==1?&stage_fixture.initialized:
            bad==2?&stage_fixture.entry_ok:bad==3?&stage_fixture.s0_ok:&core.s0_verified;
        *flag=false;
        *recover_owner()=core;
        CHECK(recover_owner_configure(&executor_bus,0,&profile,false)<0);
        CHECK(contract_bind()<0 && core.failed && core.cold_latched);
        *flag=true;
    }
    *recover_owner()=core;
    CHECK(!recover_owner_configure(&executor_bus,0,&profile,false));
    CHECK(recover_owner()->failed && recover_owner()->cold_latched);
    *recover_owner()=original_owner;
    CHECK(!contract_bind() && core.failed && core.cold_latched);
    stage_fixture.entry_ok=false;
    CHECK(trial_begin(&core,&profile,hash,profile.trial_limit)<0 && core.failed && core.cold_latched);
    stage_fixture.entry_ok=true;
    release_plan(&core);
    CHECK(!contract_bind());
    CHECK(!trial_begin(&core,&profile,hash,profile.trial_limit));
    CHECK(!core.failed && core.cold_latched && core.state==REC_PAUSED);
    /* Literal initial values, even after an interrupted PLL and another build. */
    for(u32 pass=0;pass<2;pass++) {
        u32 latency=0,gate=0;
        for(u32 id=0;id<composed.plan.node_count;id++) {
            const struct recover_plan_node *n=&composed.plan.nodes[id];
            if(n->expr_id!=GF_C_EXPR_BASE)continue;
            const struct recover_plan_arg *a=&composed.plan.args[n->arg_first];
            const struct recover_target *t=&profile.targets[a[0].value];
            u32 args5[5],value;
            for(u32 j=0;j<5;j++)args5[j]=a[j+1].value;
            CHECK(!c_value(args5,5,&value));
            if(t->space==REC_VGA_INDEXED && t->port==0x3d4 && t->index==0x52) {
                CHECK(value==2);latency++;
            }
            if(t->space==REC_BAR_MMIO && t->offset==0xc040) {
                CHECK(value==0x12345678);gate++;
            }
        }
        CHECK(latency==1 && gate==1);
        CHECK(!trial_end(&core,&core.token,REC_ABORT) && stage_fixture.initialized);
        CHECK(!recover_restore_session(&core) && stage_fixture.s0_ok);
        if(!pass) {stage_rebuild();CHECK(!contract_bind());CHECK(!trial_begin(&core,&profile,hash,profile.trial_limit));}
    }
    /* S0 failure invalidates initialization, including a preceding CHECK failure. */
    core.access_blocked=true;route_fault=3;
    CHECK(recover_restore_session(&core)<0);
    CHECK(!core.s0_verified && !stage_fixture.initialized && !gf_stage_reentry(&core));
    output("T1 baseline once, three E gates, cold latch, CR52/C040, restore and S0 failure PASS\n");
}
static void stage_record(void)
{
    /* Entry TARGET, route and 98 cell still stop; only exit TARGET continues. */
    for(u32 kind=0;kind<4;kind++) {
        const struct check_fixture *f=kind==0?&entry_fixture:&exit_fixture;
        restore_checks(f);core.verify_record=true;
        check_damage_slot=RECOVER_NONE;check_damage_mask=1;
        for(u32 id=core.expected_node;id<composed.plan.node_count;id++) {
            const struct recover_plan_node *n=&composed.plan.nodes[id];
            if(!(core.node_layout[id]->flags&REC_LAYOUT_MODE_CHECK))continue;
            const struct recover_plan_arg *a=&composed.plan.args[n->arg_first];
            u32 wanted=kind<2?REC_CHECK_TARGET:kind==2?REC_CHECK_ROUTE:REC_CHECK_CELL;
            if(a[REC_CHECK_SOURCE].value!=wanted || !a[REC_CHECK_MASK].value)continue;
            CHECK(((n->flags&REC_NODE_CHECK_ENTRY)!=0)==(kind==0));
            check_damage_slot=a[REC_CHECK_SLOT].value;
            check_damage_mask=a[REC_CHECK_MASK].value & (0U-a[REC_CHECK_MASK].value);
            break;
        }
        CHECK(check_damage_slot!=RECOVER_NONE);
        int rc=trial_step(&core,&core.token,true,checks_step,0);
        CHECK((rc!=0)==(kind!=1));
        if(kind==1)CHECK(core.state==REC_PAUSED && check_record_count>1 && !core.failed);
        else CHECK(core.state==REC_FAILED);
    }
    /* A complete comparison mismatch is evidence; a later bus error is fatal. */
    for(u32 bus=0;bus<2;bus++) {
        c_build();c_arm();core.verify_record=true;
        u32 id=composed.c_node;
        while(composed.plan.nodes[id].substep!=GF_C_VRAM_WRITE)id++;
        core.expected_node=id;core.state=REC_PAUSED;core.in_call=core.in_opcode=false;
        frame_damage=bus?6:1;
        /* Damage byte zero in both cases, including before the later bus fault. */
        CHECK(!trial_step(&core,&core.token,true,frame_step,0)==!bus);
        if(bus)CHECK(core.reason==REC_BUS && core.state==REC_FAILED);
        else CHECK(core.state==REC_PAUSED && core.expected_node==id+2);
    }
    output("T1 record entry/route/cell required, exit TARGET/VRAM evidence, later bus failure PASS\n");
}

static u32 s0_fault, s0_injected, s0_line_used;
static char s0_line[RECOVER_ROW_MAX];
static int stage_local_cleanup(void *v)
{
    (void)v;
    if(s0_fault==1) {s0_injected++;return -1;}
    return gfinit_bus_local_cleanup(&device,&core);
}
static int stage_s0_write(void *v,const struct recover_bus_access *a,u32 value)
{
    if(s0_fault==2 && core.state==REC_RESTORING &&
       a->key==REC_GF_T_PMC_IRQ && !s0_injected++)return -1;
    return bus_write_test(v,a,value);
}
static int stage_s0_put(void *v,u32 ch)
{
    (void)v;
    CHECK(s0_line_used+1<sizeof(s0_line));
    s0_line[s0_line_used++]=(char)ch;s0_line[s0_line_used]=0;
    if(ch!='\n')return 0;
    bool inject=!s0_injected &&
        (s0_fault==3 || (s0_fault==4 && frame_contains(s0_line,"phase=final")));
    s0_line_used=0;
    if(inject) {s0_injected++;return -1;}
    return 0;
}
static void stage_s0_failures(void)
{
    for(u32 which=0;which<=4;which++) {
        stage_baseline();
        /* The first functional CHECK reason must survive every later fault. */
        CHECK(core.failed && core.reason==REC_VERIFY && stage_fixture.initialized);
        s0_fault=which;s0_injected=s0_line_used=0;
        executor_bus.local_cleanup=stage_local_cleanup;
        executor_bus.write=stage_s0_write;
        struct recover_transport transport=prefix_transport;
        transport.uart.put=stage_s0_put;
        core.transport=&transport;
        CHECK(recover_restore_session(&core)<0);
        CHECK(core.reason==REC_VERIFY && core.s0_verified);
        CHECK((s0_injected!=0)==(which!=0));
        CHECK(stage_fixture.initialized==(which==0) && stage_fixture.s0_ok==(which==0));
        CHECK((gf_stage_entry(&core)==0)==(which==0));
        core.transport=0;
    }
    output("T1 CHECK then S0 local/quiesce/first-send/final-send failure revokes init PASS\n");
}

static u32 stop_lock;
static int stage_until_lock(struct recover *r,void *v)
{
    (void)v;
    while(r->expected_node!=RECOVER_NONE) {
        u32 id=r->expected_node,value=0,edge=0;
        const struct recover_plan_node *n=&r->plan->nodes[id];
        const struct recover_opcode_layout *l=r->node_layout[id];
        if(recover_plan_enter(r,id))return -1;
        int rc=0;
        if(l->flags&(REC_LAYOUT_MODE_CHECK|REC_LAYOUT_MODE_WAIT|REC_LAYOUT_MODE_ROUTE))
            rc=gfinit_plan_c_control(&composed,r);
        else if(l->flags&REC_LAYOUT_CHECK_EXPR)rc=recover_plan_check(r,&edge);
        else if(l->dir_substep!=RECOVER_NONE ||
                (l->flags&(REC_LAYOUT_MODE_VRAM|REC_LAYOUT_MODE_COLOR)))
            rc=recover_plan_access(r,&value);
        if(rc || recover_plan_leave(r,edge)) {
            field("lock_failed_node",id);field("lock_reason",r->reason);
            field("lock_first_key",r->first_key);field("lock_substep",n->substep);
            field("lock_index",profile.targets[r->first_key].index);return -1;
        }
        if(l->dir_substep==REC_DATA && r->plan->args[n->arg_first].value==stop_lock)
            return 0;
    }
    return -1;
}
static void stage_local_locks(void)
{
    const u32 keys[]={REC_GF_T_CR1F,REC_GF_T_CR11,REC_GF_T_CR44};
    for(u32 stop=0;stop<3;stop++) {
        stage_baseline();
        for(u32 pass=0;pass<2;pass++) {
            stage_rebuild();CHECK(!contract_bind());core.plan=&composed.plan;
            CHECK(!trial_begin(&core,&profile,hash,profile.trial_limit));
            frame_writes=frame_reads=drains=frame_points=0;
            color_count=0;
            /* Different originals on each call; unrelated to the init baseline. */
            const u32 originals[]={pass?3U:0U,pass?0x91U:0xa7U,pass?3U:0U};
            for(u32 i=0;i<3;i++)values[keys[i]]=originals[i];
            stop_lock=keys[stop];
            int rc=trial_step(&core,&core.token,true,stage_until_lock,0);
            CHECK(!rc);
            if(rc) {field("lock_stop",stop);field("lock_pass",pass);return;}
            bool saved=false;
            for(u32 i=0;i<core.local_saved_count;i++) {
                const struct recover_local_saved *s=&core.local_saved[i];
                if(s->key==stop_lock && s->dir_substep==REC_DATA) {
                    CHECK(s->value==originals[stop]);saved=true;
                }
            }
            CHECK(saved);
            executor_bus.local_cleanup=stage_local_cleanup;s0_fault=0;
            CHECK(!trial_end(&core,&core.token,REC_ABORT));
            for(u32 i=0;i<3;i++)CHECK(values[keys[i]]==originals[i]);
            CHECK(stage_fixture.initialized && stage_fixture.s0_ok && !core.local_saved_count);
            CHECK(stage_fixture.values[4]==0xfe && stage_fixture.values[GF_STAGE_GATE]==0x12345678);
            CHECK(!recover_restore_session(&core));
        }
    }
    output("T1 C-only each lock write/abort restores per-call CR1F/CR11/CR44 originals PASS\n");
}
