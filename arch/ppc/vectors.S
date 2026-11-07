/*-
 * Copyright (C) 1995, 1996 Wolfgang Solfrank.
 * Copyright (C) 1995, 1996 TooLs GmbH.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed by TooLs GmbH.
 * 4. The name of TooLs GmbH may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY TOOLS GMBH ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL TOOLS GMBH BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 * OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */
/* FreeBSD trap_subr32.S:93-241,346-378; single supervisor save, no FP/VMX. */
#include "ppc_frame.h"
#include "ppc_oea.h"
#include "ppc_layout.h"
.section .rodata.vectors,"a"
.balign PPC_CACHE_LINE
ppc_vector_source:
    .org ppc_vector_source+256
    b .Lvector_100
    .org ppc_vector_source+512
    b .Lvector_200
    .org ppc_vector_source+768
    b .Lvector_300
    .org ppc_vector_source+1024
    b .Lvector_400
    .org ppc_vector_source+1280
    b .Lvector_500
    .org ppc_vector_source+1536
    b .Lvector_600
    .org ppc_vector_source+1792
    b .Lvector_700
    .org ppc_vector_source+2048
    b .Lvector_800
    .org ppc_vector_source+2304
    b .Lvector_900
    .org ppc_vector_source+2560
    b .Lvector_a00
    .org ppc_vector_source+2816
    b .Lvector_b00
    .org ppc_vector_source+3072
    b .Lvector_c00
    .org ppc_vector_source+3328
    b .Lvector_d00
    .org ppc_vector_source+3584
    b .Lvector_e00
    .org ppc_vector_source+3840
    b .Lvector_f00
    .org ppc_vector_source+3872
    b .Lvector_f20
    .org ppc_vector_source+4096
    b .Lvector_1000
    .org ppc_vector_source+4352
    b .Lvector_1100
    .org ppc_vector_source+4608
    b .Lvector_1200
    .org ppc_vector_source+4864
    b .Lvector_1300
    .org ppc_vector_source+5120
    b .Lvector_1400
    .org ppc_vector_source+5376
    b .Lvector_1500
    .org ppc_vector_source+5632
    b .Lvector_1600
    .org ppc_vector_source+5888
    b .Lvector_1700
    .org ppc_vector_source+0x1800
.Lvector_100:
    mtsprg1 1
    mflr 1
    mtsprg2 1
    li 1,256
    mtsprg3 1
    ba ppc_vector_entry
.Lvector_200:
    mtsprg1 1
    mflr 1
    mtsprg2 1
    li 1,512
    mtsprg3 1
    ba ppc_vector_entry
.Lvector_300:
    mtsprg1 1
    mflr 1
    mtsprg2 1
    li 1,768
    mtsprg3 1
    ba ppc_vector_entry
.Lvector_400:
    mtsprg1 1
    mflr 1
    mtsprg2 1
    li 1,1024
    mtsprg3 1
    ba ppc_vector_entry
.Lvector_500:
    mtsprg1 1
    mflr 1
    mtsprg2 1
    li 1,1280
    mtsprg3 1
    ba ppc_vector_entry
.Lvector_600:
    mtsprg1 1
    mflr 1
    mtsprg2 1
    li 1,1536
    mtsprg3 1
    ba ppc_vector_entry
.Lvector_700:
    mtsprg1 1
    mflr 1
    mtsprg2 1
    li 1,1792
    mtsprg3 1
    ba ppc_vector_entry
.Lvector_800:
    mtsprg1 1
    mflr 1
    mtsprg2 1
    li 1,2048
    mtsprg3 1
    ba ppc_vector_entry
.Lvector_900:
    mtsprg1 1
    mflr 1
    mtsprg2 1
    li 1,2304
    mtsprg3 1
    ba ppc_vector_entry
.Lvector_a00:
    mtsprg1 1
    mflr 1
    mtsprg2 1
    li 1,2560
    mtsprg3 1
    ba ppc_vector_entry
.Lvector_b00:
    mtsprg1 1
    mflr 1
    mtsprg2 1
    li 1,2816
    mtsprg3 1
    ba ppc_vector_entry
.Lvector_c00:
    mtsprg1 1
    mflr 1
    mtsprg2 1
    li 1,3072
    mtsprg3 1
    ba ppc_vector_entry
.Lvector_d00:
    mtsprg1 1
    mflr 1
    mtsprg2 1
    li 1,3328
    mtsprg3 1
    ba ppc_vector_entry
.Lvector_e00:
    mtsprg1 1
    mflr 1
    mtsprg2 1
    li 1,3584
    mtsprg3 1
    ba ppc_vector_entry
.Lvector_f00:
    mtsprg1 1
    mflr 1
    mtsprg2 1
    li 1,3840
    mtsprg3 1
    ba ppc_vector_entry
.Lvector_f20:
    mtsprg1 1
    mflr 1
    mtsprg2 1
    li 1,3872
    mtsprg3 1
    ba ppc_vector_entry
