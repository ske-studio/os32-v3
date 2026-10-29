#include "gfinit_c_fixture_host.inc"
#include "gfinit_c_trace_host.inc"
#include "gfinit_c_contract_host.inc"
#include "gfinit_c_memory_host.inc"
#include "gfinit_c_checks_host.inc"
#include "gfinit_c_verify_host.inc"
#include "gf_stage_host.inc"
#include "gfinit_sequence_host.inc"
#include "gfinit_c_only_cases_host.inc"
#include "gfinit_s0_cases_host.inc"
#ifndef GF_C_ENTRY
#define GF_C_ENTRY _start
#endif
void GF_C_ENTRY(void)
{
    gf_fixture_init();
#ifdef HWDEV_SEQUENCE_IMAGE
    sequence_owner_cases();
#endif
    sequence_cases();
    c_live_sample();
    verify_policy_cases();
    prefix_equivalence();
    contracts();
    frame_cases();
    color_cases();
    c_walk(0,true);
    c_walk(1,true);
    c_walk(1,false);
    check_cases();
    wait_cases();
    route_cell_cases();
    stage_record();
    stage_gates();
    stage_s0_failures();
    stage_local_locks();
    output(failures?"C FAIL\n":"C PASS\n");
    __asm__ volatile("int $0x80"::"a"(1),"b"(failures?1:0):"memory");for(;;){}
}
