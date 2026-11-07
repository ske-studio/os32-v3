/*-
 * Copyright (c) 2001 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Matt Thomas <matt@3am-software.com> of Allegro Networks, Inc.
 *
 * Support for PPC64 Bridge mode added by Sanjay Lal <sanjayl@kymasys.com>
 * of Kyma Systems LLC.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */
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
/* NetBSD 36eddebc oea/pmap.c:720-868; fixed wired mapping owner is OS32.
 * Keep PTE operations independent of the software mapping array for plan D. */
#include "ppc_pmap.h"
static struct ppc_mmu_context context;
static struct ppc_mmu_context *current;
static struct ppc_mapping mapping[PM_MAPPING_COUNT];
struct ppc_pte { u32 hi, lo; };
static void pte_set(volatile u32 *pt, struct ppc_pte *shadow)
{
#ifdef DIAGNOSTIC
    if (shadow->hi & PPC_PTE_V) ppc_stop();
#endif
    shadow->hi |= PPC_PTE_V;
    pt[1] = shadow->lo;
    ppc_eieio();
    pt[0] = shadow->hi;
    ppc_tlbsync();
    ppc_sync();
}
static void pte_unset(volatile u32 *pt, struct ppc_pte *shadow, u32 va)
{
#ifdef DIAGNOSTIC
    if (!(shadow->hi & PPC_PTE_V) || !(pt[0] & PPC_PTE_V)) ppc_stop();
#endif
    shadow->hi &= ~PPC_PTE_V;
    ppc_sync();
    pt[0] &= ~PPC_PTE_V;
    ppc_sync();
    ppc_tlb_flush_one(va);
    ppc_sync();
    ppc_eieio();
    ppc_tlbsync();
    ppc_sync();
    shadow->lo |= pt[1] & PPC_PTE_RC;
}
/* slot is a word offset into HPT; -1 leaves shadow and HPT unchanged. */
static int pte_insert(struct ppc_mmu_context *ctx, u32 hash, struct ppc_pte *shadow)
{
    volatile u32 *hpt = ppc_hpt_ptr(ctx->hpt_pa);
    for (u32 secondary=0; secondary<2; secondary++) {
        u32 group = secondary ? hash ^ ctx->hpt_mask : hash;
        for (u32 i=0; i<PPC_PTEG_ENTRIES; i++) {
            u32 slot = group * (PPC_PTEG_SIZE/sizeof(u32)) + i*2;
            if (!(hpt[slot] & PPC_PTE_V)) {
                if (secondary) shadow->hi |= PPC_PTE_H;
                else shadow->hi &= ~PPC_PTE_H;
                pte_set(hpt+slot,shadow);
                return (int)slot;
            }
        }
    }
    return -1;
}
void ppc_pmap_bootstrap(const struct pm_boot_info *info)
{
    context.hpt_pa = info->hpt_pa;
    context.hpt_mask = info->hpt_mask;
    current = &context;
    volatile u32 *hpt = ppc_hpt_ptr(context.hpt_pa);
    for (u32 i=0; i<(context.hpt_mask+1)*(PPC_PTEG_SIZE/sizeof(u32)); i++) hpt[i]=0;
    for (u32 i=0; i<PPC_SR_COUNT; i++) context.sr[i]=PPC_SR_KP | (1<<4) | i;
    for (u32 i=0; i<PM_MAPPING_COUNT; i++) mapping[i].attr=0;
}
struct ppc_mmu_context *ppc_current_context(void) { return current; }
void ppc_select_context(struct ppc_mmu_context *ctx)
{
    current=ctx;
    ppc_sr_sdr1_load(ctx->sr,ctx->hpt_pa | (ctx->hpt_mask>>10));
    ppc_tlb_flush_all();
}
int ppc_pmap_enter(u32 va, u32 pa, u32 attr)
{
    struct ppc_mapping *m=0, *free=0;
    for (u32 i=0; i<PM_MAPPING_COUNT; i++) {
        if (mapping[i].attr && mapping[i].va==va) m=&mapping[i];
        if (!mapping[i].attr && !free) free=&mapping[i];
    }
    if (!m) m=free;
    if (!m) return -1;
    u32 vsid=current->sr[va>>28] & 0xffffff;
    struct ppc_pte shadow={ (vsid<<7) | ((va>>22)&0x3f), pa | attr };
    u32 hash=(vsid ^ ((va>>PPC_PAGE_SHIFT)&0xffff)) & current->hpt_mask;
    int slot=pte_insert(current,hash,&shadow);
    if (slot<0) return -1;
    if (m->attr) {
        volatile u32 *pt=ppc_hpt_ptr(current->hpt_pa)+m->slot;
        struct ppc_pte old={pt[0],m->pa | m->attr | m->rc};
        pte_unset(pt,&old,va);
        m->rc=old.lo & PPC_PTE_RC;
    } else m->rc=0;
    m->va=va; m->pa=pa; m->attr=attr; m->slot=(u32)slot;
    return 0;
}
int ppc_pmap_remove(u32 va)
{
    for (u32 i=0; i<PM_MAPPING_COUNT; i++) {
        struct ppc_mapping *m=&mapping[i];
        if (!m->attr || m->va!=va) continue;
        volatile u32 *pt=ppc_hpt_ptr(current->hpt_pa)+m->slot;
        struct ppc_pte old={pt[0],m->pa | m->attr | m->rc};
        pte_unset(pt,&old,va);
        m->rc=old.lo & PPC_PTE_RC;
        m->attr=0;
        return 0;
    }
    return 0;
}