.Lvector_1000:
    mtsprg1 1
    mflr 1
    mtsprg2 1
    li 1,4096
    mtsprg3 1
    ba ppc_vector_entry
.Lvector_1100:
    mtsprg1 1
    mflr 1
    mtsprg2 1
    li 1,4352
    mtsprg3 1
    ba ppc_vector_entry
.Lvector_1200:
    mtsprg1 1
    mflr 1
    mtsprg2 1
    li 1,4608
    mtsprg3 1
    ba ppc_vector_entry
.Lvector_1300:
    mtsprg1 1
    mflr 1
    mtsprg2 1
    li 1,4864
    mtsprg3 1
    ba ppc_vector_entry
.Lvector_1400:
    mtsprg1 1
    mflr 1
    mtsprg2 1
    li 1,5120
    mtsprg3 1
    ba ppc_vector_entry
.Lvector_1500:
    mtsprg1 1
    mflr 1
    mtsprg2 1
    li 1,5376
    mtsprg3 1
    ba ppc_vector_entry
.Lvector_1600:
    mtsprg1 1
    mflr 1
    mtsprg2 1
    li 1,5632
    mtsprg3 1
    ba ppc_vector_entry
.Lvector_1700:
    mtsprg1 1
    mflr 1
    mtsprg2 1
    li 1,5888
    mtsprg3 1
    ba ppc_vector_entry
    .org ppc_vector_source+PM_LOW_RESERVED_END+PPC_CACHE_LINE
.balign 4
.globl ppc_vectors
ppc_vectors:
    .long ppc_vector_source,PM_LOW_RESERVED_END-PPC_CACHE_LINE
.bss
.balign 16
.globl ppc_cpu_save
ppc_cpu_save:
    .space PPC_SAVE_SIZE
.text
.globl ppc_vector_install
ppc_vector_install:
    mflr 0
    stwu 1,-PM_ABI_FRAME(1)
    stw 0,PM_ABI_FRAME+4(1)
    lwz 4,0(3)
    lwz 5,4(3)
    li 3,0
    bl ppc_cache_copy
    mfmsr 3
    li 4,PPC_MSR_IP
    andc 3,3,4
    mtmsr 3
    isync
    lwz 0,PM_ABI_FRAME+4(1)
    addi 1,1,PM_ABI_FRAME
    mtlr 0
    blr
.globl ppc_vector_entry
ppc_vector_entry:
    mfsprg0 1
    stw 28,0(1)
    stw 29,4(1)
    stw 30,8(1)
    stw 31,12(1)
    mfcr 29
    stw 29,PPC_SAVE_CR(1)
    mfsrr0 30
    stw 30,PPC_SAVE_SRR0(1)
    mfsrr1 30
    stw 30,PPC_SAVE_SRR1(1)
    mfsprg3 30
    cmpwi 30,PPC_VECTOR_DSI
    bne .Lno_dar
    mfdar 30
    stw 30,PPC_SAVE_DAR(1)
    mfdsisr 30
    stw 30,PPC_SAVE_DSISR(1)
