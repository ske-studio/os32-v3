#include "gfinit_c_fixture_host.inc"
#include "gf_stage_host.inc"
#include "gfinit_s0_cases_host.inc"
void _start(void)
{
    gf_fixture_init();
    stage_s0_failures();
    stage_local_locks();
    output(failures?"S0 FAIL\n":"S0 PASS\n");
    __asm__ volatile("int $0x80"::"a"(1),"b"(failures?1:0):"memory");for(;;){}
}
