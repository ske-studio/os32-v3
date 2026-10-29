#include "gfinit_c_fixture_host.inc"
#include "gf_stage_host.inc"
#include "gfinit_c_only_cases_host.inc"
void _start(void)
{
    gf_fixture_init();
    stage_gates();
    output(failures?"C_ONLY FAIL\n":"C_ONLY PASS\n");
    __asm__ volatile("int $0x80"::"a"(1),"b"(failures?1:0):"memory");for(;;){}
}
