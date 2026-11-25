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
/* NetBSD 36eddebc zs.c:203,1039-1048: exact parent name and
 * assigned-addresses[2] + ch-a reg[0]. Tree traversal and validation are OS32. */
#include "ofw.h"
#include "kstring.h"
static cell property[PM_PROP_CAP / PM_CELL_BYTES];
static char path[PM_PATH_CAP];
static int named(int node, const char *name)
{
    int n = ofw_getprop(node,"name",property,sizeof(property));
    unsigned len = kstrlen(name);
    if (n != (int)len && n != (int)len+1) return 0;
    const char *p = (const char *)property;
    return !kstrncmp(p,name,len) && (n == (int)len || p[len] == 0);
}
/* Firmware tree preorder: first matching ch-a under an exact escc parent. */
static int channel(int node)
{
    while (node != 0 && node != -1) {
        if (named(node,"ch-a")) {
            int parent = ofw_parent(node);
            if (parent != 0 && parent != -1 && named(parent,"escc")) return node;
        }
        int found = channel(ofw_child(node));
        if (found != 0) return found;
        node = ofw_peer(node);
    }
    return 0;
}
int pm_escc_prepare(const struct ofw_env *e, struct pm_escc *out)
{
    int root = ofw_finddevice(PM_ROOT);
    if (root == -1) return -1;
    int ch = channel(ofw_child(root));
    if (!ch) return -1;
    int n = ofw_getprop(ch,"reg",property,sizeof(property));
    if (n < PM_CELL_BYTES || n > (int)sizeof(property) || n % PM_CELL_BYTES) return -1;
    cell offset = property[0];
    /* channel already checked the parent; obtain its parent for the BAR. */
    int escc = ofw_parent(ch);
    int macio = escc != 0 && escc != -1 ? ofw_parent(escc) : -1;
    if (macio == 0 || macio == -1) return -1;
    n = ofw_getprop(macio,"assigned-addresses",property,sizeof(property));
    if (n < 3*PM_CELL_BYTES || n > (int)sizeof(property) || n % PM_CELL_BYTES) return -1;
    if (property[1] != 0) return -1; /* PCI phys.mid, address above 32 bits */
    cell bar = property[2];
    if (offset > (cell)-1 - bar) return -1;
    out->csr = bar + offset;
    if (out->csr > (cell)-1 - PM_ESCC_WINDOW) return -1;
    out->data = out->csr + PM_ESCC_STRIDE;
    n = ofw_package_to_path(ch,path,sizeof(path));
    if (n <= 0 || n >= (int)sizeof(path)) return -1;
    path[n] = 0;
    out->ih = ofw_open(path);
    if (out->ih == -1) return -2;
    if (!e->real_mode && ofw_map_io(e->mmu,e->ac,out->csr) != 0) return -3;
    return 0;
}
