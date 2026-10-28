/* Real production composition, interpreter and bus, with the S5 fake device. */
#define GF_S5_ENTRY s5_entry
#include "gfinit_bus_host.c"
#undef GF_S5_ENTRY
#include "../../kdebug/gfinit_plan.c"
#include "../../kdebug/recover_profile.c"
static struct gfinit_plan composed;
static char last_row[512];
static u32 row_used;
static int plan_ready(void *v) {(void)v;return 1;}
static int plan_put(void *v,u32 ch)
{
    (void)v;
    if(row_used && last_row[row_used-1]=='\n')row_used=0;
    if(row_used+1>=sizeof(last_row))return -1;
    last_row[row_used++]=ch;last_row[row_used]=0;return 0;
}
static int plan_wait(void *v,u32 us) {(void)v;(void)us;return 0;}
static const struct recover_transport plan_transport={.uart={plan_ready,plan_put,plan_wait},.frame_us=1,.max_polls=1048576};
static bool contains(const char *text,const char *wanted)
{
    for(u32 i=0;text[i];i++) { u32 j=0;while(wanted[j] && text[i+j]==wanted[j])j++;if(!wanted[j])return true; }
    return false;
}
static u32 switch_written;
static u32 corrupt, vram_reads, vram_writes, alias_reads, minimum_reads, alias_maxoffset;
static int plan_read(void *v,const struct recover_bus_access *a,u32 *value)
{
    int rc=bus_read_test(v,a,value);
#if GF_VPLL_PROBE
    if (!rc && a->space==REC_BAR_MMIO && a->offset==RECOVER_VPLL_SWITCH_OFF &&
        switch_written && ((corrupt==4 && switch_written==1) || (corrupt==5 && switch_written==2))) {
        *value ^= 1; switch_written = 0;
    }
#endif
    if(!rc && a->space==REC_BAR_VRAM) {
        vram_reads++;
        u32 sub=core.plan->nodes[core.cursor].substep;
        if((corrupt==1 && sub==2) || (corrupt==2 && sub==5)) *value^=1;
        if(sub==5)alias_reads++;
        if(sub==2)minimum_reads++;

    }
    if(!rc)gfinit_plan_read(&composed,&core,a,*value);
    return rc;
}
static int plan_write(void *v,const struct recover_bus_access *a,u32 value)
{
#if GF_VPLL_PROBE
    if (a->space==REC_BAR_MMIO && a->offset==RECOVER_VPLL_SWITCH_OFF) switch_written++;
#endif
    if(a->space==REC_BAR_VRAM) {
        /* Both passes write all 256 before their group readback begins. */
        if(vram_writes<512)CHECK(minimum_reads==(vram_writes<256?0:256));
        else { CHECK(!alias_reads);CHECK(composed.capacity_known && a->offset<=composed.tested_bytes-4);
            if(a->offset>alias_maxoffset)alias_maxoffset=a->offset; }
        vram_writes++;
    }
    return bus_write_test(v,a,value);
}
static void plan_case(u32 damage,u32 amount)
{
    bus_setup();fixture();
    /* Match the owner: decode and append into the same backing arrays. */
    struct recover_profile expanded;
    CHECK(recover_gf_profile_expand(&expanded,&composed)==0);
    profile.targets=expanded.targets;profile.rules=expanded.rules;profile.layouts=expanded.layouts;
    profile.target_count=expanded.target_count;profile.rule_count=expanded.rule_count;profile.layout_count=expanded.layout_count;
    CHECK(gfinit_decode_rom(rom,sizeof(rom),&profile,&decoded)==0);
    CHECK(gfinit_plan_build(&composed,&decoded,&profile,0x1000000)==0);
    CHECK(composed.plan.node_count<=RECOVER_NODE_MAX && composed.plan.arg_count<=RECOVER_ARG_MAX);
    output("composed nodes/args ");number(composed.plan.node_count);output("/");number(composed.plan.arg_count);output("\n");
    decoded.plan=composed.plan;
    CHECK(contract_bind()==0);
    for(u32 i=0;i<composed.plan.node_count;i++) {
        CHECK(core.node_layout[i]>=composed.layouts && core.node_layout[i]<composed.layouts+profile.layout_count);
        if(core.node_write[i])CHECK(core.node_write[i]>=composed.rules && core.node_write[i]<composed.rules+profile.rule_count);
    }
    composed.plan.profile_crc=profile.crc;exec_arm();core.plan=&composed.plan;
    core.token.profile_crc=profile.crc;core.token.profile_version=profile.version;
    core.token.plan_hash=core.bound_hash;core.token.snapshot_crc=snapshot_crc(&core);
    executor_bus.read=plan_read;executor_bus.write=plan_write;
    for(u32 i=0;i<profile.target_count;i++)values[i]=0;
    switch_written=0;corrupt=damage;vram_reads=vram_writes=alias_reads=minimum_reads=alias_maxoffset=0;
    for(u32 i=0;i<recover_gf_prepare_plan.node_count;i++) {
        u32 value;
        CHECK(!recover_plan_enter(&core,i));CHECK(!recover_plan_access(&core,&value));CHECK(!recover_plan_leave(&core,0));
    }
    for(u32 main=0;main<GF_INIT_MAINS;main++) {
        execution.main_id=main;
        int run_rc=gfinit_exec_run(&core,&execution);
#if GF_VPLL_PROBE
        if (damage>=4 && main==2) {
            CHECK(run_rc<0 && core.reason==REC_VERIFY && cleanup_calls==1 && !core.plan);
            return;
        }
#endif
        CHECK(run_rc==0);
    }
    for(u32 i=0;i<GF_PLAN_VRAM_WORDS;i++)values[composed.vram_first+i]=0x12340000U+i;
    values[composed.pfb_key]=amount;
    if(damage==3) {
        core.expected_node=composed.vram_node+GF_PLAN_VRAM_WORDS;
        CHECK(!recover_plan_enter(&core,core.expected_node));
        CHECK(recover_plan_leave(&core,0)<0 && cleanup_calls==1);
        return;
    }
    core.transport=&plan_transport;core.in_call=true;row_used=0;
    int rc=gfinit_plan_verify(&core,&composed);
    CHECK(damage?rc<0:rc==0);
    CHECK(composed.saved_count==256);
    for(u32 i=0;i<256;i++)CHECK(composed.saved_vram[i]==0x12340000U+i);
    if(!damage) {
        CHECK(contains(last_row,"phase=gf_verify event=END saved_words=256 minimal=1 capacity_known="));
        if(amount==0 || amount==0x03000000U) {
            CHECK(!composed.capacity_known && vram_writes==512 && minimum_reads==512);
            for(u32 i=0;i<256;i++)CHECK(values[composed.vram_first+i]==((i&1)?0x55aa55aaU:0xaa55aa55U));
        } else {
            CHECK(composed.capacity_known && composed.capacity_bytes==amount);
            CHECK(composed.tested_bytes==0x01000000U && composed.full_capacity==(amount==0x01000000U));
            CHECK(vram_writes>512 && alias_reads==vram_writes-512);
            u32 offsets[GF_PLAN_ALIAS_MAX], n=alias_offsets(0x01000000U,offsets);
            CHECK(alias_reads==n && alias_maxoffset==0x01000000U-4);
            for(u32 i=0;i<n;i++) {
                u32 key=RECOVER_NONE;
                for(u32 j=0;j<profile.target_count;j++)if(profile.targets[j].space==REC_BAR_VRAM && profile.targets[j].offset==offsets[i])key=j;
                CHECK(key!=RECOVER_NONE && values[key]==(offsets[i]^0x6d32a591U));
            }
        }
        core.state=REC_RESTORING;core.restore_tick=0;core.in_opcode=false;
        CHECK(!cleanup_budget_prepare(&core));
        CHECK(recover_gf_screen_safe(&core)==0);
    } else CHECK(core.reason==REC_VERIFY && cleanup_calls==1 && (damage==1?vram_writes==256:vram_writes>512));
}
void _start(void)
{
    gf_fixture_init();
#if GF_VPLL_PROBE
    plan_case(4,0);plan_case(5,0);
#endif
    plan_case(0,0);plan_case(1,0);plan_case(0,0x01000000U);
    plan_case(0,0x02000000U);plan_case(0,0x03000000U);plan_case(2,0x01000000U);plan_case(3,0);
    bus_setup();fixture();
    /* Match the owner: decode and append into the same backing arrays. */
    struct recover_profile expanded;
    CHECK(recover_gf_profile_expand(&expanded,&composed)==0);
    profile.targets=expanded.targets;profile.rules=expanded.rules;profile.layouts=expanded.layouts;
    profile.target_count=expanded.target_count;profile.rule_count=expanded.rule_count;profile.layout_count=expanded.layout_count;
    CHECK(gfinit_decode_rom(rom,sizeof(rom),&profile,&decoded)==0);
    CHECK(gfinit_plan_build(&composed,&decoded,&profile,0x80000000U)==0);
    CHECK(composed.plan.node_count<=RECOVER_NODE_MAX && composed.plan.arg_count<=RECOVER_ARG_MAX);
    output("maximum BAR1 nodes/args ");number(composed.plan.node_count);output("/");number(composed.plan.arg_count);output("\n");
    output(failures?"GF S6 plan FAIL\n":"GF S6 plan PASS\n");
    __asm__ volatile("int $0x80"::"a"(1),"b"(failures?1:0):"memory");for(;;){}
}
