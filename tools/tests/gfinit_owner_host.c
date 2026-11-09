extern unsigned int recover_gf_test_dev;
#include "gfinit_owner_fixture_host.inc"
#include "gfinit_owner_c_cases_host.inc"
#include "gfinit_owner_lifetime_host.inc"
#include "gfinit_owner_inspect_host.inc"
#include "gfinit_owner_shell_host.inc"
#include "gfinit_owner_vpll_host.inc"
static void production_gate_test(void)
{
    production_selected=1;
    u32 before=reads+writes+alloc_calls+begins;
    struct gfinit_request q={0};
    for (u32 op=GF_OP_INSPECT;op<=GF_OP_C_ONLY;op++)
        CHECK(gfinit_call(op,&q,sizeof(q),&base,&transport,&s0)==-1);
    CHECK(reads+writes+alloc_calls+begins==before);
    production_selected=0;
    output("Production selection refuses gfinit before diagnostics I/O PASS\n");
}
static void foundation_selection_test(void)
{
    setup();observations=0;
    struct gf1_request q={sizeof(q),GF1_STATUS,0,0};
    CHECK(gfinit_call(GF_OP_FOUNDATION,&q,sizeof(q),&base,&transport,&s0)==GF1_EINVAL);
    CHECK(observations==1 && !observed_foundation && !reads && !alloc_calls);
    static struct gf_foundation production;
    production_selected=1;test_foundation_ops.production=&production;
    q=(struct gf1_request){sizeof(q),GF1_STATUS,0,0};
    CHECK(!gfinit_call(GF_OP_FOUNDATION,&q,sizeof(q),&base,&transport,&s0));
    CHECK(observed_foundation==&production && observed_row_owner==&production);
    production_selected=0;test_foundation_ops.production=0;
    CHECK(!call(GF_OP_INSPECT,0,0));
    owner->foundation_prepared=true;stage.foundation=&owner->foundation;
    owner->prepare_flags=GF_ALLOW_WRITE|GF_MODE_C|GF_NO_VERIFY_FILE|GF_VERIFY_RECORD;
    owner->policy_count=1;owner->policy_regs[0]=(struct gfinit_no_verify){GF_VERIFY_MMIO,0x200};
    owner->last_op=GF_OP_MODESET;owner->last_count=0;
    q=(struct gf1_request){sizeof(q),GF1_CYCLE,0,0};
    CHECK(!gfinit_call(GF_OP_FOUNDATION,&q,sizeof(q),&base,&transport,&s0));
    CHECK(observed_foundation==&owner->foundation && observed_row_owner==owner && !gf_cycle_policy);
    stage.foundation=0;
    q=(struct gf1_request){sizeof(q),GF1_STATUS,0,0};
    CHECK(gfinit_call(GF_OP_FOUNDATION,&q,sizeof(q),&base,&transport,&s0)==GF1_EINVAL);
    CHECK(!observed_foundation);
    u32 before=observations;
    q=(struct gf1_request){sizeof(q),GF1_STATUS,1,0};
    CHECK(gfinit_call(GF_OP_FOUNDATION,&q,sizeof(q),&base,&transport,&s0)==GF1_EINVAL);
    CHECK(observations==before && q.rc==GF1_EINVAL);
    CHECK(!gf_release_rom(owner));
}
static void command_selection_test(void)
{
    setup(); command_calls=0;
    struct gf2_request q={sizeof(q),GF5_FEATURE,GF5T_THERM,0};
    CHECK(!gfinit_call(GF_OP_COMMAND,&q,sizeof(q),&base,&transport,&s0));
    CHECK(command_calls==1 && command_test_id==GF5_FEATURE && command_session==GF5T_THERM);
    q=(struct gf2_request){sizeof(q),GF4_DISPLAY,GF4_CUI_ENTER,0};
    CHECK(!gfinit_call(GF_OP_COMMAND,&q,sizeof(q),&base,&transport,&s0));
    CHECK(command_calls==2 && command_test_id==GF4_DISPLAY && command_session==GF4_CUI_ENTER);
    q=(struct gf2_request){sizeof(q),GF5_FEATURE,0xffff0101U,0};
    CHECK(!gfinit_call(GF_OP_COMMAND,&q,sizeof(q),&base,&transport,&s0));
    CHECK(command_calls==3 && command_session==0xffff0101U);
    q=(struct gf2_request){sizeof(q),GF6_PRIM,7,0};
    CHECK(!gfinit_call(GF_OP_COMMAND,&q,sizeof(q),&base,&transport,&s0));
    CHECK(command_calls==4 && command_test_id==GF6_PRIM && command_session==7);
    q=(struct gf2_request){sizeof(q),GF6_PRIM+1,0,0};
    CHECK(gfinit_call(GF_OP_COMMAND,&q,sizeof(q),&base,&transport,&s0)==GF2_EINVAL);
    CHECK(command_calls==4 && q.rc==GF2_EINVAL);
    q=(struct gf2_request){sizeof(q),GF2_STATUS,1,0};
    CHECK(gfinit_call(GF_OP_COMMAND,&q,sizeof(q),&base,&transport,&s0)==GF2_EINVAL);
    CHECK(command_calls==4);
}
void _start(void)
{
    command_selection_test(); t2b_clock_advances();t2b_revoked_cleanup();
    foundation_selection_test();foundation_cleanup_test(); production_gate_test();vpll_budget_test();mode_c_test();owner_verify_test();cli_test();cli_write_test();cli_verify_test();inspection_test();lifecycle();failures_test();allocation_test();ownership_test();expansion_lifetime_test();output(failures?"GF owner FAIL\n":"GF owner PASS\n");__asm__ volatile("int $0x80"::"a"(1),"b"(failures?1:0));__builtin_unreachable(); }
