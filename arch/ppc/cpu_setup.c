/*-
 * Copyright (c) 2001 Matt Thomas.
 * Copyright (c) 2001 Tsubai Masanari.
 * Copyright (c) 1998, 1999, 2001 Internet Research Institute, Inc.
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
 *	This product includes software developed by
 *	Internet Research Institute, Inc.
 * 4. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */
/* NetBSD cpu_subr.c:309-374,681-696; no powersave, HID1, L2/L3 setup. */
#include "ppc_pmap.h"
void ppc_cpu_setup(void)
{
    u32 pvr=ppc_read_pvr(), version=pvr>>16;
    u32 hid=ppc_read_hid0();
    u32 count=4;
    if (version==PPC_PVR_7455) {
        hid |= PPC_HID_HIGHBAT | PPC_HID_XBSEN;
        ppc_write_hid0(hid);
        ppc_sync(); ppc_isync();
        count=8;
    }
    ppc_bat_reset(count);
    ppc_bat_set(0,PM_BAT_RAM_LOWER,PM_BAT_RAM_UPPER);
    hid |= PPC_HID_SGE | PPC_HID_BTIC | PPC_HID_LRSTK | PPC_HID_FOLD | PPC_HID_BHT;
    if (version==PPC_PVR_7450 && (pvr & 0xffff)<=PPC_REV_7450_BTIC_MAX) hid &= ~PPC_HID_BTIC;
    hid &= ~PPC_HID_SLEEP;
    ppc_write_hid0(hid);
    ppc_sync(); ppc_isync();
}
