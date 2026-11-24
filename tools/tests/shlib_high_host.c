/* ========================================================================
 * shlib_high_host.c — 高位 shlib の非連続 backing と AS ごとの attach。
 * 実行: python3 -B tools/tests/test_shlib_high.py [--mutate]
 * 実物の paging / pgalloc / physmem / shlib を ILP32 で組む。
 * 読み込み・確保・attach の途中失敗で私有 PTE と owner のページが戻ること、
 * text が RO+USER、data が AS ごとの RW+USER であることを検査する。
 * 共有 PT の通常 map は拒否し、旧 BB の恒等 USER 昇格は使わない (e11b2)。
 * GNU11 ([C1])、libc は使わない (-nostdlib)。
 * ======================================================================== */
#include "types.h"
static u32 host_cr3;
/* (e): paging.c が返す物理を数える口。pgalloc.h の宣言も同じ名に変わるので、
 * paging.c の呼び出しは全部この関数へ来る (本物へ転送する)。 */
#define pgalloc_free_n_owner host_free_hook
#include "paging_host_source.c"
#undef pgalloc_free_n_owner
/* Layout symbols are supplied by kernel_layout.linker_flags(). */
static void die(int code)
{
    __asm__ volatile("int $0x80" : : "a"(1), "b"(code));
    for (;;) {}
}
static void report(const char *text, u32 len)
{
    __asm__ volatile("int $0x80" : : "a"(4), "b"(1), "c"(text), "d"(len) : "memory");
}
#define SAY(s) report(s "\n", sizeof(s "\n") - 1)
#define CHECK(x) do { if (!(x)) { SAY("FAIL: " #x); die(1); } } while (0)
#include "pgalloc_host_source.c"
#define HOST_POOL_IRQ_SAVE() 0U
#define HOST_POOL_IRQ_RESTORE(f) ((void)(f))
#include "pgalloc_host_fixture.h"
void __cdecl kprintf(u8 attr, const char *fmt, ...) { (void)attr; (void)fmt; }

/* 試験が決める「機械の姿」: 割り当ててよい物理の上限と、いまの BB。 */

static u32 t_bb_base, t_bb_size;
static u32 bb_free_attempts;     /* (e) BB の物理を返そうとした回数 */


void gfx_bb_phys_range(u32 *base, u32 *size);
void gfx_bb_phys_range(u32 *base, u32 *size)
{
    if (base) *base = t_bb_base;
    if (size) *size = t_bb_size;
}
int host_free_hook(u32 owner, u32 pfn, int n)
{
    u32 phys = pfn * PAGE_SIZE;
    if (t_bb_size && phys < t_bb_base + t_bb_size &&
        phys + (u32)n * PAGE_SIZE > t_bb_base)
        bb_free_attempts++;
    return pgalloc_free_n_owner(owner, pfn, n);
}

/* exec_teardown_app が引く。この試験は shlib を載せない。 */
#include "appslot.h"


#include "kmalloc.h"
#include "vfs.h"
#include "os32_kapi_shared.h"

void *kmemset(void *dst, int c, u32 n) { u8 *p = dst; while (n--) *p++ = (u8)c; return dst; }
void *kmemcpy(void *dst, const void *src, u32 n) { u8 *p = dst; const u8 *q = src; while (n--) *p++ = *q++; return dst; }

static u8 file_bytes[OS32X_HDR_SIZE + 4 * PAGE_SIZE];
static u32 cursor, fail_alloc, alloc_calls, fail_read, read_calls;
/* Two cooperative ILP32 stacks interleave at VFS open and loader wait. */
extern void switch_stack(u32 **save, u32 *resume);
__asm__(".text\n"
        "switch_stack: push %ebp; push %ebx; push %esi; push %edi\n"
        "mov 20(%esp), %eax; mov %esp, (%eax); mov 24(%esp), %esp\n"
        "pop %edi; pop %esi; pop %ebx; pop %ebp; ret");
