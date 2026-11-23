#ifndef PLATFORM_HWB_H
#define PLATFORM_HWB_H

#include "kdebug/kdebug_window.h"
#include "memmap.h"
#include "io.h"
#include "kdebug/generated/kdebug_import_macros.h"

#include "exec/exec_hwb.h"

static inline u32 platform_hwb_abort_take(void)
{
    return exec_hwb_abort_take();
}

static inline u32 platform_hwb_phys_check(u32 addr, u32 width, u32 bytes)
{
    (void)addr;
    (void)width;
    (void)bytes;
    return HW_NOSYS;
}

static inline u32 platform_hwb_phys(struct hw_req *req)
{
    struct recover_window window;
    recover_window_hal_init(&window);
    u32 value = req->value;
    u32 status = HW_WINDOW;
    bool write = req->action == HW_WRITE;
    if (!kdebug_window_begin(&window, req->addr, req->width, write)) {
        u32 va = window.va + (req->addr & (MEM_PAGE_SIZE - 1U));
        int rc = write ? window.ops->store(window.opaque, va, req->width, value) :
                         window.ops->load(window.opaque, va, req->width, &value);
        status = rc ? HW_IO_ERROR : HW_OK;
    }
    if (recover_window_cleanup(&window)) status = HW_WINDOW;
    if (!write && status == HW_OK) req->value = value;
    return status;
}

/* The whole range is mapped before the first store. No calls, readback or
 * per-element window setup may split this burst. DF is clear on return. */
static inline u32 platform_hwb_block(struct hw_block_req *block)
{
    struct hw_req *req = &block->req;
    struct recover_window window;
    recover_window_hal_init(&window);
    u32 status = HW_WINDOW;
    if (!kdebug_window_begin(&window, req->addr, block->count * req->width, true)) {
        u32 va = window.va + (req->addr & (MEM_PAGE_SIZE - 1U));
        u32 count = block->count, value = req->value;
        if (req->width == 1) {
            if (!block->increment)
                __asm__ volatile("cld; rep stosb"
                                 : "+D"(va), "+c"(count) : "a"(value) : "memory", "cc");
            else
                __asm__ volatile("cld; 1: stosb; incl %%eax; decl %%ecx; jnz 1b"
                                 : "+D"(va), "+c"(count), "+a"(value) : : "memory", "cc");
        } else if (req->width == 2) {
            if (!block->increment)
                __asm__ volatile("cld; rep stosw"
                                 : "+D"(va), "+c"(count) : "a"(value) : "memory", "cc");
            else
                __asm__ volatile("cld; 1: stosw; incl %%eax; decl %%ecx; jnz 1b"
                                 : "+D"(va), "+c"(count), "+a"(value) : : "memory", "cc");
        } else if (!block->increment) {
            __asm__ volatile("cld; rep stosl"
                             : "+D"(va), "+c"(count) : "a"(value) : "memory", "cc");
        } else {
            __asm__ volatile("cld; 1: stosl; incl %%eax; decl %%ecx; jnz 1b"
                             : "+D"(va), "+c"(count), "+a"(value) : : "memory", "cc");
        }
        status = HW_OK;
    }
    if (recover_window_cleanup(&window)) status = HW_WINDOW;
    return status;
}

static inline u32 platform_hwb_io(struct hw_req *req)
{
    if (req->action == HW_READ) {
        if (req->width == 1) req->value = inp(req->addr);
        else if (req->width == 2) req->value = inpw(req->addr);
        else req->value = inpd(req->addr);
    } else {
        if (req->width == 1) outp(req->addr, req->value);
        else if (req->width == 2) outpw(req->addr, req->value);
        else outpd(req->addr, req->value);
    }
    return HW_OK;
}

#endif
