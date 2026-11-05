/* S1 bytes (193 instructions/27 kinds), synthetic supporting tables. The
 * oracle is the frozen pre-P6 expansion/executor trace. */
#define GF_S3_ENTRY old_s3_entry
#include "gfinit_exec_host.c"
#include "../../hwdev/gf/runner.c"
#include "../../hwdev/gf/save.c"
#include "../../hwdev/gf/pll.c"

struct raw_event { u32 write, off, width, value, pc, op; };
#include "gf_runner_trace_fixture.h"
/* S1 remains 27 kinds; init.c:1093-1143 adds exactly two dispatch entries. */
enum { GF_RUNNER_S1_KINDS=27, GF_RUNNER_TMDS_KINDS=2 };
static struct raw_event new_trace[TRACE_MAX];
static const struct raw_event *old_trace;
static u32 raw_count, raw_index[6], fail_at, point_count, point_fail;
static u32 restore_calls, adapter_cleanup, restore_fail_at, raw_calls;
static u32 point_fail_raw_calls;
static struct gf_init_context init;
static struct gf_init_program program;
static u32 observed[256], raw_vga[6][256], restored_entries, seed_mode;
static u32 instruction_seen[256];
static bool diagnostic_mode,a2_bad_decode, split_restore_error;
static u32 initial_value(u32 off,u32 width)
{
    return seed_mode ? ((off * 0x10203U) ^ 0xa5a50f55U) & width_mask(width) : 0;
}
/* Independent VGA aliases, two heads times CRTC/SEQ/GFX. */
static int raw_port(u32 off)
{
    for (u32 h = 0; h < 2; h++) {
        if (off == 0x6013d4U + h * 0x2000U) return h * 3;
        if (off == 0x0c03c4U + h * 0x2000U) return h * 3 + 1;
        if (off == 0x0c03ceU + h * 0x2000U) return h * 3 + 2;
    }
    return -1;
}
static u32 raw_key(u32 off, u32 width)
{
    for (u32 k = 0; k < profile.target_count; k++) {
        const struct recover_target *t = &profile.targets[k];
        struct recover_bus_access a;
        if (t->width != width || recover_target_address(t, REC_READ, &a)) continue;
        if (a.offset != off) continue;
        if (t->space != REC_VGA_INDEXED) return k;
        int p = raw_port(off - 1);
        if (p >= 0 && raw_index[p] == t->index) return k;
    }
    return RECOVER_NONE;
}
static int raw_access(u32 write, u32 off, u32 width, u32 *value)
{
    raw_calls++;
    if (raw_calls == fail_at || raw_calls == restore_fail_at) return -1;
    u32 k = raw_key(off, width);
    int p = raw_port(off);
    int data = raw_port(off - 1);
    if (!write) *value = p >= 0 ? raw_index[p] : data >= 0 ?
        raw_vga[data][raw_index[data]] : k == RECOVER_NONE ? 0 : values[k];
    if (write) {
        if (p >= 0) raw_index[p] = *value;
        else if (data >= 0) raw_vga[data][raw_index[data]] = *value & 255;
        else if (k != RECOVER_NONE) values[k] = *value & width_mask(width);
    }
    struct raw_event *trace = new_trace;
    CHECK(raw_count < TRACE_MAX);
    u32 pc = diagnostic_mode ? execution.init.fail_pc : init.fail_pc;
    u32 op = pc<sizeof(rom)?rom[pc]:0;
    if (raw_count < TRACE_MAX) trace[raw_count++] = (struct raw_event){write,off,width,*value,pc,op};
    observed[op]++;
    return 0;
}
static int new_read(void *v, u32 bar, u32 off, u32 width, u32 *value)
{
    (void)v; CHECK(bar == 0);
    return raw_access(0, off, width, value);
}
static int new_write(void *v, u32 bar, u32 off, u32 width, u32 value)
{
    (void)v; CHECK(bar == 0);
    return raw_access(1, off, width, &value);
}
static int new_point(void *v)
{
    (void)v;
    if(init.fail_pc<sizeof(rom))instruction_seen[rom[init.fail_pc]]++;
    if (++point_count != point_fail) return 0;
    point_fail_raw_calls=raw_calls;
    return -1;
}
static int raw_delay(void *v,u32 us)
{
    int rc=exec_delay(v,us);
    if(rc)return rc;
    u32 pc=diagnostic_mode ? execution.init.fail_pc : init.fail_pc;
    u32 op=pc<sizeof(rom)?rom[pc]:0;
    struct raw_event *trace=new_trace;
    CHECK(raw_count<TRACE_MAX);
    if(raw_count<TRACE_MAX)trace[raw_count++]=(struct raw_event){2,0,0,us,pc,op};
    return 0;
}
static int new_restore(void *v, struct gf_init_context *ctx)
{
    (void)v;
    restore_calls++;
    u32 count=ctx->saved_count;
    int rc = gf_init_restore(ctx);
    if(!rc) for(u32 i=0;i<count;i++) {
        const struct gf_init_saved *s=&ctx->saved[i];
        int p=raw_port(s->offset);
        int d=raw_port(s->offset-1);
        u32 k=raw_key(s->offset,s->width);
        u32 value=p>=0?raw_index[p]:d>=0?raw_vga[d][s->select_value]:k==RECOVER_NONE?0:values[k];
        CHECK(value==s->old);
        restored_entries++;
    }
    adapter_cleanup++;
    if (split_restore_error) { ctx->last_rc=GF_SCRIPT_EIO; return -1; }
    return rc;
}
static void reset_raw(void)
{
    raw_count = raw_calls = fail_at = restore_fail_at = point_count = point_fail = 0;
    restore_calls = adapter_cleanup = restored_entries = 0;
    split_restore_error=false; point_fail_raw_calls=0;
    clock_value = clock_jump = wait_total = clock_fail = wait_fail = 0;
    for (u32 i = 0; i < profile.target_count; i++)
        values[i] = profile.targets[i].space==REC_PCI_CFG || zero_target(&profile.targets[i]) ? 0 :
                    initial_value(profile.targets[i].offset,profile.targets[i].width);
    for (u32 i = 0; i < 6; i++) {
        raw_index[i] = 0xa5;
        for(u32 j=0;j<256;j++)raw_vga[i][j]=initial_value(j+256*i,1);
    }
}
static void new_context(void)
{
    reset_raw();
    for(u32 i=0;i<256;i++)instruction_seen[i]=0;
    init = (struct gf_init_context){.rom=rom,.rom_size=sizeof(rom),
        .io={.read=new_read,.write=new_write,.point=new_point,.restore=new_restore,
             .timing={exec_clock,raw_delay}}};
}
static bool equal_event(const struct raw_event *a, const struct raw_event *b)
{
    return a->write == b->write && a->off == b->off && a->width == b->width &&
           a->value == b->value && a->pc == b->pc;
}
static int diagnostic_read(void *v,const struct recover_bus_access *a,u32 *value)
{
    (void)v;
    /* Recover's decode-zero guards are bookkeeping, outside the old operand
     * stream too. S1 never reads these two zero-only registers as operands. */
    if(a->space==REC_PCI_CFG || a->offset==0x1850 || a->offset==0x1854) {
        *value=a2_bad_decode && a->space==REC_PCI_CFG && a->offset==RECOVER_CFG50;return 0;
    }
    return raw_access(0,a->offset,a->width,value);
}
static int diagnostic_write(void *v,const struct recover_bus_access *a,u32 value)
{ (void)v;return raw_access(1,a->offset,a->width,&value); }
static void diagnostic_run(void)
{
    adapter_begin();reset_raw();diagnostic_mode=true;
    executor_bus.read=diagnostic_read;executor_bus.write=diagnostic_write;
    static const struct gfinit_exec_ops timing={exec_clock,raw_delay};
    execution.ops=&timing;
    for(u32 m=0;m<5;m++) {
        execution.main_id=m;
        u32 before=raw_count;
        CHECK(trial_step(&core,&core.token,false,gfinit_exec_run,&execution)<0 && raw_count==before);
        CHECK(!trial_step(&core,&core.token,true,gfinit_exec_run,&execution));
        CHECK(core.state==REC_PAUSED);
    }
}
static int diagnostic_cleanup(void *v)
{
    (void)v;cleanup_calls++;
    CHECK(execution.init.saved_count==1); /* interrupted RESET */
    int rc=gfinit_exec_restore(&execution);
    CHECK(!execution.init.saved_count);
    return rc;
}
static int diagnostic_abort(void *v,u32 session)
{ (void)v;(void)session;return execution.init.saved_count!=0; }
static void diagnostic_cancel(void)
{
    adapter_begin();reset_raw();diagnostic_mode=true;
    executor_bus.read=diagnostic_read;executor_bus.write=diagnostic_write;
    executor_bus.local_cleanup=diagnostic_cleanup;executor_bus.abort_requested=diagnostic_abort;
    values[raw_key(0x184c,4)]=0xa5a50f55;
    CHECK(trial_step(&core,&core.token,true,gfinit_exec_run,&execution)<0);
    CHECK(core.reason==REC_ABORT && cleanup_calls==1 && !core.plan);
    CHECK(values[raw_key(0x184c,4)]==0xa5a50f55);
    diagnostic_mode=false;
    output("diagnostic RESET cancellation restores pending transaction before S0 PASS\n");
}
static void diagnostic_a2(void)
{
    for(u32 bad=0;bad<2;bad++) {
        adapter_begin();release_plan(&core);core.state=REC_IDLE;
        decoded.nodes[0]=(struct recover_plan_node){.phase=REC_PREPARE,.main_id=RECOVER_NONE,
            .pc=RECOVER_NONE,.opcode=GF_INIT_A2,.rule_id=RECOVER_NONE,.expr_id=RECOVER_NONE,
            .next={RECOVER_NONE,RECOVER_NONE}};
        decoded.entries[0]=(struct recover_plan_entry){REC_PREPARE,RECOVER_NONE,0};
        decoded.plan.node_count=decoded.plan.entry_count=1;
        exec_layouts[profile.layout_count++]=(struct recover_opcode_layout){
            .opcode=GF_INIT_A2,.dir_substep=RECOVER_NONE};
        CHECK(!contract_bind());exec_arm();
        core.token.profile_crc=profile.crc;core.token.profile_version=profile.version;
        core.token.plan_hash=core.bound_hash;core.token.snapshot_crc=snapshot_crc(&core);
        core.state=REC_PAUSED;core.in_call=core.in_opcode=false;
        reset_raw();diagnostic_mode=true;a2_bad_decode=bad;
        executor_bus.read=diagnostic_read;executor_bus.write=diagnostic_write;
        int rc=trial_step(&core,&core.token,true,gfinit_exec_prepare,&execution);
        if(bad)CHECK(rc && core.reason==REC_CONTRACT && cleanup_calls==1 && raw_count==5);
        else {
            CHECK(!rc && raw_count==GF_ARRAY(a2_trace_fixture));
            for(u32 i=0;i<raw_count && i<GF_ARRAY(a2_trace_fixture);i++)
                CHECK(equal_event(&new_trace[i],&a2_trace_fixture[i]));
        }
    }
    a2_bad_decode=false;diagnostic_mode=false;
    output("diagnostic A2 literal trace and decode guard before VGA PASS\n");
}
static void differential(bool diagnostic)
{
    setup(); fixture();
    synthetic_profile();
    old_trace=seed_mode ? old_trace_seed1 : old_trace_seed0;
    u32 old_count=GF_ARRAY(old_trace_seed0);
    new_context();
    if(diagnostic) diagnostic_run();
    else {
        diagnostic_mode=false;
        CHECK(gf_init_run(&init,&program)==0);
        CHECK(program.count==193 && gf_op_dispatch_count==GF_RUNNER_S1_KINDS+GF_RUNNER_TMDS_KINDS && !restore_calls);
    }
    output("S1 command/wait events old/new (with saves) "); number(old_count); output("/"); number(raw_count); output("\n");
    /* P0 adds four literal PLL snapshots and two latch index pairs to the
     * old command stream. This independent overlay is specified by internal.h;
     * no sequence/dispatch/run code is used to construct expectations. */
    const struct raw_event saves[]={
        {0,0x4038,4,0,0xe754,0x4a}, {0,0x4024,4,0,0xe754,0x4a},
        {0,0x403c,4,0,0xe754,0x4a}, {0,0x4004,4,0,0xe728,0x4a},
        {0,0x6013d4,1,0,0xe67f,0x51}, {1,0x6013d4,1,0,0xe67f,0x51},
        {0,0x6033d4,1,0,0xe6c0,0x51}, {1,0x6033d4,1,0,0xe6c0,0x51}
    };
    u32 oi=0, ni=0, extra=0;
    while(oi<old_count && ni<raw_count) {
        if(equal_event(&old_trace[oi],&new_trace[ni])) {oi++;ni++;continue;}
        CHECK(extra<8);
        if(extra==8)break;
        struct raw_event expected_save=saves[extra++];
        u32 before=oi;
        if(expected_save.op==0x51) {
            before=0;
            while(before<old_count && old_trace[before].pc!=expected_save.pc)before++;
        }
        expected_save.value=raw_port(expected_save.off)>=0?0xa5:initial_value(expected_save.off,expected_save.width);
        for(u32 i=0;i<before;i++)if(old_trace[i].write==1 && old_trace[i].off==expected_save.off)
            expected_save.value=old_trace[i].value;
        CHECK(equal_event(&expected_save,&new_trace[ni]));
        ni++;
    }
    CHECK(oi==old_count && ni==raw_count && extra==8);
    u32 kinds=0;
    for(u32 i=0;i<256;i++)if(instruction_seen[i])kinds++;
    CHECK(diagnostic || kinds==27);
    output("S1: 193 decoded/27 executed kinds; old operand I/O exact, 8 contract saves exact\n");

}
/* Synthetic scripts use the same span preparation and main loop, with no CRC
 * claim. Every expected value below is literal or supplied by the fake device. */
