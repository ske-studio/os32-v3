/* Simple index/data pairs only; the selected register is never written. */
#include "probe_wire.h"
#include "kdebug_window.h"
#include "memmap.h"
#include "io.h"
#include "generated/kdebug_import_macros.h"

int probe_item_valid(const struct probe_item *p)
{
    if (p->kind < PROBE_IO || p->kind > PROBE_IDXMMIO ||
        (p->width != 1 && p->width != 2 && p->width != 4)) return 0;
    u32 end = p->kind == PROBE_IO || p->kind == PROBE_IDXIO ? U16_MAX : U32_MAX;
    if ((p->address & (p->width - 1U)) || p->address > end ||
        p->width - 1U > end - p->address) return 0;
    if (p->kind == PROBE_IO || p->kind == PROBE_MMIO)
        return !p->data_address && !p->index;
    if (p->width != 1 || p->index > U8_MAX || p->data_address > end ||
        p->address == p->data_address) return 0;
    return p->kind != PROBE_IDXMMIO ||
        p->address / MEM_PAGE_SIZE == p->data_address / MEM_PAGE_SIZE;
}

i32 probe_index_read(const struct probe_item *p, u32 *value, i32 *restore_rc)
{
    *value = 0;
    *restore_rc = PROBE_OK;
    if (!probe_item_valid(p) || (p->kind != PROBE_IDXIO && p->kind != PROBE_IDXMMIO))
        return PROBE_ARG;
    if (p->kind == PROBE_IDXIO) {
        u32 flags = irq_save();
        u32 old = inp(p->address);
        outp(p->address, p->index);
        *value = inp(p->data_address);
        outp(p->address, old);
        irq_restore(flags);
        return PROBE_OK;
    }
    struct recover_window w;
    recover_window_hal_init(&w);
    i32 rc = PROBE_WINDOW;
    /* Map the entire single page before selecting; begin holds IF. */
    u32 base = p->address & ~(MEM_PAGE_SIZE - 1U);
    if (!kdebug_window_begin(&w, base, MEM_PAGE_SIZE, true)) {
        u32 index_va = w.va + p->address - base;
        u32 data_va = w.va + p->data_address - base;
        u32 old;
        rc = PROBE_IO_ERROR;
        if (!w.ops->load(w.opaque, index_va, 1, &old)) {
            if (!w.ops->store(w.opaque, index_va, 1, p->index) &&
                !w.ops->load(w.opaque, data_va, 1, value)) rc = PROBE_OK;
            /* Even a failed selection may have reached hardware. */
            if (w.ops->store(w.opaque, index_va, 1, old))
                *restore_rc = PROBE_IO_ERROR;
        }
    }
    if (recover_window_cleanup(&w)) rc = PROBE_WINDOW;
    if (rc) *value = 0;
    return rc;
}