static u32 waiter_stack[4096] __attribute__((aligned(16)));
static u32 *loader_sp, *waiter_sp;
static unsigned interleave, publishes, opens;
static int waiter_result, waiter_id;

int vfs_open(const char *path, int flags)
{
    (void)path; (void)flags;
    opens++;
    if (interleave) {
        interleave = 0;
        switch_stack(&loader_sp, waiter_sp);
    }
    cursor = 0;
    return 1;
}
void vfs_close(int fd) { (void)fd; }
int vfs_read_fd(int fd, void *dst, u32 len) {
    (void)fd;
    if (++read_calls == fail_read || (u32)len > sizeof(file_bytes) - cursor) return -1;
    kmemcpy(dst, file_bytes + cursor, len); cursor += len; return len;
}
static u32 fail_alloc_phys(u32 owner, int n) {
    if (++alloc_calls == fail_alloc) return 0;
    return pgalloc_alloc_phys(owner, n);
}
void idle_work_run(void) { switch_stack(&waiter_sp, loader_sp); }
static void *track_copy(void *dst, const void *src, u32 bytes);
#define kmemcpy track_copy
#define pgalloc_alloc_phys fail_alloc_phys
#include "shlib_host_source.c"
#undef pgalloc_alloc_phys
#undef kmemcpy
static void *track_copy(void *dst, const void *src, u32 bytes)
{
    if (dst == &sound) publishes++;
    return kmemcpy(dst, src, bytes);
}
static int cancel_waiter;
static int pending(void) { return cancel_waiter; }
static void waiter(void)
{
    waiter_result = shlib_sound_acquire(pending);
    if (!waiter_result) waiter_id = appslot_start_reserve(0);
    switch_stack(&waiter_sp, loader_sp);
    CHECK(0);
}
static void prepare_waiter(void)
{
    waiter_sp = waiter_stack + 4096 - 6;
    waiter_sp[4] = (u32)waiter;
    interleave = 1;
    waiter_result = 99;
    waiter_id = 0;
}
static void make_file(void) {
    OS32Header *oh = (OS32Header *)file_bytes;
    OS32ShlibHeader *sh = (OS32ShlibHeader *)(file_bytes + OS32X_HDR_SIZE);
    kmemset(file_bytes, 0, sizeof(file_bytes));
    oh->magic = OS32X_MAGIC; oh->version = OS32X_HDR_VERSION; oh->header_size = OS32X_HDR_SIZE;
    oh->flags = OS32X_FLAG_SHLIB; oh->load_addr = MEM_SHLIB_BASE; oh->text_size = 4 * PAGE_SIZE;
    oh->min_api_ver = KAPI_VERSION; oh->kapi_data_off = KAPI_DATA_FIELDS_OFF;
    oh->kapi_abi_generation = OS32_KAPI_ABI_GENERATION; oh->memory_layout_generation = OS32_MEMORY_LAYOUT_GENERATION;
    oh->shlib_protocol = OS32_SHLIB_PROTOCOL;
    oh->code_size = 2 * PAGE_SIZE;
    sh->magic = OS32_SHLIB_MAGIC; sh->version = 1; sh->nfunc = 1; sh->text_pages = 2;
    sh->data_vaddr = MEM_SHLIB_BASE + 2 * PAGE_SIZE; sh->data_pages = 2; sh->_rsvd[0] = OS32_SHLIB_PROTOCOL;
    ((u32 *)sh)[OS32_SHLIB_ENTRY_OFF / 4] = MEM_SHLIB_BASE + PAGE_SIZE;
    file_bytes[OS32X_HDR_SIZE + 2 * PAGE_SIZE] = 0xAB;
    file_bytes[OS32X_HDR_SIZE + 3 * PAGE_SIZE] = 0xCD;
}
static u32 pte(struct addrspace *as, u32 va) {
    u32 pt = as->app_pt_phys[(va >> 22) - as->app_pde];
    return pt ? ((u32 *)P2V(pt))[(va >> 12) & 1023] : 0;
}
/* Reboot just the immutable image pool between independent boot cases. */
static void reset_image(struct shlib_image *image)
{
    for (u32 i = 0; i < image->text_pages + image->data_pages; i++) {
        CHECK(pgalloc_free_n_owner(LEDGER_OWNER_SHLIB, image->pages[i] / PAGE_SIZE, 1));
        image->pages[i] = 0;
    }
    image->loaded = image->text_pages = image->data_pages = 0;
}

