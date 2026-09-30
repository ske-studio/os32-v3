#include "types.h"
static u32 host_cr3;
#include "paging_host_source.c"
__asm__(".globl __sqlite_start\n.set __sqlite_start, 0x200000\n"
        ".globl __sqlite_end\n.set __sqlite_end, 0x240000\n"
        ".globl __bss_end\n.set __bss_end, 0x180000\n");
static u32 limit = 16, calls;
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
#define pgalloc_alloc_n_pfn actual_alloc_n_pfn
#include "pgalloc_host_source.c"
#undef pgalloc_alloc_n_pfn
void __cdecl kprintf(u8 attr, const char *fmt, ...) { (void)attr; (void)fmt; }
int pgalloc_alloc_n_pfn(int n, u32 first, u32 end, u32 *pfn)
{
    calls++;
    if (used_pages >= limit) return 0;
    return actual_alloc_n_pfn(n, first, end, pfn);
}
#define used used_pages
void _start(void)
{
    u32 args[6] = {0x400000, 0xC00000, 3, 0x32, 0xFFFFFFFF, 0};
    u32 result;
    __asm__ volatile("int $0x80" : "=a"(result) : "a"(90), "b"(args) : "memory");
    CHECK(result == 0x400000);
    paging_init(16384);
    /* K6-RAM: paging_init が張るのは「ブート窓の内側 x 検出量」だけで、
     * RAM の上限ではない。16MiB 報告ならそこまで (従来と同じ)。 */
    CHECK(paging_boot_identity_end() == 16384UL * 1024 / PAGE_SIZE);
    CHECK(paging_is_present(PAGING_BOOT_MAP_SIZE / 2 - PAGE_SIZE));
    CHECK(!paging_is_present(PAGING_BOOT_MAP_SIZE / 2));
    CHECK(!paging_is_present(0xFFFFFFFFUL));
    CHECK(paging_set_page(0xFFFFF000UL, 0, PAGE_RW) == -1);
    CHECK(used == 0);
    /* 2026-09-23: aligned(4096) にして +4095 の捨てを無くした (TASK_MEMMAP_V3 2-3)。
     * 大きさは表そのもの、先頭は 4KB 境界。 */
    CHECK(sizeof(pt_raw) == 8 * PAGE_SIZE);
    CHECK(((unsigned long)(void *)pt_raw & (PAGE_SIZE - 1)) == 0);
    CHECK(sizeof(pd_raw) == PAGE_SIZE);
    CHECK(((unsigned long)(void *)pd_raw & (PAGE_SIZE - 1)) == 0);
    pgalloc_init(16384);
    CHECK(paging_map_phys(0xFFFFF000UL, 0xFFFFF000UL, 1, PAGE_RW | PTE_PCD) == 0);
    CHECK(paging_is_present(0xFFFFFFFFUL));
    CHECK(paging_map_phys(0xFFFFF000UL, 0, 2, PAGE_RW) == -1);
    CHECK(paging_map_phys(0, 0xFFFFF000UL, 2, PAGE_RW) == -1);
    CHECK(paging_map_phys(0, 0, 0x100000UL + 1, PAGE_RW) == -1);
    CHECK(paging_map_range(0x2000, 0x4000, 0xFFFFF000UL, PAGE_RW) == -1);
    CHECK(paging_map_range(0x4000, 0x2000, 0, PAGE_RW) == -1);
    CHECK(paging_map_range(0xFFFFF000UL, 0xFFFFFFFFUL, 0xFFFFF000UL, PAGE_RW | PTE_PCD) == 0);
    CHECK(paging_set_readonly(0xFFFFF000UL, 0xFFFFFFFFUL) == 0);
    CHECK(page_tables[1023][1023] == (0xFFFFF000UL | PAGE_RO | PTE_PCD));
    CHECK(paging_set_not_present(0xFFFFF000UL, 0xFFFFFFFFUL) == 0);
    CHECK(!paging_is_present(0xFFFFFFFFUL));
    CHECK(paging_set_readonly(0x2000000, 0x2000FFF) == 0);
    CHECK(!paging_is_present(0x2000000));
    CHECK(paging_set_readonly(0x4000, 0x2000) == -1);
    CHECK(paging_set_not_present(0x4000, 0x2000) == -1);
    CHECK(paging_pde_clear_user(0x4000, 0x2000) == -1);
    {
        struct addrspace as;
        u32 before = used, before_calls;
        CHECK(paging_addrspace_create(&as) == 0);
        before_calls = calls;
        CHECK(paging_map_phys(0x2000000, 0, 1, PAGE_RW) == -1);
        CHECK(calls == before_calls);
        CHECK(paging_set_page(0xFFFFF000UL, 0, PAGE_RW | PTE_PCD) == 0);
        paging_addrspace_destroy(&as);
        CHECK(used == before);
        CHECK(paging_map_phys(0x2000000, 0, 1, PAGE_RW) == 0);
        CHECK(used == before + 1);
        before = used;
        limit = used + 1;
        CHECK(paging_map_phys(0x27FF000, 0, 2, PAGE_RW) == -1);
        CHECK(used == before);
        CHECK(!page_tables[9] && !page_tables[10]);
        CHECK(!page_directory[9] && !page_directory[10]);
        limit = 16;
    }
    {
        struct addrspace as;
        u32 saved;
        CHECK(paging_addrspace_create(&as) == 0);
        CHECK(paging_addrspace_map_user_range(&as, 0x4000, 0x2000, PAGE_RW | PTE_USER) == -1);
        saved = page_tables[8][1023];
        CHECK(paging_addrspace_map_user_range(&as, 0x23FF000, 0x2401000, PAGE_RW | PTE_USER) == -1);
        CHECK(page_tables[8][1023] == saved);
        CHECK(paging_addrspace_map_user(&as, 0x2400000, 0, PAGE_RW | PTE_USER) == -1);
        CHECK(paging_addrspace_map_user_keep(&as, 0xFFFFF000UL, 0xFFFFFFFFUL, PAGE_RW | PTE_USER) == 0);
        CHECK((page_tables[1023][1023] & (PTE_USER | PTE_PCD)) == (PTE_USER | PTE_PCD));
        CHECK(!(page_directory[1023] & PTE_USER));
        paging_addrspace_destroy(&as);
        CHECK(paging_map_user_keep_selftest() == 0);
    }
    {
        u32 d2 = page_directory[2], d3 = page_directory[3];
        u32 before = used;
        page_directory[2] = 0;
        page_directory[3] = 0;
        CHECK(paging_set_page(0x2400000, 0, PAGE_RW) == -1);
        CHECK(used == before);
        page_directory[2] = d2;
        page_directory[3] = d3;
        host_cr3 = 0x123000;
        CHECK(paging_set_page(0x2400000, 0, PAGE_RW) == -1);
        CHECK(used == before);
        host_cr3 = paging_kernel_pd_phys();
        CHECK(paging_set_page(0x2400000, 0, PAGE_RW) == 0);
        CHECK((u32)page_tables[9] >= MEM_APP_BAND_TOP);
    }
    {
        struct addrspace a, b;
        u32 before = used, c = calls;
        CHECK(paging_addrspace_create(&a) == 0);
        CHECK(paging_addrspace_create(&b) == 0);
        paging_addrspace_destroy(&a);
        CHECK(paging_set_page(0x2800000, 0, PAGE_RW) == -1);
        CHECK(calls == c);
        host_cr3 = b.pd_phys;
        paging_addrspace_destroy(&b);
        CHECK(b.pd_phys != 0);
        host_cr3 = paging_kernel_pd_phys();
        CHECK(paging_set_page(0x2800000, 0, PAGE_RW) == -1);
        paging_addrspace_destroy(&b);
        paging_addrspace_destroy(&b);
        CHECK(used == before);
        CHECK(paging_set_page(0x2800000, 0, PAGE_RW) == 0);
        CHECK(used == before + 1);
    }
    {
        u32 old = page_tables[10][1023], before = used;
        limit = used + 1;
        CHECK(paging_map_phys(0x2BFF000, 0, 1026, PAGE_RW | PTE_USER) == -1);
        CHECK(used == before);
        CHECK(page_tables[10][1023] == old);
        CHECK(!page_tables[11] && !page_tables[12]);
        limit = 16;
        old = page_tables[0][2];
        CHECK(paging_map_range(0x2000, 0x4000, 0xFFFFF000UL, PAGE_RW) == -1);
        CHECK(page_tables[0][2] == old);
        CHECK(paging_set_readonly(0x1FFF000, 0x1FFFFFF) == 0);
        CHECK(!paging_is_present(0x1FFF000));
        CHECK(paging_map_phys(0x1000000, 0x1000000, 512, PAGE_RW | PTE_PCD) == 0);
        CHECK((page_tables[4][511] & (PAGE_RW | PTE_PCD | PTE_USER)) == (PAGE_RW | PTE_PCD));
    }
    {
        u32 flags = PAGE_RW | PTE_USER | PTE_PCD | PTE_PWT | PTE_ACCESSED;
        CHECK(paging_set_page(0xFFFFF000UL, 0x12345000, flags) == 0);
        CHECK(paging_set_readonly(0xFFFFF000UL, 0xFFFFFFFFUL) == 0);
        CHECK(page_tables[1023][1023] == (0x12345000 | (flags & ~(u32)PTE_RW)));
        CHECK(paging_set_not_present(0xFFFFF000UL, 0xFFFFFFFFUL) == 0);
        CHECK(paging_set_readonly(0xFFFFF000UL, 0xFFFFFFFFUL) == 0);
        CHECK(!paging_is_present(0xFFFFFFFFUL));
        CHECK(paging_pde_clear_user(0xFFFFF000UL, 0xFFFFFFFFUL) == 0);
        CHECK(!(page_directory[1023] & PTE_USER));
    }
    {
        struct addrspace as;
        u32 *pt = page_tables[1023];
        u32 pte = pt[1023], pde = page_directory[1023];
        u32 before, before_calls, cr3, live;
        CHECK(paging_addrspace_create(&as) == 0);
        host_cr3 = as.pd_phys;
        before = used;
        before_calls = calls;
        cr3 = host_cr3;
        live = live_addrspaces;
        paging_init(1024);
        CHECK(page_tables[1023] == pt);
        CHECK(pt[1023] == pte && page_directory[1023] == pde);
        CHECK(live_addrspaces == live && used == before);
        CHECK(calls == before_calls && host_cr3 == cr3);
        CHECK(paging_enabled());
        CHECK(((u32 *)as.pd_phys)[1023] == pde);
        host_cr3 = paging_kernel_pd_phys();
        paging_addrspace_destroy(&as);
        CHECK(live_addrspaces == 0 && used == before - 2);
    }
    {
        /* 票 S0-K / 実機 K2 (2026-09-13): **アプリの PD を、そのアプリの
         * syscall 中に歩いてはならない** ことの番人。
         *
         * カーネルはページテーブルを「物理 = 仮想」で読む。ところが PD も
         * アプリ PT も pgalloc から取られ、`PGALLOC_BASE` は 0x400000 =
         * `MEM_APP_BAND_BASE` — **アプリ帯そのもの**。アプリの PD ではその
         * 仮想番地が per-app 物理へ張り替わるので、CR3 = アプリ PD のまま
         * 表を辿ると PT のつもりでアプリ自身のデータを読む。#PF も起きず、
         * 健全な .rodata を「非 present」と答える (実機で 2 回これを踏んだ)。
         *
         * ここで固定するのは 2 つ:
         *   1. PGALLOC_BASE がアプリ帯の中にある (= 前提が成り立っている)
         *   2. exec と同じ順序で組んだ AS では、アプリ PT の物理番地が
         *      アプリ PD の下で **別の物理** に解決される (= 歩けない) */
        struct addrspace as;
        u32 code = 0x500000, sbrk_end = 0x520000;
        u32 pt_phys, pdi, pti;
        u32 *app_pt;

        CHECK(PGALLOC_BASE == MEM_APP_BAND_BASE);
        CHECK(paging_addrspace_create_n(&as, 1) == 0);
        CHECK(as.app_pde == APP_BAND_PDE && as.app_pde_count == 1);
        pt_phys = as.app_pt_phys[0];
        CHECK(paging_addrspace_clear_app_band(&as) == 0);
        CHECK(paging_addrspace_map_user_range_phys(&as, code, sbrk_end,
                                                   0x900000, PAGE_RW | PTE_USER) == 0);

        /* アプリ PT の物理がアプリ帯に入っているなら、アプリ PD の下で
         * その仮想番地は **PT ではないもの** を指す (or 非 present)。
         * 入っていない構成でも「歩いてよい」ことにはならないので、
         * 入っているときだけ強い主張をする。 */
        if (pt_phys >= MEM_APP_BAND_BASE && pt_phys < MEM_APP_BAND_TOP) {
            pdi = pt_phys >> 22;
            pti = (pt_phys >> 12) & 0x3FF;
            CHECK(pdi >= as.app_pde && pdi < as.app_pde + as.app_pde_count);
            app_pt = (u32 *)as.app_pt_phys[pdi - as.app_pde];
            /* clear_app_band の後に張ったのは [code, sbrk_end) だけなので、
             * PT 自身の番地は非 present か、per-app 物理 (PT ではない) を指す。*/
            if (app_pt[pti] & PTE_PRESENT) {
                CHECK((app_pt[pti] & 0xFFFFF000UL) != pt_phys);
            }
        }

        /* master の下では従来どおり identity で読める (create_n が書けたのも
         * これのおかげ)。歩いてよいのは master CR3 の下だけ、という証拠。 */
        CHECK(paging_current_cr3() == paging_kernel_pd_phys());
        CHECK(paging_pte_flags(pt_phys) & PTE_PRESENT);

        paging_addrspace_destroy(&as);
    }
    SAY("PASS: one-shot init preserves dynamic PT, live AS, CR3, allocator");
    SAY("PASS: final-page, virtual/physical overflow, range preflight");
    SAY("PASS: sparse NP, attribute flags, USER/PCD, user-range preflight");
    SAY("PASS: real allocator rollback, live-AS lifecycle, master-only backing");
    die(0);
}
