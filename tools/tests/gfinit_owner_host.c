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
void _start(void)
{ production_gate_test();vpll_budget_test();mode_c_test();owner_verify_test();cli_test();cli_write_test();cli_verify_test();inspection_test();lifecycle();failures_test();allocation_test();ownership_test();expansion_lifetime_test();output(failures?"GF owner FAIL\n":"GF owner PASS\n");__asm__ volatile("int $0x80"::"a"(1),"b"(failures?1:0));__builtin_unreachable(); }
