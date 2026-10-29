static void lifecycle(void)
{
    setup();CHECK(call(GF_OP_INSPECT,0,0)==0 && writes==0 && reads>0 && owner->inspected);
    CHECK(core.ops==&base && core.opaque==0 && core.s0_profile==&s0);
    CHECK(call(GF_OP_SIZE_BARS,0,0)<0 && begins==0);
    struct gfinit_request q=request(GF_ALLOW_WRITE,0);q.session++;
    CHECK(gfinit_call(GF_OP_SIZE_BARS,&q,sizeof(q),&base,&transport,&s0)<0 && begins==0);
    CHECK(call(GF_OP_SIZE_BARS,GF_ALLOW_WRITE,0)==0);
    CHECK(core.ops==&owner->ops && core.opaque==&owner->bus && core.s0_profile==&s0);
    CHECK(begins==1 && steps==1 && ends==1 && cleans==1 && events[0]=='B' && events[1]=='S' && events[2]=='E');
    CHECK(allocations==1 && frees==0);
    CHECK(call(GF_OP_PREPARE,GF_ALLOW_WRITE,0)==0 && core.state==REC_PAUSED && begins==2 && steps==2);
    CHECK(call(GF_OP_RUN_ONE,GF_ALLOW_WRITE,1)<0 && writes==0);
    for(u32 main=0;main<5;main++) {
        CHECK(call(GF_OP_RUN_ONE,GF_ALLOW_WRITE,main)==0);
        u32 before=steps;CHECK(call(GF_OP_RUN_ONE,GF_ALLOW_WRITE,main)==0 && steps==before);
    }
    CHECK(run_mask==31 && call(GF_OP_VERIFY,GF_ALLOW_WRITE,0)==0);
    #if GF_VPLL_PROBE
    CHECK(verify_calls==1 && core.state==REC_VERIFIED && frees==1 && !owner->rom && !owner->inspected);
#else
    CHECK(verify_calls==1 && core.state==REC_VERIFIED && !frees && owner->rom && owner->inspected && stage.initialized);
#endif
}
static void failures_test(void)
{
    setup();CHECK(call(GF_OP_INSPECT,0,0)==0);
    CHECK(call(GF_OP_ABORT,GF_ALLOW_WRITE,0)==0 && frees==1 && !writes && !begins && !steps && !ends);
    setup();prepared();gfinit_recover_end();CHECK(frees==0 && owner->rom);
    core.state=REC_VERIFIED;gfinit_recover_end();CHECK(frees==1 && !owner->rom);
    gfinit_recover_end();CHECK(frees==1);
    CHECK(call(GF_OP_PREPARE,GF_ALLOW_WRITE,0)<0 && begins==2 && live_pages==0);
    CHECK(core.ops==&base && !core.opaque && !core.plan);
    setup();prepared();core.state=REC_FAILED;core.failed=true;core.cold_latched=true;
    u32 sid=core.token.session;
    gfinit_recover_end();CHECK(frees==1 && !owner->rom && !gf_lease.active);
    CHECK(core.ops==&base && !core.opaque && !core.plan && core.failed && core.cold_latched);
    gfinit_recover_end();
    CHECK(call(GF_OP_INSPECT,0,0)<0 && core.token.session==sid && core.state==REC_FAILED);
    CHECK(core.failed && core.cold_latched && frees==1);
    setup();CHECK(call(GF_OP_INSPECT,0,0)==0);step_fail=1;
    CHECK(call(GF_OP_SIZE_BARS,GF_ALLOW_WRITE,0)<0 && core.state==REC_FAILED && frees==1 && cleans==1);
    CHECK(call(GF_OP_SIZE_BARS,GF_ALLOW_WRITE,0)<0 && frees==1);
    setup();peek_fail=1;CHECK(call(GF_OP_INSPECT,0,0)<0 && frees==1 && !writes);
    setup();decode_fail=1;CHECK(call(GF_OP_INSPECT,0,0)<0 && frees==1 && !writes);
    setup();prepared();CHECK(call(GF_OP_ABORT,GF_ALLOW_WRITE,0)==0 && core.reason==REC_ABORT && frees==1 && cleans==2);
    setup();prepared();run_fail=1;CHECK(call(GF_OP_RUN_ONE,GF_ALLOW_WRITE,0)<0 && core.state==REC_FAILED && frees==1 && cleans==2);
    CHECK(call(GF_OP_RUN_ONE,GF_ALLOW_WRITE,0)<0 && frees==1);
    setup();CHECK(call(GF_OP_INSPECT,0,0)==0);CHECK(call(GF_OP_SIZE_BARS,GF_ALLOW_WRITE,0)==0);
    CHECK(call(GF_OP_INSPECT,0,0)==0 && owner->inspected && owner->rom && frees==1 && allocations==2);
}
static void ownership_test(void)
{
    const enum recover_state inactive[]={REC_IDLE,REC_VERIFIED};
    for(u32 i=0;i<sizeof(inactive)/sizeof(*inactive);i++){
        setup();CHECK(call(GF_OP_INSPECT,0,0)==0 && owner->rom);
        core.state=inactive[i];gf_device.generation++;
        CHECK(call(GF_OP_SIZE_BARS,GF_ALLOW_WRITE,0)<0);
        CHECK(!live_pages && !owner->inspected && !owner->rom && !gf_lease.active);
        CHECK(!writes && !begins && !steps && !ends && frees==1);
    }
    const enum hwdev_owner_state busy[] = {
        HWDEV_UNCLASSIFIED, HWDEV_ROOT_IDE, HWDEV_LAN_SERIALFS, HWDEV_DISPLAY
    };
    for (u32 i=0;i<sizeof(busy)/sizeof(*busy);i++) {
        setup();gf_device.state=busy[i];
        CHECK(call(GF_OP_INSPECT,0,0)<0 && !reads && !writes && !live_pages && !gf_lease.active);
    }
    setup();prepared();
    CHECK(gf_lease.active);
    u32 before=steps;
    owner->generation--;
    CHECK(call(GF_OP_RUN_ONE,GF_ALLOW_WRITE,0)<0 && steps==before && !writes);
    owner->generation++;
    gf_device.generation++;
    CHECK(call(GF_OP_RUN_ONE,GF_ALLOW_WRITE,0)<0 && steps==before && !writes);
    gf_device.generation--;
    gf_lease.session++;
    CHECK(call(GF_OP_RUN_ONE,GF_ALLOW_WRITE,0)<0 && steps==before && !writes);
    gf_lease.session--;
    CHECK(call(GF_OP_ABORT,GF_ALLOW_WRITE,0)==0 && !gf_lease.active && !live_pages);
    struct hwdev_lease old=gf_lease;
    CHECK(call(GF_OP_INSPECT,0,0)==0 && gf_lease.generation>old.generation);
    CHECK(!hwdev_lease_matches(&gf_lease,&gf_device,old.generation,core.token.session));
}
static void number(u32 n)
{ char buf[16];u32 i=0;do{buf[i++]='0'+n%10;n/=10;}while(n);while(i){char c[2]={buf[--i],0};output(c);} }
static void allocation_test(void)
{
    for(u32 fail=1;fail<=5;fail++) {
        setup();plan_nodes=RECOVER_NODE_MAX;fail_alloc=fail;
        int rc=call(GF_OP_INSPECT,0,0);
        if(!rc)rc=call(GF_OP_SIZE_BARS,GF_ALLOW_WRITE,0);
        if(!rc)rc=call(GF_OP_PREPARE,GF_ALLOW_WRITE,0);
        CHECK(rc<0 && live_pages==0 && !owner->inspected && writes==0);
        CHECK(alloc_calls==fail);
    }
    setup();plan_nodes=RECOVER_NODE_MAX;prepared();
    CHECK(peak_pages==(PAGE_ALIGN_UP(sizeof(struct gfinit_decode))+
        PAGE_ALIGN_UP(sizeof(struct gfinit_plan))+GF_ROM_MAX+GF_FLOW_MAX_BYTES)/PAGE_SIZE && writes==0);
    CHECK(peak_pages<=TEST_PAGE_BUDGET);
    output("bytes control/decode/plan/ROM/flow pages_peak: ");
    number(sizeof(control));output("/");number(sizeof(struct gfinit_decode));output("/");
    number(sizeof(struct gfinit_plan));output("/");number(GF_ROM_MAX);output("/");
    number(owner->flow_bytes);output(" pages=");number(peak_pages);output("\n");
    CHECK(call(GF_OP_ABORT,GF_ALLOW_WRITE,0)==0 && live_pages==0);
    setup();probe_fail=1;CHECK(call(GF_OP_INSPECT,0,0)<0 && live_pages==0);
    setup();plan_nodes=RECOVER_NODE_MAX+1U;
    CHECK(call(GF_OP_INSPECT,0,0)==0);
    CHECK(call(GF_OP_SIZE_BARS,GF_ALLOW_WRITE,0)==0);
    CHECK(call(GF_OP_PREPARE,GF_ALLOW_WRITE,0)<0 && live_pages==0 && writes==0);
    CHECK(alloc_calls==4 && begins==1); /* refused before oversized flow */
    /* Enough free pages in total, but each hole is smaller than the requested
     * block. Exercise every allocation point, including flow replacement. */
    for(u32 fail=1;fail<=5;fail++) {
        setup();plan_nodes=RECOVER_NODE_MAX;
        int rc=0;
        if(fail>3)rc=call(GF_OP_INSPECT,0,0);
        if(fail>4)rc=call(GF_OP_SIZE_BARS,GF_ALLOW_WRITE,0);
        CHECK(rc==0);
        /* INSPECT now allocates compiled, decode, then ROM. Leave exactly
         * the preceding blocks contiguous, fragment the requested block. */
        u32 keep=0;
        if(fail==2 || fail==3)keep=PAGE_ALIGN_UP(sizeof(struct gfinit_plan))/PAGE_SIZE;
        if(fail==3)keep+=PAGE_ALIGN_UP(sizeof(struct gfinit_decode))/PAGE_SIZE;
        u32 first=0;
        while(first<TEST_ARENA_PAGES && page_owner[first])first++;
        for(u32 i=first+keep;i<TEST_ARENA_PAGES;i++)
            if(!page_owner[i] && (i-first-keep)%2==0)page_owner[i]=2;
        u32 available=0;for(u32 i=0;i<TEST_ARENA_PAGES;i++)available+=page_owner[i]==0;
        CHECK(available>=128);
        if(fail<=3)rc=call(GF_OP_INSPECT,0,0);
        else if(fail==4)rc=call(GF_OP_SIZE_BARS,GF_ALLOW_WRITE,0);
        else rc=call(GF_OP_PREPARE,GF_ALLOW_WRITE,0);
        CHECK(rc<0 && alloc_calls==fail && writes==0 && live_pages==0);
        for(u32 i=0;i<TEST_ARENA_PAGES;i++)CHECK(page_owner[i]!=1);
    }
}
static void expansion_lifetime_test(void)
{
    setup();CHECK(call(GF_OP_INSPECT,0,0)==0);
    CHECK(owner->compiled && expand_calls==1);
    u32 inspect_pages=PAGE_ALIGN_UP(sizeof(struct gfinit_plan))/PAGE_SIZE +
        PAGE_ALIGN_UP(sizeof(struct gfinit_decode))/PAGE_SIZE + GF_ROM_MAX/PAGE_SIZE;
    CHECK(live_pages==inspect_pages);
    struct gfinit_plan *backing=owner->compiled;
    CHECK(call(GF_OP_SIZE_BARS,GF_ALLOW_WRITE,0)==0 && expand_calls==2);
    CHECK(owner->compiled==backing);
    CHECK(call(GF_OP_PREPARE,GF_ALLOW_WRITE,0)==0 && expand_calls==3);
    struct recover_bus_access access={0};u32 value;
    CHECK(gf_read(&owner->bus,&access,&value)==0 && plan_reads==1);
    /* B4: retain a previous RUN plan and enter SIZE_BARS without end release. */
    core.state=REC_VERIFIED;
    CHECK(gf_begin(&core,owner,&transport,true,GF_ALLOW_WRITE,0,0)==0 && expand_calls==4);
    core.plan=&owner->size_plan;
    CHECK(gf_read(&owner->bus,&access,&value)==0 && plan_reads==1);
    CHECK(owner->compiled==backing && owner->profile.rule_count==1);
    /* A refused detach must leave all backing, profile and lease intact. */
    u32 held=live_pages, calls=alloc_calls;
    detach_fail=1;
    CHECK(gf_release_rom(owner)<0 && live_pages==held && gf_lease.active);
    CHECK(owner->compiled==backing && owner->profile.rules==backing->rules);
    CHECK(gf_inspect(&core,&base,&transport,&s0)<0 && alloc_calls==calls);
    CHECK(live_pages==held && owner->compiled==backing && gf_lease.active);
    detach_fail=0;CHECK(gf_release_rom(owner)==0 && !live_pages && !gf_lease.active);
    CHECK(!owner->compiled && !owner->profile.rules && !owner->profile.rule_count);
    setup();expand_fail=1;CHECK(call(GF_OP_INSPECT,0,0)<0);
    CHECK(!live_pages && !gf_lease.active && !owner->inspected && !reads);
    setup();boot_fail=1;CHECK(call(GF_OP_INSPECT,0,0)<0);
    CHECK(!live_pages && !gf_lease.active && !owner->inspected);
    setup();probe_fail=1;CHECK(call(GF_OP_INSPECT,0,0)<0);
    CHECK(!live_pages && !gf_lease.active && !owner->inspected);
    setup();CHECK(call(GF_OP_INSPECT,0,0)==0);expand_fail=1;
    CHECK(call(GF_OP_SIZE_BARS,GF_ALLOW_WRITE,0)<0 && !live_pages && !gf_lease.active);
}
