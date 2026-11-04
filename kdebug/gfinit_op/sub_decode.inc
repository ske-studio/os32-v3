/*
 * Copyright 2012 Red Hat Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 * Authors: Ben Skeggs
 */
    case GF_INIT_SUB: {
        o->index = r[pc + 1];
        if (!display && o->index != GF_INIT_SUB_FIRST && o->index != GF_INIT_SUB_FIRST + 1 && o->index != GF_INIT_SUB_LAST) return -1;
        /* nouveau init.c:400-424,1663-1682; zero entry means no call. */
        u32 table = display ? le(r, init_table + GF_INIT_BIT_SCRIPT, 2) : GF_INIT_SCRIPT_TABLE;
        if (!table || span(size, table + o->index * 2, 2)) return -1;
        o->ref = le(r, table + o->index * 2, 2); break;
    }
