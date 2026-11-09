#include "ppc_oea.h"
.text
.globl fixture_real
fixture_real:
    li 0,0
    mtmsr 0
    isync
    b fixture_setup_real
.globl fixture_translated
fixture_translated:
    li 0,0x32
    mtmsr 0
    isync
    b fixture_main
/* Keep the fault instruction and continuation unambiguous. */
.globl fixture_guard
fixture_guard:
.globl fixture_guard_load
fixture_guard_load:
    lwz 3,0(3)
.globl fixture_guard_resume
fixture_guard_resume:
    blr
.globl fixture_finish
fixture_finish:
    li 0,0
    mtmsr 0
    isync
.globl fixture_done
fixture_done:
    b fixture_done
.section .note.GNU-stack,"",@progbits
