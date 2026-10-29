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
