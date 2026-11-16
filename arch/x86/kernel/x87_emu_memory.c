/* OS32 memory boundary. Fixed GDT flat user segments; no raw user pointers. */
#include "x87_emu_internal.h"
#include "paging.h"
#include "gdt.h"

static enum x87_emu_result copy(const struct x87_emu_access *a,
    const struct x87_emu_decoded *d, u8 *out, const u8 *in)
{
    /* At most 108 bytes spans at most two pages. Validate both before copying. */
    u32 pa[2], len[2], count = 0, done = 0;
    bool write = in != 0;
    if (!a || !a->as || !a->trap || d->segment >= X87_SEGMENTS ||
        !d->memory_bytes || d->memory_bytes > X87_EMU_MEMORY_BYTES ||
        d->offset > (u32)-1 - (d->memory_bytes - 1u)) return X87_EMU_MEMORY_FAULT;
    u16 sel = a->trap->segment[d->segment];
    if (d->segment == X87_SS ? sel != USER_DS :
        (sel != USER_DS && (sel != USER_CS || write))) return X87_EMU_MEMORY_FAULT;
    while (done < d->memory_bytes) {
        u32 va = d->offset + done;
        if (!as_access_page(a->as, va, write, &pa[count])) return X87_EMU_MEMORY_FAULT;
        u32 n = PAGE_SIZE - (va & (PAGE_SIZE - 1));
        if (n > d->memory_bytes - done) n = d->memory_bytes - done;
        len[count++] = n;
        done += n;
    }
    done = 0;
    for (u32 i = 0; i < count; ++i) {
        u8 *p = P2V(pa[i]);
        for (u32 j = 0; j < len[i]; ++j) {
            if (write) p[j] = in[done++];
            else out[done++] = p[j];
        }
    }
    return X87_EMU_OK;
}
enum x87_emu_result x87_emu_read(const struct x87_emu_access *a,
    const struct x87_emu_decoded *d, u8 *p) { return copy(a, d, p, 0); }
enum x87_emu_result x87_emu_write(const struct x87_emu_access *a,
    const struct x87_emu_decoded *d, const u8 *p) { return copy(a, d, 0, p); }
enum x87_emu_result x87_emu_fetch(void *opaque, u32 offset, u8 *p)
{
    const struct x87_emu_access *a = opaque;
    if (a->trap->segment[X87_CS] != USER_CS) return X87_EMU_MEMORY_FAULT;
    struct x87_emu_decoded d = {.offset = offset, .memory_bytes = 1, .segment = X87_CS};
    return x87_emu_read(a, &d, p);
}
