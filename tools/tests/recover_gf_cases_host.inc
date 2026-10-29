static void inventory_test(void)
{
    CHECK(recover_profile_ok(&gf_fixture_profile)==0);
    CHECK(ARRAY_SIZE(inventory)==619 && ARRAY_SIZE(gf_effects)==619);
    u32 counts[5]={0};
    for(u32 i=0;i<ARRAY_SIZE(inventory);i++) {
        const struct gf_classification *c=&inventory[i];
        const struct gf_effect *e=&gf_effects[i];
        CHECK(c->effect==i && c->kind<5); counts[c->kind]++;
        if(c->kind==REC_GF_PLL || c->kind==REC_GF_PLL_UNUSED) {
            CHECK(e->expression==GF_PLL);
            if(c->kind==REC_GF_PLL) {
                const struct recover_rule *w=rule_by_id(&gf_fixture_profile,c->rule);
                CHECK(w && w->phase==REC_RUN && w->pc==e->pc && gf_fixture_profile.targets[w->key].offset==e->reg);
            } else CHECK(c->rule==RECOVER_NONE &&
                ((e->pc==0xe728 && (e->reg==0x4038 || e->reg==0x403c || e->reg==0x4600)) ||
                 (e->pc==0xe623 && e->reg==0x1584)));
            continue;
        }
        if(c->kind!=REC_GF_RULE) {
            const struct recover_rule *w=rule_by_id(&gf_fixture_profile,c->rule);
            CHECK(w && w->phase==REC_RUN && w->pc==e->pc && w->substep==i);
            CHECK(w && w->value.mode==REC_EXPR &&
                (c->kind==REC_GF_COPY ? (w->value.expr_id==REC_GF_EXPR_COPY || w->value.expr_id==REC_GF_EXPR_COPY_NV) :
                 w->value.expr_id==REC_GF_SAVED_BAR));
            continue;
        }
        const struct recover_rule *w=rule_by_id(&gf_fixture_profile,c->rule);
        CHECK(w!=0); if(!w) continue;
        const struct recover_target *t=&gf_fixture_profile.targets[w->key];
        struct recover_bus_access bus;
        u32 dir=e->space ? (e->reg%2 ? REC_DATA : REC_SELECT) : REC_WRITE;
        CHECK(recover_target_address(t,dir,&bus)==0);
        CHECK(bus.offset==e->reg && bus.width==e->width && t->head==e->head);
        CHECK(w->pc==e->pc && w->phase==REC_RUN && w->substep==i);
        CHECK(e->expression==GF_CONSTANT || e->expression==GF_SELECT);
        if(e->space) CHECK(t->port==e->port && t->index==e->index);
        u32 full=width_mask(e->width), mask=(~e->keep|e->value)&full;
        if(e->reg==0x1850) { CHECK(w->value.mode==REC_LITERAL && w->value.bits==0); continue; }
        CHECK(w->write_mask==mask && w->value.mask==mask && w->value.bits==e->value);
        for(u32 j=0;j<4;j++) {
            u32 old=(j==0 ? 0 : j==1 ? full : j==2 ? 0x55555555U : 0xaaaaaaaaU)&full;
            u32 next=(old&e->keep)|e->value;
            CHECK(value_ok(&w->value,t->width,next));
            CHECK(((old^next)&~w->write_mask)==0);
            if(mask) { u32 bit=mask&(~mask+1); CHECK(!value_ok(&w->value,t->width,next^bit)); }
        }
        if(e->flags&GF_N1_CFG_ZERO) {
            CHECK(w->flags&REC_N1); CHECK(w->main_id==3 && w->pc==0xe81e);
            CHECK(w->guard_count==4 && w->requires_count==3);
            for(u32 j=0;j<4;j++) CHECK(w->guards[j].key==j &&
                w->guards[j].mask==~0U && w->guards[j].expected==0 &&
                w->guards[j].when==(REC_BEFORE|REC_AFTER));
        }
    }
    CHECK(counts[0]==513 && counts[1]==76 && counts[4]==25 && counts[2]==2 && counts[3]==3);
    CHECK(gf_fixture_profile.vram_reserved.base==0 && gf_fixture_profile.vram_reserved.len==1024);
    for(u32 i=0;i<gf_fixture_profile.target_count;i++) {
        const struct recover_target *t=&gf_fixture_profile.targets[i];
        if(t->space==REC_BAR_MMIO && t->offset!=0x1700) CHECK(t->access!=REC_RW);
    }
    for(u32 i=1;i<REC_GF_PREPARE_COUNT;i++) {
        const struct recover_rule *w=rule_by_id(&profile,recover_gf_prepare[i]);
        CHECK(w && w->requires_count==1 && w->requires[0]==recover_gf_prepare[i-1]);
    }
    for(u32 i=1;i<REC_GF_SIZE_BARS_COUNT;i++) {
        const struct recover_rule *w=rule_by_id(&profile,recover_gf_size_bars[i]);
        CHECK(w && w->requires_count==1 && w->requires[0]==recover_gf_size_bars[i-1]);
    }
    print("inventory: 513 rules, 76 PLL covered / 25 chip-excluded / 0 pending, 2 COPY, 3 temporary saved, 0 alias gap; 619 accounted\n");
}
static void node(u32 at,const struct recover_rule *w,u32 dir,u32 value)
{
    nodes[at]=(struct recover_plan_node){.phase=w->phase,.main_id=w->main_id,
        .pc=w->pc,.opcode=dir==REC_WRITE?256:257,.substep=w->substep,.ctx=0,
        .rule_id=w->id,.expr_id=RECOVER_NONE,.arg_first=2*at,.arg_count=2,
        .next={at+1,at+1}};
    args[2*at]=(struct recover_plan_arg){REC_IMM,w->key};
    args[2*at+1]=(struct recover_plan_arg){REC_IMM,value};
}
/* Exact GF rules, only opcode layouts adapted to their source-row substeps. */
static struct recover_opcode_layout trace_layouts[6];
static void trace_plan(void)
{
    setup();
    const struct recover_rule *n1=0,*sel=0;
    for(u32 i=0;i<profile.rule_count;i++) if(gf_fixture_profile.rules[i].flags&REC_N1) n1=&gf_fixture_profile.rules[i];
    CHECK(n1); if(!n1) return;
    sel=rule_by_id(&profile,n1->requires[0]); n1key=n1->key; values[n1key]=0x25;
    node(0,&gf_fixture_profile.rules[0],REC_WRITE,0); node(1,&gf_fixture_profile.rules[1],REC_WRITE,0);
    entries[0]=(struct recover_plan_entry){REC_PREPARE,RECOVER_NONE,0};
    for(u32 i=0;i<3;i++) {
        nodes[2+i]=(struct recover_plan_node){.phase=REC_RUN,.main_id=i,.pc=0x100+i,
            .opcode=256,.substep=0,.rule_id=RECOVER_NONE,.expr_id=RECOVER_NONE,
            .next={3+i,3+i}};
        entries[1+i]=(struct recover_plan_entry){REC_RUN,i,2+i};
    }
    node(5,sel,REC_SELECT,1); node(6,n1,REC_DATA,5);
    nodes[6].expr_id=REC_GF_RMW; nodes[6].arg_count=4;
    args[13]=(struct recover_plan_arg){REC_ARG_READ,n1key};
    args[14]=(struct recover_plan_arg){REC_IMM,0xdf};
    args[15]=(struct recover_plan_arg){REC_IMM,0};
    nodes[7]=(struct recover_plan_node){.phase=REC_RUN,.main_id=3,.pc=0xe824,
        .opcode=256,.rule_id=RECOVER_NONE,.expr_id=RECOVER_NONE,
        .next={RECOVER_NONE,RECOVER_NONE}};
    entries[4]=(struct recover_plan_entry){REC_RUN,3,5};
    links[0]=(struct recover_plan_dep){6,gf_fixture_profile.rules[0].id,0};
    links[1]=(struct recover_plan_dep){6,gf_fixture_profile.rules[1].id,1};
    trace_layouts[0]=layouts[0]; trace_layouts[1]=layouts[1];
    trace_layouts[2]=(struct recover_opcode_layout){257,sel->substep,2,two,REC_SELECT,0};
    trace_layouts[3]=(struct recover_opcode_layout){257,n1->substep,4,four,REC_DATA,0};
    profile.layouts=trace_layouts; profile.layout_count=4;
    profile.crc=recover_profile_crc(&profile);
    plan=(struct recover_plan){.version=1,.profile_crc=profile.crc,.nodes=nodes,
        .node_count=8,.args=args,.arg_count=16,.ctx_count=1,.deps=links,.dep_count=2,
        .entries=entries,.entry_count=5};
}
static int run(void *unused);
static int operation(struct recover *r,void *arg)
{
    (void)arg;
    for(u32 i=0;i<8;i++) {
        if(recover_plan_enter(r,i)) return -1;
        if(i==6 && inject_when==REC_BEFORE) values[inject_key]=1;
        if(nodes[i].rule_id!=RECOVER_NONE) {
            const struct recover_opcode_layout *l=layout_for(&profile,&nodes[i]);
            u32 key=args[2*i].value, value=i==6 ? 5 : args[2*i+1].value;
            if(recover_access(r,i,l->dir_substep,key,gf_fixture_profile.targets[key].width,&value)) return -1;
            if(i==6) n1_completed++;
        }
        if(recover_plan_leave(r,0)) return -1;
    }
    return 0;
}
static int run(void *unused)
{
    (void)unused;
    if(recover_bind_plan(&core,&profile,&plan,&buffers,&hash)) return -2;
    if(trial_begin(&core,&profile,hash,1000)) return -3;
    return trial_step(&core,&core.token,true,operation,0);
}
static int size_operation(struct recover *r,void *arg)
{
    (void)arg;
    for(u32 i=0;i<plan.node_count;i++) {
        const struct recover_plan_node *n=&plan.nodes[i];
        const struct recover_opcode_layout *l=layout_for(&profile,n);
        u32 key=plan.args[n->arg_first].value, value=0;
        if(recover_plan_enter(r,i)) return -1;
        if(n->rule_id!=RECOVER_NONE) {
            const struct recover_rule *w=rule_by_id(&profile,n->rule_id);
            u32 old=0;
            if(!evaluate(r,n,w,&old,&value)) return -1;
        }
        if(recover_access(r,i,l->dir_substep,key,gf_fixture_profile.targets[key].width,&value)) return -1;
        if(l->dir_substep==REC_READ) {
            u32 bar=key-7;
            if(recover_bar_probe(r,bar,r->bars[bar].raw,value,0,0,0,0)) return -1;
        }
        if(recover_plan_leave(r,0)) return -1;
    }
    return 0;
}
static int prepare_operation(struct recover *r,void *arg)
{
    (void)arg;
    for(u32 i=0;i<plan.node_count;i++) {
        const struct recover_plan_node *n=&plan.nodes[i];
        const struct recover_opcode_layout *l=layout_for(&profile,n);
        u32 key=plan.args[n->arg_first].value;
        u32 value=plan.args[n->arg_first+1].value;
        if(n->expr_id==REC_GF_PMC_ENABLE) value=values[key]|1U;
        if(n->expr_id==REC_GF_CR11_UNLOCK) value=values[key]&0x7fU;
        if(recover_plan_enter(r,i) ||
            recover_access(r,i,l->dir_substep,key,gf_fixture_profile.targets[key].width,&value) ||
            recover_plan_leave(r,0)) return -1;
    }
    return 0;
}
static void fixed_test(void)
{
    setup();
    profile.layouts=gf_fixture_profile.layouts;
    profile.layout_count=gf_fixture_profile.layout_count;
    profile.crc=recover_profile_crc(&profile);
    plan=recover_gf_size_bars_plan; plan.profile_crc=profile.crc;
    CHECK(recover_bind_plan(&core,&profile,&plan,&buffers,&hash)==0);
    CHECK(writes==0);
    CHECK(trial_begin(&core,&profile,hash,1000)==0);
    CHECK(trial_step(&core,&core.token,true,size_operation,0)==0);
    CHECK(rawcommand==0x102 && rawbars[0]==0x24000000 &&
        rawbars[1]==0x30000008 && rawbars[2]==0x25000000);
    CHECK(trial_end(&core,&core.token,REC_NORMAL)==0);
    CHECK(core.cache.bar[0].valid && core.cache.bar[1].valid && core.cache.bar[2].valid);
    setup();
    profile.layouts=gf_fixture_profile.layouts;
    profile.layout_count=gf_fixture_profile.layout_count;
    profile.crc=recover_profile_crc(&profile);
    plan=recover_gf_prepare_plan; plan.profile_crc=profile.crc;
    CHECK(recover_bind_plan(&core,&profile,&plan,&buffers,&hash)==0 && writes==0);
    for(u32 i=0;i<plan.node_count;i++) CHECK(node_ok(&profile,&plan,i));
    values[12]=0xa5;
    CHECK(trial_begin(&core,&profile,hash,1000)==0);
    CHECK(trial_step(&core,&core.token,true,prepare_operation,0)==0);
    CHECK(values[12]==0x25 && indices[12]==0x11);
    CHECK(trial_end(&core,&core.token,REC_NORMAL)==0);
    print("fixed: SIZE_BARS bind/probe/restore PASS; PREPARE indexed READ bind/execute PASS\n");
}
static void trace_test(void)
{
    trace_plan(); CHECK(run(0)==0); CHECK(data_writes==1 && values[n1key]==5);
    CHECK(trial_end(&core,&core.token,REC_NORMAL)==0);
    CHECK(core.s0_verified && !core.s1_verified && core.cold_latched);
    CHECK(cleanup_calls==1);
    for(u32 when=REC_BEFORE;when<=REC_AFTER;when++) for(u32 key=0;key<4;key++) {
        trace_plan(); inject_when=when; inject_key=key;
        CHECK(run(0)<0); CHECK(cleanup_calls==1 && n1_completed==0);
        CHECK(data_writes==(when==REC_AFTER)); CHECK(core.state==REC_FAILED);
        u32 before=writes, value=0;
        CHECK(recover_access(&core,7,REC_WRITE,2,4,&value)<0 && writes==before);
    }
    trace_plan();
    plan.node_count=1; plan.entry_count=1; plan.dep_count=0;
    nodes[0].rule_id=RECOVER_NONE; /* W layout with no classified rule */
    nodes[0].next[0]=nodes[0].next[1]=RECOVER_NONE;
    CHECK(recover_bind_plan(&core,&profile,&plan,&buffers,&hash)<0 && writes==0);
    for(u32 bad=0;bad<6;bad++) {
        trace_plan();
        if(bad==0) nodes[6].rule_id=RECOVER_NONE;
        if(bad==1) args[13].value=0x20;
        if(bad==2) nodes[6].pc++;
        if(bad==3) nodes[6].main_id=2;
        if(bad==4) links[0].producer=7;
        if(bad==5) args[10].value=3; /* wrong alias target */
        CHECK(run(0)==-2 && writes==0 && cleanup_calls==0);
    }
    print("trace: N1 normal; 8 pre/post failures; unknown/value/pc/main/dependency/alias deny\n");
}
