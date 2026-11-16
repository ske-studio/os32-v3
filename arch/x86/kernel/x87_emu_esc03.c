/*
 * Copyright (c) 2012 NONAKA Kimihiro
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

/*
 * modified by SimK
 */


/* NP21/W port; source ranges are recorded in x87_emu_vendor/UPSTREAM.json. */
#include "x87_emu_internal.h"

struct esc {
    const struct x87_emu_trap *trap;
    const struct x87_emu_access *access;
    struct x87_emu_decoded *d;
    struct x87_emu_effect *e;
    u8 input[X87_EMU_MEMORY_BYTES];
};
/* CPU/operand boundary: select width before read, stage all writes. */
static enum x87_emu_result memory(struct esc *c, unsigned bytes, bool write)
{
    c->d->memory_bytes = bytes;
    c->d->write = write;
    if (write) return X87_EMU_OK;
    return x87_emu_read(c->access, c->d, c->input);
}
#define MEMORY(bytes, write) do { enum x87_emu_result r = memory(c, bytes, write); if (r) return r; } while (0)
static void record(struct esc *c)
{
    c->e->state.fip = c->trap->eip;
    c->e->state.fcs = c->trap->segment[X87_CS];
    c->e->state.fop = ((c->d->opcode & 7) << 8) | c->d->modrm;
    if (c->d->memory) {
        c->e->state.fdp = c->d->offset;
        c->e->state.fds = c->trap->segment[c->d->segment];
    }
}
/* EA_TREE: original /0..7 arithmetic and comparison selection. */
static void EA_TREE(struct esc *c, sw_extFloat80_t rhs)
{
    unsigned idx = (c->d->modrm >> 3) & 7;
    sw_extFloat80_t lhs = x87_get(c->e, x87_top(&c->e->state));
    x87_binary(c->e, idx, lhs, rhs);
    if (idx == 3) x87_pop(c->e);
}
/* SF_ESC0: D8 register or m32 real arithmetic. */
static enum x87_emu_result SF_ESC0(struct esc *c)
{
    sw_extFloat80_t rhs;
    if (c->d->memory) {
        MEMORY(4, false);
        rhs = x87_load32(c->e, c->input, false);
    } else rhs = x87_get(c->e, x87_st(&c->e->state, c->d->modrm & 7));
    record(c);
    EA_TREE(c, rhs);
    return X87_EMU_OK;
}
/* SF_ESC1: D9 retains the upstream memory/register and idx/sub branches. */
static enum x87_emu_result SF_ESC1(struct esc *c)
{
    struct x87_emu_effect *e = c->e;
    unsigned idx = (c->d->modrm >> 3) & 7, sub = c->d->modrm & 7;
    unsigned top = x87_top(&e->state), other = x87_st(&e->state, sub);
    if (c->d->memory) {
        switch (idx) {
        case 0:
            MEMORY(4, false); record(c);
            x87_push(e, x87_load32(e, c->input, false)); break;
        case 2: case 3:
            MEMORY(4, true); record(c); x87_store32(e, false);
            if (idx == 3) x87_pop(e);
            break;
        case 4:
            MEMORY(c->d->operand16 ? 14 : 28, false);
            x87_emu_state_decode(&e->state, c->input, c->d->operand16, true); break;
        case 5:
            MEMORY(2, false); e->state.cw = x87_get_le(c->input, 2) & 0x7fff; break;
        case 6:
            MEMORY(c->d->operand16 ? 14 : 28, true);
            x87_emu_state_encode(&e->state, e->output, c->d->operand16, true);
            e->state.cw |= 0x3f; break;
        case 7:
            MEMORY(2, true); x87_put_le(e->output, e->state.cw, 2); break;
        default: return X87_EMU_UNSUPPORTED;
        }
        return X87_EMU_OK;
    }
    record(c);
    switch (idx) {
    case 0: x87_push(e, x87_get(e, other)); break;
    case 1: {
        sw_extFloat80_t a = x87_get(e, top), b = x87_get(e, other);
        e->state.sw &= ~X87_C1;
        x87_put(&e->state, top, b); x87_put(&e->state, other, a); break;
    }
    case 2:
        if (sub) return X87_EMU_UNSUPPORTED;
        break;
    case 4:
        switch (sub) {
        case 0: case 1: {
            sw_extFloat80_t v = x87_get(e, top);
            e->state.sw &= ~X87_C1;
            if (!(e->raised & X87_SF)) {
                if (sub) v.signExp &= 0x7fff; else v.signExp ^= 0x8000;
            }
            x87_put(&e->state, top, v); break;
        }
        case 4: {
            sw_extFloat80_t zero = {.signif = 0, .signExp = 0};
            x87_compare_values(e, x87_get(e, top), zero, true); break;
        }
        case 5: x87_fxam(e); break;
        default: return X87_EMU_UNSUPPORTED;
        }
        break;
    case 5:
        if (sub == 7) return X87_EMU_UNSUPPORTED;
        x87_constant(e, sub); break;
    case 6:
        if (sub != 6 && sub != 7) return X87_EMU_UNSUPPORTED;
        e->state.sw &= ~X87_C1;
        x87_set_top(&e->state, top + (sub == 6 ? -1 : 1)); break;
    case 7:
        if (sub != 2 && sub != 4) return X87_EMU_UNSUPPORTED;
        x87_unary(e, sub == 2); break;
    default: return X87_EMU_UNSUPPORTED;
    }
    return X87_EMU_OK;
}
/* SF_ESC2: integer EA_TREE and FUCOMPP; excluded FCMOV remains unsupported. */
static enum x87_emu_result SF_ESC2(struct esc *c)
{
    if (c->d->memory) {
        MEMORY(4, false); record(c);
        EA_TREE(c, x87_load32(c->e, c->input, true));
    } else {
        if (c->d->modrm != 0xe9) return X87_EMU_UNSUPPORTED;
        record(c);
        sw_extFloat80_t a = x87_get(c->e, x87_top(&c->e->state));
        sw_extFloat80_t b = x87_get(c->e, x87_st(&c->e->state, 1));
        x87_compare_values(c->e, a, b, false);
        x87_pop(c->e); x87_pop(c->e);
    }
    return X87_EMU_OK;
}
/* SF_ESC3: integer/ext80 transfers and control; FISTTP/FCMOV/FCOMI excluded. */
static enum x87_emu_result SF_ESC3(struct esc *c)
{
    struct x87_emu_effect *e = c->e;
    unsigned idx = (c->d->modrm >> 3) & 7;
    if (!c->d->memory) {
        if (c->d->modrm == 0xe2) e->state.sw &= 0x7f00;
        else if (c->d->modrm == 0xe3) x87_emu_state_init(&e->state, false);
        else return X87_EMU_UNSUPPORTED;
        return X87_EMU_OK;
    }
    switch (idx) {
    case 0:
        MEMORY(4, false); record(c);
        x87_push(e, x87_load32(e, c->input, true)); break;
    case 2: case 3:
        MEMORY(4, true); record(c); x87_store32(e, true);
        if (idx == 3) x87_pop(e);
        break;
    case 5: {
        struct x87_emu_ext80 r;
        MEMORY(10, false); record(c);
        for (unsigned i = 0; i < 10; ++i) r.bytes[i] = c->input[i];
        x87_push(e, x87_unpack(&r)); break;
    }
    case 7: {
        struct x87_emu_ext80 r;
        MEMORY(10, true); record(c);
        x87_pack(&r, x87_get(e, x87_top(&e->state)));
        for (unsigned i = 0; i < 10; ++i) e->output[i] = r.bytes[i];
        e->state.sw &= ~X87_C1;
        x87_pop(e); break;
    }
    default: return X87_EMU_UNSUPPORTED;
    }
    return X87_EMU_OK;
}
enum x87_emu_result x87_emu_execute(const struct x87_emu_trap *trap,
    const struct x87_emu_access *access, struct x87_emu_decoded *d,
    struct x87_emu_effect *e)
{
    struct esc c = {.trap = trap, .access = access, .d = d, .e = e};
    unsigned idx = (d->modrm >> 3) & 7;
    d->waiting = !(d->opcode == 0xd9 && d->memory && (idx == 6 || idx == 7)) &&
        !(d->opcode == 0xdb && !d->memory && (d->modrm == 0xe2 || d->modrm == 0xe3));
    if (d->waiting && (e->state.sw & ~e->state.cw & 0x3f)) return X87_EMU_PENDING_MF;
    struct x87_emu_softfloat_scope scope;
    x87_emu_scope_begin(e->state.cw, &scope);
    enum x87_emu_result r;
    switch (d->opcode) {
    case 0x9b: r = X87_EMU_OK; break;
    case 0xd8: r = SF_ESC0(&c); break;
    case 0xd9: r = SF_ESC1(&c); break;
    case 0xda: r = SF_ESC2(&c); break;
    case 0xdb: r = SF_ESC3(&c); break;
    default: r = X87_EMU_UNSUPPORTED; break;
    }
    x87_emu_scope_end(&scope, e);
    return r;
}
