#include "gfinit_c_fixture_host.inc"
static struct gf_stage_state stage_fixture;
#include "gfinit_sequence_host.inc"
void _start(void)
{
    gf_fixture_init();
    sequence_cases();
    output(failures?"INIT FAIL\n":"INIT PASS\n");
    __asm__ volatile("int $0x80"::"a"(1),"b"(failures?1:0):"memory");for(;;){}
}
