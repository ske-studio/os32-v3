/* Shared short-lived device aperture. No permission policy lives here. */
#include "kdebug_window.h"
#include "memmap.h"
#include "../kernel/paging.h"
#ifndef KPEEK_HOST
#include "io.h"
#endif
#ifndef KDEBUG_WINDOW_HOST
#include "generated/kdebug_import_macros.h"
#endif
#define RECOVER_PAGE_MASK (MEM_PAGE_SIZE - 1U)

static u32 window_hal_irq_save(void *opaque) { (void)opaque; return irq_save(); }
static void window_hal_irq_restore(void *opaque,u32 flags) { (void)opaque; irq_restore(flags); }
static u32 window_hal_pte(void *opaque,u32 va) { (void)opaque; return paging_pte_raw(va); }
static int window_hal_map(void *opaque,u32 va,u32 pa,u32 pages,u32 flags)
{
    (void)opaque;
    return paging_map_phys(va,pa,pages,flags);
}
static int window_hal_unmap(void *opaque,u32 va,u32 raw)
{ (void)opaque; return paging_pte_restore(va,raw); }
static int window_hal_load(void *opaque,u32 va,u32 width,u32 *value)
{
    (void)opaque;
    if (width==1) *value=*(volatile u8 *)(uptr)va;
    else if (width==2) *value=*(volatile u16 *)(uptr)va;
    else *value=*(volatile u32 *)(uptr)va;
    return 0;
}
static int window_hal_store(void *opaque,u32 va,u32 width,u32 value)
{
    (void)opaque;
    if (width==1) *(volatile u8 *)(uptr)va=(u8)value;
    else if (width==2) *(volatile u16 *)(uptr)va=(u16)value;
    else *(volatile u32 *)(uptr)va=value;
    return 0;
}
static const struct recover_window_ops window_hal_ops={
    window_hal_irq_save,window_hal_irq_restore,window_hal_pte,window_hal_map,
    window_hal_unmap,window_hal_load,window_hal_store
};
void recover_window_hal_init(struct recover_window *w)
{
    *w=(struct recover_window){.ops=&window_hal_ops,.va=MEM_DEVICE_APERTURE_BASE};
}

/* One owner across peek and recovery; a damaged PTE poisons this boot. */
static bool window_busy, window_failed;
int recover_window_cleanup(struct recover_window *w)
{
    if (!w || !w->ops) return -1;
    if (!w->owned) return w->failed ? -1 : 0;
    for (u32 i = 0; i < w->pages; i++) {
        if (w->ops->unmap(w->opaque, w->va + i * MEM_PAGE_SIZE, w->raw[i])) {
            w->failed = true;
            window_failed = true;
        }
    }
    w->owned = false;
    window_busy = false;
    w->ops->irq_restore(w->opaque, w->flags);
    return w->failed ? -1 : 0;
}

int kdebug_window_begin(struct recover_window *w, u32 pa, u32 len, bool write)
{
    if (!w || !w->ops || w->owned || w->failed || window_failed ||
        !w->ops->irq_save || !w->ops->irq_restore || !w->ops->pte_read ||
        !w->ops->map || !w->ops->unmap || !len || len > MEM_PAGE_SIZE ||
        pa > ~0U - (len - 1) || (w->va & RECOVER_PAGE_MASK)) return -1;
    u32 flags = w->ops->irq_save(w->opaque);
    if (window_busy) { w->ops->irq_restore(w->opaque, flags); return -1; }
    window_busy = true;
    w->flags = flags;
    w->pages = ((pa & RECOVER_PAGE_MASK) + len + RECOVER_PAGE_MASK) / MEM_PAGE_SIZE;
    for (u32 i = 0; i < w->pages; i++)
        w->raw[i] = w->ops->pte_read(w->opaque, w->va + i * MEM_PAGE_SIZE);
    w->owned = true;
    if (w->ops->map(w->opaque, w->va, pa & ~RECOVER_PAGE_MASK, w->pages,
                    PTE_PRESENT | PTE_PCD | (write ? PTE_RW : 0))) {
        w->failed = true;
        recover_window_cleanup(w);
        return -1;
    }
    return 0;
}

int recover_window_access(struct recover_window *w, u32 pa, u32 width,
                          bool write, u32 *value)
{
    if (!w || !value || !w->ops || !w->ops->load || !w->ops->store ||
        (width != 1 && width != 2 && width != 4) || pa % width) return -1;
    if (kdebug_window_begin(w, pa, width, write)) return -1;
    u32 addr = w->va + (pa & RECOVER_PAGE_MASK);
    int rc = write ? w->ops->store(w->opaque, addr, width, *value) :
                    w->ops->load(w->opaque, addr, width, value);
    if (rc) w->failed = true;
    return recover_window_cleanup(w);
}
