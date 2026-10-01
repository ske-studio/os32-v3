/* ========================================================================
 *  app_bb_overlap_host.c — 共有 BB と CPL=3 アプリの私有領域が重ならないこと
 *
 *  実行: python3 -B tools/tests/test_app_bb_overlap.py [--mutate]
 *
 *  2026-09-30 の後退 (8MB + PEGC、PM が NP21/W で観測): CPL=3 アプリを 1 本
 *  起動して終了するたびに pgalloc の used_pages が 74 ずつ増え、4 本目で
 *  NOMEM・v86 -t は 159 ページの連続が取れずに失敗した。原因は exec が私有
 *  領域 (スタック / exec_heap) を帯の上端 0x800000 まで張ってから、PEGC の
 *  BB [0x7B5000, 0x800000) を恒等で重ねて写していたこと — 私有ページ 74 枚
 *  の PTE が BB の物理で上書きされ、teardown で戻らない。
 *
 *  修正 (exec/exec.c ring3_band_set): 私有領域の上端を帯の上端と
 *  sys_usable_mem_end() の低い方にする。BB は従来どおり恒等のまま丸ごと写す
 *  (gfx_get_framebuffer / pegc_init が同じ番地を使う) ので、カーネル・アプリ
 *  の BB ポインタは変わらない。
 *
 *  実物の kernel/paging.c + kernel/pgalloc.c + kernel/physmem.c を ILP32 で
 *  そのまま組み、exec/exec.c から切り出した本物の ring3_band_set /
 *  app_map_region / exec_bb_overlaps_user / exec_map_shared_bb /
 *  exec_teardown_app (test_app_bb_overlap.py が生成する exec_bb_overlap.inc)
 *  で「起動 → 終了」をまわす。
 *
 *  見るもの:
 *    (a) 8MB + PEGC: 起動・終了 10 回で used_pages が毎回戻り、V86 の backing
 *        (V86_BACKING_PAGES = 159 の連続) が取れる
 *    (b) 私有 PTE (スタック / exec_heap) が BB の物理を指さない・恒等でない
 *    (c) BB の仮想番地の PTE が BB の物理を恒等 + USER で指す (カーネル・
 *        アプリの BB ポインタが BB に届く)
 *    (d) 私有領域の上端 (band_top = RING3_USTACK_TOP) が BB の下にあり、
 *        PDE の所有範囲 (帯 1 枚) は変わらない
 *    (e) teardown が BB の物理を pgalloc へ返そうとしない (paging.c の
 *        pgalloc_free_n_owner 呼び出しを数える)。T1b 以後は teardown の最後の
 *        ledger_reclaim_owner(AS) が取り残しを回収するので、used_pages が戻る
 *        だけでは漏れが無い証拠にならない — exec_as_leftover_pages (回収で
 *        返った取り残し) と ledger_bad_free (他 owner のページ = BB を返そうと
 *        して断られた回数) が増えないことも見る
 *    (f) BB が私有領域と重なる形 (上端が下がっていない) は起動を断り、
 *        私有 PTE を触らない
 *    (g) 17MB (BB が帯の上) / 12MB (帯の上、PDE 2) / 9801 planar (0x6A000、
 *        帯の下) / Cirrus (デバイス窓、PCD 付き) では私有領域の上端が従来の
 *        0x800000 のままで、BB は共有 PT に USER で写り、PCD を保つ
 *
 *  C89 ([C1])。libc は使わない (-nostdlib で直接走る)。
 * ======================================================================== */
#include "types.h"
static u32 host_cr3;
/* (e): paging.c が返す物理を数える口。pgalloc.h の宣言も同じ名に変わるので、
 * paging.c の呼び出しは全部この関数へ来る (本物へ転送する)。 */
