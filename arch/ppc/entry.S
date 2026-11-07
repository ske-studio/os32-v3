/*
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
/* Adapted from NetBSD 36eddebc Locore.c:69-186, 7450/7455 only.
 * The ABI frame, explicit stop and MMIO primitives are OS32 additions. */
#include "ppc_layout.h"
    .text
    .balign PM_CELL_BYTES
    .globl _start
_start:
    sync
    isync
    lis 1,__stack_top@ha
    addi 1,1,__stack_top@l
    mfmsr 8
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
    li 9,PM_BAT_RAM_LOWER
    mtibatl 0,9
    mtdbatl 0,9
    li 9,PM_BAT_RAM_UPPER
    mtibatu 0,9
    mtdbatu 0,9
    isync
    mtmsr 8
    isync
    li 0,0
    lis 8,_edata@ha
    addi 8,8,_edata@l
    lis 9,_end@ha
    addi 9,9,_end@l
.Lclear:
    cmpw 0,8,9
    bge .Lcleared
    stb 0,0(8)
    stb 0,1(8)
    stb 0,2(8)
    stb 0,3(8)
    addi 8,8,PM_CELL_BYTES
    b .Lclear
.Lcleared:
    stwu 0,-PM_ABI_FRAME(1)
    mr 3,5
#ifdef PPC_MMU
    bl powermac_mmu_boot
#else
    bl powermac_boot
#endif
    b ppc_stop
    .globl ppc_load8
ppc_load8:
    lbz 3,0(3)
    eieio
    blr
    .globl ppc_store8
ppc_store8:
    stb 4,0(3)
    eieio
    blr
    .globl ppc_sync
ppc_sync:
    sync
    blr
    .globl ppc_disable
ppc_disable:
    mfmsr 3
    li 4,0
    ori 4,4,PM_MSR_EE
    andc 3,3,4
    mtmsr 3
    isync
    blr
    .globl ppc_stop
ppc_stop:
    sync
    bl ppc_disable
    sync
    .globl ppc_stop_loop
ppc_stop_loop:
    b ppc_stop_loop
    .globl ppc_stop_other
ppc_stop_other:
    b ppc_stop_other
    .section .note.GNU-stack,"",@progbits
