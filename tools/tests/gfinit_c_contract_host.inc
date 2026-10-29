static u32 changing_key,changing_bit,changing_reads;
static int changing_c_read(void *v,const struct recover_bus_access *a,u32 *value)
{
    if(a->key==changing_key && ++changing_reads==2)values[a->key]^=changing_bit;
    return bus_read_test(v,a,value);
}
static void c_live_sample(void)
{
    /* C rows grant full-width writes, while generic plans also validate
     * preserved bits. Keep that primitive's runtime-sample regression here. */
    bus_setup();contract_setup(1);
    contract_rules[0].write_mask=1;
    contract_rules[0].value=(struct recover_value){.mode=REC_EXPR,.mask=1,.bits=1,
        .max=~0U,.expr_id=REC_GF_RMW};
    contract_layouts[0]=(struct recover_opcode_layout){1,0,4,four,REC_WRITE,0};
    decoded.nodes[0].expr_id=REC_GF_RMW;decoded.nodes[0].arg_count=4;
    decoded.args[0]=IMM(0);decoded.args[1]=READ(0);decoded.args[2]=IMM(~1U);decoded.args[3]=IMM(1);
    CHECK(!contract_bind());contract_arm();
    changing_key=0;changing_bit=0x80000000;changing_reads=0;values[0]=0;
    executor_bus=ops;executor_bus.read=changing_c_read;executor_bus.write=bus_write_test;
    core.ops=&executor_bus;u32 value=0;
    CHECK(!recover_plan_enter(&core,0));CHECK(!recover_plan_access(&core,&value));
    CHECK(changing_reads==2 && values[0]==0x80000001);
    CHECK(!recover_plan_leave(&core,0));
}
static void contracts(void)
{
    const u32 fp_values[]={0,0x20000022,1,16,0x10000000};
    for(u32 i=0;i<sizeof(fp_values)/sizeof(*fp_values);i++) {
        u32 enabled;
        CHECK(!c_fp_on(&fp_values[i],1,&enabled) && enabled==(i>=2));
    }
    c_build();u32 crc=profile.crc,hash=core.bound_hash;
    CHECK(composed.mode_flags==GF_MODE_C && composed.vram_node==RECOVER_NONE);
    CHECK(profile.targets[key_for(&profile,0x680510,0,0,0)].read_safe); /* appended C read permission */
    core.plan=0;decoded.args[decoded.nodes[composed.c_node].arg_first+1].value=0;
    CHECK(contract_bind()==0 && core.bound_hash!=hash);
    c_build();u32 id=composed.c_node;
    while(core.node_layout[id]->dir_substep!=REC_DATA)id++;
    core.plan=0;
    /* Keep reachability, remove the SELECT permission (not the DATA node). */
    decoded.nodes[id-1].substep=GF_C_SECTION;
    decoded.nodes[id-1].rule_id=RECOVER_NONE;
    CHECK(contract_bind()<0);
    c_build();core.plan=0;decoded.captures[0].producer_node=decoded.captures[0].consumer_node;
    CHECK(contract_bind()<0);
    c_build();core.plan=0;decoded.captures[0].read_ordinal++;
    CHECK(contract_bind()<0);
    bus_setup();fixture();CHECK(!recover_gf_profile_expand(&profile,&composed));
    CHECK(!gfinit_decode_rom(rom,sizeof(rom),&profile,&decoded));
    CHECK(!gfinit_plan_build(&composed,&decoded,&profile,0x1000000));
    CHECK(!composed.mode_flags && composed.c_node==RECOVER_NONE && profile.crc!=crc);
    for(u32 i=0;i<composed.plan.node_count;i++)CHECK(composed.plan.nodes[i].phase!=REC_MODE_C);
    CHECK(composed.plan.entry_count==7 && composed.plan.entries[6].phase==REC_VRAM_TEST);
}
