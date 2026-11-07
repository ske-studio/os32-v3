/*-
 * SPDX-License-Identifier: BSD-2-Clause AND BSD-4-Clause
 *
 * Copyright (c) 2001 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Matt Thomas <matt@3am-software.com> of Allegro Networks, Inc.
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
 *
 * $NetBSD: pmap.c,v 1.28 2000/03/26 20:42:36 kleink Exp $
 */
/*-
 * Copyright (C) 2001 Benno Rice.
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
 * THIS SOFTWARE IS PROVIDED BY Benno Rice ``AS IS'' AND ANY EXPRESS OR
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
/*-
 * Copyright (c) 2007, 2021 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Tim Rightnour
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
 * Copyright (C) 1996 Wolfgang Solfrank.
 * Copyright (C) 1996 TooLs GmbH.
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
/* NetBSD ofw_machdep.c:215-310; FreeBSD mmu_oea.c:820-842,1995-2029.
 * The interval scan subtracts reservations without growing region arrays. */
#include "ofw.h"
#include "ppc_pmap.h"
#include "ppc_memmap.h"
#include "kstring.h"
static struct pm_regions ram, avail;
static struct pm_boot_info info;
static unsigned char property[PM_MEMORY_REGIONS*4*PM_CELL_BYTES];
static u32 be32(const unsigned char *p)
{
    return ((u32)p[0]<<24)|((u32)p[1]<<16)|((u32)p[2]<<8)|p[3];
}
static int regions(struct pm_regions *r, unsigned ac, unsigned sc, int len)
{
    r->count=0;
    if (len<=0) return -1;
    unsigned stride=(ac+sc)*PM_CELL_BYTES;
    for (unsigned pos=0; pos+stride<=(unsigned)len; pos+=stride) {
        const unsigned char *p=property+pos;
        if ((ac>1 && be32(p)) || (sc>1 && be32(p+ac*PM_CELL_BYTES))) continue;
        u32 start=be32(p+(ac-1)*PM_CELL_BYTES);
        u32 size=be32(p+(ac+sc-1)*PM_CELL_BYTES);
        if (!size || size>0xffffffffUL-start) continue;
        r->v[r->count++]=(struct pm_region){start,size};
    }
    return 0;
}
static int hpt_allocate(void)
{
    u32 pages=0;
    for (u32 i=0; i<ram.count; i++) pages+=ram.v[i].size>>PPC_PAGE_SHIFT;
    u32 n=PM_PTEG_INITIAL;
    while (n<pages) n<<=1;
    n>>=1;
    u32 size=n*PPC_PTEG_SIZE;
    for (u32 i=0; i<avail.count; i++) {
        u32 start=avail.v[i].start, end=start+avail.v[i].size;
        if (end>PM_BAT_RAM_END) end=PM_BAT_RAM_END;
        if (start<PM_LOW_RESERVED_END) start=PM_LOW_RESERVED_END;
        start=(start+size-1)&~(size-1);
        while (start<end && size<=end-start) {
            u32 image_start=pm_v2p_ram(pm_image_start), image_end=pm_v2p_ram(pm_image_end);
            if (start<image_end && start+size>image_start) {
                start=(image_end+size-1)&~(size-1);
                continue;
            }
            info.hpt_pa=start; info.hpt_mask=n-1;
            return 0;
        }
    }
    return -1;
}
static int compatible_macrisc(int root)
{
    int len=ofw_getprop(root,"compatible",property,sizeof(property));
    if (len<=0) return 0;
    unsigned pos=0;
    while (pos<(unsigned)len) {
        unsigned end=pos;
        while (end<(unsigned)len && property[end]) end++;
        unsigned n=end-pos;
        if ((n==7 && !kstrncmp((const char *)property+pos,"MacRISC",7)) ||
            (n==8 && !kstrncmp((const char *)property+pos,"MacRISC2",8)) ||
            (n==8 && !kstrncmp((const char *)property+pos,"MacRISC3",8)) ||
            (n==8 && !kstrncmp((const char *)property+pos,"MacRISC4",8))) return 1;
        pos=end+1;
    }
    return 0;
}
static int timebase(int node)
{
    while (node!=0 && node!=-1) {
        int n=ofw_getprop(node,"timebase-frequency",property,sizeof(property));
        if (n>0) { info.tb_hz=be32(property); return 0; }
        if (!timebase(ofw_child(node))) return 0;
        node=ofw_peer(node);
    }
    return -1;
}
static void fail(int out, const char *why)
{
    ofw_write(out,why,kstrlen(why));
    ppc_stop();
}
void powermac_mmu_boot(ppc_ci ci)
{
    struct ofw_env e;
    struct pm_escc s;
    ofw_init(ci);
    if (ofw_setup(&e)) ppc_stop();
    if (pm_escc_prepare(&e,&s)) fail(e.out,"FAIL:ESCC\r\n");
    info.escc_pa=s.csr;
    int root=ofw_finddevice(PM_ROOT);
    if (timebase(ofw_child(root))) fail(e.out,"FAIL:TB\r\n");
    int memory=ofw_finddevice(PM_MEMORY);
    if (memory==-1) fail(e.out,"FAIL:MEMORY\r\n");
    int n=ofw_getprop(memory,"reg",property,PM_MEMORY_REGIONS*(e.ac+e.sc)*PM_CELL_BYTES);
    if (regions(&ram,e.ac,e.sc,n)) fail(e.out,"FAIL:REG\r\n");
    int mac=compatible_macrisc(root);
    n=ofw_getprop(memory,"available",property,PM_MEMORY_REGIONS*(mac?2:e.ac+e.sc)*PM_CELL_BYTES);
    if (regions(&avail,mac?1:e.ac,mac?1:e.sc,n)) fail(e.out,"FAIL:AVAILABLE\r\n");
    info.ram=&ram; info.avail=&avail;
    if (hpt_allocate()) fail(e.out,"FAIL:HPT\r\n");
    static const char takeover[]="TAKEOVER\r\n";
    if (ofw_write(e.out,takeover,sizeof(takeover)-1)!=sizeof(takeover)-1)
        fail(e.out,"FAIL:STDOUT\r\n");
    if (pm_escc_drain(&s)) fail(e.out,"FAIL:DRAIN\r\n");
    ppc_takeover(&info);
    ppc_stop();
}