static int synthetic(const u8 *code, u32 length)
{
    fixture(); new_context();
    for(u32 i=0;i<length;i++)rom[0x100+i]=code[i];
    program=(struct gf_init_program){.main_pc={0x100}};
    struct gf_script_span span={.pc=0x100,.length=length};
    return init_prepare_span(&init,&program,&span);
}
static void control_cases(void)
{
    /* Nested repeats, SUB_DIRECT and SUB6, skipped calls, forced RESET, MACRO. */
    const u8 nested[]={0x33,2,0x33,3,0x6f,0,0x36,0x36,0x5b,0,3,0x6b,6,
                       0x38,0x5b,0,3,0x6b,6,0x74,0x10,0,0x72,0x71};
    CHECK(synthetic(nested,sizeof(nested))==0);
    rom[0x300]=0x6f;rom[0x301]=0;rom[0x302]=0x71;
    put(GF_INIT_SCRIPT_TABLE+12,0x300,2);
    for(u32 i=0;i<program.count;i++) {
        u32 pc=program.insns[i].pc;
        if(rom[pc]==0x6b)CHECK(gfinit_decode_instruction(rom,sizeof(rom),pc,&program.insns[i])==0);
    }
    struct gf_script_span sub={.pc=0x300,.length=3};
    CHECK(init_prepare_span(&init,&program,&sub)==0);
    CHECK(program.insns[0].end_repeat==0x107 && program.insns[1].end_repeat==0x106);
    CHECK(gf_init_run_one(&init,&program)==0 && raw_count==8 && !init.calls && !init.repeats);
    CHECK(init.wait_us==0 && init.steps==29);
    for(u32 i=0;i<raw_count;i++)CHECK(new_trace[i].write==1 && new_trace[i].width==4);
    /* Conditions fail once, stay skipped, NOT toggles, RESUME clears. */
    const u8 conditions[]={0x75,0,0x6f,0,0x75,1,0x38,0x6f,0,0x72,0x39,0,0x6f,0,0x71};
    CHECK(synthetic(conditions,sizeof(conditions))==0);
    put(GF_INIT_CONDITION_TABLE+4,1,4);put(GF_INIT_CONDITION_TABLE+8,1,4);
    put(GF_INIT_IO_FLAG_TABLE+7,1,1);put(GF_INIT_IO_FLAG_TABLE+8,1,1);
    for(u32 i=0;i<program.count;i++) {
        u32 pc=program.insns[i].pc;
        CHECK(gfinit_decode_instruction(rom,sizeof(rom),pc,&program.insns[i])==0);
    }
    CHECK(gf_init_run_one(&init,&program)==0 && init.skip && raw_count==4);
    CHECK(new_trace[0].write==0 && new_trace[1].write==1 &&
          new_trace[2].write==1 && new_trace[3].write==0);
    /* REPEATs are structural in skip too; no writes until NOT. */
    const u8 skip_repeat[]={0x38,0x33,2,0x6f,0,0x36,0x38,0x6f,0,0x71};
    CHECK(synthetic(skip_repeat,sizeof(skip_repeat))==0);
    CHECK(gf_init_run_one(&init,&program)==0 && raw_count==1 && !init.repeats);
    /* A recursive subroutine hits exactly the existing call ceiling. */
    const u8 recurse[]={0x5b,0,1,0x71};
    CHECK(synthetic(recurse,sizeof(recurse))==0);
    CHECK(gf_init_run_one(&init,&program)<0 && init.calls==16 && raw_count==0);
    /* Nested valid loops exceed the instruction budget without waiting. */
    const u8 budget[]={0x33,254,0x33,254,0x63,0x36,0x36,0x71};
    CHECK(synthetic(budget,sizeof(budget))==0);
    CHECK(gf_init_run_one(&init,&program)<0 && init.steps==4096);
    const u8 unmatched[]={0x33,2,0x71}, extra_end[]={0x36,0x71};
    CHECK(synthetic(unmatched,sizeof(unmatched))<0 && raw_count==0);
    CHECK(synthetic(extra_end,sizeof(extra_end))<0 && raw_count==0);
    u8 deep[28];
    for(u32 depth=8;depth<=9;depth++) {
        for(u32 i=0;i<depth;i++){deep[2*i]=0x33;deep[2*i+1]=1;deep[2*depth+i]=0x36;}
        deep[3*depth]=0x71;
        int rc=synthetic(deep,3*depth+1);
        if(depth==8)CHECK(rc==0 && gf_init_run_one(&init,&program)==0 && !init.repeats);
        else CHECK(rc<0 && raw_count==0);
    }
    /* DONE cannot return across an active repeat owned by its callee. */
    new_context();init.calls=1;init.call[0].ctx=0;init.repeats=1;
    struct gfinit_instruction done={0};
    CHECK(gf_op_done_run(&init,&done)<0 && init.calls==1);
    output("controls: nested/skip REPEAT, SUB/SUB_DIRECT, conditions, MACRO, limits PASS\n");
}
static void head_cases(void)
{
    const u8 heads[]={0x53,0x44,3,0x53,0x44,0,0x53,0x11,7,0x71};
    CHECK(synthetic(heads,sizeof(heads))==0);
    CHECK(gf_init_run_one(&init,&program)==0 && raw_count==6 && init.head==0);
    for(u32 i=0;i<raw_count;i++)CHECK(new_trace[i].off==0x6013d4U+(i%2));
    const u8 group[]={0x54,4,0x44,3,0x11,7,0x44,0,0x11,9,0x71};
    CHECK(synthetic(group,sizeof(group))==0);
    CHECK(gf_init_run_one(&init,&program)==0 && raw_count==8);
    CHECK(new_trace[2].off==0x6033d4 && new_trace[6].off==0x6013d4);
    const u8 skipped[]={0x38,0x53,0x44,3,0x54,2,0x44,0,0x44,3,0x72,0x53,0x11,7,0x71};
    CHECK(synthetic(skipped,sizeof(skipped))==0);
    CHECK(gf_init_run_one(&init,&program)==0 && init.head==1 && raw_count==2);
    CHECK(new_trace[0].off==0x6033d4);
    /* Both latch aliases independently honor CR44; skip rejects either. */
    struct gfinit_instruction latch={.index=0x20,.value=0x44,.count=1,.ref=0x200};
    new_context();init.head=1;rom[0x200]=0x55;raw_vga[3][0x20]=0x77;
    CHECK(gf_op_cr_index_address_latched_run(&init,&latch)==0 && init.head==1);
    CHECK(new_trace[4].off==0x6013d4 && new_trace[5].off==0x6013d5);
    CHECK(raw_index[3]==0xa5 && raw_vga[3][0x20]==0x77);
    new_context();init.skip=true;
    CHECK(gf_op_cr_index_address_latched_run(&init,&latch)<0 && raw_count==0);
    latch.index=0x44;latch.value=0x20;
    CHECK(gf_op_cr_index_address_latched_run(&init,&latch)<0 && raw_count==0);
    /* Read-only indexed selectors use CR44's head0 alias as well. */
    struct gfinit_instruction restrict_op={.port=0x3d4,.index=0x44,.target=0x200,.ref=0x200};
    new_context();init.head=1;put(0x200,0x1234,4);
    CHECK(gf_op_io_restrict_prog_run(&init,&restrict_op)==0);
    CHECK(new_trace[0].off==0x6013d4 && init.head==1);
    restrict_op.ref=GF_INIT_IO_FLAG_TABLE;
    new_context();init.head=1;
    CHECK(gf_op_io_flag_condition_run(&init,&restrict_op)==0 && new_trace[0].off==0x6013d4 && init.head==1);
    restrict_op.ref=0x200;restrict_op.target=GF_PLL_CORE;put(0x200,200000,4);
    new_context();init.head=1;
    CHECK(gf_op_io_restrict_pll2_run(&init,&restrict_op)==0 && new_trace[0].off==0x6013d4 && init.head==1);
    const u32 ports[]={0x3c4,0x3ce}, aliases[]={0xc23c4,0xc23ce};
    for(u32 i=0;i<2;i++) {
        new_context();init.head=1;restrict_op.port=ports[i];
        CHECK(gf_op_io_restrict_prog_run(&init,&restrict_op)==0 && new_trace[0].off==aliases[i]);
    }
    restrict_op.port=0x3d4;
    const gf_op_run_fn indexed[]={gf_op_io_restrict_prog_run,gf_op_io_restrict_pll2_run,
        gf_op_io_flag_condition_run,gf_op_copy_run,gf_op_cr_run,
        gf_op_index_io_run,gf_op_zm_index_io_run};
    for(u32 i=0;i<sizeof(indexed)/sizeof(indexed[0]);i++) {
        new_context();init.skip=true;init.head=1;
        CHECK(indexed[i](&init,&restrict_op)<0 && raw_count==0 && init.head==1);
        restrict_op.index=0x11;
        CHECK(indexed[i](&init,&restrict_op)==0 && raw_count==0 && init.head==1);
        restrict_op.index=0x44;
    }
    output("CR44 roundtrip/group/skip/latch/read aliases PASS\n");
}
static void wait_cases(void)
{
    new_context();
    CHECK(gf_init_wait(&init,2501)==0 && init.wait_us==2501 && wait_total==2501 && point_count==6);
    u32 points=point_count;
    CHECK(gf_init_wait(&init,0)==0 && point_count==points && init.wait_us==2501);
    init.wait_us=GF_INIT_WAIT_MAX-1;
    CHECK(gf_init_wait(&init,2)<0 && point_count==points && init.wait_us==GF_INIT_WAIT_MAX-1);
    for(u32 fail=1;fail<=6;fail++) {
        new_context();point_fail=fail;
        CHECK(gf_init_wait(&init,2501)<0 && init.wait_us==2501 && point_count==fail);
        CHECK(wait_total==(fail/2)*1000U-(fail==6?499U:0));
    }
    new_context();wait_fail=1;
    CHECK(gf_init_wait(&init,2501)<0 && init.wait_us==2501 && wait_total==0);
    new_context();clock_fail=1;
    CHECK(gf_init_wait(&init,1)<0 && wait_total==0);
    new_context();clock_value=GF_INIT_EXEC_US;
    CHECK(gf_init_wait(&init,1)<0 && init.elapsed_us==GF_INIT_EXEC_US && wait_total==0);
    new_context();init.started_us=0xfffffff0;clock_value=0x10;
    CHECK(gf_init_wait(&init,1)==0 && init.elapsed_us==33);
    output("wait chunks/accounting/cancel/clock/deadline PASS\n");
}
static void pll_case(void)
{
    /* Constrain synthetic limits to the design's known 26/7/1/1/2 solution.
     * Expected five writes are literal GF6200_4C_DESIGN values, not generated
     * from the solver or sequence under test. */
    fixture();new_context();
    u32 at=GF_INIT_PLL_TABLE+4+35*2;
    put(at+20,26,1);put(at+21,26,1);put(at+22,7,1);put(at+23,7,1);put(at+29,2,1);
    struct gfinit_instruction pll={.target=0x680508,.value=25175};
    struct gfinit_pll_limits limits;
    struct gfinit_pll_solution solution;
    CHECK(gfinit_pll_parse(rom,sizeof(rom),GF_INIT_PLL_TABLE,pll.target,&limits)==0);
    CHECK(gfinit_pll_solve(&limits,25175,&solution)==0 && solution.n1==26 && solution.m1==7 &&
          solution.n2==1 && solution.m2==1 && solution.p==2 && solution.actual==25071);
    const u32 offsets[]={0xc040,0x680580,0x680578,0x680508,0xc040};
    const u32 bits[]={0,0x100,0x8000011f,0x21a07,0};
    CHECK(gf_op_pll2_run(&init,&pll)==0 && raw_count==9 && !init.saved_count);
    for(u32 i=0;i<4;i++)CHECK(!new_trace[i].write && new_trace[i].off==offsets[i]);
    for(u32 i=0;i<5;i++)CHECK(new_trace[4+i].write==1 && new_trace[4+i].off==offsets[i] && new_trace[4+i].value==bits[i]);
    for(u32 failure=1;failure<=9;failure++) {
        new_context();fail_at=failure;
        CHECK(gf_op_pll2_run(&init,&pll)<0);
        CHECK(new_restore(0,&init)==0 && !init.saved_count);
    }
    new_context();init.skip=true;
    CHECK(gf_op_pll2_run(&init,&pll)==0 && raw_count==0);
    init.skip=false;pll.value=0;
    CHECK(gf_op_pll2_run(&init,&pll)<0 && raw_count==0);
    output("PLL 26/7/1/1/2 literal sequence, skip and failures PASS\n");
}
static void save_cases(void)
{
    new_context();
    for(u32 i=0;i<GF_INIT_SAVE_MAX;i++)CHECK(gf_init_save(&init,0,0x184c,4,i)==0);
    CHECK(gf_init_save(&init,0,0x1850,4,0)<0 && init.saved_count==GF_INIT_SAVE_MAX);
    CHECK(gf_init_save_indexed(&init,0,0x6013d4,0x6013d5,0x20,0)<0 &&
          init.saved_count==GF_INIT_SAVE_MAX && raw_count==0);
    gf_init_commit(&init);CHECK(init.saved_count==0 && raw_count==0);
    CHECK(gf_init_restore(&init)==0 && raw_count==0);
    for(u32 failure=0;failure<=4;failure++) {
        new_context();
        CHECK(gf_init_save(&init,0,0x184c,4,0x123)==0);
        CHECK(gf_init_save(&init,0,0x6013d4,1,0xa5)==0);
        CHECK(gf_init_save_indexed(&init,0,0x6013d4,0x6013d5,0x20,0x77)==0);
        restore_fail_at=failure;
        /* Failed select skips its data, but older entries are still tried. */
        CHECK(new_restore(0,&init)==(failure?-1:0));
        CHECK(adapter_cleanup==1 && raw_calls==(failure==1?3U:4U));
        CHECK(init.saved_count==(failure?3U:0U));
        if(failure!=4)CHECK(values[raw_key(0x184c,4)]==0x123);
    }
    /* Forced RESET preserves its register even with skip and interrupted wait. */
    const u8 reset[]={0x38,0x65,0,2,0,0,0x11,0,0,0,0x22,0,0,0,0x71};
    for(u32 failure=0;failure<2;failure++) {
        CHECK(synthetic(reset,sizeof(reset))==0);
        values[raw_key(0x184c,4)]=0xa5a50f55;
        wait_fail=failure;
        int rc=gf_init_run_one(&init,&program);
        if(failure) {
            CHECK(rc<0 && init.saved_count==1);
            CHECK(new_restore(0,&init)==0 && restored_entries==1);
        } else CHECK(rc==0 && init.saved_count==0 && wait_total==10);
        CHECK(values[raw_key(0x184c,4)]==0xa5a50f55);
        CHECK(values[raw_key(0x1850,4)]==0);
    }
    /* Every bus failure in the real fixture, including transaction middle and
     * final restoration writes, must stop and invoke the real table cleanup. */
    fixture();new_context();
    CHECK(gf_init_run(&init,&program)==0);
    u32 events=raw_calls, restored=0;
    for(u32 failure=1;failure<=events;failure++) {
        new_context();fail_at=failure;
        CHECK(gf_init_run(&init,&program)<0);
        CHECK(restore_calls==1 && adapter_cleanup==1 && init.saved_count==0);
        restored+=restored_entries;
    }
    CHECK(restored>0);
    output("I/O failure sweep ");number(events);output(" points, restored entries ");number(restored);output(" PASS\n");
    /* Point/clock/deadline errors use the runner's failure exit too. */
    new_context();point_fail=1;
    CHECK(gf_init_run(&init,&program)<0 && raw_count==0 && restore_calls==1);
    new_context();clock_fail=1;
    CHECK(gf_init_run(&init,&program)<0 && raw_count==0 && restore_calls==1);
    new_context();clock_value=GF_INIT_EXEC_US;
    CHECK(gf_init_run(&init,&program)<0 && raw_count==0 && restore_calls==1);
    /* Corruption is caught before the first hardware operation. */
    fixture();new_context();rom[0xdeb0]=0xff;
    CHECK(gf_init_run(&init,&program)<0 && raw_count==0);
    fixture();new_context();rom[0xdeb5]^=1;
    CHECK(gf_init_run(&init,&program)<0 && raw_count==0);
    output("save capacity/reverse restore/restore failures/RESET/runner exits PASS\n");
}
/* PM decision 19: extend this runner's real-save/restore fixture. */
static struct gf_init_program split_frozen;
static struct raw_event split_wrapper_trace[sizeof(split_trace)/sizeof(split_trace[0])];
static void split_fixture(void)
{
    fixture(); new_context(); diagnostic_mode=false;
    for(u32 i=0;i<sizeof(split_signature);i++)rom[SPLIT_BIT+i]=split_signature[i];
    rom[SPLIT_BIT+8]=12; rom[SPLIT_BIT+9]=6; rom[SPLIT_BIT+10]=1;
    rom[SPLIT_BIT+12]='I'; rom[SPLIT_BIT+13]=1;
    put(SPLIT_BIT+14,12,2); put(SPLIT_BIT+16,SPLIT_I,2);
    put(SPLIT_I,SPLIT_SUB_TABLE,2); put(SPLIT_I+4,SPLIT_MACRO_TABLE,2);
    put(SPLIT_SUB_TABLE,SPLIT_SUB,2);
    put(SPLIT_MACRO_TABLE,SPLIT_REG,4); put(SPLIT_MACRO_TABLE+4,SPLIT_VALUE,4);
    for(u32 i=0;i<sizeof(split_main);i++)rom[SPLIT_MAIN+i]=split_main[i];
    for(u32 i=0;i<sizeof(split_sub);i++) {
        rom[SPLIT_SUB+i]=split_sub[i]; rom[SPLIT_DIRECT+i]=split_sub[i];
    }
    program=(struct gf_init_program){0};
}
static int split_prepare(void)
{
    return gf_init_script_prepare(&init,&program,SPLIT_MAIN,SPLIT_HEAD,SPLIT_OR,SPLIT_DL);
}
static int split_execute(const struct gf_init_program *p)
{
    return gf_init_script_execute(&init,p,SPLIT_MAIN,SPLIT_HEAD,SPLIT_OR,SPLIT_DL);
}
static void split_unchanged(void)
{
    const u8 *before=(const u8 *)&split_frozen, *after=(const u8 *)&program;
    for(u32 i=0;i<sizeof(program);i++) {
        __asm__ volatile("" : : : "memory");
        CHECK(before[i]==after[i]);
    }
}
static void split_effects(void)
{
    CHECK(raw_count==sizeof(split_trace)/sizeof(split_trace[0]));
    for(u32 i=0;i<raw_count && i<sizeof(split_trace)/sizeof(split_trace[0]);i++)
        CHECK(equal_event(&new_trace[i],&split_trace[i]));
    CHECK(init.steps==SPLIT_STEPS && init.wait_us==SPLIT_WAIT);
    CHECK(init.head==SPLIT_HEAD && init.or_mask==SPLIT_OR && !init.calls && !init.repeats);
}
static void split_cases(void)
{
    split_fixture();
    init.started_us=7; clock_value=11;
    init.steps=GF_INIT_SCHEDULE; init.wait_us=GF_INIT_WAIT_MAX;
    init.elapsed_us=GF_INIT_EXEC_US; init.last_rc=GF_SCRIPT_ETIME;
    init.calls=init.repeats=1; init.skip=true;
    program.count=GF_INIT_INSNS;
    CHECK(split_prepare()==0 && program.count==SPLIT_INSNS);
    CHECK(!raw_count && !raw_calls && !wait_total && !restore_calls);
    CHECK(!init.steps && !init.wait_us && init.elapsed_us==4 && init.started_us==7);
    CHECK(init.pc==SPLIT_MAIN && init.head==SPLIT_HEAD && init.or_mask==SPLIT_OR &&
          init.deadline==SPLIT_DL && init.display_script && init.last_rc==GF_SCRIPT_EIO);
    CHECK(program.insns[0].end_repeat==SPLIT_MAIN+4);
    u32 prepare_points=point_count;
    split_frozen=program;
    u32 rom_crc=crc32_core_update(CRC32_INIT,rom,sizeof(rom));
    /* Execution reinitializes stale execution fields without decoding again. */
    init.pc=RECOVER_NONE; init.head=0; init.or_mask=0; init.deadline=0;
    init.calls=init.repeats=1; init.skip=true; init.display_script=false; init.fail_pc=0;
    CHECK(split_execute(&program)==0 && init.last_rc==GF_SCRIPT_OK && !restore_calls);
    split_effects(); split_unchanged();
    CHECK(init.started_us==7 && init.elapsed_us==4+SPLIT_WAIT);
    CHECK(crc32_core_update(CRC32_INIT,rom,sizeof(rom))==rom_crc);
    u32 execute_points=point_count-prepare_points;
    CHECK(new_restore(0,&init)==0 && restore_calls==1 && adapter_cleanup==1);
    /* Wrapper and split calls have identical checkpoints and operand trace. */
    split_fixture();
    CHECK(gf_init_run_script(&init,&program,SPLIT_MAIN,SPLIT_HEAD,SPLIT_OR,SPLIT_DL)==0);
    split_effects();
    CHECK(point_count==prepare_points+execute_points && !restore_calls);
    for(u32 i=0;i<raw_count && i<sizeof(split_wrapper_trace)/sizeof(split_wrapper_trace[0]);i++)
        split_wrapper_trace[i]=new_trace[i];
    split_fixture(); CHECK(split_prepare()==0); CHECK(split_execute(&program)==0);
    for(u32 i=0;i<raw_count && i<sizeof(split_wrapper_trace)/sizeof(split_wrapper_trace[0]);i++)
        CHECK(equal_event(&new_trace[i],&split_wrapper_trace[i]));
    /* Later execution uses the retained const graph with a fresh owner budget. */
    new_context(); init.started_us=100; clock_value=100;
    init.steps=init.wait_us=init.elapsed_us=0; init.last_rc=GF_SCRIPT_EIO;
    init.calls=init.repeats=1; init.skip=true;
    CHECK(split_execute(&program)==0 && init.started_us==100 && !restore_calls);
    split_effects(); split_unchanged();
    /* Steps/waits consumed by the outer operation remain charged. */
    split_fixture(); CHECK(split_prepare()==0); init.steps=GF_INIT_SCHEDULE;
    split_restore_error=true;
    CHECK(split_execute(&program)==GF_SCRIPT_ETIME && init.last_rc==GF_SCRIPT_ETIME);
    CHECK(init.steps==GF_INIT_SCHEDULE && !raw_count && restore_calls==1);
    split_fixture(); CHECK(split_prepare()==0); init.wait_us=GF_INIT_WAIT_MAX-SPLIT_WAIT+1;
    CHECK(split_execute(&program)==GF_SCRIPT_ETIME && restore_calls==1);
    CHECK(init.wait_us==GF_INIT_WAIT_MAX-SPLIT_WAIT+1 && !wait_total && raw_count==3);
    /* A clock failure before the first checkpoint update exposes elapsed's
     * continuity too, without relying on its subsequently recomputed value. */
    split_fixture(); CHECK(split_prepare()==0); init.elapsed_us=23; clock_fail=1;
    CHECK(split_execute(&program)==GF_SCRIPT_EIO && init.elapsed_us==23 && !raw_count);
    CHECK(restore_calls==1);
    split_fixture(); CHECK(split_prepare()==0); clock_value=SPLIT_DL;
    split_restore_error=true;
    CHECK(split_execute(&program)==GF_SCRIPT_ETIME && init.last_rc==GF_SCRIPT_ETIME);
    CHECK(init.deadline==SPLIT_DL && !raw_count && restore_calls==1);
    /* Each preparation checkpoint can fail, before any device operation. */
    for(u32 n=1;n<=prepare_points;n++) {
        split_fixture(); point_fail=n; split_restore_error=true;
        CHECK(split_prepare()==GF_SCRIPT_EIO && init.last_rc==GF_SCRIPT_EIO);
        CHECK(!raw_count && !raw_calls && !program.count && restore_calls==1 && point_count==n);
    }
    /* Failure at each execution checkpoint stops immediately, keeps the
     * immutable program, and delegates cleanup exactly once. */
    for(u32 n=1;n<=execute_points;n++) {
        split_fixture(); CHECK(split_prepare()==0); split_frozen=program;
        point_fail=point_count+n; split_restore_error=true;
        CHECK(split_execute(&program)==GF_SCRIPT_EIO && init.last_rc==GF_SCRIPT_EIO);
        CHECK(point_count==point_fail && raw_calls==point_fail_raw_calls &&
              restore_calls==1 && adapter_cleanup==1);
        split_unchanged();
    }
    for(u32 n=1;n<=5;n++) { /* three macros and the two indexed CR writes */
        split_fixture(); CHECK(split_prepare()==0); split_frozen=program;
        fail_at=n; split_restore_error=true;
        CHECK(split_execute(&program)==GF_SCRIPT_EIO && raw_calls==n && restore_calls==1);
        split_unchanged();
    }
    /* Invalid inputs never consume the owner's outstanding transaction. */
    split_fixture(); init.saved_count=1;
    CHECK(split_prepare()==GF_SCRIPT_EIO && split_execute(&program)==GF_SCRIPT_EIO);
    CHECK(init.saved_count==1 && !restore_calls && !point_count);
    init.saved_count=0;
    CHECK(gf_init_script_prepare(&init,&program,0,0,0,SPLIT_DL)==GF_SCRIPT_EIO);
    CHECK(gf_init_script_execute(&init,&program,0,0,0,SPLIT_DL)==GF_SCRIPT_EIO);
    CHECK(gf_init_script_prepare(0,&program,SPLIT_MAIN,0,0,SPLIT_DL)==GF_SCRIPT_EIO);
    CHECK(gf_init_script_execute(&init,0,SPLIT_MAIN,0,0,SPLIT_DL)==GF_SCRIPT_EIO);
    CHECK(!restore_calls && !point_count);
    /* Malformed reachable callee fails before even the valid main writes. */
    split_fixture(); rom[SPLIT_DIRECT]=0xff;
    CHECK(split_prepare()==GF_SCRIPT_EIO && !program.count && !raw_count && restore_calls==1);
    split_fixture(); clock_value=SPLIT_DL; split_restore_error=true;
    CHECK(split_prepare()==GF_SCRIPT_ETIME && init.last_rc==GF_SCRIPT_ETIME && restore_calls==1);
    split_fixture(); rom[SPLIT_DIRECT]=0xff;
    CHECK(gf_init_run_script(&init,&program,SPLIT_MAIN,SPLIT_HEAD,SPLIT_OR,SPLIT_DL)==GF_SCRIPT_EIO);
    CHECK(!raw_count && restore_calls==1 && adapter_cleanup==1);
    /* Interrupted RESET keeps the saved table if cleanup itself fails. */
    const u8 reset[]={0x65,0,4,0,0,1,0,0,0,2,0,0,0,0x71};
    for(u32 fail=0;fail<2;fail++) {
        split_fixture();
        for(u32 i=0;i<sizeof(reset);i++)rom[SPLIT_MAIN+i]=reset[i];
        CHECK(split_prepare()==0); split_frozen=program;
        wait_fail=1; restore_fail_at=fail?4:0;
        CHECK(split_execute(&program)==GF_SCRIPT_EIO && restore_calls==1);
        CHECK(init.saved_count==fail && raw_calls==4 && !wait_total);
        CHECK(init.fail_pc==SPLIT_MAIN); split_unchanged();
    }
    output("split runner: prepare/const execute/budgets/cleanup/wrapper PASS\n");
}
void _start(void)
{
    gf_fixture_init(); differential(false); differential(true);
    seed_mode=1; differential(false); differential(true); seed_mode=0;
    diagnostic_mode=false; diagnostic_cancel(); diagnostic_a2();
#ifdef GF_RUNNER_TRACE_ONLY
    if (0)
#endif
    { control_cases(); head_cases(); wait_cases(); pll_case(); save_cases(); split_cases(); }
    output(failures?"GF runner FAIL\n":"GF runner PASS\n");
    __asm__ volatile("int $0x80" : : "a"(1),"b"(failures?1:0));
    __builtin_unreachable();
}
