/* OS32 fault fixtures. Reserved pages/record belong to this image. */
#include "ppc_frame.h"
#include "ppc_oea.h"
#include "ppc_layout.h"
.bss
.balign PPC_PAGE_SIZE
.globl ppc_probe_pages
ppc_probe_pages:
    .space 2*PPC_PAGE_SIZE
.balign 16
.globl ppc_probe
ppc_probe:
    .space 7*4+22*4
.text
.globl ppc_probe_load
ppc_probe_load:
    lis 5,ppc_probe@ha
    addi 5,5,ppc_probe@l
    lis 6,.Lload@ha
    addi 6,6,.Lload@l
    stw 6,8(5)
.Lload:
    lwz 3,0(3)
    blr
.globl ppc_probe_store_ro
ppc_probe_store_ro:
    lis 5,ppc_probe@ha
    addi 5,5,ppc_probe@l
    lis 6,.Lstore@ha
    addi 6,6,.Lstore@l
    stw 6,8(5)
    lis 6,.Lro_return@ha
    addi 6,6,.Lro_return@l
    stw 6,28(5)
    stw 1,32(5)
    mfcr 6
    stw 6,36(5)
    stmw 13,40(5)
.Lstore:
    stw 4,0(3)
    li 3,0
.Lro_return:
    blr
.globl ppc_probe_exec
ppc_probe_exec:
    mflr 0
    stwu 1,-PM_ABI_FRAME(1)
    stw 0,PM_ABI_FRAME+4(1)
    mtctr 3
    bctrl
    lwz 0,PM_ABI_FRAME+4(1)
    addi 1,1,PM_ABI_FRAME
    mtlr 0
    blr
.section .rodata
.balign PPC_CACHE_LINE
.globl ppc_probe_code
ppc_probe_code:
    li 3,0x4567
    blr
    .space 2*PPC_CACHE_LINE-8

.text
.globl ppc_probe_canary
ppc_probe_canary:
    stwu 1,-528(1)
    stmw 0,32(1)
    mflr 0
    stw 0,532(1)
    stw 3,512(1)
    lis 12,ppc_probe@ha
    addi 12,12,ppc_probe@l
    cmpwi 3,0
    bne 1f
    lis 0,.Lcan_load@h
    ori 0,0,.Lcan_load@l
    stw 0,8(12)
1:  cmpwi 3,1
    bne 2f
    lis 0,.Lcan_store@h
    ori 0,0,.Lcan_store@l
    stw 0,8(12)
    lis 0,.Lcan_resume@h
    ori 0,0,.Lcan_resume@l
    stw 0,28(12)
    stw 1,32(12)
2:
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
    lis 3,PM_TEST_ALIAS@h
    lwz 0,512(1)
    cmpwi 0,0
    beq .Lsetup_load
    cmpwi 0,1
    beq .Lsetup_ro
    cmpwi 0,2
    beq .Lsetup_exec
    li 0,4096
    mtctr 0
    b .Lsetup_common
.Lsetup_load:
    li 0,0x321
    mtctr 0
    b .Lsetup_common
.Lsetup_ro:
    li 0,0x321
    mtctr 0
    b .Lsetup_common
.Lsetup_exec:
    mtctr 3
.Lsetup_common:
    lis 0,.Lcan_resume@h
    ori 0,0,.Lcan_resume@l
    mtlr 0
    lis 0,0xa000
    mtxer 0
    lis 0,0x1357
    ori 0,0,0x9bdf
    mtcr 0
    stw 0,36(12)
    stmw 13,40(12)
    li 0,0x100
    stmw 0,176(1)
    mflr 0
    stw 0,304(1)
    mfcr 0
    stw 0,308(1)
    mfctr 0
    stw 0,312(1)
    mfxer 0
    stw 0,316(1)
    lwz 0,512(1)
    cmpwi 0,0
    beq .Lgo_load
    cmpwi 0,1
    beq .Lgo_ro
    cmpwi 0,2
    beq .Lgo_exec
    li 0,0
    stw 0,312(1)
    lwz 0,308(1)
    mtcr 0
    lwz 0,176(1)
.Ldec_busy:
    bdnz .Ldec_busy
    b .Lcan_resume
.Lgo_load:
    lis 0,0x1234
    ori 0,0,0x5678
    stw 0,188(1)
    lwz 0,308(1)
    mtcr 0
    lwz 0,176(1)
.Lcan_load:
    lwz 3,0(3)
    b .Lcan_resume
.Lgo_ro:
    li 0,1
    stw 0,188(1)
    lwz 0,308(1)
    mtcr 0
    lwz 0,176(1)
.Lcan_store:
    stw 4,0(3)
    b .Lcan_resume
.Lgo_exec:
    li 0,0x4567
    stw 0,188(1)
    lwz 0,308(1)
    mtcr 0
    lwz 0,176(1)
    bctr
.Lcan_resume:
    stmw 0,352(1)
    mflr 0
    stw 0,480(1)
    mfcr 0
    stw 0,484(1)
    mfctr 0
    stw 0,488(1)
    mfxer 0
    stw 0,492(1)
    li 3,0
    addi 4,1,176
    addi 5,1,352
    li 6,36
    mtctr 6
1:  lwz 6,0(4)
    lwz 7,0(5)
    cmpw 6,7
    beq 2f
    li 3,1
2:  addi 4,4,4
    addi 5,5,4
    bdnz 1b
    stw 3,512(1)
    lwz 0,532(1)
    mtlr 0
    lmw 2,40(1)
    lwz 3,512(1)
    lwz 0,32(1)
    addi 1,1,528
    blr
.globl ppc_dec_observe
ppc_dec_observe:
    b ppc_dec_observe
.globl ppc_fatal_probe
ppc_fatal_probe:
    sc
    b ppc_stop

.section .note.GNU-stack,"",@progbits
