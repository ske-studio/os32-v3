/* ========================================================================
 *  app_band_pde_host.c — T2c の疎な高位 AS と boot ledger の回帰
 *  (docs/tasks/v3/TASK_T2_APPBAND.md §4 / §5-1)
 *
 *  実 paging.c / pgalloc.c / physmem.c と kselftest の ledger 手順を ILP32 で
 *  実行し、特権命令だけホスト用に差し替える。
 *  旧物理 byte 予算、高位の3つの疎 PDE、master USER 隔離、確保失敗の
 *  巻戻し、PT/PD の返却、8/17MB の boot ledger と先頭 lease PT を検査。
 * ======================================================================== */
#include "types.h"
static u32 host_cr3;
#define pgalloc_alloc_phys test_failphys
#include "paging_host_source.c"
#undef pgalloc_alloc_phys
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
/* pgalloc_host_source.c は irq_save() を 0 に置き換えてある。 */
#define HOST_POOL_IRQ_SAVE() 0U
#define HOST_POOL_IRQ_RESTORE(f) ((void)(f))
#include "pgalloc_host_fixture.h"
void __cdecl kprintf(u8 attr, const char *fmt, ...) { (void)attr; (void)fmt; }
#define used used_pages

/* 帯の 2 枚目に当たる PDE / 仮想番地 (最大枚数が 2 未満なら試験は縮む) */
#define PDE2      (APP_BAND_PDE + 1)
#define BAND2_VA  (MEM_APP_BAND_BASE + MEM_APP_BAND_PDE_SIZE)

static u32 fail_at, fail_calls;
u32 test_failphys(u32 owner, int n) {
    if (++fail_calls == fail_at) return 0;
    return pgalloc_alloc_phys(owner, n);
}

/* Execute the actual boot ledger test, including its expectation and cleanup. */
static u32 ledger_test_failures;
static void check(int ok, const char *name)
{
    if (!ok) {
        u32 n = 0;
        while (name[n]) n++;
        report(name, n); SAY("");
        ledger_test_failures++;
    }
}
#include "kselftest_ledger_source.c"

static void say_number(u32 n)
{
    char buf[10];
    u32 len = 0, i;
    do { buf[len++] = '0' + n % 10; n /= 10; } while (n);
    for (i = 0; i < len / 2; i++) {
        char c = buf[i]; buf[i] = buf[len - i - 1]; buf[len - i - 1] = c;
    }
    report(buf, len);
}
#define FIELD(s, n) do { report(s, sizeof(s) - 1); say_number(n); } while (0)
static void ledger_boot_regression(u32 kb)
{
    struct addrspace as;
    u32 owner, phys, i, app_pts = 0, lease_pts = 0, left = 0;
    u32 before;
    int rc;
    initialized = 0; /* fixture: each memory size starts a fresh pool */
    paging_init(kb);
    host_pool_boot(kb);
    before = pgalloc_free_pages();
    CHECK(ledger_owner_new(LEDGER_KIND_AS, 0, "kstest", &owner));
    rc = paging_addrspace_create(&as, owner);
    CHECK(rc == 0);
    phys = pgalloc_alloc_phys(owner, 2);
    CHECK(phys);
    for (i = 0; i < MEM_APP_BAND_MAX_PDES; i++) app_pts += !!as.app_pt_phys[i];
    for (i = 0; i < MEM_LEASE_MAX_PDES; i++) lease_pts += !!as.lease_pt_phys[i];
    FIELD("ledger boot: KB=", kb); FIELD(" create_rc=", (u32)rc);
    FIELD(" owner_pages=", ledger_owner_pages(owner)); FIELD(" PD=", !!as.pd_phys);
    FIELD(" lease_PT=", lease_pts); FIELD(" high_PT=", app_pts); SAY(" data=2");
    CHECK(ledger_owner_pages(owner) == PDE_COUNT * sizeof(u32) / PAGE_SIZE + 2);
    CHECK(!app_pts && !lease_pts);
    paging_addrspace_destroy(&as);
    CHECK(ledger_reclaim_owner(owner, &left) && left == 2);
    CHECK(ledger_owner_retire(owner));
    CHECK(pgalloc_free_pages() == before);
    test_ledger();
    CHECK(!ledger_test_failures);
    CHECK(pgalloc_free_pages() == before);
    /* Launch construction additionally preallocates the first lease PT. */
    CHECK(ledger_owner_new(LEDGER_KIND_AS, 0, "lease-test", &owner));
    CHECK(!paging_addrspace_create_lease(&as, owner));
    CHECK(as.lease_pt_phys[0]);
    CHECK(ledger_owner_pages(owner) == (PDE_COUNT + PTE_COUNT) * sizeof(u32) / PAGE_SIZE);
    CHECK(!selftest_as_end(&as));
    CHECK(pgalloc_free_pages() == before && ledger_selfcheck("boot-regression"));
}

