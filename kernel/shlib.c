/* T2c: noncontiguous shlib originals; only private AS aliases are USER. */
#include "shlib.h"
#include "config.h"
#include "memmap.h"
#include "kprintf.h"
#include "kstring.h"
#include "paging.h"
#include "pgalloc.h"
#include "vfs.h"
#include "os32_kapi_shared.h"
#include "os32x_hdr.h"

static u32 g_pages[MEM_SHLIB_SIZE / PAGE_SIZE];
static u32 g_loaded, g_version, g_text_pages, g_data_vaddr, g_data_pages;
static int g_reject;
static u32 g_reject_min_api;

int shlib_init(void)
{
    OS32Header oh;
    OS32ShlibHeader *sh;
    u32 pages, i, size;
    int fd;
    if (g_loaded) return 0;
    fd = vfs_open(SYS_SHLIB_GUI, 0);
    if (fd < 0) return -1;
    if (vfs_read_fd(fd, &oh, sizeof(oh)) != sizeof(oh)) goto fail;
    if (oh.min_api_ver > KAPI_VERSION) {
        g_reject = SHLIB_REJECT_MIN_API;
        g_reject_min_api = oh.min_api_ver;
        goto fail;
    }
    if (os32x_layout_check(&oh, sizeof(oh), KAPI_DATA_FIELDS_OFF)) {
        g_reject = SHLIB_REJECT_LAYOUT;
        goto fail;
    }
    if (oh.flags != OS32X_FLAG_SHLIB || oh.load_addr != MEM_SHLIB_BASE ||
        oh.entry_offset != 0 || oh.text_size < PAGE_SIZE ||
        oh.text_size > MEM_SHLIB_SIZE || oh.bss_size > MEM_SHLIB_SIZE - oh.text_size)
        goto fail;
    size = oh.text_size + oh.bss_size;
    pages = PAGE_ALIGN_UP(size) / PAGE_SIZE;
    for (i = 0; i < pages; i++) {
        u32 n = 0;
        g_pages[i] = pgalloc_alloc_phys(LEDGER_OWNER_SHLIB, 1);
        if (!g_pages[i]) goto fail;
        kmemset(P2V(g_pages[i]), 0, PAGE_SIZE);
        if (i * PAGE_SIZE < oh.text_size) {
            n = oh.text_size - i * PAGE_SIZE;
            if (n > PAGE_SIZE) n = PAGE_SIZE;
        }
        if (n && vfs_read_fd(fd, P2V(g_pages[i]), n) != (int)n) goto fail;
    }
    sh = P2V(g_pages[0]);
    if (sh->magic != OS32_SHLIB_MAGIC || sh->_rsvd[0] != oh.shlib_protocol ||
        sh->nfunc > OS32_SHLIB_MAX_FUNC || !sh->text_pages ||
        sh->text_pages > pages || sh->data_vaddr != MEM_SHLIB_BASE + sh->text_pages * PAGE_SIZE ||
        sh->data_pages > pages - sh->text_pages ||
        sh->text_pages + sh->data_pages != pages) goto fail;
    for (i = 0; i < sh->nfunc; i++) {
        u32 entry = ((u32 *)sh)[OS32_SHLIB_ENTRY_OFF / sizeof(u32) + i];
        if (entry < MEM_SHLIB_BASE + PAGE_SIZE ||
            entry >= MEM_SHLIB_BASE + sh->text_pages * PAGE_SIZE) goto fail;
    }
    g_version = sh->version;
    g_text_pages = sh->text_pages;
    g_data_pages = sh->data_pages;
    g_data_vaddr = sh->data_vaddr;
    g_loaded = 1;
    vfs_close(fd);
    return 0;
fail:
    vfs_close(fd);
    for (i = 0; i < MEM_SHLIB_SIZE / PAGE_SIZE; i++) {
        if (g_pages[i]) pgalloc_free_n_owner(LEDGER_OWNER_SHLIB, g_pages[i] / PAGE_SIZE, 1);
        g_pages[i] = 0;
    }
    return -1;
}

int shlib_loaded(void) { return g_loaded; }
int shlib_reject_reason(void) { return g_reject; }
u32 shlib_reject_min_api(void) { return g_reject_min_api; }
u32 shlib_version(void) { return g_loaded ? g_version : 0; }
u32 shlib_text_end(void) { return MEM_SHLIB_BASE + g_text_pages * PAGE_SIZE; }
u32 shlib_data_pages(void) { return g_loaded ? g_data_pages : 0; }

int shlib_read_page(u32 va, u32 frame)
{
    u32 page = (va - MEM_SHLIB_BASE) / PAGE_SIZE;
    return g_loaded && va >= MEM_SHLIB_BASE && page < g_text_pages &&
           g_pages[page] == frame &&
           pgalloc_page_owned(frame / PAGE_SIZE, LEDGER_OWNER_SHLIB);
}

int shlib_addrspace_attach(struct addrspace *as)
{
    u32 i, phys;
    if (!g_loaded) return 0;
    if (!as || paging_current_cr3() != paging_kernel_pd_phys()) return -1;
    for (i = 0; i < g_text_pages; i++)
        if (paging_addrspace_map_user(as, MEM_SHLIB_BASE + i * PAGE_SIZE,
                                     g_pages[i], PAGE_RO | PTE_USER)) goto fail;
    for (i = 0; i < g_data_pages; i++) {
        phys = pgalloc_alloc_phys(as->owner, 1);
        if (!phys) goto fail;
        kmemcpy(P2V(phys), P2V(g_pages[g_text_pages + i]), PAGE_SIZE);
        if (paging_addrspace_map_user(as, g_data_vaddr + i * PAGE_SIZE,
                                     phys, PAGE_RW | PTE_USER)) {
            pgalloc_free_n_owner(as->owner, phys / PAGE_SIZE, 1);
            goto fail;
        }
    }
    return 0;
fail:
    shlib_addrspace_detach(as);
    return -1;
}

void shlib_addrspace_detach(struct addrspace *as)
{
    if (!g_loaded || !as || paging_current_cr3() != paging_kernel_pd_phys()) return;
    /* master isolates the old translation before freeing AS-owned data;
     * owner checks keep the shared RO originals alive. */
    if (as->app_pt_phys[0]) {
        u32 i;
        for (i = 0; i < g_text_pages; i++)
            paging_addrspace_map_user(as, MEM_SHLIB_BASE + i * PAGE_SIZE, 0, 0);
    }
    paging_addrspace_free_user_range(as, g_data_vaddr,
                                    g_data_vaddr + g_data_pages * PAGE_SIZE);
}
