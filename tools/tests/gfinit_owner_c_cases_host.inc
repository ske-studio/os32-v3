static void mode_c_test(void)
{
    setup();prepared();
    CHECK(!core.route_write_approved);
    CHECK(owner->prepare_flags==GF_ALLOW_WRITE && !owner->compiled->mode_flags);
    u32 before=begins;
    CHECK(call(GF_OP_PREPARE,GF_ALLOW_WRITE|GF_MODE_C,0)<0 && begins==before);
    CHECK(owner->prepare_flags==GF_ALLOW_WRITE && !owner->compiled->mode_flags);
    for(u32 main=0;main<GF_INIT_MAINS;main++) CHECK(call(GF_OP_RUN_ONE,GF_ALLOW_WRITE,main)==0);
    u32 old_writes=writes;
    CHECK(call(GF_OP_MODESET,GF_ALLOW_WRITE,0)<0 && writes==old_writes && !mode_calls);
    CHECK(call(GF_OP_ABORT,GF_ALLOW_WRITE,0)==0);
    setup();
    CHECK(call(GF_OP_INSPECT,GF_MODE_C,0)<0 && !reads);
    CHECK(call(GF_OP_INSPECT,0,0)==0);
    CHECK(call(GF_OP_SIZE_BARS,GF_ALLOW_WRITE|GF_MODE_C,0)<0 && !begins);
    CHECK(call(GF_OP_SIZE_BARS,GF_ALLOW_WRITE,0)==0);
    CHECK(call(GF_OP_PREPARE,GF_MODE_C,0)<0);
    CHECK(call(GF_OP_PREPARE,GF_ALLOW_WRITE|4,0)<0); /* BUS50_ONE deferred */
#if GF_VPLL_PROBE
    CHECK(call(GF_OP_PREPARE,GF_ALLOW_WRITE|GF_MODE_C,0)<0 && !live_pages);
#else
    CHECK(call(GF_OP_PREPARE,GF_ALLOW_WRITE|GF_MODE_C,0)==0);
    CHECK(owner->prepare_flags==(GF_ALLOW_WRITE|GF_MODE_C));
    CHECK(owner->compiled->mode_flags==GF_MODE_C && core.state==REC_PAUSED);
    CHECK(core.route_write_approved);
    u32 budget_bytes=(128+(plan_nodes-1)*12)*(RECOVER_ROW_MAX+1U);
    u32 budget_seconds=(budget_bytes*10+9599)/9600;
    CHECK(core.log_limit==(budget_seconds*2+RECOVER_BUDGET_MARGIN_S)*PIT_HZ);
    CHECK(transport.max_polls==budget_bytes*2+1024);
    before=begins;
    CHECK(call(GF_OP_PREPARE,GF_ALLOW_WRITE|GF_MODE_C,0)==0 && begins==before);
    CHECK(call(GF_OP_PREPARE,GF_ALLOW_WRITE,0)<0 && begins==before);
    CHECK(owner->prepare_flags==(GF_ALLOW_WRITE|GF_MODE_C));
    for(u32 main=0;main<GF_INIT_MAINS;main++) {
        u32 old_writes=writes;
        CHECK(call(GF_OP_MODESET,GF_ALLOW_WRITE,0)<0 && writes==old_writes && !mode_calls);
        CHECK(call(GF_OP_RUN_ONE,GF_ALLOW_WRITE|GF_MODE_C,main)<0);
        CHECK(call(GF_OP_RUN_ONE,GF_ALLOW_WRITE,main)==0 && core.state==REC_PAUSED);
    }
    CHECK(owner->next_main==5);
    CHECK(call(GF_OP_VERIFY,GF_ALLOW_WRITE,0)<0 && !verify_calls && core.state==REC_PAUSED);
    struct gfinit_request wrong=request(GF_ALLOW_WRITE,0);wrong.session++;
    old_writes=writes;
    CHECK(gfinit_call(GF_OP_MODESET,&wrong,sizeof(wrong),&base,&transport,&s0)<0 && writes==old_writes);
    CHECK(call(GF_OP_MODESET,0,0)<0 && writes==old_writes);
    core.state=REC_RUNNING;
    CHECK(call(GF_OP_MODESET,GF_ALLOW_WRITE,0)<0 && writes==old_writes && !mode_calls);
    core.state=REC_PAUSED;owner->last_op=RECOVER_NONE;
    core.expected_node=owner->compiled->c_node;
    owner->compiled->layouts[0].flags=REC_LAYOUT_MODE_CHECK;
    core.node_layout[core.expected_node]=&owner->compiled->layouts[0];
    CHECK(call(GF_OP_MODESET,GF_ALLOW_WRITE,0)==0 && mode_calls==1 && writes==old_writes+1 && core.state==REC_PAUSED);
    CHECK(call(GF_OP_MODESET,GF_ALLOW_WRITE,0)==0 && mode_calls==1); /* retransmit */
    CHECK(call(GF_OP_ABORT,GF_ALLOW_WRITE,0)==0 && live_pages && stage.initialized && gf_lease.active);
    /* main4 -> C -> abort -> restore's release callback -> c-only.
     * The real core restore/sid behavior is covered by gf_stage_host.inc. */
    u32 held=live_pages, sid=core.token.session, init_runs=run_mask;
    void *backing=owner->flow;
    gfinit_recover_end();
    CHECK(stage.initialized && stage.s0_ok && gf_owned(&core) && gf_lease.session==sid);
    struct gfinit_request retry=request(GF_ALLOW_WRITE|GF_MODE_C|GF_VERIFY_RECORD,0);
    retry.session=0;
    CHECK(!gfinit_call(GF_OP_C_ONLY,&retry,sizeof(retry),&base,&transport,&s0));
    CHECK(core.token.session==sid+1 && gf_owned(&core) && stage.initialized);
    CHECK(live_pages==held && owner->flow==backing && run_mask==init_runs);
    CHECK(core.log_limit==1800*PIT_HZ && owner->profile.trial_limit==60*PIT_HZ);
    CHECK(!call(GF_OP_ABORT,GF_ALLOW_WRITE,0));
    reentry_fault=1;retry=request(GF_ALLOW_WRITE|GF_MODE_C,0);retry.session=0;
    CHECK(gfinit_call(GF_OP_C_ONLY,&retry,sizeof(retry),&base,&transport,&s0)<0);
    CHECK(!stage.initialized && !gf_lease.active && !live_pages);
    output("T1 owner main4/C/abort/restore callback/c-only lease and backing retained PASS\n");

#endif
#if !GF_VPLL_PROBE
    setup();CHECK(call(GF_OP_INSPECT,0,0)==0);CHECK(call(GF_OP_SIZE_BARS,GF_ALLOW_WRITE,0)==0);
    CHECK(call(GF_OP_PREPARE,GF_ALLOW_WRITE|GF_MODE_C,0)==0);
    for(u32 main=0;main<GF_INIT_MAINS;main++) CHECK(call(GF_OP_RUN_ONE,GF_ALLOW_WRITE,main)==0);
    core.expected_node=owner->compiled->c_node;
    owner->compiled->layouts[0].flags=REC_LAYOUT_MODE_CHECK;
    core.node_layout[core.expected_node]=&owner->compiled->layouts[0];mode_fail=1;
    CHECK(call(GF_OP_MODESET,GF_ALLOW_WRITE,0)<0 && mode_calls==1 && core.state==REC_FAILED && cleans==2 && !live_pages);
#endif
    output("MODE_C modeset gates / PAUSED / failure / cache PASS\n");
}
static void owner_verify_test(void)
{
    setup();CHECK(!call(GF_OP_INSPECT,0,0));CHECK(!call(GF_OP_SIZE_BARS,GF_ALLOW_WRITE,0));
    CHECK(call(GF_OP_PREPARE,GF_ALLOW_WRITE|GF_VERIFY_REQUIRED,0)<0);
    CHECK(call(GF_OP_MODESET,GF_ALLOW_WRITE|GF_VERIFY_REQUIRED,0)<0);
    struct gfinit_prepare_request payload={.request=request(GF_ALLOW_WRITE|GF_MODE_C|GF_NO_VERIFY_FILE,0),
        .count=1,.regs={{GF_VERIFY_MMIO,0x123456}}};
    payload.request.size=sizeof(payload);
    u32 before=begins;
    CHECK(gfinit_call(GF_OP_PREPARE,&payload,sizeof(payload),&base,&transport,&s0)<0 && begins==before);
    CHECK(core.state==REC_VERIFIED);
    output("A2 unmatched file selector and C-only option gates PASS\n");
}
