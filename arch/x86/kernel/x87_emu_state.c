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

unsigned long long x87_get_le(const u8 *p, unsigned n)
{
    unsigned long long v = 0;
    for (unsigned i = 0; i < n; ++i) v |= (unsigned long long)p[i] << (8 * i);
    return v;
}
void x87_put_le(u8 *p, unsigned long long v, unsigned n)
{
    for (unsigned i = 0; i < n; ++i) p[i] = v >> (8 * i);
}
sw_extFloat80_t x87_unpack(const struct x87_emu_ext80 *r)
{
    sw_extFloat80_t v;
    v.signif = x87_get_le(r->bytes, 8);
    v.signExp = x87_get_le(r->bytes + 8, 2);
    return v;
}
void x87_pack(struct x87_emu_ext80 *r, sw_extFloat80_t v)
{
    x87_put_le(r->bytes, v.signif, 8);
    x87_put_le(r->bytes + 8, v.signExp, 2);
}
void x87_put(struct x87_emu_state *s, unsigned n, sw_extFloat80_t v)
{
    unsigned exp = v.signExp & 0x7fff;
    unsigned tag = exp == 0 && !v.signif ? 1 :
        (!exp || exp == 0x7fff || !(v.signif >> 63) ? 2 : 0);
    x87_pack(&s->reg[n], v);
    x87_set_tag(s, n, tag);
}
sw_extFloat80_t x87_indefinite(void)
{
    sw_extFloat80_t v = {.signif = 0xc000000000000000ULL, .signExp = 0xffff};
    return v;
}
/* FPU_FINIT / SF_FPU_FINIT: only creation clears register payloads. */
void x87_emu_state_init(struct x87_emu_state *s, bool new_owner)
{
    if (new_owner) {
        u8 *p = (u8 *)s;
        for (unsigned i = 0; i < sizeof(*s); ++i) p[i] = 0;
    }
    s->cw = 0x037f;
    s->sw = 0;
    s->tw = 0xffff;
    s->fip = s->fdp = 0;
    s->fcs = s->fds = s->fop = 0;
    s->reserved_cw = s->reserved_sw = s->reserved_tw = s->reserved_fds = 0xffff;
}
/* Protected mode branches of FPU_FSTENV / FPU_FSAVE. No live side effects. */
void x87_emu_state_encode(const struct x87_emu_state *s, u8 *p,
                         bool op16, bool env)
{
    unsigned start = op16 ? X87_EMU_ENV16_BYTES : X87_EMU_ENV_BYTES;
    if (op16) {
        x87_put_le(p, s->cw, 2); x87_put_le(p+2, s->sw, 2);
        x87_put_le(p+4, s->tw, 2); x87_put_le(p+6, s->fip, 2);
        x87_put_le(p+8, s->fcs, 2); x87_put_le(p+10, s->fdp, 2);
        x87_put_le(p+12, s->fds, 2);
    } else {
        x87_put_le(p, s->cw | 0xffff0000u, 4);
        x87_put_le(p+4, s->sw | 0xffff0000u, 4);
        x87_put_le(p+8, s->tw | 0xffff0000u, 4);
        x87_put_le(p+12, s->fip, 4);
        x87_put_le(p+16, s->fcs | ((u32)(s->fop & 0x7ff) << 16), 4);
        x87_put_le(p+20, s->fdp, 4);
        x87_put_le(p+24, s->fds | 0xffff0000u, 4);
    }
    if (!env) for (unsigned i = 0; i < 8; ++i)
        for (unsigned j = 0; j < 10; ++j)
            p[start + i * 10 + j] = s->reg[x87_st(s, i)].bytes[j];
}
/* FPU_FLDENV / FPU_FRSTOR: 16-bit protected ENV has no FOP field. */
void x87_emu_state_decode(struct x87_emu_state *s, const u8 *p,
                         bool op16, bool env)
{
    unsigned start = op16 ? X87_EMU_ENV16_BYTES : X87_EMU_ENV_BYTES;
    if (op16) {
        s->cw = x87_get_le(p, 2); s->sw = x87_get_le(p+2, 2);
        s->tw = x87_get_le(p+4, 2); s->fip = x87_get_le(p+6, 2);
        s->fcs = x87_get_le(p+8, 2); s->fdp = x87_get_le(p+10, 2);
        s->fds = x87_get_le(p+12, 2);
    } else {
        s->cw = x87_get_le(p, 2); s->sw = x87_get_le(p+4, 2);
        s->tw = x87_get_le(p+8, 2); s->fip = x87_get_le(p+12, 4);
        s->fcs = x87_get_le(p+16, 2); s->fop = x87_get_le(p+18, 2) & 0x7ff;
        s->fdp = x87_get_le(p+20, 4); s->fds = x87_get_le(p+24, 2);
    }
    s->reserved_cw = s->reserved_sw = s->reserved_tw = s->reserved_fds = 0xffff;
    if (!env) for (unsigned i = 0; i < 8; ++i)
        for (unsigned j = 0; j < 10; ++j)
            s->reg[x87_st(s, i)].bytes[j] = p[start + i * 10 + j];
}
/* FPU_prepush / FPU_push / FPU_pop, using physical tags and staged TOP. */
void x87_push(struct x87_emu_effect *e, sw_extFloat80_t v)
{
    unsigned n = (x87_top(&e->state) - 1) & 7;
    e->state.sw &= ~X87_C1;
    if (x87_tag(&e->state, n) != 3) {
        e->raised |= X87_IE | X87_SF | X87_C1;
        e->state.sw |= X87_C1;
        v = x87_indefinite();
    }
    x87_set_top(&e->state, n);
    x87_put(&e->state, n, v);
}
void x87_pop(struct x87_emu_effect *e)
{
    x87_set_tag(&e->state, x87_top(&e->state), 3);
    x87_set_top(&e->state, x87_top(&e->state) + 1);
}

