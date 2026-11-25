/* OS32-owned byte/EA decoder. ESC functions retain instruction selection. */
#include "x87_emu.h"
struct cursor {
    const struct x87_emu_trap *trap;
    x87_emu_fetch_fn fetch;
    void *opaque;
    struct x87_emu_decoded *d;
};
static enum x87_emu_result take(struct cursor *c, unsigned n, u32 *v)
{
    *v = 0;
    for (unsigned i = 0; i < n; ++i) {
        u8 b;
        if (c->d->length == X87_EMU_INSN_BYTES) return X87_EMU_UNSUPPORTED;
        if (c->trap->eip > (u32)-1 - c->d->length) return X87_EMU_MEMORY_FAULT;
        enum x87_emu_result r = c->fetch(c->opaque, c->trap->eip + c->d->length, &b);
        if (r != X87_EMU_OK) return r;
        ++c->d->length;
        *v |= (u32)b << (i * 8);
    }
    return X87_EMU_OK;
}
#define TAKE(n, v) do { enum x87_emu_result r = take(&c, n, &(v)); if (r) return r; } while (0)
enum x87_emu_result x87_emu_decode(const struct x87_emu_trap *t,
    x87_emu_fetch_fn fetch, void *opaque, struct x87_emu_decoded *out)
{
    struct x87_emu_decoded d = {0};
    struct cursor c = {t, fetch, opaque, &d};
    u32 b, disp = 0, base = 0;
    int override = -1;
    d.waiting = true;
    for (;;) {
        TAKE(1, b);
        switch (b) {
        case 0x66: d.operand16 = true; continue;
        case 0x67: d.address16 = true; continue;
        case 0x26: override = X87_ES; continue;
        case 0x2e: override = X87_CS; continue;
        case 0x36: override = X87_SS; continue;
        case 0x3e: override = X87_DS; continue;
        case 0x64: override = X87_FS; continue;
        case 0x65: override = X87_GS; continue;
        default: break;
        }
        break;
    }
    d.opcode = b;
    if (b == 0x9b) { *out = d; return X87_EMU_OK; }
    if (b < 0xd8 || b > 0xdf) return X87_EMU_UNSUPPORTED;
    TAKE(1, b); d.modrm = b;
    unsigned mod = b >> 6, rm = b & 7;
    d.memory = mod != 3;
    d.segment = X87_DS;
    if (d.memory) {
        unsigned bytes = mod == 1 ? 1 : mod == 2 ? (d.address16 ? 2 : 4) : 0;
        if (d.address16) {
            static const u8 r1[] = {X87_EBX,X87_EBX,X87_EBP,X87_EBP,X87_ESI,X87_EDI,X87_EBP,X87_EBX};
            base = t->gpr[r1[rm]];
            if (rm < 4) base += t->gpr[rm & 1 ? X87_EDI : X87_ESI];
            if (rm == 2 || rm == 3 || rm == 6) d.segment = X87_SS;
            if (!mod && rm == 6) { base = 0; bytes = 2; d.segment = X87_DS; }
        } else {
            if (rm == 4) {
                TAKE(1, b); d.sib = b;
                unsigned index = (b >> 3) & 7;
                rm = b & 7;
                if (index != X87_ESP) base = t->gpr[index] << (b >> 6);
            }
            if (!mod && rm == X87_EBP) bytes = 4;
            else {
                base += t->gpr[rm];
                if (rm == X87_ESP || rm == X87_EBP) d.segment = X87_SS;
            }
        }
        if (bytes) TAKE(bytes, disp);
        if (bytes == 1) disp = (u32)(i32)(signed char)disp;
        d.offset = base + disp;
        if (d.address16) d.offset &= 0xffff;
    }
    if (override >= 0) d.segment = override;
    *out = d;
    return X87_EMU_OK;
}
