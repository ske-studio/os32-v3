/*-
 * Copyright (c) 2021 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Jason R. Thorpe.
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
/* NetBSD 36eddebc Locore.c:218-230,376-398,654-783 adapted to
 * fixed-size cells and a synchronous, single-caller firmware interface. */
#include "ofw.h"
static ppc_ci client;
static unsigned calls, detached;
/* Dedicated terminal so a rejected CI call has an unambiguous observation. */
void ofw_detached_stop(void) __attribute__((noreturn,noinline));
void ofw_detached_stop(void) { for (;;) {} }
void ofw_init(ppc_ci ci)
{
    if (detached) { ppc_disable(); ofw_detached_stop(); }
    client = ci;
}
unsigned ofw_call_count(void) { return calls; }
unsigned ofw_detach(void) { detached = 1; return calls; }
int ofw_call(const char *name, unsigned ni, unsigned no, cell *cells)
{
    if (detached) { ppc_disable(); ofw_detached_stop(); }
    cell args[3 + PM_CI_CAP] = {ppc_ptr_cell(name), ni, no};
    if (ni > PM_CI_CAP || no > PM_CI_CAP - ni) return -1;
    for (unsigned i=0; i<ni; i++) args[3+i] = cells[i];
    ++calls;
    int rc = client(args);
    for (unsigned i=0; i<no; i++) cells[ni+i] = args[3+ni+i];
    return rc;
}
int ofw_finddevice(const char *path)
{
    cell a[2] = {ppc_ptr_cell(path), 0};
    return ofw_call("finddevice",1,1,a) == -1 ? -1 : (int)a[1];
}
int ofw_getprop(int ph, const char *name, void *buf, unsigned cap)
{
    cell a[5] = {ph,ppc_ptr_cell(name),ppc_ptr_cell(buf),cap,0};
    return ofw_call("getprop",4,1,a) == -1 ? -1 : (int)a[4];
}
static int relation(const char *name, int ph)
{
    cell a[2] = {ph,0};
    return ofw_call(name,1,1,a) == -1 ? -1 : (int)a[1];
}
int ofw_child(int ph) { return relation("child",ph); }
int ofw_peer(int ph) { return relation("peer",ph); }
int ofw_parent(int ph) { return relation("parent",ph); }
int ofw_package_to_path(int ph, char *buf, unsigned cap)
{
    cell a[4] = {ph,ppc_ptr_cell(buf),cap,0};
    return ofw_call("package-to-path",3,1,a) == -1 ? -1 : (int)a[3];
}
int ofw_open(const char *path)
{
    cell a[2] = {ppc_ptr_cell(path),0};
    if (ofw_call("open",1,1,a) == -1 || a[1] == 0) return -1;
    return (int)a[1];
}
int ofw_write(int ih, const void *buf, unsigned n)
{
    cell a[4] = {ih,ppc_ptr_cell(buf),n,0};
    return ofw_call("write",3,1,a) == -1 ? -1 : (int)a[3];
}
void ofw_exit(void) { ofw_call("exit",0,0,0); }
int ofw_call_method(const char *method, int ih, unsigned ni,
                    unsigned no, cell *cells)
{
    cell a[PM_CI_CAP] = {ppc_ptr_cell(method),ih};
    if (ni > PM_METHOD_MAX_INPUTS || no > PM_CI_CAP - ni - 3) return -1;
    for (unsigned i=0; i<ni; i++) a[2+ni-1-i] = cells[i];
    if (ofw_call("call-method",ni+2,no+1,a) == -1) return -1;
    if (a[ni+2] != 0) return (int)a[ni+2];
    for (unsigned i=0; i<no; i++) cells[i] = a[ni+2+no-i];
    return 0;
}
int ofw_map_io(int mmu, unsigned ac, cell pa)
{
    cell cells[5], *p = cells;
    if (ac != 1 && ac != 2) return -1;
    if (ac == 2) *p++ = 0;
    *p++ = pa; *p++ = pa; *p++ = PM_ESCC_WINDOW; *p++ = PM_MAP_MODE_IO;
    return ofw_call_method("map",mmu,ac+3,0,cells);
}
int ofw_option_truefalse(const char *prop, int proplen)
{
    if (proplen <= 0) return 0;
    switch (prop[0]) {
    case 'y': case 'Y': case 't': case 'T': case '1': return 1;
    default: return 0;
    }
}
int ofw_setup(struct ofw_env *e)
{
    char prop[PM_OPTION_CAP];
    e->in = e->out = -1;
    int chosen = ofw_finddevice(PM_CHOSEN);
    if (chosen == -1) return -1;
    if (ofw_getprop(chosen,"stdin",&e->in,sizeof(e->in)) != sizeof(e->in) ||
        ofw_getprop(chosen,"stdout",&e->out,sizeof(e->out)) != sizeof(e->out)) return -1;
    if (e->out == 0) e->out = ofw_open(PM_SCREEN);
    if (e->out == -1) return -1;
    int root = ofw_finddevice(PM_ROOT);
    int options = ofw_finddevice(PM_OPTIONS);
    ofw_finddevice(PM_OPENPROM);
    chosen = ofw_finddevice(PM_CHOSEN);
    if (root == -1 || chosen == -1) return -2;
    e->real_mode = 0;
    if (options != -1) {
        int n = ofw_getprop(options,"real-mode?",prop,sizeof(prop));
        if (n > 0 && n <= (int)sizeof(prop)) e->real_mode = ofw_option_truefalse(prop,n);
    }
    e->ac = e->sc = 1;
    ofw_getprop(root,"#address-cells",&e->ac,sizeof(e->ac));
    ofw_getprop(root,"#size-cells",&e->sc,sizeof(e->sc));
#ifdef PPC_MMU
    /* NetBSD ofw_machdep.c:223-253 handles both 32- and 64-bit cells. */
    if ((e->sc != 1 && e->sc != 2) || (e->ac != 1 && e->ac != 2)) return -2;
#else
    if (e->sc != 1 || (e->ac != 1 && e->ac != 2)) return -2;
#endif
    e->memory = e->mmu = -1;
    ofw_getprop(chosen,"memory",&e->memory,sizeof(e->memory));
    ofw_getprop(chosen,"mmu",&e->mmu,sizeof(e->mmu));
    if (e->memory == -1 || e->mmu == -1) return -2;
    return 0;
}
