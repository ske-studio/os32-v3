/* Synthetic instructions only. No GF ROM row is authorized by this fixture. */
#define TK 6U
#define GUARD_FIRST 8U
#define SYNTH_NODES 8U
static struct recover_plan_node sn[SYNTH_NODES];
static struct recover_plan_arg sa[SYNTH_NODES * 3];
static struct recover_plan_dep sd[2];
static struct recover_plan_entry se[6];
static const u32 one_kind[] = {REC_IMM};
static const u32 two_kinds[] = {REC_IMM, REC_IMM};
static const u32 expr_kinds[] = {REC_IMM, REC_IMM, REC_IMM};
static const struct recover_opcode_layout sl[] = {
    {0, 0, 0, 0, RECOVER_NONE}, {0, 1, 1, one_kind, REC_READ},
    {1, REC_WRITE, 2, two_kinds, REC_WRITE},
    {1, REC_SELECT, 2, two_kinds, REC_SELECT}, {1, REC_DATA, 2, two_kinds, REC_DATA},
    {2, 0, 3, expr_kinds, REC_WRITE}
};
static int add_expr(const u32 *a, u32 argc, u32 *out)
{
    if (argc != 2 || a[0] > ~0U - a[1]) return -1;
    *out = a[0] + a[1]; return 0;
}
static const struct recover_expr expressions[] = {{1, 2, two_kinds, add_expr}};
static u32 requires[1], allowed[] = {0x20, 0x24};
static u32 shash, requested, force_dir, force_key;
static bool override_request;
static u32 inject_guard, access_returns;
static void synthetic(u32 count_nodes)
{
    setup(false);
    profile.layouts = sl; profile.layout_count = sizeof(sl)/sizeof(sl[0]);
    profile.exprs = expressions; profile.expr_count = 1;
    profile.rule_count = 5; profile.max_steps = 16;
    for (u32 i = 0; i < SYNTH_NODES; i++) {
        sn[i] = (struct recover_plan_node){ .phase = REC_PREPARE,
            .main_id = RECOVER_NONE, .pc = RECOVER_NONE, .rule_id = RECOVER_NONE,
            .expr_id = RECOVER_NONE, .next = {i + 1, i + 1} };
    }
    sn[count_nodes - 1].next[0] = sn[count_nodes - 1].next[1] = RECOVER_NONE;
    se[0] = (struct recover_plan_entry){ REC_PREPARE, RECOVER_NONE, 0 };
    plan = (struct recover_plan){ .version = 1, .nodes = sn, .node_count = count_nodes,
        .args = sa, .arg_count = count_nodes * 3, .ctx_count = 3,
        .deps = sd, .entries = se, .entry_count = 1 };
    override_request = false; inject_guard = RECOVER_NONE; access_returns = 0; force_dir = force_key = RECOVER_NONE;
}
static void writer(u32 node, u32 id, u32 key, u32 dir, u32 value)
{
    rules[id] = (struct recover_rule){ .id = id, .key = key, .phase = REC_PREPARE,
        .main_id = RECOVER_NONE, .pc = RECOVER_NONE, .substep = dir,
        .write_mask = width_mask(targets[key].width), .verify_mask = width_mask(targets[key].width),
        .value = {.mode = REC_LITERAL, .mask = width_mask(targets[key].width), .bits = value,
                  .expr_id = RECOVER_NONE} };
    if (profile.rule_count <= id) profile.rule_count = id + 1;
    sn[node].opcode = 1; sn[node].substep = dir; sn[node].rule_id = id;
    sn[node].arg_first = node * 3; sn[node].arg_count = 2;
    sa[node * 3] = (struct recover_plan_arg){REC_IMM, key};
    sa[node * 3 + 1] = (struct recover_plan_arg){REC_IMM, value};
}
static int bind_synthetic(void)
{
    profile.crc = recover_profile_crc(&profile); plan.profile_crc = profile.crc;
    return recover_bind_plan(&r, &profile, &plan, &buffers, &shash);
}
static void bar_cache(void)
{
    r.cache = (struct recover_bar_measurement){ .identity = f.id, .measurement_session = 1 };
    for (u32 i = 0; i < 3; i++) r.cache.bar[i] = (struct recover_bar_extent){
        .raw = i ? 0 : f.bar, .base = i ? 0 : f.bar, .length = 0x1000000U, .valid = 1 };
}
static void synthetic_begin(void)
{
    CHECK(bind_synthetic() == 0);
    CHECK(trial_begin(&r, &profile, shash, 100) == 0);
}
static int execute_node(struct recover *core, void *arg)
{
    u32 id = (u32)arg;
    if (recover_plan_enter(core, id)) return -1;
    const struct recover_plan_node *n = &sn[id];
    const struct recover_opcode_layout *l = layout_for(&profile, n);
    if (l->dir_substep != RECOVER_NONE) {
        u32 value = n->arg_count > 1 ? sa[n->arg_first + 1].value : 0;
        if (n->expr_id != RECOVER_NONE) value += sa[n->arg_first + 2].value;
        if (override_request) value = requested;
        if (inject_guard != RECOVER_NONE) f.values[inject_guard] = 1;
        if (recover_access(core, id, force_dir == RECOVER_NONE ? l->dir_substep : force_dir,
            force_key == RECOVER_NONE ? sa[n->arg_first].value : force_key,
            targets[sa[n->arg_first].value].width, &value)) return -1;
        access_returns++;
        if (recover_safe_point(core)) return -1; /* B1: no manual resource release */
    }
    return recover_plan_leave(core, 0);
}
static int step_node(u32 n) { return trial_step(&r, &r.token, true, execute_node, (void *)n); }
static void address_tests(void)
{
    synthetic(1);
    struct recover_target t = {.space=REC_VGA_INDEXED, .head=1, .port=RECOVER_SEQ,
        .index=1, .width=1, .access=REC_RW, .read_safe=1};
    struct recover_bus_access a;
    CHECK(recover_target_address(&t, REC_SELECT, &a) == 0 && a.offset == 0xc23c4U);
    CHECK(recover_target_address(&t, REC_DATA, &a) == 0 && a.offset == 0xc23c5U);
    t.port = RECOVER_CRTC;
    CHECK(recover_target_address(&t, REC_SELECT, &a) == 0 && a.offset == 0x6033d4U);
    CHECK(recover_target_address(&t, REC_DATA, &a) == 0 && a.offset == 0x6033d5U);
    t.port++; CHECK(recover_target_address(&t, REC_DATA, &a) < 0);
    t = (struct recover_target){.space=REC_BAR_MMIO, .offset=0xc03c4, .index=RECOVER_NONE,
        .width=1, .read_safe=1};
    CHECK(recover_target_address(&t, REC_WRITE, &a) < 0);
    t.space=REC_VGA_DIRECT; t.offset=0; t.port=0x3c9;
    CHECK(recover_target_address(&t, REC_READ, &a) < 0);
}
static void bind_tests(void)
{
    synthetic(1); CHECK(trial_begin(&r, &profile, 123, 100) < 0 && writes() == 0);
    synthetic(1); CHECK(bind_synthetic() == 0);
    CHECK(recover_bind_plan(&r, &profile, &plan, &buffers, &shash) < 0);
    CHECK(trial_begin(&r, &profile, shash ^ 1, 100) < 0 && !r.plan && writes() == 0);
    for (u32 bad = 0; bad < 10; bad++) {
        synthetic(2); writer(0, 5, TK, REC_WRITE, 0x24);
        switch (bad) {
        case 0: sn[0].next[0]=0; break;
        case 1: sn[0].rule_id=99; break;
        case 2: sn[0].phase=REC_RUN; break;
        case 3: sn[0].pc=12; break;
        case 4: sn[0].arg_first=~0U; break;
        case 5: sn[0].arg_count=3; break;
        case 6: sn[0].opcode=99; break;
        case 7: sa[0].kind=REC_ARG_READ; break;
        case 8: sn[0].next[0]=RECOVER_NONE; sn[0].next[1]=RECOVER_NONE; break;
        case 9: plan.node_count=RECOVER_NODE_MAX+1; break;
        }
        CHECK(bind_synthetic() < 0 && writes() == 0);
    }
    synthetic(1); writer(0, 5, TK, REC_WRITE, 0x24); rules[5].phase=REC_RESTORE;
    CHECK(bind_synthetic()<0);
    synthetic(1); writer(0,5,TK,REC_WRITE,0x24);
    sn[0].phase=rules[5].phase=se[0].phase=REC_RUN;
    sn[0].main_id=rules[5].main_id=se[0].main_id=0;
    sn[0].pc=12; rules[5].pc=13; CHECK(bind_synthetic()<0);
    synthetic(1); writer(0, 5, TK, REC_WRITE, 0x24); synthetic_begin();
    CHECK(step_node(0) == 0 && f.values[TK] == 0x24);
    CHECK(step_node(0) < 0 && !r.plan);
}
static void values_tests(void)
{
    for (u32 mode = 0; mode < 4; mode++) {
        for (u32 bad = 0; bad < 2; bad++) {
            synthetic(1); writer(0, 5, TK, REC_WRITE, 0x24);
            rules[5].value = (struct recover_value){.mode = mode, .mask=0xf0, .bits=0x20,
                .min=0x20, .max=0x25, .expr_id=RECOVER_NONE, .allowed=mode == REC_SET ? allowed : 0, .allowed_count=mode == REC_SET ? 2 : 0};
            if (mode == REC_LITERAL) { rules[5].value.bits=0x24; rules[5].value.mask=0xff; }
            if (mode == REC_EXPR) {
                rules[5].value.expr_id=1; sn[0].expr_id=1; sn[0].opcode=2; sn[0].substep=0;
                rules[5].substep=0; sn[0].arg_count=3; sa[1].value=0x20; sa[2]=(struct recover_plan_arg){REC_IMM,4};
            }
            synthetic_begin(); override_request=true;
            requested = bad ? (mode == REC_SET ? 0x22 : mode == REC_RANGE ? 0x28 : mode == REC_EXPR ? 0x23 : 0x34) : 0x24;
            CHECK((step_node(0) == 0) == !bad);
            CHECK(count('w', TK) == (bad ? 0U : 1U));
        }
    }
    synthetic(1); writer(0,5,TK,REC_WRITE,0x24); rules[5].value.mask=0xf0; rules[5].value.bits=0x20;
    rules[5].value.mode=REC_RANGE; rules[5].value.min=0; rules[5].value.max=0xff;
    sa[1].value=0x34; CHECK(bind_synthetic()<0);
    synthetic(1); writer(0,5,TK,REC_WRITE,0x24); targets[TK].access=REC_UNCLASSIFIED;
    CHECK(bind_synthetic()<0);
    synthetic(1); writer(0,5,TK,REC_WRITE,0x24); rules[5].value.mode=REC_SAVED;
    CHECK(bind_synthetic()<0);
}
static void value_static_tests(void)
{
    synthetic(1); writer(0,5,TK,REC_WRITE,0x22);
    rules[5].value=(struct recover_value){.mode=REC_SET,.mask=0xf0,.bits=0x20,
        .allowed=allowed,.allowed_count=2,.expr_id=RECOVER_NONE}; CHECK(bind_synthetic()<0);
    synthetic(1); writer(0,5,TK,REC_WRITE,0x28);
    rules[5].value=(struct recover_value){.mode=REC_RANGE,.mask=0xf0,.bits=0x20,
        .min=0x20,.max=0x25,.expr_id=RECOVER_NONE}; CHECK(bind_synthetic()<0);
    setup(true); rules[0].value.mode=REC_SAVED; CHECK(recover_profile_ok(&profile)<0);
    /* Isolate snapshot classification from the independent SAVED rule gate. */
    setup(false); regs[0].kind=REC_W1C; targets[DATA0].access=REC_W1C;
    rules[0].value.mode=REC_LITERAL; CHECK(recover_profile_ok(&profile)<0);
}
static void dependency_tests(void)
{
    for (u32 mode = 0; mode < 5; mode++) {
        synthetic(3); writer(0,5,TK,REC_WRITE,0x24); writer(2,6,TK+1,REC_WRITE,0x25);
        requires[0]=5; rules[6].requires=requires; rules[6].requires_count=1;
        if (mode==1) sn[0].next[1]=2; /* producer still dominates: valid */
        if (mode==2) { sn[0].rule_id=RECOVER_NONE; sn[0].opcode=0; sn[0].substep=0;
            sn[0].arg_count=0; writer(1,5,TK,REC_WRITE,0x24); sn[0].next[1]=2; }
        if (mode>=3) sn[2].ctx=1;
        if (mode==4) { sd[0]=(struct recover_plan_dep){2,5,0}; plan.dep_count=1; }
        int rc=bind_synthetic(); CHECK((rc==0)==(mode==0 || mode==1 || mode==4));
        if (!rc) {
            CHECK(trial_begin(&r,&profile,shash,100)==0);
            CHECK(step_node(0)==0); CHECK(step_node(1)==0);
            if (mode==4) valid_bits[0]=0; /* stale producer success is not enough */
            CHECK((step_node(2)==0)==(mode!=4));
        }
    }
    synthetic(2); writer(0,5,TK,REC_WRITE,1); writer(1,6,TK+1,REC_WRITE,2);
    requires[0]=6; rules[5].requires=requires; rules[5].requires_count=1;
    CHECK(bind_synthetic()<0);
}
static void indexed_tests(void)
{
    for (u32 mode=0; mode<4; mode++) {
        synthetic(2); targets[TK]=(struct recover_target){.space=REC_VGA_INDEXED,
            .port=RECOVER_SEQ,.index=1,.width=1,.access=REC_RW,.read_safe=1};
        writer(0,5,TK,REC_SELECT,1); writer(1,6,TK,REC_DATA,0x20); bar_cache();
        synthetic_begin();
        if (mode==1) f.abort_selected=true;
        if (mode==2) force_dir=REC_DATA;
        if (mode==3) { override_request=true; requested=2; }
        CHECK((step_node(0)==0)==(mode==0));
        if (mode==0) { CHECK(f.last_offset==0xc03c4); CHECK(step_node(1)==0);
            CHECK(trial_end(&r,&r.token,REC_NORMAL)==0); }
        CHECK(!f.owned && f.local_calls==1);
        CHECK(f.releases==(mode<2 ? 1U : 0U));
        CHECK(count('w',TK)==(mode==0 ? 1U : 0U));
        u32 n=f.n; trial_end(&r,&r.token,REC_ABORT); CHECK(n==f.n);
    }
}
static void local_saved_tests(void)
{
    synthetic(2); targets[TK]=(struct recover_target){.space=REC_VGA_INDEXED,
        .port=RECOVER_SEQ,.index=1,.width=1,.access=REC_RW,.read_safe=1};
    writer(0,5,TK,REC_SELECT,1); writer(1,6,TK,REC_DATA,0x20);
    for (u32 i=7;i<9;i++) rules[i]=(struct recover_rule){.id=i,.key=TK,
        .phase=REC_RESTORE,.main_id=RECOVER_NONE,.pc=RECOVER_NONE,
        .substep=i==7 ? REC_SELECT : REC_DATA,.write_mask=0xff,.verify_mask=0xff,
        .value={.mode=REC_SAVED,.expr_id=RECOVER_NONE},.flags=REC_LOCAL};
    profile.rule_count=9; bar_cache(); synthetic_begin(); f.restore_index=true;
    CHECK(step_node(0)==0); CHECK(step_node(1)==0);
    CHECK(trial_end(&r,&r.token,REC_NORMAL)==0);
    CHECK(f.values[TK]==INITIAL && f.selected_value==0 && f.releases==1);
    u32 writes_before=writes(); CHECK(local_cleanup(&f)==0);
    CHECK(writes()==writes_before && f.releases==1);
}
static void n1_setup(void)
{
    synthetic(8);
    targets[TK]=(struct recover_target){.space=REC_VGA_INDEXED,.port=RECOVER_SEQ,
        .index=1,.width=1,.access=REC_RW,.read_safe=1};
    for (u32 i=0;i<4;i++) {
        targets[GUARD_FIRST+i]=(struct recover_target){.space=i<2 ? REC_PCI_CFG : REC_BAR_MMIO,
            .offset=i<2 ? RECOVER_CFG50+i*4 : RECOVER_PBUS1850+(i-2)*4,
            .index=RECOVER_NONE,.width=4,.access=REC_RW,.read_safe=1};
        f.values[GUARD_FIRST+i]=0;
    }
    writer(4,5,TK,REC_SELECT,1); writer(5,6,TK,REC_DATA,0);
    for (u32 i=1;i<8;i++) {
        sn[i].phase=REC_RUN; sn[i].main_id=i<4 ? i-1 : i<7 ? 3 : 4;
        sn[i].pc=i==5 ? RECOVER_N1_PC : i;
    }
    rules[5].phase=REC_RUN; rules[5].main_id=3; rules[5].pc=4;
    rules[6].flags=REC_N1; rules[6].phase=REC_RUN; rules[6].main_id=3; rules[6].pc=RECOVER_N1_PC;
    for (u32 i=1;i<6;i++) se[i]=(struct recover_plan_entry){REC_RUN,i-1,i<5 ? i : 7};
    plan.entry_count=6;
    bar_cache();
}
static void guards_tests(void)
{
    for (u32 bad=0;bad<6;bad++) {
        n1_setup(); synthetic_begin();
        for (u32 i=0;i<5;i++) CHECK(step_node(i)==0);
        if (bad<4) inject_guard=GUARD_FIRST+bad;
        if (bad==4) f.break_guard=true;
        u32 before=access_returns;
        CHECK((step_node(5)==0)==(bad==5));
        CHECK(access_returns==before+(bad==5 ? 1U : 0U));
        CHECK(count('w',TK)==(bad>=4 ? 1U : 0U));
    }
    for (u32 i=0;i<4;i++) {
        n1_setup(); writer(0,7,GUARD_FIRST+i,REC_WRITE,1); CHECK(bind_synthetic()<0);
    }
    n1_setup(); rules[6].pc++; sn[5].pc++; CHECK(bind_synthetic()<0);
}
static void crc_tests(void)
{
    synthetic(1); writer(0,5,TK,REC_WRITE,0x24);
    u32 crc=recover_profile_crc(&profile);
    targets[TK].offset+=4; CHECK(crc!=recover_profile_crc(&profile)); targets[TK].offset-=4;
    rules[5].value.bits++; CHECK(crc!=recover_profile_crc(&profile)); rules[5].value.bits--;
    profile.expr_count=0; CHECK(crc!=recover_profile_crc(&profile)); profile.expr_count=1;
    profile.layout_count--; CHECK(crc!=recover_profile_crc(&profile)); profile.layout_count++;
    CHECK(bind_synthetic()==0); u32 hash=shash; release_plan(&r);
    plan.rom_crc++; CHECK(bind_synthetic()==0 && hash!=shash); release_plan(&r);
}
static int measure_all(struct recover *core, void *arg)
{
    (void)arg;
    if (recover_plan_enter(core,0)) return -1;
    u32 raw[3]={f.bar,f.extra_bars[0],f.extra_bars[1]};
    for (u32 i=0;i<3;i++)
        if (recover_bar_probe(core,i,raw[i],0xfff00000U,0,0,0,0)) return -1;
    f.command=RECOVER_COMMAND_OVERRIDE;
    return recover_plan_leave(core,0);
}
static void cache_tests(void)
{
    synthetic(1); sn[0].phase=se[0].phase=REC_SIZE_BARS;
    f.extra_bars[0]=0x20000000; f.extra_bars[1]=0x30000000;
    synthetic_begin(); CHECK(trial_step(&r,&r.token,true,measure_all,0)==0);
    CHECK(!r.cache.bar[0].valid);
    CHECK(trial_end(&r,&r.token,REC_NORMAL)==0 && r.cache.bar[0].valid);
    struct recover_bar_measurement cache=r.cache;
    synthetic(1); r.cache=cache; f.extra_bars[0]=0x20000000; f.extra_bars[1]=0x30000000;
    CHECK(bind_synthetic()==0); CHECK(trial_begin(&r,&profile,shash,100)==0);
    CHECK(r.bars[0].length==0x100000 && r.cache.bar[0].valid);
    synthetic(1); r.cache=cache; f.extra_bars[0]=0x20000000; f.extra_bars[1]=0x30001000;
    CHECK(bind_synthetic()==0); CHECK(trial_begin(&r,&profile,shash,100)==0);
    CHECK(!r.cache.bar[0].valid);
}
static void bar_tests(void)
{
    for (u32 bad=0;bad<5;bad++) {
        synthetic(1); sn[0].phase=REC_SIZE_BARS; se[0].phase=REC_SIZE_BARS;
        CHECK(bind_synthetic()==0); CHECK(trial_begin(&r,&profile,shash,100)==0);
        r.state=REC_RUNNING; r.in_call=true; CHECK(recover_plan_enter(&r,0)==0);
        struct recover_range ex={0x20000000,0x1000}, up={0x10000000,0x1000000};
        u32 raw=f.bar, mask=0xfff00000;
        if (bad==1) ex.base=f.bar;
        if (bad==2) up.len=0x1000;
        if (bad==3) mask=0xffe10000;
        if (bad==4) raw++;
        CHECK((recover_bar_probe(&r,0,raw,mask,&ex,1,&up,1)==0)==!bad);
        CHECK((r.measurement.bar[0].valid!=0)==!bad);
        CHECK(!r.cache.bar[0].valid);
    }
    synthetic(1); targets[TK]=(struct recover_target){.space=REC_BAR_MMIO,.offset=0x100,
        .index=RECOVER_NONE,.width=4,.access=REC_RW,.read_safe=1};
    writer(0,5,TK,REC_WRITE,0); CHECK(bind_synthetic()==0);
    CHECK(trial_begin(&r,&profile,shash,100)<0 && writes()==0);
    synthetic(1); targets[TK]=(struct recover_target){.space=REC_BAR_MMIO,.offset=0x1000000,
        .index=RECOVER_NONE,.width=4,.access=REC_RW,.read_safe=1};
    writer(0,5,TK,REC_WRITE,0); bar_cache(); CHECK(bind_synthetic()==0);
    CHECK(trial_begin(&r,&profile,shash,100)<0 && writes()==0);
}
static void descriptor_tests(void)
{
    address_tests(); bind_tests(); values_tests(); value_static_tests(); dependency_tests(); indexed_tests(); local_saved_tests();
    guards_tests(); crc_tests(); bar_tests(); cache_tests();
}
