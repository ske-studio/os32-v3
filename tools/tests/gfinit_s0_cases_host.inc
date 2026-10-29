
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
