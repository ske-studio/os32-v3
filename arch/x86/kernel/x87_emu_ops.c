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

static bool unsupported(sw_extFloat80_t v)
{
    return (v.signExp & 0x7fff) && !(v.signif >> 63);
}
static bool nan80(sw_extFloat80_t v)
{
    return (v.signExp & 0x7fff) == 0x7fff && (v.signif << 1);
}
static sw_extFloat80_t operand(struct x87_emu_effect *e, sw_extFloat80_t v)
{
    if (unsupported(v)) { e->raised |= X87_IE; return x87_indefinite(); }
    if (!(v.signExp & 0x7fff) && v.signif) e->raised |= X87_DE;
    return v;
}
sw_extFloat80_t x87_get(struct x87_emu_effect *e, unsigned n)
{
    if (x87_tag(&e->state, n) == 3) {
        e->raised |= X87_IE | X87_SF;
        e->state.sw &= ~X87_C1;
        return x87_indefinite();
    }
    return x87_unpack(&e->state.reg[n]);
}
/* FPU_Compare: Special includes infinity; only actual NaN/unsupported is invalid. */
void x87_compare_values(struct x87_emu_effect *e, sw_extFloat80_t a,
                        sw_extFloat80_t b, bool ordered)
{
    e->state.sw &= ~(X87_CC | X87_C1);
    bool invalid = unsupported(a) || unsupported(b);
    a = operand(e, a); b = operand(e, b);
    if (nan80(a) || nan80(b)) e->raised &= ~X87_DE;
    if (invalid || nan80(a) || nan80(b) || (e->raised & X87_SF)) {
        e->state.sw |= X87_CC;
        if (invalid || ordered || extF80_isSignalingNaN(a) || extF80_isSignalingNaN(b))
            e->raised |= X87_IE;
    } else if (extF80_eq(a, b)) e->state.sw |= X87_C3;
    else if (extF80_lt(a, b)) e->state.sw |= X87_C0;
}
static sw_extFloat80_t binary(unsigned op, sw_extFloat80_t a, sw_extFloat80_t b)
{
    switch (op) {
    case 0: return extF80_add(a, b);
    case 1: return extF80_mul(a, b);
    case 4: return extF80_sub(a, b);
    case 5: return extF80_sub(b, a);
    case 6: return extF80_div(a, b);
    default: return extF80_div(b, a);
    }
}
/* x87 unmasked range errors return a wrapped exponent (Intel SDM Vol.1
 * 8.5.4/8.5.5), unlike SoftFloat's masked IEEE result. Reevaluate with an
 * exact power-of-two scaling of operands; no native arithmetic or f64 loss. */
static sw_extFloat80_t scale_operand(sw_extFloat80_t v, int shift)
{
    int exp = v.signExp & 0x7fff;
    u16 sign = v.signExp & 0x8000;
    if (!v.signif || exp == 0x7fff) return v;
    if (!exp) {
        exp = 1;
        while (!(v.signif >> 63)) { v.signif <<= 1; --exp; }
    }
    exp += shift;
    if (exp <= 0) {
        unsigned n = 1 - exp;
        v.signif = n >= 64 ? (v.signif != 0) :
            (v.signif >> n) | ((v.signif << (64 - n)) != 0);
        exp = 0;
    }
    v.signExp = sign | exp;
    return v;
}
/* FPU_FADD/FMUL/FSUB/FSUBR/FDIV/FDIVR: SoftFloat operation and PC unchanged.
 * Repeat with truncation only to recover x87 C1 (magnitude rounded upward). */
