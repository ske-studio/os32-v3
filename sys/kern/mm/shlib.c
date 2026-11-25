/* T2c: noncontiguous shlib originals; only private AS aliases are USER. */
#include "shlib.h"
#include "config.h"
#include "io.h"
#include "idle_work.h"
#include "memmap.h"
#include "kprintf.h"
#include "kstring.h"
#include "paging_app.h"
#include "pgalloc.h"
#include "vfs.h"
#include "os32_kapi_shared.h"
#include "os32x_hdr.h"
#include "os32_sndl_table.h"

struct shlib_image {
    u32 base, limit;
    const char *path;
    u32 *pages;
    u32 loaded, text_pages, data_vaddr, data_pages;
    int reject;
    u32 reject_min_api;
};

static u32 gui_frames[MEM_SHLIB_SIZE / PAGE_SIZE];
static u32 sound_frames[(MEM_SHLIB_END - MEM_SHLIB_SND_BASE) / PAGE_SIZE];

static struct shlib_image gui = {
    .base = MEM_SHLIB_BASE, .limit = MEM_SHLIB_END, .path = SYS_SHLIB_GUI,
    .pages = gui_frames
};
static struct shlib_image sound = {
    .base = MEM_SHLIB_SND_BASE, .limit = MEM_SHLIB_END, .path = SYS_SHLIB_SND,
    .pages = sound_frames
};

static int load_image(struct shlib_image *image, int (*cancelled)(void))
{
    OS32Header oh;
    u8 wire[OS32X_HDR_SIZE];
    OS32ShlibHeader *sh;
    u32 pages, i, size;
    int fd, error = EXEC_ERR_INVALID;
    u32 capacity = image->limit - image->base;
    if (cancelled && cancelled()) return EXEC_ERR_GENERAL;
    if (image->loaded) return 0;
    fd = vfs_open(image->path, 0);
    if (fd < 0) return EXEC_ERR_NOT_FOUND;
    if (vfs_read_fd(fd, wire, sizeof(wire)) != sizeof(wire)) goto fail;
    if (os32x_decode(&oh, wire, sizeof(wire)) != OS32X_LAYOUT_OK) goto fail;
    int layout = os32x_layout_check(&oh, sizeof(oh), KAPI_DATA_FIELDS_OFF);
    if (layout == OS32X_LAYOUT_MIN_API) {
        image->reject = SHLIB_REJECT_MIN_API;
        image->reject_min_api = oh.min_api_ver;
        goto fail;
    }
    if (layout != OS32X_LAYOUT_OK) {
        image->reject = SHLIB_REJECT_LAYOUT;
        goto fail;
    }
    if (oh.flags != OS32X_FLAG_SHLIB || oh.load_addr != image->base ||
        oh.entry_offset != 0 || oh.text_size < PAGE_SIZE ||
        !oh.code_size || (oh.code_size & (PAGE_SIZE - 1)) || oh.code_size > oh.text_size ||
        oh.text_size > capacity || oh.bss_size > capacity - oh.text_size)
        goto fail;
    size = oh.text_size + oh.bss_size;
    pages = PAGE_ALIGN_UP(size) / PAGE_SIZE;
    for (i = 0; i < pages; i++) {
        u32 n = 0;
        if (cancelled && cancelled()) goto fail;
        image->pages[i] = pgalloc_alloc_phys(LEDGER_OWNER_SHLIB, 1);
        if (!image->pages[i]) {
            error = EXEC_ERR_NOMEM;
            goto fail;
        }
        kmemset(P2V(image->pages[i]), 0, PAGE_SIZE);
        if (i * PAGE_SIZE < oh.text_size) {
            n = oh.text_size - i * PAGE_SIZE;
            if (n > PAGE_SIZE) n = PAGE_SIZE;
        }
        if (n && vfs_read_fd(fd, P2V(image->pages[i]), n) != (int)n) goto fail;
    }
    sh = P2V(image->pages[0]);
    if ((image->base == MEM_SHLIB_SND_BASE && (sh->version != SNDL_TABLE_VERSION || sh->nfunc < SNDL_V1_MIN_ENTRIES)) ||
        sh->magic != OS32_SHLIB_MAGIC || sh->_rsvd[0] != oh.shlib_protocol ||
        sh->nfunc > OS32_SHLIB_MAX_FUNC || !sh->text_pages ||
        sh->text_pages > pages || oh.code_size != sh->text_pages * PAGE_SIZE ||
        sh->data_vaddr != image->base + sh->text_pages * PAGE_SIZE ||
        sh->data_pages > pages - sh->text_pages ||
        sh->text_pages + sh->data_pages != pages) goto fail;
    for (i = 0; i < sh->nfunc; i++) {
        u32 entry = ((u32 *)sh)[OS32_SHLIB_ENTRY_OFF / sizeof(u32) + i];
        if (entry < image->base + PAGE_SIZE ||
            entry >= image->base + sh->text_pages * PAGE_SIZE) goto fail;
    }
    if (cancelled && cancelled()) goto fail;
    image->text_pages = sh->text_pages;
    image->data_pages = sh->data_pages;
    image->data_vaddr = sh->data_vaddr;
    image->loaded = 1;
    vfs_close(fd);
    return 0;
fail:
    vfs_close(fd);
    for (i = 0; i < capacity / PAGE_SIZE; i++) {
        if (image->pages[i]) pgalloc_free_n_owner(LEDGER_OWNER_SHLIB, image->pages[i] / PAGE_SIZE, 1);
        image->pages[i] = 0;
    }
    return error;
}

