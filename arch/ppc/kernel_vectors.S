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
/* Kernel D1: FreeBSD trap_subr32.S:93-158,231,346-378 and
 * OpenBSD macppc/locore.S:853-949; MPC7450UM 4.3/4.6.
 * Unlike the standalone image, every frame access before dispatch is real.
 * Single CPU, integer only. EE stays clear; synchronous reentry has one
 * independent spill slot. No access is made through the interrupted r1.
 * All resident symbols below must be reserved and identity mapped by A1/D3.
 */
#include "ppc_frame.h"
#include "ppc_oea.h"
#define KFRAME_BUSY 164
.section .rodata.vectors,"a"
.balign 32
.globl ppc_kernel_vector_source
ppc_kernel_vector_source:
    .org ppc_kernel_vector_source+256
    b .Lvector_100
    .org ppc_kernel_vector_source+512
    b .Lvector_200
    .org ppc_kernel_vector_source+768
    b .Lvector_300
    .org ppc_kernel_vector_source+1024
    b .Lvector_400
    .org ppc_kernel_vector_source+1280
    b .Lvector_500
    .org ppc_kernel_vector_source+1536
    b .Lvector_600
    .org ppc_kernel_vector_source+1792
    b .Lvector_700
    .org ppc_kernel_vector_source+2048
    b .Lvector_800
    .org ppc_kernel_vector_source+2304
    b .Lvector_900
    .org ppc_kernel_vector_source+2560
    b .Lvector_a00
    .org ppc_kernel_vector_source+2816
    b .Lvector_b00
    .org ppc_kernel_vector_source+3072
    b .Lvector_c00
    .org ppc_kernel_vector_source+3328
    b .Lvector_d00
    .org ppc_kernel_vector_source+3584
    b .Lvector_e00
    .org ppc_kernel_vector_source+3840
    b .Lvector_f00
    .org ppc_kernel_vector_source+3872
    b .Lvector_f20
    .org ppc_kernel_vector_source+4096
    b .Lvector_1000
    .org ppc_kernel_vector_source+4352
    b .Lvector_1100
    .org ppc_kernel_vector_source+4608
    b .Lvector_1200
    .org ppc_kernel_vector_source+4864
    b .Lvector_1300
    .org ppc_kernel_vector_source+5120
    b .Lvector_1400
    .org ppc_kernel_vector_source+5376
    b .Lvector_1500
    .org ppc_kernel_vector_source+5632
    b .Lvector_1600
    .org ppc_kernel_vector_source+5888
    b .Lvector_1700
    .org ppc_kernel_vector_source+0x1800
.Lvector_100:
    mtsprg1 1
    mflr 1
    mtsprg2 1
    li 1,256
    mtsprg3 1
    ba ppc_kernel_vector_entry
.Lvector_200:
    mtsprg1 1
    mflr 1
    mtsprg2 1
    li 1,512
    mtsprg3 1
    ba ppc_kernel_vector_entry
.Lvector_300:
    mtsprg1 1
    mflr 1
    mtsprg2 1
    li 1,768
    mtsprg3 1
    ba ppc_kernel_vector_entry
.Lvector_400:
    mtsprg1 1
    mflr 1
    mtsprg2 1
    li 1,1024
    mtsprg3 1
    ba ppc_kernel_vector_entry
.Lvector_500:
    mtsprg1 1
    mflr 1
    mtsprg2 1
    li 1,1280
    mtsprg3 1
    ba ppc_kernel_vector_entry
.Lvector_600:
    mtsprg1 1
    mflr 1
    mtsprg2 1
    li 1,1536
    mtsprg3 1
    ba ppc_kernel_vector_entry
.Lvector_700:
    mtsprg1 1
    mflr 1
    mtsprg2 1
    li 1,1792
    mtsprg3 1
    ba ppc_kernel_vector_entry
.Lvector_800:
    mtsprg1 1
    mflr 1
    mtsprg2 1
    li 1,2048
    mtsprg3 1
    ba ppc_kernel_vector_entry
.Lvector_900:
    mtsprg1 1
    mflr 1
    mtsprg2 1
    li 1,2304
    mtsprg3 1
    ba ppc_kernel_vector_entry
.Lvector_a00:
    mtsprg1 1
    mflr 1
    mtsprg2 1
    li 1,2560
    mtsprg3 1
    ba ppc_kernel_vector_entry
.Lvector_b00:
    mtsprg1 1
    mflr 1
    mtsprg2 1
    li 1,2816
    mtsprg3 1
    ba ppc_kernel_vector_entry