void _start(void)
{
    /* 17MB 構成の pool backing を恒等で読み書きできるよう張る。 */
    u32 args[6] = {0x400000, 0xD00000, 3, 0x32, 0xFFFFFFFF, 0};
    u32 result;
    __asm__ volatile("int $0x80" : "=a"(result) : "a"(90), "b"(args) : "memory");
    CHECK(result == 0x400000);

    host_map_fixed_paging();
    ledger_boot_regression(17408);
    ledger_boot_regression(8192);
    initialized = 0;
    paging_init(16384);
    host_pool_boot(16384);


    /* Old physical-budget ceiling remains separate from high VA capacity. */
    CHECK(paging_app_band_pdes(MEM_PHYS_EXEC_FLOOR, 0, 0xE00000) == 1);
    CHECK(paging_app_band_pdes(MEM_PHYS_EXEC_FLOOR, 0x400000, 0xE00000) == MEM_LEGACY_APP_PDES);
    CHECK(paging_app_band_pdes(MEM_PHYS_EXEC_FLOOR, 0x400000, 0x600000) == 1);
    {
        struct addrspace a, b;
        u32 oa, ob, before = pgalloc_free_pages(), k, pa[3], va[3];
        CHECK(ledger_owner_new(LEDGER_KIND_AS, 2, "A", &oa));
        CHECK(ledger_owner_new(LEDGER_KIND_AS, 3, "B", &ob));
        CHECK(!paging_addrspace_create_lease(&a, oa));
        CHECK(!paging_addrspace_create_lease(&b, ob));
        CHECK(ledger_owner_pages(oa) == 2); /* PD + lease PT, no app PT yet */
        for (k = 0; k < MEM_APP_BAND_MAX_PDES; k++) {
            CHECK(!a.app_pt_phys[k]);
            CHECK(!page_directory[APP_BAND_PDE + k]);
        }
        va[0] = MEM_EXEC_LOAD_ADDR;
        va[1] = MEM_EXEC_HEAP_BASE;
        va[2] = MEM_APP_STACK_TOP - PAGE_SIZE;
        for (k = 0; k < 3; k++) {
            u32 kpt = (va[k] >> 22) - APP_BAND_PDE;
            pa[k] = pgalloc_alloc_phys(oa, 1); CHECK(pa[k]);
            CHECK(!paging_addrspace_map_user(&a, va[k], pa[k], PAGE_RW | PTE_USER));
            CHECK(a.app_pt_phys[kpt]);
            CHECK(!b.app_pt_phys[kpt]);
            CHECK(!page_directory[va[k] >> 22]);
            CHECK(((u32 *)P2V(a.app_pt_phys[kpt]))[(va[k] >> 12) & 1023] == (pa[k] | PAGE_RW | PTE_USER));
        }
        CHECK(ledger_owner_pages(oa) == 8); /* three sparse PTs and three pages */
        for (k = 0; k < 3; k++) CHECK(paging_addrspace_free_user_range(&a, va[k], va[k] + PAGE_SIZE) == 1);
        CHECK(!selftest_as_end(&a));
        CHECK(!selftest_as_end(&b));
        CHECK(pgalloc_free_pages() == before);
        CHECK(!paging_app_band_selftest());
        CHECK(pgalloc_free_pages() == before);
    }
    {
        struct addrspace a;
        u32 owner, phys, before = pgalloc_free_pages(), stage;
        for (stage = 1; stage <= 2; stage++) {
            CHECK(ledger_owner_new(LEDGER_KIND_AS, 2, "create-fail", &owner));
            fail_at = stage; fail_calls = 0;
            CHECK(paging_addrspace_create_lease(&a, owner) == -1);
            CHECK(!ledger_owner_pages(owner) && ledger_owner_retire(owner));
            CHECK(pgalloc_free_pages() == before);
        }
        fail_at = 0;
        CHECK(ledger_owner_new(LEDGER_KIND_AS, 2, "map-fail", &owner));
        CHECK(!paging_addrspace_create_lease(&a, owner));
        phys = pgalloc_alloc_phys(owner, 2); CHECK(phys);
        for (stage = 1; stage <= 2; stage++) {
            fail_at = stage; fail_calls = 0;
            CHECK(paging_addrspace_map_user_range_phys(&a,
                  MEM_APP_BAND_BASE + MEM_APP_BAND_PDE_SIZE - PAGE_SIZE,
                  MEM_APP_BAND_BASE + MEM_APP_BAND_PDE_SIZE + PAGE_SIZE,
                  phys, PAGE_RW | PTE_USER) == -1);
            CHECK(ledger_owner_pages(owner) == 4); /* PD/lease/data, no partial PT */
            CHECK(!a.app_pt_phys[0] && !a.app_pt_phys[1]);
            CHECK(!((u32 *)P2V(a.pd_phys))[APP_BAND_PDE]);
        }
        fail_at = 0;
        CHECK(pgalloc_free_n_owner(owner, phys / PAGE_SIZE, 2));
        CHECK(!selftest_as_end(&a)); CHECK(pgalloc_free_pages() == before);
    }
    SAY("PASS: legacy byte budget; sparse high AS, three disjoint PDEs, master isolation and owner0");
    die(0);
}

/* Paging-only fixture has no exec slots; abort delivery is appmem_map_host. */
void exec_addrspace_abort(struct addrspace *as) { (void)as; }