int shlib_init(void) { return load_image(&gui, 0) ? -1 : 0; }
int shlib_loaded(void) { return gui.loaded; }
int shlib_reject_reason(void) { return gui.reject; }
u32 shlib_reject_min_api(void) { return gui.reject_min_api; }
u32 shlib_data_pages(void) { return gui.data_pages; }
u32 shlib_sound_data_pages(void)
{
    return sound.data_pages;
}

/* Each waiter retains the result of the attempt it joined, even if a
 * subsequent caller has already retried. Only the loader opens the VFS file. */
struct sound_waiter {
    struct sound_waiter *next;
    volatile int pending;
    int result;
};
static struct sound_waiter *sound_waiters;
static int sound_loading;

int shlib_sound_acquire(int (*cancelled)(void))
{
    int can_sleep = _irq_enabled();
    unsigned irq = irq_save();
    if (gui.text_pages + gui.data_pages > (sound.base - gui.base) / PAGE_SIZE) {
        irq_restore(irq);
        return OS32_ERR_BUSY;
    }
    if (sound_loading) {
        if (!can_sleep) {
            irq_restore(irq);
            return -1;
        }
        struct sound_waiter waiter = {sound_waiters, 1, -1};
        sound_waiters = &waiter;
        irq_restore(irq);
        while (waiter.pending) {
            if (cancelled && cancelled()) {
                irq = irq_save();
                if (waiter.pending) {
                    struct sound_waiter **link = &sound_waiters;
                    while (*link != &waiter) link = &(*link)->next;
                    *link = waiter.next;
                    irq_restore(irq);
                    return -1;
                }
                irq_restore(irq);
                break;
            }
            idle_work_run();
            irq = irq_save();
            /* Test under exclusion, then enable interrupts and sleep in one
             * operation. Publication before this point suppresses the nap. */
            if (waiter.pending) _idle();
            irq_restore(irq);
        }
        return waiter.result;
    }
    if (sound.loaded) {
        irq_restore(irq);
        return 0;
    }
    sound_loading = 1;
    irq_restore(irq);
    struct shlib_image staging = {
        .base = sound.base, .limit = sound.limit, .path = sound.path, .pages = sound_frames
    };
    int rc = load_image(&staging, cancelled);
    irq = irq_save();
    if (!rc) kmemcpy(&sound, &staging, sizeof(sound));
    while (sound_waiters) {
        struct sound_waiter *w = sound_waiters;
        sound_waiters = w->next;
        w->result = rc;
        w->pending = 0;
    }
    sound_loading = 0;
    irq_restore(irq);
    return rc;
}

static int read_page(const struct shlib_image *image, u32 va, u32 frame)
{
    u32 page = (va - image->base) / PAGE_SIZE;
    return image->loaded && va >= image->base && page < image->text_pages &&
           image->pages[page] == frame &&
           pgalloc_page_owned(frame / PAGE_SIZE, LEDGER_OWNER_SHLIB);
}

int shlib_read_page(u32 va, u32 frame)
{
    return read_page(&gui, va, frame) || read_page(&sound, va, frame);
}

static int attach_image(struct addrspace *as, const struct shlib_image *image)
{
    u32 i, phys;
    if (!image->loaded) return 0;
    if (!as || paging_current_cr3() != paging_kernel_pd_phys()) return -1;
    for (i = 0; i < image->text_pages; i++)
        if (paging_addrspace_map_user(as, image->base + i * PAGE_SIZE,
                                     image->pages[i], PAGE_RO | PTE_USER)) return -1;
    for (i = 0; i < image->data_pages; i++) {
        phys = pgalloc_alloc_phys(as->owner, 1);
        if (!phys) return -1;
        kmemcpy(P2V(phys), P2V(image->pages[image->text_pages + i]), PAGE_SIZE);
        if (paging_addrspace_map_user(as, image->data_vaddr + i * PAGE_SIZE,
                                     phys, PAGE_RW | PTE_USER)) {
            pgalloc_free_n_owner(as->owner, phys / PAGE_SIZE, 1);
            return -1;
        }
    }
    return 0;
}

static void detach_image(struct addrspace *as, const struct shlib_image *image)
{
    if (!image->loaded || !as || paging_current_cr3() != paging_kernel_pd_phys()) return;
    /* master isolates the old translation before freeing AS-owned data;
     * owner checks keep the shared RO originals alive. */
    if (as->app_pt_phys[0]) {
        u32 i;
        for (i = 0; i < image->text_pages; i++)
            paging_addrspace_map_user(as, image->base + i * PAGE_SIZE, 0, 0);
    }
    paging_addrspace_free_user_range(as, image->data_vaddr,
                                    image->data_vaddr + image->data_pages * PAGE_SIZE);
    paging_addrspace_drop_empty_pt(as, image->base);
}

void shlib_addrspace_detach(struct addrspace *as)
{
    u32 header_frame;
    /* The sound header is installed first and is immutable. Its exact
     * backing identifies the attached set without changing the AS ABI. */
    if (as && sound.loaded && !as_va_to_pa_read(as->pd_phys, sound.base, &header_frame) &&
        header_frame == sound.pages[0]) detach_image(as, &sound);
    detach_image(as, &gui);
}

int shlib_addrspace_attach(struct addrspace *as)
{
    if (!attach_image(as, &gui)) return 0;
    shlib_addrspace_detach(as);
    return -1;
}

int shlib_sound_attach(struct addrspace *as)
{
    if (!sound.loaded || !as) return -1;
    if (!attach_image(as, &sound)) return 0;
    detach_image(as, &sound);
    return -1;
}
