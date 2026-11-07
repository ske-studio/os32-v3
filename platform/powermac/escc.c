/*
 * Copyright (c) 1996, 1998 Bill Studenmund
 * Copyright (c) 1995 Gordon W. Ross
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
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */
/* NetBSD 36eddebc zs.c:905-923, 789-798. Drain is an OS32 addition
 * using the same bounded poll and indexed-register access. No extra delay. */
#include "ofw.h"
int pm_escc_write(const struct pm_escc *s, const char *p, unsigned n)
{
    for (unsigned i=0; i<n; i++) {
        unsigned wait = 0, rr0;
        do {
            rr0 = ppc_load8(s->csr);
        } while (!(rr0 & PM_RR0_TX_READY) && wait++ < PM_POLL_LIMIT);
        if (!(rr0 & PM_RR0_TX_READY)) return -1;
        ppc_store8(s->data,(unsigned char)p[i]);
    }
    return 0;
}
int pm_escc_drain(const struct pm_escc *s)
{
    unsigned wait = 0, rr1;
    do {
        ppc_store8(s->csr,PM_RR1_INDEX);
        rr1 = ppc_load8(s->csr);
    } while (!(rr1 & PM_RR1_ALL_SENT) && wait++ < PM_POLL_LIMIT);
    return (rr1 & PM_RR1_ALL_SENT) ? 0 : -1;
}