.Lvector_c00:
    mtsprg1 1
    mflr 1
    mtsprg2 1
    li 1,3072
    mtsprg3 1
    ba ppc_kernel_vector_entry
.Lvector_d00:
    mtsprg1 1
    mflr 1
    mtsprg2 1
    li 1,3328
    mtsprg3 1
    ba ppc_kernel_vector_entry
.Lvector_e00:
    mtsprg1 1
    mflr 1
    mtsprg2 1
    li 1,3584
    mtsprg3 1
    ba ppc_kernel_vector_entry
.Lvector_f00:
    mtsprg1 1
    mflr 1
    mtsprg2 1
    li 1,3840
    mtsprg3 1
    ba ppc_kernel_vector_entry
.Lvector_f20:
    mtsprg1 1
    mflr 1
    mtsprg2 1
    li 1,3872
    mtsprg3 1
    ba ppc_kernel_vector_entry
.Lvector_1000:
    mtsprg1 1
    mflr 1
    mtsprg2 1
    li 1,4096
    mtsprg3 1
    ba ppc_kernel_vector_entry
.Lvector_1100:
    mtsprg1 1
    mflr 1
    mtsprg2 1
    li 1,4352
    mtsprg3 1
    ba ppc_kernel_vector_entry
.Lvector_1200:
    mtsprg1 1
    mflr 1
    mtsprg2 1
    li 1,4608
    mtsprg3 1
    ba ppc_kernel_vector_entry
.Lvector_1300:
    mtsprg1 1
    mflr 1
    mtsprg2 1
    li 1,4864
    mtsprg3 1
    ba ppc_kernel_vector_entry
.Lvector_1400:
    mtsprg1 1
    mflr 1
    mtsprg2 1
    li 1,5120
    mtsprg3 1
    ba ppc_kernel_vector_entry
.Lvector_1500:
    mtsprg1 1
    mflr 1
    mtsprg2 1
    li 1,5376
    mtsprg3 1
    ba ppc_kernel_vector_entry
.Lvector_1600:
    mtsprg1 1
    mflr 1
    mtsprg2 1
    li 1,5632
    mtsprg3 1
    ba ppc_kernel_vector_entry
.Lvector_1700:
    mtsprg1 1
    mflr 1
    mtsprg2 1
    li 1,5888
    mtsprg3 1
    ba ppc_kernel_vector_entry
    .org ppc_kernel_vector_source+0x3000+32
.balign 4
.globl ppc_kernel_vectors
ppc_kernel_vectors:
    .long ppc_kernel_vector_source,0x3000-32

.bss
.balign 16
.Lscratch:
    .space PPC_SAVE_SIZE
/* Busy words are separate: a translated dispatcher may fault into spill. */
.Ltrap_busy:
    .space 4
.Lspill_busy:
    .space 4
.balign 4096
.globl ppc_kernel_trap_stack
ppc_kernel_trap_stack:
    .space PPC_KERNEL_STACK_SIZE
.Ltrap_top:
.globl ppc_kernel_spill_stack
ppc_kernel_spill_stack:
    .space PPC_KERNEL_STACK_SIZE
.Lspill_top:
.globl ppc_kernel_fatal_stack
ppc_kernel_fatal_stack:
    .space PPC_KERNEL_FATAL_STACK_SIZE
.Lfatal_top:
.text
.globl ppc_kernel_vector_entry
ppc_kernel_vector_entry:
    lis 1,.Lscratch@ha
    addi 1,1,.Lscratch@l
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
    mfdar 30
    stw 30,PPC_SAVE_DAR(1)
    mfdsisr 30
    stw 30,PPC_SAVE_DSISR(1)
    mr 31,1
    mfsprg3 30
    cmpwi 30,PPC_VECTOR_DSI
    beq .Lspill_slot
    cmpwi 30,PPC_VECTOR_ISI
    beq .Lspill_slot
    lis 28,.Ltrap_busy@ha
    addi 28,28,.Ltrap_busy@l
    lis 1,.Ltrap_top@ha
    addi 1,1,.Ltrap_top@l
    b .Lclaim
.Lspill_slot:
    lis 28,.Lspill_busy@ha
    addi 28,28,.Lspill_busy@l
    lis 1,.Lspill_top@ha
    addi 1,1,.Lspill_top@l
