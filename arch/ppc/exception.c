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
/* OpenBSD trap.c:279-295; exact probe record is OS32's acceptance harness. */
#include "ppc_frame.h"
#include "ppc_pmap.h"
#include "ppc_memmap.h"
void ppc_exception(struct ppc_frame *f)
{
    if (f->vector==PPC_VECTOR_DEC) { ppc_dec_interrupt(f); return; }
    u32 page=(f->vector==PPC_VECTOR_DSI ? f->dar : f->srr0)&PPC_PAGE_MASK;
    if (ppc_probe.active && f->vector==ppc_probe.vector &&
        f->srr0==ppc_probe.fault_pc && page==ppc_probe.va) {
        int map=ppc_probe.action==PPC_PROBE_MAP;
        int match=f->vector==PPC_VECTOR_ISI ?
            map && (f->srr1&PPC_ISI_MISS) :
            map ? (f->dsisr&(PPC_DSISR_MISS|PPC_DSISR_STORE|PPC_DSISR_PROTECT))==PPC_DSISR_MISS :
            (f->dsisr&(PPC_DSISR_MISS|PPC_DSISR_STORE|PPC_DSISR_PROTECT))==
                (PPC_DSISR_STORE|PPC_DSISR_PROTECT);
        if (match) {
            pm_mmu_dump(f->vector==PPC_VECTOR_DSI ? "DSI" : "ISI",f);
            if (map) {
                if (ppc_pmap_enter(ppc_probe.va,ppc_probe.pa,ppc_probe.attr)) ppc_stop();
                if (f->vector==PPC_VECTOR_ISI)
                    ppc_cache_copy(pm_p2v_ram(ppc_probe.pa),pm_p2v_ram(ppc_probe.pa),PPC_CACHE_LINE);
            } else {
                f->srr0=ppc_probe.onfault.pc;
                f->gpr[1]=ppc_probe.onfault.sp;
                f->gpr[3]=1;
                f->cr=ppc_probe.onfault.cr;
                for (u32 i=0;i<19;i++) f->gpr[13+i]=ppc_probe.onfault.regs13_31[i];
            }
            ppc_probe.active=0;
            return;
        }
    }
    pm_mmu_dump("FATAL",f);
    ppc_stop();
}