void x87_binary_to(struct x87_emu_effect *e, unsigned destination, unsigned op,
                   sw_extFloat80_t a, sw_extFloat80_t b)
{
    a = operand(e, a); b = operand(e, b);
    if (nan80(a) || nan80(b)) e->raised &= ~X87_DE;
    e->state.sw &= ~X87_C1;
    if (op == 2 || op == 3) { x87_compare_values(e, a, b, true); return; }
    sw_extFloat80_t v = binary(op, a, b);
    u8 range_flag = 0;
    if ((softfloat_exceptionFlags & softfloat_flag_overflow) && !(e->state.cw & X87_OE))
        range_flag = softfloat_flag_overflow;
    else if (!(e->state.cw & X87_UE) &&
        ((softfloat_exceptionFlags & softfloat_flag_underflow) ||
         (!(v.signExp & 0x7fff) && v.signif))) range_flag = softfloat_flag_underflow;
    if (range_flag) {
        int shift = range_flag == softfloat_flag_overflow ? -24576 : 24576;
        if (op == 1) { a = scale_operand(a, shift / 2); b = scale_operand(b, shift / 2); }
        else if (op == 6) { a = scale_operand(a, shift / 2); b = scale_operand(b, -shift / 2); }
        else if (op == 7) { a = scale_operand(a, -shift / 2); b = scale_operand(b, shift / 2); }
        else { a = scale_operand(a, shift); b = scale_operand(b, shift); }
        softfloat_exceptionFlags = 0;
        v = binary(op, a, b);
        softfloat_exceptionFlags |= range_flag;
    }
    u8 flags = softfloat_exceptionFlags, mode = softfloat_roundingMode;
    softfloat_roundingMode = softfloat_round_minMag;
    sw_extFloat80_t truncated = binary(op, a, b);
    softfloat_roundingMode = mode;
    softfloat_exceptionFlags = flags;
    if ((flags & softfloat_flag_inexact) &&
        ((v.signExp & 0x7fff) > (truncated.signExp & 0x7fff) ||
         ((v.signExp & 0x7fff) == (truncated.signExp & 0x7fff) && v.signif > truncated.signif)))
        e->state.sw |= X87_C1;
    x87_put(&e->state, destination, v);
}
void x87_binary(struct x87_emu_effect *e, unsigned op, sw_extFloat80_t a, sw_extFloat80_t b)
{
    x87_binary_to(e, x87_top(&e->state), op, a, b);
}
/* FPU_FSQRT / FPU_FRNDINT. */
void x87_unary(struct x87_emu_effect *e, bool square_root)
{
    sw_extFloat80_t a = operand(e, x87_get(e, x87_top(&e->state)));
    sw_extFloat80_t v = square_root ? extF80_sqrt(a) :
        extF80_roundToInt(a, softfloat_roundingMode, true);
    u8 flags = softfloat_exceptionFlags, mode = softfloat_roundingMode;
    softfloat_roundingMode = softfloat_round_minMag;
    sw_extFloat80_t t = square_root ? extF80_sqrt(a) :
        extF80_roundToInt(a, softfloat_round_minMag, true);
    softfloat_roundingMode = mode;
    softfloat_exceptionFlags = flags;
    e->state.sw &= ~X87_C1;
    if ((flags & softfloat_flag_inexact) &&
        ((v.signExp & 0x7fff) > (t.signExp & 0x7fff) ||
         ((v.signExp & 0x7fff) == (t.signExp & 0x7fff) && v.signif > t.signif))) e->state.sw |= X87_C1;
    x87_put(&e->state, x87_top(&e->state), v);
}
/* FPU_FLD_F32 / FPU_FLD_I32, integer wire replaces native float alias. */
sw_extFloat80_t x87_load32(struct x87_emu_effect *e, const u8 *p, bool integer)
{
    u32 bits = x87_get_le(p, 4);
    if (integer) return i32_to_extF80((i32)bits);
    if (!(bits & 0x7f800000) && (bits & 0x7fffff)) e->raised |= X87_DE;
    sw_float32_t v = {bits};
    return f32_to_extF80(v);
}
/* FPU_FST_F32 / FPU_FST_I32, buffer replaces direct memory store. */
void x87_store32(struct x87_emu_effect *e, bool integer)
{
    if (integer) { x87_store_integer(e, 4); return; }
    sw_extFloat80_t a = x87_get(e, x87_top(&e->state));
    if (unsupported(a)) { e->raised |= X87_IE; a = x87_indefinite(); }
    u32 bits = extF80_to_f32(a).v;
    if (!(e->state.cw & X87_UE) && a.signif &&
        (a.signExp & 0x7fff) < 0x3f81) softfloat_exceptionFlags |= softfloat_flag_underflow;
    u8 flags = softfloat_exceptionFlags, mode = softfloat_roundingMode;
    softfloat_roundingMode = softfloat_round_minMag;
    u32 trunc = extF80_to_f32(a).v;
    softfloat_roundingMode = mode;
    softfloat_exceptionFlags = flags;
    e->state.sw &= ~X87_C1;
    if ((flags & softfloat_flag_inexact) && bits != trunc) e->state.sw |= X87_C1;
    x87_put_le(e->output, bits, 4);
}
/* FPU_FXAM, with unsupported encodings distinguished from normal numbers. */
void x87_fxam(struct x87_emu_effect *e)
{
    sw_extFloat80_t v = x87_unpack(&e->state.reg[x87_top(&e->state)]);
    e->state.sw &= ~(X87_CC | X87_C1);
    if (v.signExp & 0x8000) e->state.sw |= X87_C1;
    if (x87_tag(&e->state, x87_top(&e->state)) == 3) e->state.sw |= X87_C3 | X87_C0;
    else if (unsupported(v)) return;
    else if (nan80(v)) e->state.sw |= X87_C0;
    else if ((v.signExp & 0x7fff) == 0x7fff) e->state.sw |= X87_C2 | X87_C0;
    else if (!(v.signExp & 0x7fff)) e->state.sw |= v.signif ? X87_C3 | X87_C2 : X87_C3;
    else e->state.sw |= X87_C2;
}

