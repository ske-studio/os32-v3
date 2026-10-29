/* Complete fake device; reference interpreter below does not consume plan
 * nodes, layouts or executor branches. Input is S1 data plus synthetic tables. */
#define GF_S2_ENTRY s2_entry
#include "gfinit_host.c"
#undef GF_S2_ENTRY
#include "../../kdebug/gfinit_exec.c"
#define TRACE_MAX 8192U
struct trace_item {u32 dir,key,value;};
static struct trace_item actual[TRACE_MAX];
static u32 expected_count,actual_count;
static u32 clock_value,clock_jump,wait_total,clock_fail,wait_fail,bus_fail;
static u32 vpll_reads;
static struct recover_target exec_targets[RECOVER_TARGET_MAX];
static struct recover_opcode_layout exec_layouts[RECOVER_RULE_MAX];
static struct gfinit_exec execution;
static void trace_add(struct trace_item *trace,u32 *count,u32 dir,u32 key,u32 value)
{
    CHECK(*count<TRACE_MAX);
    if(*count<TRACE_MAX)trace[(*count)++]=(struct trace_item){dir,key,value};
}
static int exec_bus_write(void *v,const struct recover_bus_access *a,u32 value)
{
    (void)v;
    if(bus_fail)return -1;
    trace_add(actual,&actual_count,a->dir_substep,a->key,value);
    if(a->dir_substep==REC_SELECT)indices[a->key]=value;else values[a->key]=value;
    return 0;
}
static int exec_bus_read(void *v,const struct recover_bus_access *a,u32 *value)
{
    (void)v;
    if(bus_fail)return -1;
    if (core.entered && core.plan && core.cursor < core.plan->node_count) {
        const struct recover_plan_node *n = &core.plan->nodes[core.cursor];
        if (a->key == core.plan->args[n->arg_first].value &&
            n->main_id == RECOVER_VPLL_MAIN && n->pc == RECOVER_VPLL_PC &&
            ((n->substep >= RECOVER_VPLL_FIRST && n->substep <= RECOVER_VPLL_LAST) ||
             (n->substep >= RECOVER_VPLL_REPEAT_FIRST && n->substep <= RECOVER_VPLL_REPEAT_LAST)))
            vpll_reads++;
    }
    *value=a->dir_substep==REC_SELECT?indices[a->key]:values[a->key];
    return 0;
}
static int exec_clock(void *v,u32 *out)
{(void)v;if(clock_fail)return -1;*out=clock_value;clock_value+=clock_jump;clock_jump=0;return 0;}
static int exec_delay(void *v,u32 us)
{
    (void)v;if(wait_fail)return -1;
    trace_add(actual,&actual_count,RECOVER_NONE,0,us);
    clock_value+=us;wait_total+=us;return 0;
}
static const struct gfinit_exec_ops executor_ops={exec_clock,exec_delay};
static struct recover_ops executor_bus;
static void synthetic_profile(void)
{
    profile=gf_fixture_profile;
    profile.trial_limit=profile.opcode_limit=profile.restore_limit=1000;
    profile.max_steps=4096;profile.restore_max_polls=65536;profile.restore_log_bytes=65536;
    profile.layouts=exec_layouts;
    for(u32 i=0;i<profile.layout_count;i++)exec_layouts[i]=gf_fixture_profile.layouts[i];
    exec_layouts[profile.layout_count++]=(struct recover_opcode_layout){
        .opcode=GF_INIT_RUNNER,.dir_substep=RECOVER_NONE};
    profile.targets=exec_targets;
    for(u32 i=0;i<profile.target_count;i++)exec_targets[i]=gf_fixture_profile.targets[i];
}
static void exec_arm(void)
{
    contract_arm();core.in_call=true;core.in_opcode=true;
    executor_bus=ops;executor_bus.read=exec_bus_read;executor_bus.write=exec_bus_write;core.ops=&executor_bus;
    clock_value=clock_jump=wait_total=clock_fail=wait_fail=bus_fail=actual_count=expected_count=0;
    execution=(struct gfinit_exec){.rom=rom,.rom_size=sizeof(rom),.ops=&executor_ops,
        .program=&decoded.program};
}
static char exec_log[4096];
static u32 exec_log_n;
static bool exec_log_busy;
static int exec_log_get(void *opaque) { (void)opaque; return -1; }
static int exec_log_ready(void *opaque) { (void)opaque; return !exec_log_busy; }
static int exec_log_put(void *opaque, u32 byte)
{
    (void)opaque;
    if (exec_log_n + 1 >= sizeof(exec_log)) return -1;
    exec_log[exec_log_n++] = byte; exec_log[exec_log_n] = 0; return 0;
}
static int exec_log_wait(void *opaque, u32 us) { (void)opaque; (void)us; return 0; }
static bool exec_log_has(const char *text)
{
    for (u32 i = 0; i < exec_log_n; i++) {
        u32 n = 0;
        while (text[n] && exec_log[i + n] && text[n] == exec_log[i + n]) n++;
        if (!text[n]) return true;
    }
    return false;
}
static const struct recover_transport exec_transport = {
    .get=exec_log_get, .uart={exec_log_ready,exec_log_put,exec_log_wait}, .frame_us=1042, .max_polls=4096
};
static void adapter_begin(void)
{
    setup();fixture();CHECK(!gfinit_decode_rom(rom,sizeof(rom),&gf_fixture_profile,&decoded));
    synthetic_profile();profile.regs=0;profile.count=0;profile.quiesce=0;profile.quiesce_count=0;
    profile.index_save_targets=0;profile.index_save_count=0;
    profile.rule_count=0;profile.expected_device=0;
    for(u32 i=0;i<profile.target_count;i++)exec_targets[i].access=REC_COLD;
    CHECK(!contract_bind());exec_arm();
    core.token.profile_crc=profile.crc;core.token.profile_version=profile.version;
    core.token.plan_hash=core.bound_hash;core.token.snapshot_crc=snapshot_crc(&core);
    core.state=REC_PAUSED;core.in_call=core.in_opcode=false;
}
static int bus_record_operation(struct recover *r,void *arg)
{
    (void)arg;u32 value=0xabc;
    if(recover_plan_enter(r,r->expected_node) ||
       recover_gf_runner_access(r,0x123,0,0x200,4,true,&value) ||
       recover_gf_runner_access(r,0x123,0,0x200,4,false,&value))return -1;
    CHECK(value==0xabc);
    return recover_plan_leave(r,0);
}
static void adapter_cases(void)
{
    adapter_begin();
    CHECK(trial_step(&core,&core.token,false,gfinit_exec_run,&execution)<0 && !actual_count);
    execution.main_id=1;
    CHECK(trial_step(&core,&core.token,true,gfinit_exec_run,&execution)<0 && cleanup_calls==1);
    for(u32 kind=0;kind<4;kind++) {
        adapter_begin();
        if(kind==0)clock_fail=1;
        if(kind==1)bus_fail=1;
        if(kind==2)wait_fail=1;
        if(kind==3)clock_jump=GF_INIT_EXEC_US;
        CHECK(trial_step(&core,&core.token,true,gfinit_exec_run,&execution)<0);
        CHECK(cleanup_calls==1 && !core.plan);
        if(kind==3)CHECK(core.reason==REC_TIMEOUT);
    }
    adapter_begin();exec_log_n=0;exec_log_busy=true;core.transport=&exec_transport;
    CHECK(trial_step(&core,&core.token,true,gfinit_exec_run,&execution)<0 && !actual_count);
    core.transport=0;exec_log_busy=false;
    adapter_begin();core.expected_node=4;execution.main_id=4;
    exec_log_n=0;exec_log[0]=0;core.transport=&exec_transport;
    CHECK(!trial_step(&core,&core.token,true,gfinit_exec_run,&execution));
    CHECK(exec_log_has("phase=gf_pre event=BEGIN main=4 pc=59442 opcode=113 exec=1"));
    CHECK(exec_log_has("phase=gf_pre event=END main=4 pc=59442 opcode=113 exec=1"));
    core.transport=0;
    adapter_begin();exec_log_n=0;exec_log[0]=0;core.transport=&exec_transport;
    CHECK(!trial_step(&core,&core.token,true,bus_record_operation,0));
    CHECK(exec_log_has("phase=gf_bus event=BEGIN pc=291 bar=0 off=512 width=4 old=4294967295 new=2748"));
    CHECK(exec_log_has("phase=gf_bus event=END pc=291 bar=0 off=512 width=4 readback=4294967295 rc=0"));
    CHECK(exec_log_has("phase=gf_bus event=END pc=291 bar=0 off=512 width=4 readback=2748 rc=0"));
    core.transport=0;
    output("runner adapter: approval/order, clock/bus/wait/deadline, REC failure and S0 PASS\n");
}
#ifndef GF_S3_ENTRY
#define GF_S3_ENTRY _start
#endif
void GF_S3_ENTRY(void)
{
    gf_fixture_init();adapter_cases();
    output(failures?"GF S3 FAIL\n":"GF S3 PASS\n");
    __asm__ volatile("int $0x80" : : "a"(1),"b"(failures?1:0));__builtin_unreachable();
}
