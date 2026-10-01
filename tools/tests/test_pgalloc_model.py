"""Real ILP32 allocator/model integration; privileged IRQ only is substituted."""
import pathlib
import subprocess
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[2]

class Integration(unittest.TestCase):
    def run_c(self, body, flags=(), exec_claim=False, physical_core=False):
        # Arithmetic/ownership unit tests intentionally exercise the private core;
        # staged public high-PFN behavior is tested with real paging separately.
        if physical_core:
            # The metadata-only init never publishes ONLINE; open the private
            # gate so the arithmetic core runs (owner-API unit, T1b).
            body = body.replace('@ONLINE@', 'online = 1;')
        with tempfile.TemporaryDirectory(prefix='os32-model-') as tmp:
            tmp = pathlib.Path(tmp)
            source = (ROOT / 'kernel/pgalloc.c').read_text()
            source = source.replace('#include "io.h"', '')
            source += (ROOT / 'kernel/sys.c').read_text().replace('#include "io.h"', '')
            # 旧 legacy pgalloc_init の代わりの足場 (T1a で製品から撤去)。
            source += (ROOT / 'tools/tests/pgalloc_host_fixture.h').read_text()
            if exec_claim:
                # Compile the actual layout helper, not a parallel test formula.
                exec_source = (ROOT / 'exec/exec.c').read_text()
                reserve = next(line for line in exec_source.splitlines()
                               if line.startswith('#define EXEC_DYN_RESERVE '))
                claim = exec_source.split('static void exec_child_claim(', 1)[1]
                claim = 'static void exec_child_claim(' + claim.split('\n}', 1)[0] + '\n}\n'
                source += '\n' + reserve + '\n' + claim
            pre = '''#include "types.h"
int paging_boot_context(void) { return 1; }
/* Allocator-only fixture runs in the master identity context. */
u32 paging_current_cr3(void) { return 1; }
u32 paging_kernel_pd_phys(void) { return 1; }
static void outp(unsigned int p, unsigned int v) { (void)p; (void)v; }
#define NOINST __attribute__((no_instrument_function))
static void host_verify_commit(void) NOINST;
static unsigned int host_if = 0x202, saves, restores;
static unsigned int irq_save(void) NOINST;
static void irq_restore(unsigned int f) NOINST;
static unsigned int irq_save(void) { unsigned int f = host_if; host_if &= ~0x200U; saves++; return f; }
static void irq_restore(unsigned int f) { host_verify_commit(); restores++; host_if = f; }
/* io.h を外しているので CPU 停止の原始命令も贋物にする。sys.c の
 * sys_halt() / sys_reboot() が呼ぶだけで、この試験では到達しない
 * (本物は hlt。ホストで実行すると CPL=3 で #GP になる)。 */
static void _halt(void) NOINST;
static void _halt(void) { }
static void _stop(void) { __asm__ volatile("int $0x80" : : "a"(1), "b"(250)); }
void kprintf(unsigned char a, const char *f, ...) { (void)a; (void)f; }
'''
            (tmp / 'test.c').write_text(pre + source + '''
static void host_bad_irq(void) __attribute__((noreturn, no_instrument_function));
static void host_bad_irq(void) {
    __asm__ volatile("int $0x80" : : "a"(1), "b"(250) : "memory"); for (;;) {}
}
static void host_verify_commit(void) {
    u32 p, e, a, ne, na, o, cnt[LEDGER_MAX_OWNERS];
    if (host_if & 0x200U) host_bad_irq();
    if (!initialized) return;
    ne = na = 0;
    for (o = 0; o < LEDGER_MAX_OWNERS; o++) cnt[o] = 0;
    for (p = 0; p < limit_pfn; p++) {
        e = (eligible[p / 32] >> (p % 32)) & 1;
        a = (bitmap[p / 32] >> (p % 32)) & 1;
        if (a && !e) host_bad_irq();
        /* T1b: eligible なページでは allocated ⇔ owner ≠ 0 (L1 と L2) */
        if (e && a != (owner_map[p] != 0)) host_bad_irq();
        cnt[owner_map[p]]++;
        ne += e; na += a;
    }
    if (ne != total_pages || na != used_pages) host_bad_irq();
    for (o = 1; o < LEDGER_MAX_OWNERS; o++)
        if (cnt[o] != ledger_owners[o].pages) host_bad_irq();
}
void __cyg_profile_func_enter(void *fn, void *caller) NOINST;
void __cyg_profile_func_exit(void *fn, void *caller) NOINST;
void __cyg_profile_func_enter(void *fn, void *caller) {
    (void)caller;
    if ((fn == (void *)init_core || fn == (void *)bit ||
         fn == (void *)bmp_set || fn == (void *)bmp_clear || fn == (void *)bmp_test)
        && (host_if & 0x200U)) host_bad_irq();
}
void __cyg_profile_func_exit(void *fn, void *caller) { (void)fn; (void)caller; }
#include "physmem.h"
extern int sys_memory_init_model(struct physmem *, void *, u32, u32,
                                 int (*)(u32, u32, void *)) __attribute__((weak));
extern int pgalloc_init_model(struct physmem *, void *, u32, u32,
                             int (*)(u32, u32, void *)) __attribute__((weak));
static int __attribute__((unused)) denied(u32 p, u32 n, void *v) { (void)p; (void)n; (void)v; return 0; }
static int __attribute__((unused)) verified(u32 p, u32 n, void *v) { (void)p; (void)n; (void)v; return 1; }
#define STR1(x) #x
#define STR(x) STR1(x)
#define CHECK(x) do { if (!(x)) { static const char msg[] = "CHECK " STR(__LINE__) ": " #x "\\n"; __asm__ volatile("int $0x80" : : "a"(4), "b"(2), "c"(msg), "d"(sizeof(msg)-1) : "memory"); return 1; } } while (0)
#define K LEDGER_OWNER_KERNEL
static int test(void) {
''' + body + '''
return 0;
}
void _start(void) { int r = test(); __asm__ volatile("int $0x80" : : "a"(1), "b"(r) : "memory"); for (;;) {} }
''')
            cmd = ['gcc', '-m32', '-march=i386', '-std=gnu11', '-Wall', '-Wextra', '-Werror', '-ffreestanding', '-fno-pie', '-fno-stack-protector', '-nostdlib', '-static', '-no-pie', '-ffunction-sections', '-finstrument-functions', '-Wl,--gc-sections']
            cmd += ['-I' + str(ROOT / p) for p in ('include', 'kernel', 'lib', 'drivers', 'sdk/include/os32')]
            subprocess.run(cmd + list(flags) + [str(tmp / 'test.c'), str(ROOT / 'kernel/physmem.c'), '-o', str(tmp / 'test')], check=True)
            result = subprocess.run([str(tmp / 'test')])
            self.assertEqual(result.returncode, 0, 'C CHECK failure (250 = IRQ/commit invariant): ' + str(result.returncode) + '\n' + (tmp / 'test.c').read_text())

    def test_metadata_failure_transactions(self):
        self.run_c('''
    struct physmem m, before;
    /* L1 (2 bitmap) + L2 (1B/PFN) for the 4GiB model: 1,310,720B = 320 pages. */
    static u32 backing[327680 + 1] __attribute__((aligned(4096)));
    unsigned char *a, *b;
    u32 i, bytes, first;
    physmem_bootstrap_legacy(&m, 16384);
    CHECK(physmem_add_trusted(&m, 1048575, 1048576, PHYSMEM_SOURCE_SYNTHETIC));
    bytes = pgalloc_metadata_bytes(&m);
    CHECK(bytes == 1310720UL);
    first = 4096 - bytes / PAGE_SIZE;
    before = m;
    for (i = 0; i < 327681; i++) backing[i] = 0xace01234UL;
    CHECK(!pgalloc_init_model(&m, backing, bytes - 1, first, verified));
    CHECK(!pgalloc_init_model(&m, backing, bytes, 4096, verified));
    CHECK(!pgalloc_init_model(&m, backing, bytes, 1024, verified));
    CHECK(!pgalloc_init_model(&m, backing, bytes, first, denied));
    a = (unsigned char *)&m; b = (unsigned char *)&before;
    for (i = 0; i < sizeof(m); i++) CHECK(a[i] == b[i]);
    for (i = 0; i < 327681; i++) CHECK(backing[i] == 0xace01234UL);
    /* 窓の撤去 (2026-09-09) で bootstrap の区間が 1 つ減った。容量ちょうどに
     * するのは分割 1 回あたり 2 区間なので、29 + 末尾 1 本 (= 63) ではなく
     * 30 回で 64 に届く。狙いは「容量いっぱいのモデルでは init_model が
     * 何も変えずに失敗する」ことの確認で、そこは変わらない。 */
    for (i = 0; i < 30; i++)
        CHECK(physmem_exclude(&m, 20000 + i * 2, 20001 + i * 2, PHYSMEM_RESERVED));
    CHECK(m.count == PHYSMEM_MAX_RANGES);
    before = m;
    CHECK(!pgalloc_init_model(&m, backing, bytes, first - 100, verified));
    for (i = 0; i < sizeof(m); i++) CHECK(a[i] == b[i]);
    for (i = 0; i < 327681; i++) CHECK(backing[i] == 0xace01234UL);
    physmem_bootstrap_legacy((struct physmem *)backing, 16384);
    CHECK(!pgalloc_init_model((struct physmem *)backing, backing, 262144, 4094, verified));
    /* 窓の撤去で bootstrap の区間は 3 本 (旧 4 本)。 */
    CHECK(((struct physmem *)backing)->count == 3);
    physmem_bootstrap_legacy(&m, 16384);
    CHECK(pgalloc_init_model(&m, backing, 262144, 4094, verified));
    CHECK(backing[327680] == 0xace01234UL);
    /* 4,096 PFN: L1 1,024B + L2 4,096B = 2 pages (T1b). */
    CHECK(pgalloc_metadata_bytes(&m) == 2 * PAGE_SIZE);
    CHECK(pgalloc_limit_pfn() == 4096);
''', flags=('-DPHYSMEM_HOST_TEST=1', '-DPGALLOC_HOST_TEST=1'))

    def test_production_rejects_forged_synthetic_and_host_backing(self):
        for flags in ((), ('-D__KERNEL_BUILD__',),
                      ('-D__KERNEL_BUILD__', '-DPHYSMEM_HOST_TEST=1', '-DPGALLOC_HOST_TEST=1')):
            self.run_c('''
    struct physmem m;
    static u32 backing[2048] __attribute__((aligned(4096)));
    physmem_bootstrap_legacy(&m, 16384);
    m.ranges[1].sources = PHYSMEM_SOURCE_SYNTHETIC;
    CHECK(pgalloc_metadata_bytes(&m) == 0);
    CHECK(!pgalloc_init_model(&m, backing, sizeof(backing), 4094, verified));
    m.ranges[1].sources = PHYSMEM_SOURCE_LEGACY;
    CHECK(pgalloc_metadata_bytes(&m) == 2 * PAGE_SIZE);
    CHECK(!pgalloc_init_model(&m, backing, sizeof(backing), 4094, verified));
    CHECK(pgalloc_total_pages() == 0);
''', flags=flags)

    def test_32_and_64_mib_capacity(self):
        for end in (8192, 16384):
            self.run_c('''
    struct physmem m;
    static u32 backing[5120 + 1] __attribute__((aligned(4096)));
    u32 bytes, first, p, before;
    physmem_bootstrap_legacy(&m, 16384);
    CHECK(physmem_add_trusted(&m, 4096, END, PHYSMEM_SOURCE_SYNTHETIC));
    bytes = pgalloc_metadata_bytes(&m);
    /* L1 2 bitmap + L2 1B/PFN (T1b) */
    CHECK(bytes == ((END / 32 * 8 + END + 4095) & ~4095UL));
    first = 4096 - bytes / PAGE_SIZE;
    backing[bytes / 4] = 0x12345678UL;
    CHECK(pgalloc_init_model(&m, backing, bytes, first, verified));
    @ONLINE@
    CHECK(pgalloc_limit_pfn() == END);
    before = pgalloc_free_pages();
    CHECK(pgalloc_alloc_n_owner(LEDGER_OWNER_KERNEL, END - 4096, 4096, END, LEDGER_BOTTOM_UP, &p) && p == 4096);
    CHECK(pgalloc_free_pages() == before - (END - 4096));
    CHECK(!pgalloc_alloc_n_owner(LEDGER_OWNER_BOOT, 2, 4095, 4097, LEDGER_BOTTOM_UP, &p));
    CHECK(pgalloc_free_pages() == before - (END - 4096));
    host_if = 2;
    CHECK(pgalloc_free_n_owner(LEDGER_OWNER_KERNEL, p, END - 4096));
    CHECK(host_if == 2 && saves == restores);
    CHECK(pgalloc_free_pages() == before);
    CHECK(backing[bytes / 4] == 0x12345678UL);
'''.replace('END', str(end)), flags=('-DPHYSMEM_HOST_TEST=1', '-DPGALLOC_HOST_TEST=1'), physical_core=True)

    def test_generic_never_returns_unmapped_machine_ram(self):
        self.run_c('''
    struct physmem m;
    static u32 backing[1024] __attribute__((aligned(4096)));
    u32 p;
    physmem_bootstrap_legacy(&m, 8192);
    CHECK(physmem_add_trusted(&m, 3072, 3073, PHYSMEM_SOURCE_MACHINE));
    CHECK(pgalloc_init_model(&m, backing, sizeof(backing), 1983, verified));
    CHECK(pgalloc_alloc_phys(K, 959) == 0);
    CHECK(pgalloc_alloc_phys(K, 1) == 0);
    p = 99;
    CHECK(!pgalloc_alloc_n_owner(K, 1, 3072, 3073, LEDGER_BOTTOM_UP, &p) && p == 99);
''', flags=('-DPGALLOC_HOST_TEST=1',))

    def test_sys_model_handoff(self):
        self.run_c('''
    struct physmem m;
    static u32 backing[327680] __attribute__((aligned(4096)));
    u32 p;
    CHECK(sys_memory_init_model != 0);
    physmem_bootstrap_legacy(&m, 16384);
    CHECK(physmem_add_trusted(&m, 1048575, 1048576, PHYSMEM_SOURCE_SYNTHETIC));
    /* metadata (L1 + L2) of the 4GiB model is 320 pages: [3776, 4096). */
    CHECK(sys_memory_init_model(&m, backing, sizeof(backing), 3776, verified));
    /* Before ONLINE, explicit high-PFN allocation is still gated. */
    CHECK(!pgalloc_alloc_n_owner(LEDGER_OWNER_KERNEL, 1, 1048575, 1048576, LEDGER_BOTTOM_UP, &p));
    @ONLINE@
    CHECK(sys_usable_mem_end() == 3776 * PAGE_SIZE);
    sys_mem_kb = 65536;
    CHECK(sys_usable_mem_end() == 3776 * PAGE_SIZE);
    /* sys_reserve_top was retired in T1e (TASK_T1_LEDGER §3-6): the usable
       end is min(frozen exec ceiling, ledger_arena_top()), and the arena top
       is the lowest PERSIST page inside [MEM_EXEC_LOAD_ADDR, arena end),
       frozen once at step 6. Before the freeze it is the arena end. */
    CHECK(ledger_arena_top() == 3776);
    CHECK(pgalloc_alloc_n_owner(LEDGER_OWNER_BOOT, 1, 3775, 3776, LEDGER_TOP_DOWN, &p));
    CHECK(sys_usable_mem_end() == 3776 * PAGE_SIZE);   /* not frozen yet */
    ledger_arena_freeze();
    CHECK(ledger_arena_top() == 3775);
    CHECK(sys_usable_mem_end() == 3775 * PAGE_SIZE);
    /* frozen once: a later PERSIST page lower in the arena changes nothing */
    CHECK(pgalloc_alloc_n_owner(LEDGER_OWNER_BOOT, 1, 3000, 3001, LEDGER_TOP_DOWN, &p));
    ledger_arena_freeze();
    CHECK(ledger_arena_top() == 3775 && sys_usable_mem_end() == 3775 * PAGE_SIZE);
    CHECK(owner_map[3775] == LEDGER_OWNER_BOOT && ledger_owner_pages(LEDGER_OWNER_BOOT) == 2);
    CHECK(!pgalloc_alloc_n_owner(LEDGER_OWNER_KERNEL, 1, 3775, 3776, LEDGER_BOTTOM_UP, &p));
    CHECK(!sys_memory_init_model(&m, backing, sizeof(backing), 3776, verified));
''', flags=('-DPHYSMEM_HOST_TEST=1', '-DPGALLOC_HOST_TEST=1'), physical_core=True)

    def test_sys_low_stable(self):
        self.run_c('''
    u32 base, p;
    sys_mem_kb = 0xffffffffUL;
    /* Retired hotdeploy window (2026-09-09): the arena ends at real RAM,
       clamped to PHYSMEM_LEGACY_MAX_PFN = 16MiB. */
    CHECK(sys_usable_mem_end() == 0x1000000UL);
    base = sys_usable_mem_end();
    host_pool_boot(16384);
    /* invalid / DEVICE owners cannot allocate RAM */
    CHECK(!pgalloc_alloc_n_owner(0, 1, 4095, 4096, LEDGER_TOP_DOWN, &p) &&
          !pgalloc_alloc_n_owner(LEDGER_OWNER_GFX, 1, 4095, 4096, LEDGER_TOP_DOWN, &p));
    /* sys has not frozen a model here: the ledger's arena top is not used
       (T1e — sys_reserve_top is gone, nothing lowers this ceiling). */
    CHECK(pgalloc_alloc_n_owner(LEDGER_OWNER_BOOT, 1, 4095, 4096, LEDGER_TOP_DOWN, &p));
    ledger_arena_freeze();
    CHECK(sys_usable_mem_end() == base);
    CHECK(host_alloc_range(1, base - PAGE_SIZE, base) == 0);
''')

    def test_dynamic_sparse_final(self):
        self.run_c('''
    struct physmem m;
    static u32 backing[327680] __attribute__((aligned(4096)));
    u32 p, before;
    CHECK(pgalloc_init_model != 0);
    physmem_bootstrap_legacy(&m, 16384);
    CHECK(physmem_add_trusted(&m, 4096, 8192, PHYSMEM_SOURCE_SYNTHETIC));
    CHECK(physmem_add_trusted(&m, 12288, 16384, PHYSMEM_SOURCE_SYNTHETIC));
    CHECK(physmem_add_trusted(&m, 1048575, 1048576, PHYSMEM_SOURCE_SYNTHETIC));
    CHECK(!pgalloc_init_model(&m, backing, sizeof(backing) - 1, 3776, verified));
    CHECK(!pgalloc_init_model(&m, backing + 1, sizeof(backing), 3776, verified));
    CHECK(!pgalloc_init_model(&m, backing, sizeof(backing), 3777, verified));
    CHECK(!pgalloc_init_model(&m, backing, sizeof(backing), 3776, 0));
    CHECK(pgalloc_init_model(&m, backing, sizeof(backing), 3776, verified));
    @ONLINE@
    CHECK(pgalloc_limit_pfn() == 1048576);
    CHECK(pgalloc_alloc_n_owner(K, 1, 1048575, 1048576, LEDGER_BOTTOM_UP, &p) && p == 1048575);
    CHECK(pgalloc_free_n_owner(K, p, 1));
    CHECK(pgalloc_alloc_n_owner(K, 4096, 4096, 8192, LEDGER_BOTTOM_UP, &p) && p == 4096);
    CHECK(pgalloc_free_n_owner(K, p, 4096));
    CHECK(pgalloc_alloc_n_owner(K, 4096, 12288, 16384, LEDGER_BOTTOM_UP, &p) && p == 12288);
    before = pgalloc_free_pages();
    p = 99;
    CHECK(!pgalloc_alloc_n_owner(K, 0, 1048575, 1048576, LEDGER_BOTTOM_UP, &p));
    CHECK(!pgalloc_alloc_n_owner(K, -1, 1048575, 1048576, LEDGER_BOTTOM_UP, &p));
    CHECK(!pgalloc_alloc_n_owner(K, 2147483647, 1048575, 1048576, LEDGER_BOTTOM_UP, &p));
    CHECK(!pgalloc_alloc_n_owner(K, 1, 3776, 4096, LEDGER_BOTTOM_UP, &p));
    CHECK(!pgalloc_alloc_n_owner(K, 1, 8192, 12288, LEDGER_BOTTOM_UP, &p));
    CHECK(!pgalloc_alloc_n_owner(K, 1, 8192, 12288, LEDGER_TOP_DOWN, &p));
    CHECK(!pgalloc_alloc_n_owner(K, 1, 1048575, 1048576, 7, &p)); /* unknown direction */
    CHECK(p == 99);
    CHECK(!pgalloc_free_n_owner(K, 1048575, 2));
    CHECK(!pgalloc_alloc_n_owner(K, 4097, 8191, 12289, LEDGER_BOTTOM_UP, &p));
    CHECK(!pgalloc_alloc_n_owner(K, 4097, 8191, 12289, LEDGER_TOP_DOWN, &p));
    CHECK(!pgalloc_alloc_n_owner(K, 1, 1048575, 1048577, LEDGER_BOTTOM_UP, &p));
    CHECK(!pgalloc_alloc_n_owner(K, 1, 0xfffff, 0, LEDGER_BOTTOM_UP, &p));
    CHECK(!pgalloc_free_n_owner(K, 3776, 64));
    CHECK(!pgalloc_init_model(&m, backing, sizeof(backing), 3776, verified));
    CHECK(pgalloc_free_pages() == before);
    CHECK(host_if == 0x202 && saves == restores);
''', flags=('-DPHYSMEM_HOST_TEST=1', '-DPGALLOC_HOST_TEST=1'), physical_core=True)

    def test_pool_exec_child_claim_releases_exact_ab(self):
        self.run_c('''
    u32 a, b, baseline, total, gap, p;
    int na, nb, pass;
    sys_mem_kb = 16384;
    host_pool_boot(sys_mem_kb);
    baseline = pgalloc_free_pages();
    total = pgalloc_total_pages();
    exec_child_claim(&a, &na, &b, &nb);
    /* 窓の撤去 (2026-09-09) で mem_end が 0xfc0000 -> 0x1000000 に伸びた。 */
    CHECK(a == 0x500000UL && na == 2495);
    CHECK(b == 0xfbf000UL && nb == 65);
    CHECK(b + nb * PAGE_SIZE == 0x1000000UL);
    gap = a + na * PAGE_SIZE;
    CHECK(b - gap == EXEC_DYN_RESERVE);
    for (pass = 0; pass < 2; pass++) {
        host_if = pass ? 2 : 0x202;
        /* Live dynamic allocation in the intentional A/B hole survives exit. */
        CHECK(host_alloc_range(1, gap, b) == gap);
        CHECK(ledger_claim_fixed(K, a / PAGE_SIZE, a / PAGE_SIZE + na));
        CHECK(ledger_claim_fixed(K, b / PAGE_SIZE, b / PAGE_SIZE + nb));
        /* same owner: idempotent (nested claims) */
        CHECK(ledger_claim_fixed(K, a / PAGE_SIZE, a / PAGE_SIZE + na));
        CHECK(ledger_claim_fixed(K, b / PAGE_SIZE, b / PAGE_SIZE + nb));
        CHECK(pgalloc_free_pages() == baseline - na - nb - 1);
        p = 99;
        CHECK(!pgalloc_alloc_n_owner(K, 1, a / PAGE_SIZE, (a / PAGE_SIZE) + na, LEDGER_BOTTOM_UP, &p));
        CHECK(!pgalloc_alloc_n_owner(K, 1, b / PAGE_SIZE, (b / PAGE_SIZE) + nb, LEDGER_BOTTOM_UP, &p));
        CHECK(p == 99);
        CHECK(pgalloc_free_n_owner(K, a / PAGE_SIZE, na));
        CHECK(pgalloc_free_n_owner(K, b / PAGE_SIZE, nb));
        CHECK(pgalloc_free_pages() == baseline - 1);
        CHECK(pgalloc_total_pages() == total);
        CHECK(pgalloc_free_n_owner(K, gap / PAGE_SIZE, 1));
        CHECK(pgalloc_free_pages() == baseline);
        CHECK(host_if == (pass ? 2U : 0x202U) && saves == restores);
    }
''', exec_claim=True)

    def test_pool_shlib_failed_load_releases_one_mib(self):
        self.run_c('''
    u32 baseline, total;
    int pages, pass;
    host_pool_boot(16384);
    baseline = pgalloc_free_pages();
    total = pgalloc_total_pages();
    pages = (int)(MEM_SHLIB_SIZE / PAGE_SIZE);
    CHECK(MEM_SHLIB_BASE == 0x400000UL && pages == 256);
    for (pass = 0; pass < 2; pass++) {
        host_if = pass ? 2 : 0x202;
        /* shlib_init: claim before vfs_read, free on failed load/validation. */
        CHECK(ledger_claim_fixed(LEDGER_OWNER_SHLIB, MEM_SHLIB_BASE / PAGE_SIZE,
                                 MEM_SHLIB_END / PAGE_SIZE));
        CHECK(pgalloc_free_pages() == baseline - pages);
        CHECK(ledger_owner_pages(LEDGER_OWNER_SHLIB) == (u32)pages);
        CHECK(pgalloc_free_n_owner(LEDGER_OWNER_SHLIB, MEM_SHLIB_BASE / PAGE_SIZE, pages));
        CHECK(pgalloc_free_pages() == baseline);
        CHECK(pgalloc_total_pages() == total);
        CHECK(host_if == (pass ? 2U : 0x202U) && saves == restores);
    }
''')

    def test_surface_and_live_mixed_claim(self):
        self.run_c('''
    u32 p, allocated, sid, baseline, total;
    struct ledger_surface sf = { .width = 1, .height = 1, .pitch = PAGE_SIZE,
        .planes = 1, .npages = 1, .owner = LEDGER_OWNER_BOOT,
        .backing = LEDGER_SB_RAM, .backend = LEDGER_SF_PEGC,
        .role = LEDGER_ROLE_CLIENT };
    host_pool_boot(16384);
    p = MEM_POOL_BASE / PAGE_SIZE;
    CHECK(pgalloc_alloc_n_owner(LEDGER_OWNER_BOOT, 1, p + 1, p + 2,
                                LEDGER_TOP_DOWN, &allocated));
    sf.first = allocated;
    CHECK(ledger_surface_create(&sf, &sid));
    CHECK(ledger_surface_transfer(sid, LEDGER_OWNER_GSHELL));
    CHECK(owner_map[p + 1] == LEDGER_OWNER_GSHELL &&
          ledger_surfaces[sid].owner == LEDGER_OWNER_GSHELL);
    CHECK(ledger_owner_pages(LEDGER_OWNER_BOOT) == 0 &&
          ledger_owner_pages(LEDGER_OWNER_GSHELL) == 1);
    total = pgalloc_total_pages();
    baseline = pgalloc_free_pages();
    CHECK(host_alloc_range(1, (p + 2) * PAGE_SIZE, (p + 3) * PAGE_SIZE));
    CHECK(!ledger_claim_fixed(K, p, p + 4)); /* other owner rejects atomically */
    CHECK(owner_map[p] == 0 && owner_map[p + 3] == 0);
    CHECK(pgalloc_free_pages() == baseline - 1);
    CHECK(!pgalloc_alloc_n_owner(K, 1, p + 1, p + 2, LEDGER_BOTTOM_UP, &allocated));
    CHECK(!pgalloc_free_n_owner(K, p + 1, 2));
    CHECK(pgalloc_free_pages() == baseline - 1);
    CHECK(pgalloc_free_n_owner(K, p + 2, 1));
    CHECK(pgalloc_free_pages() == baseline);
    CHECK(ledger_surface_transfer(sid, LEDGER_OWNER_GSHELL)); /* GUI reentry */
    CHECK(owner_map[p + 1] == LEDGER_OWNER_GSHELL && pgalloc_total_pages() == total);
    CHECK(ledger_claim_fixed(K, 0, p + 1)); /* boot-ineligible pages never resurrect */
    CHECK(pgalloc_free_pages() == baseline - 1);
    CHECK(!pgalloc_free_n_owner(K, 0, (int)p + 1));
    CHECK(pgalloc_free_n_owner(K, p, 1));
    CHECK(pgalloc_free_pages() == baseline);
''')

    def test_mark_metadata_and_final_page(self):
        self.run_c('''
    struct physmem m;
    static u32 backing[327680] __attribute__((aligned(4096)));
    u32 baseline, total, p;
    physmem_bootstrap_legacy(&m, 16384);
    CHECK(physmem_add_trusted(&m, 1048575, 1048576, PHYSMEM_SOURCE_SYNTHETIC));
    CHECK(pgalloc_init_model(&m, backing, sizeof(backing), 3776, verified));
    baseline = pgalloc_free_pages();
    total = pgalloc_total_pages();
    /* metadata (L1 + L2、T1b) は [3776, 4096)。 */
    CHECK(ledger_claim_fixed(K, 3775, 3775 + 65)); /* live + metadata */
    CHECK(ledger_claim_fixed(K, 8192, 8193)); /* UNKNOWN below high-water */
    CHECK(pgalloc_free_pages() == baseline - 1);
    CHECK(!pgalloc_free_n_owner(K, 3775, 65));
    CHECK(!pgalloc_free_n_owner(K, 3776, 64));
    CHECK(pgalloc_free_n_owner(K, 3775, 1));
    CHECK(ledger_claim_fixed(K, 1048575, 1048576));
    CHECK(ledger_claim_fixed(K, 1048575, 1048576));
    CHECK(pgalloc_free_pages() == baseline - 1);
    CHECK(pgalloc_free_n_owner(K, 1048575, 1));
    CHECK(pgalloc_free_pages() == baseline);
    CHECK(pgalloc_total_pages() == total);
    p = 99;
    CHECK(!pgalloc_alloc_n_owner(K, 1, 3776, 4096, LEDGER_BOTTOM_UP, &p));
    CHECK(!pgalloc_alloc_n_owner(K, 1, 8192, 8193, LEDGER_BOTTOM_UP, &p));
    CHECK(p == 99);
''', flags=('-DPHYSMEM_HOST_TEST=1', '-DPGALLOC_HOST_TEST=1'))

    def test_surface_and_atomic_free(self):
        self.run_c('''
    u32 p, first, sid, before;
    struct ledger_surface sf = { .width = 1, .height = 1, .pitch = PAGE_SIZE,
        .planes = 1, .npages = 1, .owner = LEDGER_OWNER_BOOT,
        .backing = LEDGER_SB_RAM, .backend = LEDGER_SF_PEGC,
        .role = LEDGER_ROLE_CLIENT };
    host_pool_boot(16384);
    p = MEM_POOL_BASE;
    CHECK(pgalloc_alloc_n_owner(LEDGER_OWNER_BOOT, 1, p / PAGE_SIZE,
                                p / PAGE_SIZE + 1, LEDGER_TOP_DOWN, &first));
    sf.first = first;
    CHECK(ledger_surface_create(&sf, &sid));
    CHECK(ledger_surface_transfer(sid, LEDGER_OWNER_GSHELL));
    before = pgalloc_free_pages();
    CHECK(!pgalloc_free_n_owner(K, p / PAGE_SIZE, 1));
    CHECK(pgalloc_free_pages() == before);
    CHECK(host_alloc_range(2, p + PAGE_SIZE, p + 3 * PAGE_SIZE) == p + PAGE_SIZE);
    before = pgalloc_free_pages();
    CHECK(!pgalloc_free_n_owner(K, p / PAGE_SIZE, 3));
    CHECK(pgalloc_free_pages() == before);
    CHECK(!pgalloc_free_n_owner(K, p / PAGE_SIZE + 1, 3));
    CHECK(pgalloc_free_pages() == before);
    CHECK(pgalloc_free_n_owner(K, p / PAGE_SIZE + 1, 2));
    CHECK(pgalloc_free_pages() == before + 2);
    host_pool_boot(8192);
    CHECK(pgalloc_free_pages() == before + 2);
    CHECK(host_if == 0x202 && saves == restores);
''')

if __name__ == '__main__':
    unittest.main()
