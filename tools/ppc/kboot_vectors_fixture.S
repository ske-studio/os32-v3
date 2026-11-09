/* Test-only transitions and integer canary; product entry is linked intact. */
#include "ppc_frame.h"
#include "ppc_oea.h"
.text
.globl fixture_real
fixture_real:
    li 0,0
    mtmsr 0
    isync
    mtibatu 0,0
    mtibatu 1,0
    mtibatu 2,0
    mtibatu 3,0
    mtdbatu 0,0
    mtdbatu 1,0
    mtdbatu 2,0
    mtdbatu 3,0
    sync
    isync
    b fixture_setup_real
.globl fixture_translated
fixture_translated:
    li 0,0x32
    mtmsr 0
    isync
    b fixture_main
.globl fixture_guard_check
fixture_guard_check:
    mflr 0
    stwu 1,-32(1)
    stw 0,36(1)
    li 0,0
    mtmsr 0
    isync
    bl fixture_check_real_guard
    li 0,0x32
    mtmsr 0
    isync
    lwz 0,36(1)
    mtlr 0
    addi 1,1,32
    blr
.globl fixture_finish
fixture_finish:
    li 0,0
    mtmsr 0
    isync
.globl fixture_done
fixture_done:
    b fixture_done
.globl fixture_canary
fixture_canary:
    stwu 1,-160(1)
    stmw 13,16(1)
    mflr 0
    stw 0,164(1)
    lis 12,fixture_saved_sp@ha
    stw 1,fixture_saved_sp@l(12)
    slwi 3,3,12
    addis 3,3,0x5000
    li 0,256
    li 2,258
    li 4,260
    li 5,261
    li 6,262
    li 7,263
    li 8,264
    li 9,265
    li 10,266
    li 11,267
    li 13,269
    li 14,270
    li 15,271
    li 16,272
    li 17,273
    li 18,274
    li 19,275
    li 20,276
    li 21,277
    li 22,278
    li 23,279
    li 24,280
    li 25,281
    li 26,282
    li 27,283
    li 28,284
    li 29,285
    li 30,286
    li 31,287
    li 12,0x1234
    mtlr 12
    li 12,0x2345
    mtctr 12
    lis 12,0xa000
    mtxer 12
    lis 12,0x1234
    ori 12,12,0x5678
    mtcr 12
    li 12,268
    lis 1,0x0030
    ori 1,1,0x1000
.globl fixture_load
fixture_load:
    lwz 3,0(3)
.globl fixture_resume
fixture_resume:
    mtsprg0 1
    lis 1,fixture_regs@ha
    addi 1,1,fixture_regs@l
    stmw 0,0(1)
    mfsprg0 0
    stw 0,4(1)
    mflr 0
    stw 0,128(1)
    mfcr 0
    stw 0,132(1)
    mfctr 0
    stw 0,136(1)
    mfxer 0
    stw 0,140(1)
    mfmsr 0
    stw 0,144(1)
    lis 12,fixture_saved_sp@ha
    lwz 1,fixture_saved_sp@l(12)
    lwz 0,164(1)
    mtlr 0
    lmw 13,16(1)
    addi 1,1,160
    blr
.bss
.balign 16
.globl fixture_regs
fixture_regs:
    .space 148
fixture_saved_sp:
    .space 4
.section .note.GNU-stack,"",@progbits