#define pgalloc_free_n_owner host_free_hook
#include "paging_host_source.c"
#undef pgalloc_free_n_owner
__asm__(".globl __sqlite_start\n.set __sqlite_start, 0x200000\n"
        ".globl __sqlite_end\n.set __sqlite_end, 0x240000\n"
        ".globl __bss_end\n.set __bss_end, 0x180000\n");
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
int vfs_open(const char *path, int flags) { (void)path; (void)flags; cursor = 0; return 1; }
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
#define pgalloc_alloc_phys fail_alloc_phys
#include "shlib_host_source.c"
#undef pgalloc_alloc_phys
static void make_file(void) {
    OS32Header *oh = (OS32Header *)file_bytes;
    OS32ShlibHeader *sh = (OS32ShlibHeader *)(file_bytes + OS32X_HDR_SIZE);
    kmemset(file_bytes, 0, sizeof(file_bytes));
    oh->magic = OS32X_MAGIC; oh->version = OS32X_HDR_VERSION; oh->header_size = OS32X_HDR_SIZE;
    oh->flags = OS32X_FLAG_SHLIB; oh->load_addr = MEM_SHLIB_BASE; oh->text_size = 4 * PAGE_SIZE;
    oh->min_api_ver = KAPI_VERSION; oh->kapi_data_off = KAPI_DATA_FIELDS_OFF;
    oh->kapi_abi_generation = OS32_KAPI_ABI_GENERATION; oh->memory_layout_generation = OS32_MEMORY_LAYOUT_GENERATION;
    oh->shlib_protocol = OS32_SHLIB_PROTOCOL;
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
void _start(void) {
    u32 args[6] = {0x400000, 0xC00000, 3, 0x32, 0xFFFFFFFF, 0};
    u32 result, before, i, owner, shpages;
    struct addrspace as;
    __asm__ volatile("int $0x80" : "=a"(result) : "a"(90), "b"(args) : "memory");
    CHECK(result == 0x400000); host_map_fixed_paging(); paging_init(8192); host_pool_boot(8192);
    for (i = MEM_POOL_BASE / PAGE_SIZE + 1; i < MEM_POOL_BASE / PAGE_SIZE + 32; i += 2)
        CHECK(ledger_claim_fixed(LEDGER_OWNER_BOOT, i, i + 1));
    before = used_pages; make_file();
    for (i = 1; i <= 4; i++) {
        fail_alloc = i; alloc_calls = read_calls = 0;
        CHECK(shlib_init() == -1); CHECK(!g_loaded && used_pages == before);
        CHECK(!ledger_owner_pages(LEDGER_OWNER_SHLIB));
    }
    fail_alloc = 0;
    for (i = 1; i <= 5; i++) {
        fail_read = i; read_calls = alloc_calls = 0;
        CHECK(shlib_init() == -1); CHECK(!g_loaded && used_pages == before);
        CHECK(!ledger_owner_pages(LEDGER_OWNER_SHLIB));
    }
    fail_read = 0; read_calls = alloc_calls = 0; CHECK(!shlib_init());
    CHECK(g_pages[1] != g_pages[0] + PAGE_SIZE); CHECK(!page_directory[MEM_SHLIB_BASE >> 22]);
    shpages = ledger_owner_pages(LEDGER_OWNER_SHLIB); CHECK(shpages == 4);
    for (i = 1; i <= 2; i++) {
        CHECK(ledger_owner_new(LEDGER_KIND_AS, 2, "attach", &owner));
        CHECK(!paging_addrspace_create_lease(&as, owner));
        fail_alloc = i; alloc_calls = 0;
        CHECK(shlib_addrspace_attach(&as) == -1);
        CHECK(!pte(&as, MEM_SHLIB_BASE)); CHECK(!pte(&as, g_data_vaddr));
        CHECK(ledger_owner_pages(LEDGER_OWNER_SHLIB) == shpages);
        /* Sparse PTs may remain until destroy, but no private data remains. */
        CHECK(ledger_owner_pages(owner) == 2 || ledger_owner_pages(owner) == 3);
        paging_addrspace_destroy(&as); CHECK(!ledger_owner_pages(owner)); CHECK(ledger_owner_retire(owner));
        CHECK(used_pages == before + shpages);
    }
    fail_alloc = 0; CHECK(ledger_owner_new(LEDGER_KIND_AS, 2, "attach", &owner));
    CHECK(!paging_addrspace_create_lease(&as, owner)); CHECK(!shlib_addrspace_attach(&as));
    CHECK(!(pte(&as, MEM_SHLIB_BASE) & PTE_RW));
    CHECK((pte(&as, MEM_SHLIB_BASE) & ~0xFFFUL) == g_pages[0]);
    CHECK((pte(&as, g_data_vaddr) & ~0xFFFUL) != g_pages[2]);
    CHECK(*(u8 *)P2V(pte(&as, g_data_vaddr) & ~0xFFFUL) == 0xAB);
    *(u8 *)P2V(pte(&as, g_data_vaddr) & ~0xFFFUL) = 0xFF;
    CHECK(*(u8 *)P2V(g_pages[2]) == 0xAB);
    shlib_addrspace_detach(&as); paging_addrspace_destroy(&as);
    CHECK(!ledger_owner_pages(owner) && ledger_owner_retire(owner));
    CHECK(used_pages == before + shpages && !ledger_bad_free);
    SAY("shlib_high: PASS fragmented originals, private copy, every allocation/read failure and owner0"); die(0);
}
