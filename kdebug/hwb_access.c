/* HW_BASIC_DESIGN §3: one access, no device policy or implicit readback. */
#include "hwb_wire.h"
#include "os32_kapi_shared.h"
#include "drivers/pci/pci_hwb.h"
#include "exec/exec_hwb.h"
#include "kdebug_window.h"
#include "memmap.h"
#include "io.h"
#include "generated/kdebug_import_macros.h"

static u32 hwb_phys(struct hw_req *req)
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

static void hwb_io(struct hw_req *req)
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
}

i32 hwb_access(void *buf, u32 size)
{
    struct hw_req *req = buf;
    if (!req || size != HW_REQ_BYTES || req->version != HW_VERSION)
        return OS32_ERR_INVAL;
    u32 status = req->status, value = req->value;
    if (req->action == HW_READ) req->value = 0;
    req->status = HW_ARG;
    if (status) return 0;
    if (req->action == HW_POINT) {
        if (req->space || req->width || req->bdf || req->addr || value) return 0;
        req->value = exec_hwb_abort_take();
        req->status = HW_OK;
        return 0;
    }
    if ((req->action != HW_READ && req->action != HW_WRITE) ||
        (req->action == HW_READ && value) ||
        (req->width != 1 && req->width != 2 && req->width != 4) ||
        (req->space != HW_PCI && req->space != HW_IO && req->space != HW_PHYS) ||
        (req->space != HW_PCI && req->bdf) ||
        (req->action == HW_WRITE && req->width < 4 &&
         value >> (req->width * BITS_PER_BYTE))) return 0;
    if (req->space == HW_PCI) {
        if (req->bdf > HW_BDF_MASK || req->addr >= HW_PCI_CONFIG_BYTES ||
            req->width > HW_PCI_CONFIG_BYTES - req->addr ||
            (req->addr & (req->width - 1U))) return 0;
        req->status = req->action == HW_READ ?
            pci_hwb_read(req->bdf, req->addr, req->width, &req->value) :
            pci_hwb_write(req->bdf, req->addr, req->width, value);
    } else if (req->space == HW_IO) {
        if (req->addr > U16_MAX || req->width - 1U > U16_MAX - req->addr) return 0;
        hwb_io(req);
        req->status = HW_OK;
    } else {
        if ((req->addr & (req->width - 1U)) ||
            req->width - 1U > U32_MAX - req->addr) return 0;
        req->status = hwb_phys(req);
    }
    if (req->action == HW_READ) {
        if (req->status != HW_OK) req->value = 0;
        else if (req->width == 1) req->value &= U8_MAX;
        else if (req->width == 2) req->value &= U16_MAX;
    }
    return 0;
}