static void make_sound_file(void)
{
    make_file();
    OS32Header *header = (OS32Header *)file_bytes;
    OS32ShlibHeader *sh = (OS32ShlibHeader *)(file_bytes + OS32X_HDR_SIZE);
    header->load_addr = MEM_SHLIB_SND_BASE;
    sh->data_vaddr = MEM_SHLIB_SND_BASE + 2 * PAGE_SIZE;
    sh->nfunc = 10;
    for (unsigned i = 0; i < sh->nfunc; i++)
        ((u32 *)(sh + 1))[i] = MEM_SHLIB_SND_BASE + PAGE_SIZE;
}

static int cancel_after_page(void) { return read_calls >= 2; }

static void coexistence_cases(void)
{
    static const u32 lengths[] = {127, 128, 129, 256};
    u32 start = used_pages;
    for (unsigned i = 0; i < sizeof(lengths) / sizeof(lengths[0]); i++) {
        u32 pages = lengths[i], owner;
        struct addrspace as;
        make_file();
        ((OS32Header *)file_bytes)->bss_size = (pages - 4) * PAGE_SIZE;
        OS32ShlibHeader *sh = (OS32ShlibHeader *)(file_bytes + OS32X_HDR_SIZE);
        sh->data_pages = pages - sh->text_pages;
        CHECK(!shlib_init() && gui.text_pages + gui.data_pages == pages);
        CHECK(ledger_owner_new(LEDGER_KIND_AS, 2, "GUI", &owner));
        CHECK(!paging_addrspace_create_lease(&as, owner));
        CHECK(!shlib_addrspace_attach(&as));
        CHECK(pte(&as, gui.base + (pages - 1) * PAGE_SIZE) & PTE_RW);
        make_sound_file();
        unsigned prior_opens = opens;
        if (pages <= 128) {
            CHECK(!shlib_sound_acquire(0) && opens == prior_opens + 1);
            CHECK(!shlib_sound_attach(&as));
        } else {
            CHECK(shlib_sound_acquire(0) < 0 && opens == prior_opens);
        }
        shlib_addrspace_detach(&as);
        paging_addrspace_destroy(&as);
        CHECK(ledger_owner_retire(owner));
        reset_image(&sound);
        reset_image(&gui);
        CHECK(used_pages == start && !ledger_bad_free);
    }
}
void _start(void) {
    u32 args[6] = {KERNEL_LOAD_ADDR, 0x1000000 - KERNEL_LOAD_ADDR, 3, 0x32, 0xFFFFFFFF, 0};
    u32 result, before, i, owner, shpages;
    struct addrspace as;
    __asm__ volatile("int $0x80" : "=a"(result) : "a"(90), "b"(args) : "memory");
    CHECK(result == KERNEL_LOAD_ADDR); host_map_fixed_paging(); paging_init(8192); host_pool_boot(8192);
    for (i = MEM_SHM_RESV_START / PAGE_SIZE + 1; i < MEM_SHM_RESV_START / PAGE_SIZE + 32; i += 2)
        CHECK(ledger_claim_fixed(LEDGER_OWNER_BOOT, i, i + 1));
    before = used_pages; make_file();
    OS32Header *header = (OS32Header *)file_bytes;
    header->min_api_ver++;
    CHECK(shlib_init() == -1 && shlib_reject_reason() == SHLIB_REJECT_MIN_API);
    CHECK(shlib_reject_min_api() == header->min_api_ver && !alloc_calls);
    header->shlib_protocol++;
    CHECK(shlib_init() == -1 && shlib_reject_reason() == SHLIB_REJECT_LAYOUT);
    CHECK(!gui.loaded && !alloc_calls);
    make_file();
    for (i = 0; i < 4; i++) {
        static const u32 bad[] = {0, PAGE_SIZE - 1, 5 * PAGE_SIZE, PAGE_SIZE};
        ((OS32Header *)file_bytes)->code_size = bad[i];
        read_calls = alloc_calls = 0;
        CHECK(shlib_init() == -1 && !gui.loaded && used_pages == before);
        CHECK(!ledger_owner_pages(LEDGER_OWNER_SHLIB));
    }
    make_file();
    for (i = 1; i <= 4; i++) {
        fail_alloc = i; alloc_calls = read_calls = 0;
        CHECK(shlib_init() == -1); CHECK(!gui.loaded && used_pages == before);
        CHECK(!ledger_owner_pages(LEDGER_OWNER_SHLIB));
    }
    fail_alloc = 0;
    for (i = 1; i <= 5; i++) {
        fail_read = i; read_calls = alloc_calls = 0;
        CHECK(shlib_init() == -1); CHECK(!gui.loaded && used_pages == before);
        CHECK(!ledger_owner_pages(LEDGER_OWNER_SHLIB));
    }
    fail_read = 0; read_calls = alloc_calls = 0; CHECK(!shlib_init());
    CHECK(gui.pages[1] != gui.pages[0] + PAGE_SIZE); CHECK(!page_directory[MEM_SHLIB_BASE >> 22]);
    shpages = ledger_owner_pages(LEDGER_OWNER_SHLIB); CHECK(shpages == 4);
    for (i = 1; i <= 2; i++) {
        CHECK(ledger_owner_new(LEDGER_KIND_AS, 2, "attach", &owner));
        CHECK(!paging_addrspace_create_lease(&as, owner));
        fail_alloc = i; alloc_calls = 0;
        CHECK(shlib_addrspace_attach(&as) == -1);
        CHECK(!pte(&as, MEM_SHLIB_BASE)); CHECK(!pte(&as, gui.data_vaddr));
        CHECK(ledger_owner_pages(LEDGER_OWNER_SHLIB) == shpages);
        /* The empty image PT is released before AS destruction. */
        CHECK(ledger_owner_pages(owner) == 2);
        paging_addrspace_destroy(&as); CHECK(!ledger_owner_pages(owner)); CHECK(ledger_owner_retire(owner));
        CHECK(used_pages == before + shpages);
    }
    fail_alloc = 0; CHECK(ledger_owner_new(LEDGER_KIND_AS, 2, "attach", &owner));
    CHECK(!paging_addrspace_create_lease(&as, owner)); CHECK(!shlib_addrspace_attach(&as));
    CHECK(!(pte(&as, MEM_SHLIB_BASE) & PTE_RW));
    CHECK((pte(&as, MEM_SHLIB_BASE) & ~0xFFFUL) == gui.pages[0]);
    CHECK((pte(&as, gui.data_vaddr) & ~0xFFFUL) != gui.pages[2]);
    CHECK(*(u8 *)P2V(pte(&as, gui.data_vaddr) & ~0xFFFUL) == 0xAB);
    *(u8 *)P2V(pte(&as, gui.data_vaddr) & ~0xFFFUL) = 0xFF;
    CHECK(*(u8 *)P2V(gui.pages[2]) == 0xAB);
    shlib_addrspace_detach(&as); paging_addrspace_destroy(&as);
    CHECK(!ledger_owner_pages(owner) && ledger_owner_retire(owner));
    CHECK(used_pages == before + shpages && !ledger_bad_free);
    /* A failed sound read leaves GUI usable. The next attempt may retry. */
    make_file();
    CHECK(shlib_sound_acquire(0) < 0 && !sound.loaded && used_pages == before + shpages);
    header->load_addr = MEM_SHLIB_SND_BASE;
    OS32ShlibHeader *sh = (OS32ShlibHeader *)(file_bytes + OS32X_HDR_SIZE);
    sh->data_vaddr = MEM_SHLIB_SND_BASE + 2 * PAGE_SIZE;
    ((u32 *)(sh + 1))[0] = MEM_SHLIB_SND_BASE + PAGE_SIZE;
    CHECK(shlib_sound_acquire(0) < 0 && used_pages == before + shpages);
    sh->nfunc = 10;
    for (i = 0; i < sh->nfunc; i++)
        ((u32 *)(sh + 1))[i] = MEM_SHLIB_SND_BASE + PAGE_SIZE;
    header->bss_size = MEM_SHLIB_END - MEM_SHLIB_SND_BASE;
    CHECK(shlib_sound_acquire(0) < 0 && !sound.loaded);
    header->bss_size = 0;
    unsigned opens_before = opens;
    prepare_waiter();
    fail_read = 2; read_calls = 0;
    CHECK(shlib_sound_acquire(0) < 0 && !sound.loaded && !publishes);
    CHECK(opens == opens_before + 1 && waiter_result == 99);
    /* Retry before resuming the participant in the failed attempt. */
    fail_read = 0;
    CHECK(!shlib_sound_acquire(0) && sound.loaded && publishes == 1);
    switch_stack(&loader_sp, waiter_sp);
    CHECK(waiter_result < 0 && opens == opens_before + 2 && publishes == 1);
    /* A fresh boot also exercises two participants in a successful attempt. */
    reset_image(&sound);
    prepare_waiter();
    CHECK(!shlib_sound_acquire(0) && publishes == 2);
    int loader_id = appslot_start_reserve(0);
    switch_stack(&loader_sp, waiter_sp);
    CHECK(!waiter_result && opens == opens_before + 3 && publishes == 2);
    CHECK(loader_id >= APP_ID_MIN && waiter_id >= APP_ID_MIN && waiter_id != loader_id);
    appslot_start_release(loader_id);
    CHECK(appslot_alloc_id() == loader_id);
    appslot_start_release(waiter_id);
    struct addrspace second;
    u32 owner2;
    CHECK(ledger_owner_new(LEDGER_KIND_AS, 2, "sound", &owner));
    CHECK(!paging_addrspace_create_lease(&as, owner));
    fail_alloc = 2; alloc_calls = 0;
    CHECK(shlib_sound_attach(&as) < 0 && ledger_owner_pages(owner) == 2);
    fail_alloc = 0;
    CHECK(!shlib_sound_attach(&as));
    CHECK(ledger_owner_new(LEDGER_KIND_AS, 3, "sound2", &owner2));
    CHECK(!paging_addrspace_create_lease(&second, owner2));
    CHECK(!shlib_sound_attach(&second));
    CHECK(pte(&as, sound.base) == pte(&second, sound.base));
    CHECK(shlib_read_page(sound.base, pte(&as, sound.base) & PTE_FRAME_MASK));
    CHECK(!(pte(&as, sound.base) & PTE_RW));
    CHECK((pte(&as, sound.data_vaddr) & PTE_RW) &&
          pte(&as, sound.data_vaddr) != pte(&second, sound.data_vaddr));
    shlib_addrspace_detach(&as); paging_addrspace_destroy(&as);
    CHECK(pte(&second, sound.data_vaddr) & PTE_RW);
    shlib_addrspace_detach(&second); paging_addrspace_destroy(&second);
    CHECK(ledger_owner_retire(owner) && ledger_owner_retire(owner2));
    reset_image(&gui);
    reset_image(&sound);
    CHECK(used_pages == before);
    make_sound_file();
    read_calls = 0;
    CHECK(shlib_sound_acquire(cancel_after_page) < 0 && used_pages == before);
    prepare_waiter();
    cancel_waiter = 1;
    CHECK(!shlib_sound_acquire(0) && waiter_result < 0 && !waiter_id && !sound_waiters);
    cancel_waiter = 0;
    reset_image(&sound);
    coexistence_cases();
    SAY("shlib_high: PASS fragmented originals, private copy, every allocation/read failure and owner0"); die(0);
}

/* Paging-only fixture has no exec slots; abort delivery is appmem_map_host. */
void exec_addrspace_abort(struct addrspace *as) { (void)as; }
