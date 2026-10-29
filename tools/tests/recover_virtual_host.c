#include "recover_virtual_fixture_host.inc"
#include "recover_virtual_cases_host.inc"
void _start(void)
{
    if (TEST_EXPIRE==3) run_txbusy();
    else if (TEST_EXPIRE==2) run_poll_stop();
    else if (TEST_EXPIRE==1) run_expire(); else run();
    __asm__ volatile("int $0x80"::"a"(1),"b"(0));__builtin_unreachable();
}
