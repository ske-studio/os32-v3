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
    case GF_INIT_IO_FLAG_CONDITION: case GF_INIT_CONDITION: case GF_INIT_MACRO: {
        u32 entry = r[pc + 1];
        u32 base = o->opcode == GF_INIT_IO_FLAG_CONDITION ? GF_INIT_IO_FLAG_TABLE : o->opcode == GF_INIT_CONDITION ? GF_INIT_CONDITION_TABLE : GF_INIT_MACRO_TABLE;
        u32 width = o->opcode == GF_INIT_IO_FLAG_CONDITION ? GF_INIT_IO_FLAG_ENTRY : o->opcode == GF_INIT_CONDITION ? GF_INIT_CONDITION_ENTRY : GF_INIT_MACRO_ENTRY;
        u32 count = o->opcode == GF_INIT_IO_FLAG_CONDITION ? GF_INIT_IO_FLAG_COUNT : o->opcode == GF_INIT_CONDITION ? GF_INIT_CONDITION_COUNT : GF_INIT_MACRO_COUNT;
        /* Display references use BIT I, not the known S1 table bounds.
         * nouveau init.c:400-407,1727-1744. */
        if (display) {
            u32 offset = o->opcode == GF_INIT_IO_FLAG_CONDITION ? GF_INIT_BIT_IO_FLAG :
                         o->opcode == GF_INIT_CONDITION ? GF_INIT_BIT_CONDITION : GF_INIT_BIT_MACRO;
            base = le(r, init_table + offset, 2);
            if (!base) return -1;
        } else if (entry >= count) return -1;
        o->ref = base + entry * width; o->index = entry;
        if (span(size, o->ref, width)) return -1;
        if (o->opcode == GF_INIT_IO_FLAG_CONDITION) {
            o->port = le(r, o->ref, 2); o->index = r[o->ref + GF_INIT_IO_FLAG_INDEX];
            o->mask = r[o->ref + GF_INIT_IO_FLAG_REG_MASK]; o->shift = r[o->ref + GF_INIT_IO_FLAG_SHIFT];
            if (o->shift >= GF_SHIFT_BITS || span(size, le(r, o->ref + GF_INIT_IO_FLAG_DATA, 2),
                                      (o->mask >> o->shift) + 1)) return -1;
        } else {
            o->target = le(r, o->ref, 4);
            o->mask = le(r, o->ref + (o->opcode == GF_INIT_CONDITION ?
                GF_INIT_CONDITION_MASK : GF_INIT_MACRO_VALUE), 4);
            o->value = o->opcode == GF_INIT_CONDITION ? le(r, o->ref + GF_INIT_CONDITION_VALUE, 4) : o->mask;
        }
        break;
    }
