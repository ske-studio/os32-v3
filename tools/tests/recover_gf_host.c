#include "recover_gf_fixture_host.inc"
#include "recover_gf_cases_host.inc"
#include "recover_cleanup_host.inc"

void _start(void)
{
    gf_fixture_init();
    setup(); inventory_test(); fixed_test(); trace_test(); cleanup_envelope_tests();
    print(failures?"GF profile FAIL\n":"GF profile PASS\n");
    __asm__ volatile("int $0x80" : : "a"(1), "b"(failures?1:0));
    __builtin_unreachable();
}