static const uint64_t fpu_const_l2t_sig[4] = {
	UINT64_C(0xD49A784BCD1B8AFE), UINT64_C(0xD49A784BCD1B8AFE),
	UINT64_C(0xD49A784BCD1B8AFF), UINT64_C(0xD49A784BCD1B8AFE)
};
static const uint64_t fpu_const_l2e_sig[4] = {
	UINT64_C(0xB8AA3B295C17F0BC), UINT64_C(0xB8AA3B295C17F0BB),
	UINT64_C(0xB8AA3B295C17F0BC), UINT64_C(0xB8AA3B295C17F0BB)
};
static const uint64_t fpu_const_pi_sig[4] = {
	UINT64_C(0xC90FDAA22168C235), UINT64_C(0xC90FDAA22168C234),
	UINT64_C(0xC90FDAA22168C235), UINT64_C(0xC90FDAA22168C234)
};
static const uint64_t fpu_const_lg2_sig[4] = {
	UINT64_C(0x9A209A84FBCFF799), UINT64_C(0x9A209A84FBCFF798),
	UINT64_C(0x9A209A84FBCFF799), UINT64_C(0x9A209A84FBCFF798)
};
static const uint64_t fpu_const_ln2_sig[4] = {
	UINT64_C(0xB17217F7D1CF79AC), UINT64_C(0xB17217F7D1CF79AB),
	UINT64_C(0xB17217F7D1CF79AC), UINT64_C(0xB17217F7D1CF79AB)
};

/* FPU_MakeConst / FPU_FLD1..FPU_FLDZ; exact bit constants, RC table retained. */
void x87_constant(struct x87_emu_effect *e, unsigned n)
{
    static const uint64_t *const sig[] = {0, fpu_const_l2t_sig,
        fpu_const_l2e_sig, fpu_const_pi_sig, fpu_const_lg2_sig, fpu_const_ln2_sig};
    static const u16 exp[] = {0x3fff, 0x4000, 0x3fff, 0x4000, 0x3ffd, 0x3ffe, 0};
    sw_extFloat80_t v;
    v.signExp = exp[n];
    v.signif = n == 0 ? 0x8000000000000000ULL : n == 6 ? 0 :
        sig[n][(e->state.cw >> 10) & 3];
    x87_push(e, v);
}