.Lno_dar:
    mfmsr 30
    ori 30,30,(PPC_MSR_IR|PPC_MSR_DR|PPC_MSR_RI)
    mtmsr 30
    isync
    mr 31,1
    mfsprg1 1
    stwu 1,-(PPC_FRAME_SIZE+PPC_CALL_SIZE)(1)
    stw 0,PPC_CALL_SIZE(1)
    mfsprg1 30
    stw 30,PPC_CALL_SIZE+4(1)
    stw 2,PPC_CALL_SIZE+8(1)
    stw 3,PPC_CALL_SIZE+12(1)
    stw 4,PPC_CALL_SIZE+16(1)
    stw 5,PPC_CALL_SIZE+20(1)
    stw 6,PPC_CALL_SIZE+24(1)
    stw 7,PPC_CALL_SIZE+28(1)
    stw 8,PPC_CALL_SIZE+32(1)
    stw 9,PPC_CALL_SIZE+36(1)
    stw 10,PPC_CALL_SIZE+40(1)
    stw 11,PPC_CALL_SIZE+44(1)
    stw 12,PPC_CALL_SIZE+48(1)
    stw 13,PPC_CALL_SIZE+52(1)
    stw 14,PPC_CALL_SIZE+56(1)
    stw 15,PPC_CALL_SIZE+60(1)
    stw 16,PPC_CALL_SIZE+64(1)
    stw 17,PPC_CALL_SIZE+68(1)
    stw 18,PPC_CALL_SIZE+72(1)
    stw 19,PPC_CALL_SIZE+76(1)
    stw 20,PPC_CALL_SIZE+80(1)
    stw 21,PPC_CALL_SIZE+84(1)
    stw 22,PPC_CALL_SIZE+88(1)
    stw 23,PPC_CALL_SIZE+92(1)
    stw 24,PPC_CALL_SIZE+96(1)
    stw 25,PPC_CALL_SIZE+100(1)
    stw 26,PPC_CALL_SIZE+104(1)
    stw 27,PPC_CALL_SIZE+108(1)
    lwz 30,0(31)
    stw 30,PPC_CALL_SIZE+112(1)
    lwz 30,4(31)
    stw 30,PPC_CALL_SIZE+116(1)
    lwz 30,8(31)
    stw 30,PPC_CALL_SIZE+120(1)
    lwz 30,12(31)
    stw 30,PPC_CALL_SIZE+124(1)
    mfsprg2 30
    stw 30,PPC_CALL_SIZE+PPC_FRAME_LR(1)
    mfctr 30
    stw 30,PPC_CALL_SIZE+PPC_FRAME_CTR(1)
    mfxer 30
    stw 30,PPC_CALL_SIZE+PPC_FRAME_XER(1)
    mfsprg3 30
    stw 30,PPC_CALL_SIZE+PPC_FRAME_VECTOR(1)
    lwz 30,PPC_SAVE_CR(31)
    stw 30,PPC_CALL_SIZE+PPC_FRAME_CR(1)
    lwz 30,PPC_SAVE_SRR0(31)
    stw 30,PPC_CALL_SIZE+PPC_FRAME_SRR0(1)
    lwz 30,PPC_SAVE_SRR1(31)
    stw 30,PPC_CALL_SIZE+PPC_FRAME_SRR1(1)
    lwz 30,PPC_SAVE_DAR(31)
    stw 30,PPC_CALL_SIZE+PPC_FRAME_DAR(1)
    lwz 30,PPC_SAVE_DSISR(31)
    stw 30,PPC_CALL_SIZE+PPC_FRAME_DSISR(1)
    addi 3,1,PPC_CALL_SIZE
    bl ppc_exception
    mfsprg0 7
    lwz 2,PPC_CALL_SIZE+PPC_FRAME_SRR0(1)
    stw 2,PPC_SAVE_SRR0(7)
    lwz 2,PPC_CALL_SIZE+PPC_FRAME_SRR1(1)
    stw 2,PPC_SAVE_SRR1(7)
    lwz 2,PPC_CALL_SIZE+PPC_FRAME_LR(1)
    mtlr 2
    lwz 2,PPC_CALL_SIZE+PPC_FRAME_CTR(1)
    mtctr 2
    lwz 2,PPC_CALL_SIZE+PPC_FRAME_XER(1)
    mtxer 2
    lwz 2,PPC_CALL_SIZE+PPC_FRAME_CR(1)
    mtsprg1 2
    lwz 31,PPC_CALL_SIZE+124(1)
    lwz 30,PPC_CALL_SIZE+120(1)
    lwz 29,PPC_CALL_SIZE+116(1)
    lwz 28,PPC_CALL_SIZE+112(1)
    lwz 27,PPC_CALL_SIZE+108(1)
    lwz 26,PPC_CALL_SIZE+104(1)
    lwz 25,PPC_CALL_SIZE+100(1)
    lwz 24,PPC_CALL_SIZE+96(1)
    lwz 23,PPC_CALL_SIZE+92(1)
    lwz 22,PPC_CALL_SIZE+88(1)
    lwz 21,PPC_CALL_SIZE+84(1)
    lwz 20,PPC_CALL_SIZE+80(1)
    lwz 19,PPC_CALL_SIZE+76(1)
    lwz 18,PPC_CALL_SIZE+72(1)
    lwz 17,PPC_CALL_SIZE+68(1)
    lwz 16,PPC_CALL_SIZE+64(1)
    lwz 15,PPC_CALL_SIZE+60(1)
    lwz 14,PPC_CALL_SIZE+56(1)
    lwz 13,PPC_CALL_SIZE+52(1)
    lwz 12,PPC_CALL_SIZE+48(1)
    lwz 11,PPC_CALL_SIZE+44(1)
    lwz 10,PPC_CALL_SIZE+40(1)
    lwz 9,PPC_CALL_SIZE+36(1)
    lwz 8,PPC_CALL_SIZE+32(1)
    lwz 7,PPC_CALL_SIZE+28(1)
    lwz 6,PPC_CALL_SIZE+24(1)
    lwz 5,PPC_CALL_SIZE+20(1)
    lwz 4,PPC_CALL_SIZE+16(1)
    lwz 3,PPC_CALL_SIZE+12(1)
    lwz 2,PPC_CALL_SIZE+8(1)
    lwz 0,PPC_CALL_SIZE+0(1)
    lwz 1,PPC_CALL_SIZE+4(1)
    mtsprg2 2
    mtsprg3 3
    mfmsr 2
    li 3,(PPC_MSR_DR|PPC_MSR_IR|PPC_MSR_ME|PPC_MSR_RI)
    andc 2,2,3
    mtmsr 2
    isync
    mfsprg1 2
    mtcr 2
    mfsprg0 2
    lwz 3,PPC_SAVE_SRR0(2)
    mtsrr0 3
    lwz 3,PPC_SAVE_SRR1(2)
    mtsrr1 3
    mfsprg2 2
    mfsprg3 3
    rfi
.section .note.GNU-stack,"",@progbits