.Lclaim:
    lwz 29,0(28)
    cmpwi 29,0
    bne ppc_kernel_vector_fatal
    li 29,1
    stw 29,0(28)
    addi 1,1,-(PPC_FRAME_SIZE+PPC_CALL_SIZE)
    li 29,0
    stw 29,0(1)
    stw 28,PPC_CALL_SIZE+KFRAME_BUSY(1)
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
    li 30,0
    stw 30,PPC_CALL_SIZE+168(1)
    stw 30,PPC_CALL_SIZE+172(1)
/* All 176 bytes of frame storage now owned; no translated stack used. */
.globl ppc_kernel_frame_complete
ppc_kernel_frame_complete:
    lwz 30,PPC_CALL_SIZE+PPC_FRAME_VECTOR(1)
    cmpwi 30,PPC_VECTOR_DSI
    beq .Lspill
    cmpwi 30,PPC_VECTOR_ISI
    bne .Ldispatch
.Lspill:
    addi 3,1,PPC_CALL_SIZE
    bl ppc_hpt_spill
    cmpwi 3,1
    beq ppc_kernel_vector_return
/* Failed spill moves the complete frame off the spill stack before dispatch.
 * An unresolved nested fault cannot overwrite the active trap frame. */
    lis 4,.Ltrap_busy@ha
    addi 4,4,.Ltrap_busy@l
    lwz 5,0(4)
    cmpwi 5,0
    bne ppc_kernel_vector_fatal
    li 5,1
    stw 5,0(4)
    lis 6,.Ltrap_top@ha
    addi 6,6,.Ltrap_top@l
    addi 6,6,-(PPC_FRAME_SIZE+PPC_CALL_SIZE)
    li 5,0
    stw 5,0(6)
    li 7,PPC_FRAME_SIZE/4
    mtctr 7
    addi 7,1,PPC_CALL_SIZE-4
    addi 8,6,PPC_CALL_SIZE-4
.Lcopy_frame:
    lwzu 9,4(7)
    stwu 9,4(8)
    bdnz .Lcopy_frame
    lwz 7,PPC_CALL_SIZE+KFRAME_BUSY(1)
    stw 5,0(7)
    mr 1,6
    stw 4,PPC_CALL_SIZE+KFRAME_BUSY(1)
.Ldispatch:
/* SRR1's translation bits certify the caller had an operational root/alias.
 * Early boot has no safe MMIO diagnostics: stop without touching an alias. */
    lwz 3,PPC_CALL_SIZE+PPC_FRAME_SRR1(1)
    andi. 3,3,(PPC_MSR_IR|PPC_MSR_DR)
    cmpwi 3,(PPC_MSR_IR|PPC_MSR_DR)
    bne ppc_kernel_vector_fatal
    mfmsr 3
    ori 3,3,(PPC_MSR_IR|PPC_MSR_DR|PPC_MSR_RI)
    mtmsr 3
    isync
    addi 3,1,PPC_CALL_SIZE
    bl ppc_trap_dispatch
.globl ppc_kernel_vector_return
ppc_kernel_vector_return:
/* Identity code/stack; stop translation before reading the restored frame.
 * No mutable global scratch survives a translated C call. */
    mfmsr 3
    li 4,(PPC_MSR_IR|PPC_MSR_DR|PPC_MSR_RI)
    andc 3,3,4
    mtmsr 3
    isync
    lwz 3,PPC_CALL_SIZE+KFRAME_BUSY(1)
    li 4,0
    stw 4,0(3)
    lwz 3,PPC_CALL_SIZE+PPC_FRAME_SRR0(1)
    mtsrr0 3
    lwz 3,PPC_CALL_SIZE+PPC_FRAME_SRR1(1)
    mtsrr1 3
    lwz 3,PPC_CALL_SIZE+PPC_FRAME_LR(1)
    mtlr 3
    lwz 3,PPC_CALL_SIZE+PPC_FRAME_CTR(1)
    mtctr 3
    lwz 3,PPC_CALL_SIZE+PPC_FRAME_XER(1)
    mtxer 3
    lwz 3,PPC_CALL_SIZE+PPC_FRAME_CR(1)
    mtcr 3
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
    rfi
/* No MMIO, C, or writes through a potentially exhausted stack on reentry. */
.globl ppc_kernel_vector_fatal
ppc_kernel_vector_fatal:
    li 1,0
    mtmsr 1
    isync
    lis 1,.Lfatal_top@ha
    addi 1,1,.Lfatal_top@l
    addi 1,1,-PPC_CALL_SIZE
.globl ppc_kernel_vector_stop
ppc_kernel_vector_stop:
    b ppc_kernel_vector_stop
.section .note.GNU-stack,"",@progbits
