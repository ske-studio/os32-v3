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

/* FPU_SetCW, Begin/EndPrecisionControl and SoftFloatBegin/End. */
void x87_emu_scope_begin(u16 cw, struct x87_emu_softfloat_scope *s)
{
    static const u8 rounding[] = {softfloat_round_near_even, softfloat_round_min,
        softfloat_round_max, softfloat_round_minMag};
    s->rounding_mode = softfloat_roundingMode;
    s->detect_tininess = softfloat_detectTininess;
    s->exception_flags = softfloat_exceptionFlags;
    s->ext80_precision = extF80_roundingPrecision;
    softfloat_roundingMode = rounding[(cw >> 10) & 3];
    softfloat_detectTininess = softfloat_tininess_afterRounding;
    softfloat_exceptionFlags = 0;
    extF80_roundingPrecision = ((cw >> 8) & 3) == 0 ? 32 :
        ((cw >> 8) & 3) == 2 ? 64 : 80;
}
void x87_emu_scope_end(const struct x87_emu_softfloat_scope *s, struct x87_emu_effect *e)
{
    u8 f = softfloat_exceptionFlags;
    e->raised |= ((f & softfloat_flag_invalid) >> 4) |
        ((f & softfloat_flag_infinite) >> 1) |
        ((f & softfloat_flag_overflow) << 1) |
        ((f & softfloat_flag_underflow) << 3) |
        ((f & softfloat_flag_inexact) << 5);
    softfloat_roundingMode = s->rounding_mode;
    softfloat_detectTininess = s->detect_tininess;
    softfloat_exceptionFlags = s->exception_flags;
    extF80_roundingPrecision = s->ext80_precision;
}
/* fpu_checkexception plus OS32's deferred exception/transaction policy. */
enum x87_emu_result x87_emu_exception(const struct x87_emu_state *before,
    const struct x87_emu_decoded *d, struct x87_emu_effect *e)
{
    if (d->waiting && (before->sw & ~before->cw & 0x3f)) return X87_EMU_PENDING_MF;
    /* Invalid operation has priority over denormal/range/precision, including
     * masked stack overflow while loading a denormal memory operand. */
    if (e->raised & X87_IE) e->raised &= ~(X87_DE | X87_ZE | X87_OE | X87_UE | X87_PE);
    u16 unmasked = e->raised & ~before->cw & 0x3f;
    if (unmasked & (X87_IE | X87_DE | X87_ZE)) {
        /* Operand faults suppress destination, TOP (invalid comparisons report unordered). */
        u16 c1 = (unmasked & X87_DE) ? 0 : (e->state.sw & X87_C1);
        struct x87_emu_state current = e->state;
        e->state = *before;
        e->state.fip = current.fip; e->state.fcs = current.fcs; e->state.fop = current.fop;
        e->state.fdp = current.fdp; e->state.fds = current.fds;
        e->state.sw = (before->sw & ~X87_C1) | c1;
        unsigned idx = (d->modrm >> 3) & 7;
        bool compare = ((d->opcode == 0xd8 || d->opcode == 0xdc || d->opcode == 0xde) &&
                        (idx == 2 || idx == 3)) ||
            (d->opcode == 0xda && (d->memory ? (idx == 2 || idx == 3) : d->modrm == 0xe9)) ||
            (d->opcode == 0xd9 && !d->memory && d->modrm == 0xe4) ||
            (d->opcode == 0xdd && !d->memory && (idx == 4 || idx == 5));
        if (compare && (e->raised & (X87_IE | X87_DE)))
            e->state.sw = (e->state.sw & ~X87_CC) | (current.sw & X87_CC);
        e->commit &= ~(X87_COMMIT_REGS | X87_COMMIT_MEMORY | X87_COMMIT_GPR);
        if (unmasked & X87_IE) e->raised &= ~(X87_DE | X87_ZE | X87_OE | X87_UE | X87_PE);
        else if (unmasked & X87_DE) e->raised &= ~(X87_ZE | X87_OE | X87_UE | X87_PE);
        else e->raised &= ~(X87_OE | X87_UE | X87_PE);
    } else if (unmasked & (X87_OE | X87_UE)) {
        /* Memory destinations are not written or popped on unmasked O/U. */
        if (d->memory && d->write) {
            e->raised &= ~X87_PE;
            e->state.sw &= ~X87_C1;
            e->state.sw = (e->state.sw & ~X87_TOP) | (before->sw & X87_TOP);
            e->state.tw = before->tw;
            e->commit &= ~(X87_COMMIT_REGS | X87_COMMIT_MEMORY);
        }
    }
    e->state.sw |= e->raised & 0x7f;
    if (e->state.sw & ~e->state.cw & 0x3f) e->state.sw |= X87_ESB;
    else e->state.sw &= ~X87_ESB;
    return X87_EMU_OK;
}
