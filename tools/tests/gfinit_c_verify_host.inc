static u32 policy_bad_key, policy_reads, policy_used, policy_bus_fault;
static char policy_line[RECOVER_ROW_MAX], policy_end[RECOVER_ROW_MAX];
static int policy_put(void *v,u32 ch)
{
    (void)v;CHECK(policy_used+1<sizeof(policy_line));
    policy_line[policy_used++]=(char)ch;policy_line[policy_used]=0;
    if(ch=='\n') {
        if(frame_contains(policy_line,"phase=gf_bus event=END"))
            for(u32 i=0;i<=policy_used;i++)policy_end[i]=policy_line[i];
        policy_used=0;
    }
    return 0;
}
static const struct recover_transport policy_transport={
    .uart={exec_log_ready,policy_put,exec_log_wait},.frame_us=1,.max_polls=1048576};
static int policy_read(void *v,const struct recover_bus_access *a,u32 *value)
{
    int rc=bus_read_test(v,a,value);
    if(a->key==policy_bad_key && ++policy_reads>1){if(policy_bus_fault)return -1;*value^=1;}
    return rc;
}
static int policy_step(struct recover *r,void *v)
{ u32 value=0;(void)v;return recover_plan_access(r,&value); }
static void verify_policy_cases(void)
{
    c_build();u32 hash=core.bound_hash,crc=profile.crc;
    field("default_hash",hash);field("default_crc",crc);
    CHECK(!gfinit_verify_policy(&composed,&profile,0,0,0));
    core.plan=0;CHECK(!contract_bind() && hash!=core.bound_hash && crc!=profile.crc);
    hash=core.bound_hash;crc=profile.crc;
    CHECK(!gfinit_verify_policy(&composed,&profile,GF_VERIFY_REQUIRED,0,0));
    core.plan=0;CHECK(!contract_bind() && hash==core.bound_hash && crc==profile.crc);
    u32 recorded=0,required=0;
    for(u32 i=0;i<profile.rule_count;i++) {
        const struct recover_rule *w=&profile.rules[i];
        if(w->phase!=REC_MODE_C) {CHECK(!(w->flags&REC_VERIFY_RECORD));continue;}
        const struct recover_target *t=&profile.targets[w->key];
        if(w->substep==GF_C_SELECT)CHECK(!(w->flags&REC_VERIFY_RECORD));
        else if(t->space==REC_BAR_MMIO && (t->offset==0x68050c || t->offset==0x680524 ||
                t->offset==0x680580 || t->offset==0xc040 || t->offset==0x600800 ||
                t->offset==0x600804 || t->offset==0x68052c || t->offset==0x680608)) {
            CHECK(!(w->flags&REC_VERIFY_RECORD));required++;
        }
        if(w->flags&REC_VERIFY_RECORD)recorded++;
    }
    CHECK(recorded>0 && required>=8);
    const struct gfinit_no_verify indexed[]={{GF_VERIFY_CR,0x1c},{GF_VERIFY_SR,1}};
    CHECK(!gfinit_verify_policy(&composed,&profile,0,indexed,2));
    for(u32 i=0;i<profile.rule_count;i++) {
        const struct recover_rule *w=&profile.rules[i];
        const struct recover_target *t=&profile.targets[w->key];
        if(w->phase==REC_MODE_C && w->substep!=GF_C_SELECT && t->space==REC_VGA_INDEXED &&
           ((t->port==RECOVER_CRTC && t->index==0x1c) || (t->port==RECOVER_SEQ && t->index==1)))
            CHECK(w->flags&REC_VERIFY_RECORD);
    }
    const struct gfinit_no_verify missing={GF_VERIFY_MMIO,0x123456};
    CHECK(gfinit_verify_policy(&composed,&profile,0,&missing,1)<0);
    /* Required write mismatch still fails; optional/explicit mismatch closes
     * with rc=0. Use the real binder/access path, corrupt only the readback. */
    for(u32 mode=0;mode<4;mode++) {
        c_build();
        struct gfinit_no_verify reg={GF_VERIFY_MMIO,(mode==0 || mode==3)?0x680a24:0x68050c};
        CHECK(!gfinit_verify_policy(&composed,&profile,mode!=2?GF_VERIFY_REQUIRED:0,
                                    mode==2?&reg:0,mode==2?1:0));
        core.plan=0;CHECK(!contract_bind());c_arm();
        u32 id=composed.c_node;
        while(id<composed.plan.node_count) {
            const struct recover_rule *w=core.node_write[id];
            if(w && w->substep!=GF_C_SELECT && profile.targets[w->key].offset==reg.reg)break;
            id++;
        }
        CHECK(id<composed.plan.node_count);policy_bad_key=core.node_write[id]->key;policy_reads=0;policy_bus_fault=mode==3;
        executor_bus.read=policy_read;core.expected_node=id;
        core.transport=&policy_transport;policy_used=0;policy_end[0]=0;exec_log_busy=false;
        CHECK(!recover_plan_enter(&core,id));
        int rc=policy_step(&core,0);
        output("VERIFY execution mode/rc/reads/state/reason ");val(mode);val((u32)rc);val(policy_reads);val(core.state);val(core.reason);output("\n");
        CHECK((rc!=0)==(mode==1 || mode==3));
        if(mode==3)CHECK(core.reason==REC_BUS);
        else if(mode==1)CHECK(core.reason==REC_VERIFY);
        else {
            CHECK(core.state==REC_RUNNING && !core.failed && policy_reads>=2);
            CHECK(frame_contains(policy_end," mismatch=1 space=1 port=0 index=4294967295 rc=0"));
            output(policy_end);
        }
    }
    output("VERIFY default hash/CRC, required targets, explicit override, unmatched selector and mismatch execution PASS\n");
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
