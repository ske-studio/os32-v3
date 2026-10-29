static void stage_shared_sequence(void)
{
    struct stage_row {u32 sub,space,offset,port,index,width,verify;};
    static struct stage_row rows[C_EXTRA_NODES];
    stage_baseline();
    u32 count=composed.plan.node_count-composed.c_node;
    CHECK(count<C_EXTRA_NODES);
    for(u32 i=0;i<count;i++) {
        const struct recover_plan_node *n=&composed.plan.nodes[composed.c_node+i];
        u32 sub=n->substep==GF_C_CHECK_SAVED?GF_C_CHECK:n->substep;
        rows[i]=(struct stage_row){.sub=sub};
        if(n->rule_id==RECOVER_NONE)continue;
        const struct recover_target *t=&profile.targets[composed.plan.args[n->arg_first].value];
        const struct recover_rule *w=rule_by_id(&profile,n->rule_id);
        rows[i]=(struct stage_row){sub,t->space,t->offset,t->port,t->index,t->width,w->verify_mask};
    }
    stage_rebuild();
    CHECK(composed.plan.node_count==count);
    for(u32 i=0;i<count;i++) {
        const struct recover_plan_node *n=&composed.plan.nodes[i];
        u32 sub=n->substep==GF_C_CHECK_SAVED?GF_C_CHECK:n->substep;
        CHECK(sub==rows[i].sub);
        if(n->rule_id==RECOVER_NONE)continue;
        const struct recover_target *t=&profile.targets[composed.plan.args[n->arg_first].value];
        const struct recover_rule *w=rule_by_id(&profile,n->rule_id);
        CHECK(t->space==rows[i].space && t->offset==rows[i].offset && t->port==rows[i].port);
        CHECK(t->index==rows[i].index && t->width==rows[i].width && w->verify_mask==rows[i].verify);
    }
    output("T2 same-session and C-only share ordered writes, waits, sections and verification masks PASS\n");
}
static void stage_gates(void)
{
    stage_shared_sequence();
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
