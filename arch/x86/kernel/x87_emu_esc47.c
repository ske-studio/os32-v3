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


/* NP21/W SF_ESC4..7 (1995..2359); CPU/memory/exception boundary replaced. */
#include "x87_emu_internal.h"

/* DC/DE register forms reverse /4../7 relative to the EA_TREE forms. */
static void sti_arithmetic(struct esc *c, unsigned idx, unsigned sub)
{
    struct x87_emu_effect *e = c->e;
    unsigned destination = x87_st(&e->state, sub);
    sw_extFloat80_t top = x87_get(e, x87_top(&e->state));
    sw_extFloat80_t other = x87_get(e, destination);
    if (idx == 2 || idx == 3) x87_compare_values(e, top, other, true);
    else x87_binary_to(e, destination, idx >= 4 ? (idx ^ 1) : idx, other, top);
}
static void exchange(struct x87_emu_effect *e, unsigned sub)
{
    unsigned top = x87_top(&e->state), other = x87_st(&e->state, sub);
    sw_extFloat80_t a = x87_get(e, top), b = x87_get(e, other);
    e->state.sw &= ~X87_C1;
    x87_put(&e->state, top, b); x87_put(&e->state, other, a);
}
/* SF_ESC4: register destination ST(i), or binary64 EA_TREE. */
enum x87_emu_result x87_esc4(struct esc *c)
{
    unsigned idx = (c->d->modrm >> 3) & 7, sub = c->d->modrm & 7;
    if (c->d->memory) {
        MEMORY(8, false); x87_record(c);
        x87_ea_tree(c, x87_load64(c->e, c->input));
    } else {
        x87_record(c);
        sti_arithmetic(c, idx, sub);
        if (idx == 3) x87_pop(c->e);
    }
    return X87_EMU_OK;
}
/* SF_ESC5: binary64, stack and full environment image transfers. */
enum x87_emu_result x87_esc5(struct esc *c)
{
    struct x87_emu_effect *e = c->e;
    unsigned idx = (c->d->modrm >> 3) & 7, sub = c->d->modrm & 7;
    if (!c->d->memory) {
        x87_record(c);
        switch (idx) {
        case 0: x87_set_tag(&e->state, x87_st(&e->state, sub), 3); break;
        case 1: exchange(e, sub); break;
        case 2: case 3:
            x87_copy_register(e, x87_st(&e->state, sub));
            if (idx == 3) x87_pop(e);
            break;
        case 4: case 5: {
            sw_extFloat80_t a = x87_get(e, x87_top(&e->state));
            sw_extFloat80_t b = x87_get(e, x87_st(&e->state, sub));
            x87_compare_values(e, a, b, false);
            if (idx == 5) x87_pop(e);
            break;
        }
        default: return X87_EMU_UNSUPPORTED;
        }
        return X87_EMU_OK;
    }
    switch (idx) {
    case 0:
        MEMORY(8, false); x87_record(c);
        x87_push(e, x87_load64(e, c->input)); break;
    case 2: case 3:
        MEMORY(8, true); x87_record(c); x87_store64(e);
        if (idx == 3) x87_pop(e);
        break;
    case 4: /* FPU_FRSTOR: codec restores FDP, never replace it with the image EA. */
        MEMORY(c->d->operand16 ? X87_EMU_SAVE16_BYTES : X87_EMU_MEMORY_BYTES, false);
        x87_emu_state_decode(&e->state, c->input, c->d->operand16, false); break;
    case 6: /* FPU_FSAVE: old environment/registers first, then FINIT. */
        MEMORY(c->d->operand16 ? X87_EMU_SAVE16_BYTES : X87_EMU_MEMORY_BYTES, true);
        x87_emu_state_encode(&e->state, e->output, c->d->operand16, false);
        x87_emu_state_init(&e->state, false); break;
    case 7:
        MEMORY(2, true); x87_put_le(e->output, e->state.sw, 2); break;
    default: return X87_EMU_UNSUPPORTED; /* FISTTP and reserved forms */
    }
    return X87_EMU_OK;
}
/* SF_ESC6: signed WORD EA_TREE, register arithmetic-and-pop or FCOMPP. */
enum x87_emu_result x87_esc6(struct esc *c)
{
    unsigned idx = (c->d->modrm >> 3) & 7, sub = c->d->modrm & 7;
    if (c->d->memory) {
        MEMORY(2, false); x87_record(c);
        x87_ea_tree(c, x87_load_integer(c->input, 2));
    } else {
        if (idx == 3 && sub != 1) return X87_EMU_UNSUPPORTED;
        x87_record(c);
        sti_arithmetic(c, idx, sub);
        if (idx == 3) x87_pop(c->e);
        /* Unmasked operand errors roll both destination and pop back in policy. */
        x87_pop(c->e);
    }
    return X87_EMU_OK;
}
/* SF_ESC7: signed WORD/QWORD conversions and status to saved AX. */
enum x87_emu_result x87_esc7(struct esc *c)
{
    struct x87_emu_effect *e = c->e;
    unsigned idx = (c->d->modrm >> 3) & 7, sub = c->d->modrm & 7;
    if (!c->d->memory) {
        if (c->d->modrm == 0xe0) {
            e->gpr[X87_EAX] = (e->gpr[X87_EAX] & 0xffff0000u) | e->state.sw;
            return X87_EMU_OK;
        }
        x87_record(c);
        switch (idx) {
        case 0:
            x87_set_tag(&e->state, x87_st(&e->state, sub), 3); x87_pop(e); break;
        case 1: exchange(e, sub); break;
        case 2: case 3:
            x87_copy_register(e, x87_st(&e->state, sub)); x87_pop(e); break;
        default: return X87_EMU_UNSUPPORTED; /* FCOMIP/FUCOMIP */
        }
        return X87_EMU_OK;
    }
    switch (idx) {
    case 0: case 5:
        MEMORY(idx == 0 ? 2 : 8, false); x87_record(c);
        x87_push(e, x87_load_integer(c->input, c->d->memory_bytes)); break;
    case 2: case 3: case 7:
        MEMORY(idx == 7 ? 8 : 2, true); x87_record(c);
        x87_store_integer(e, c->d->memory_bytes);
        if (idx != 2) x87_pop(e);
        break;
    default: return X87_EMU_UNSUPPORTED; /* FISTTP, FBLD and FBSTP */
    }
    return X87_EMU_OK;
}