/* FPU_FLD_F64[_EA] / c_double_to_floatx80: integer bit wire, no native FP. */
sw_extFloat80_t x87_load64(struct x87_emu_effect *e, const u8 *p)
{
    sw_float64_t v = {x87_get_le(p, 8)};
    if (!(v.v & 0x7ff0000000000000ULL) && (v.v & 0xfffffffffffffULL)) e->raised |= X87_DE;
    return f64_to_extF80(v);
}
/* int16/int64_to_floatx80 and FPU_FLD_I16[_EA]/I64. */
sw_extFloat80_t x87_load_integer(const u8 *p, unsigned bytes)
{
    if (bytes == 2) return i32_to_extF80((i16)x87_get_le(p, 2));
    if (bytes == 4) return i32_to_extF80((i32)x87_get_le(p, 4));
    return i64_to_extF80((long long)x87_get_le(p, 8));
}
/* The NP2/plain and round_to_zero wrappers share these width/mode branches.
 * minMag is used to recover C1; no FISTTP opcode is exposed. */
static unsigned long long integer_bits(sw_extFloat80_t a, unsigned bytes, u8 mode)
{
    if (bytes == 2) return (u16)extF80_to_i16(a, mode, true);
    if (bytes == 4) return (u32)extF80_to_i32(a, mode, true);
    return (unsigned long long)extF80_to_i64(a, mode, true);
}
/* FPU_FST_I16/I32/I64: retain invalid/RC/exact semantics, stage the store. */
void x87_store_integer(struct x87_emu_effect *e, unsigned bytes)
{
    sw_extFloat80_t a = x87_get(e, x87_top(&e->state));
    if (unsupported(a)) { e->raised |= X87_IE; a = x87_indefinite(); }
    unsigned long long bits = integer_bits(a, bytes, softfloat_roundingMode);
    u8 flags = softfloat_exceptionFlags;
    unsigned long long trunc = integer_bits(a, bytes, softfloat_round_minMag);
    softfloat_exceptionFlags = flags;
    e->state.sw &= ~X87_C1;
    if ((flags & softfloat_flag_inexact) && bits != trunc) e->state.sw |= X87_C1;
    x87_put_le(e->output, bits, bytes);
}
/* FPU_FST_F64 / floatx80_to_c_double. */
void x87_store64(struct x87_emu_effect *e)
{
    sw_extFloat80_t a = x87_get(e, x87_top(&e->state));
    if (unsupported(a)) { e->raised |= X87_IE; a = x87_indefinite(); }
    unsigned long long bits = extF80_to_f64(a).v;
    if (!(e->state.cw & X87_UE) && a.signif && (a.signExp & 0x7fff) < 0x3c01)
        softfloat_exceptionFlags |= softfloat_flag_underflow;
    u8 flags = softfloat_exceptionFlags, mode = softfloat_roundingMode;
    softfloat_roundingMode = softfloat_round_minMag;
    unsigned long long trunc = extF80_to_f64(a).v;
    softfloat_roundingMode = mode;
    softfloat_exceptionFlags = flags;
    e->state.sw &= ~X87_C1;
    if ((flags & softfloat_flag_inexact) && bits != trunc) e->state.sw |= X87_C1;
    x87_put_le(e->output, bits, 8);
}
/* FPU_FST: source checked before destination/pop, including ST(0) itself. */
void x87_copy_register(struct x87_emu_effect *e, unsigned destination)
{
    sw_extFloat80_t v = x87_get(e, x87_top(&e->state));
    e->state.sw &= ~X87_C1;
    x87_put(&e->state, destination, v);
}
