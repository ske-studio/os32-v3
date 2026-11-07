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
/* NetBSD clock.c:148-159; single integer-only periodic DEC owner. */
#include "ppc_frame.h"
#include "ppc_layout.h"
static u32 period, count;
void ppc_dec_init(u32 tb_hz)
{
    period=tb_hz/PM_DEC_HZ;
    count=0;
    ppc_write_dec(period);
}
u32 ppc_dec_count(void) { return count; }
void ppc_dec_interrupt(struct ppc_frame *frame)
{
    if (!period) return;
    int ticks=ppc_read_dec();
    while (ticks<0) ticks+=(int)period;
    ppc_write_dec((u32)ticks);
    count++;
    if (count<=PM_DEC_DUMP_N) {
        /* Use the shared frame representation for integer diagnostic fields. */
        struct ppc_frame diagnostic={0};
        diagnostic.gpr[0]=count;
        diagnostic.gpr[1]=(u32)ticks;
        diagnostic.gpr[2]=ppc_read_tb();
        pm_mmu_dump("DEC-COUNT/DEC/TB",&diagnostic);
        pm_mmu_dump("DEC",frame);
    }
}
