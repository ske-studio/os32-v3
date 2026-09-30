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
            body = body.replace('pgalloc_alloc_n_pfn(', 'alloc_n_pfn(')
        with tempfile.TemporaryDirectory(prefix='os32-model-') as tmp:
            tmp = pathlib.Path(tmp)
            source = (ROOT / 'kernel/pgalloc.c').read_text()
            source = source.replace('#include "io.h"', '')
            source += (ROOT / 'kernel/sys.c').read_text().replace('#include "io.h"', '')
            if exec_claim:
                # Compile the actual layout helper, not a parallel test formula.
                exec_source = (ROOT / 'exec/exec.c').read_text()
                reserve = next(line for line in exec_source.splitlines()
                               if line.startswith('#define EXEC_DYN_RESERVE '))
                claim = exec_source.split('static void exec_child_claim(', 1)[1]
                claim = 'static void exec_child_claim(' + claim.split('\n}', 1)[0] + '\n}\n'
                source += '\n' + reserve + '\n' + claim
            pre = '''#include "types.h"
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
void kprintf(unsigned char a, const char *f, ...) { (void)a; (void)f; }
'''
            (tmp / 'test.c').write_text(pre + source + '''
static void host_bad_irq(void) __attribute__((noreturn, no_instrument_function));
static void host_bad_irq(void) {
    __asm__ volatile("int $0x80" : : "a"(1), "b"(250) : "memory"); for (;;) {}
}
static void host_verify_commit(void) {
    u32 p, e, a, ne, na;
    if (host_if & 0x200U) host_bad_irq();
    if (!initialized) return;
    ne = na = 0;
    for (p = 0; p < limit_pfn; p++) {
        e = (eligible[p / 32] >> (p % 32)) & 1;
        a = (bitmap[p / 32] >> (p % 32)) & 1;
        if (a && !e) host_bad_irq();
        ne += e; na += a;
    }
    if (ne != total_pages || na != used_pages) host_bad_irq();
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
static int test(void) {
''' + body + '''
return 0;
}
void _start(void) { int r = test(); __asm__ volatile("int $0x80" : : "a"(1), "b"(r) : "memory"); for (;;) {} }
''')
            cmd = ['gcc', '-m32', '-march=i386', '-std=gnu89', '-Wall', '-Wextra', '-Werror', '-Wdeclaration-after-statement', '-ffreestanding', '-fno-pie', '-fno-stack-protector', '-nostdlib', '-static', '-no-pie', '-ffunction-sections', '-finstrument-functions', '-Wl,--gc-sections']
            cmd += ['-I' + str(ROOT / p) for p in ('include', 'kernel', 'lib', 'drivers', 'sdk/include/os32')]
            subprocess.run(cmd + list(flags) + [str(tmp / 'test.c'), str(ROOT / 'kernel/physmem.c'), '-o', str(tmp / 'test')], check=True)
            result = subprocess.run([str(tmp / 'test')])
            self.assertEqual(result.returncode, 0, 'C CHECK failure (250 = IRQ/commit invariant): ' + str(result.returncode) + '\n' + (tmp / 'test.c').read_text())

    def test_metadata_failure_transactions(self):
        self.run_c('''
    struct physmem m, before;
    static u32 backing[65536 + 1] __attribute__((aligned(4096)));
    unsigned char *a, *b;
    u32 i;
    physmem_bootstrap_legacy(&m, 16384);
    CHECK(physmem_add_trusted(&m, 1048575, 1048576, PHYSMEM_SOURCE_SYNTHETIC));
    before = m;
    for (i = 0; i < 65537; i++) backing[i] = 0xace01234UL;
    CHECK(!pgalloc_init_model(&m, backing, 262143, 4032, verified));
    CHECK(!pgalloc_init_model(&m, backing, 262144, 4096, verified));
    CHECK(!pgalloc_init_model(&m, backing, 262144, 1024, verified));
    CHECK(!pgalloc_init_model(&m, backing, 262144, 4032, denied));
    a = (unsigned char *)&m; b = (unsigned char *)&before;
    for (i = 0; i < sizeof(m); i++) CHECK(a[i] == b[i]);
    for (i = 0; i < 65537; i++) CHECK(backing[i] == 0xace01234UL);
    /* 窓の撤去 (2026-09-09) で bootstrap の区間が 1 つ減った。容量ちょうどに
     * するのは分割 1 回あたり 2 区間なので、29 + 末尾 1 本 (= 63) ではなく
     * 30 回で 64 に届く。狙いは「容量いっぱいのモデルでは init_model が
     * 何も変えずに失敗する」ことの確認で、そこは変わらない。 */
    for (i = 0; i < 30; i++)
        CHECK(physmem_exclude(&m, 20000 + i * 2, 20001 + i * 2, PHYSMEM_RESERVED));
    CHECK(m.count == PHYSMEM_MAX_RANGES);
    before = m;
    CHECK(!pgalloc_init_model(&m, backing, 262144, 3900, verified));
    for (i = 0; i < sizeof(m); i++) CHECK(a[i] == b[i]);
    for (i = 0; i < 65537; i++) CHECK(backing[i] == 0xace01234UL);
    physmem_bootstrap_legacy((struct physmem *)backing, 16384);
    CHECK(!pgalloc_init_model((struct physmem *)backing, backing, 262144, 4095, verified));
    /* 窓の撤去で bootstrap の区間は 3 本 (旧 4 本)。 */
    CHECK(((struct physmem *)backing)->count == 3);
    physmem_bootstrap_legacy(&m, 16384);
    CHECK(pgalloc_init_model(&m, backing, 262144, 4095, verified));
    CHECK(backing[65536] == 0xace01234UL);
    CHECK(pgalloc_metadata_bytes(&m) == PAGE_SIZE);
    CHECK(pgalloc_limit_pfn() == 4096);
''', flags=('-DPHYSMEM_HOST_TEST=1', '-DPGALLOC_HOST_TEST=1'))

    def test_production_rejects_forged_synthetic_and_host_backing(self):
        for flags in ((), ('-D__KERNEL_BUILD__',),
                      ('-D__KERNEL_BUILD__', '-DPHYSMEM_HOST_TEST=1', '-DPGALLOC_HOST_TEST=1')):
            self.run_c('''
    struct physmem m;
    static u32 backing[1024] __attribute__((aligned(4096)));
    physmem_bootstrap_legacy(&m, 16384);
    m.ranges[1].sources = PHYSMEM_SOURCE_SYNTHETIC;
    CHECK(pgalloc_metadata_bytes(&m) == 0);
    CHECK(!pgalloc_init_model(&m, backing, sizeof(backing), 4031, verified));
    m.ranges[1].sources = PHYSMEM_SOURCE_LEGACY;
    CHECK(pgalloc_metadata_bytes(&m) == PAGE_SIZE);
    CHECK(!pgalloc_init_model(&m, backing, sizeof(backing), 4031, verified));
    CHECK(pgalloc_total_pages() == 0);
''', flags=flags)

    def test_32_and_64_mib_capacity(self):
        for end in (8192, 16384):
            self.run_c('''
    struct physmem m;
    static u32 backing[2048 + 1] __attribute__((aligned(4096)));
    u32 bytes, first, p, before;
    physmem_bootstrap_legacy(&m, 16384);
    CHECK(physmem_add_trusted(&m, 4096, END, PHYSMEM_SOURCE_SYNTHETIC));
    bytes = pgalloc_metadata_bytes(&m);
    CHECK(bytes == ((END / 32 * 8 + 4095) & ~4095UL));
    first = 4096 - bytes / PAGE_SIZE;
    backing[2048] = 0x12345678UL;
    CHECK(pgalloc_init_model(&m, backing, bytes, first, verified));
    CHECK(pgalloc_limit_pfn() == END);
    before = pgalloc_free_pages();
    CHECK(pgalloc_alloc_n_pfn(END - 4096, 4096, END, &p) && p == 4096);
    CHECK(pgalloc_free_pages() == before - (END - 4096));
    CHECK(!pgalloc_reserve_pfn(4095, 4097));
    CHECK(pgalloc_free_pages() == before - (END - 4096));
    host_if = 2;
    CHECK(pgalloc_free_n_pfn(p, END - 4096));
    CHECK(host_if == 2 && saves == restores);
    CHECK(pgalloc_free_pages() == before);
    CHECK(backing[2048] == 0x12345678UL);
'''.replace('END', str(end)), flags=('-DPHYSMEM_HOST_TEST=1', '-DPGALLOC_HOST_TEST=1'), physical_core=True)

    def test_generic_never_returns_unmapped_machine_ram(self):
        self.run_c('''
    struct physmem m;
    static u32 backing[1024] __attribute__((aligned(4096)));
    u32 p;
    physmem_bootstrap_legacy(&m, 8192);
    CHECK(physmem_add_trusted(&m, 3072, 3073, PHYSMEM_SOURCE_MACHINE));
    CHECK(pgalloc_init_model(&m, backing, sizeof(backing), 1983, verified));
    CHECK(pgalloc_alloc_n(959) == 0);
    CHECK(pgalloc_alloc_page() == 0);
    p = 99;
    CHECK(!pgalloc_alloc_n_pfn(1, 3072, 3073, &p) && p == 99);
''', flags=('-DPGALLOC_HOST_TEST=1',))

    def test_sys_model_handoff(self):
        self.run_c('''
    struct physmem m;
    static u32 backing[65536] __attribute__((aligned(4096)));
    u32 p;
    CHECK(sys_memory_init_model != 0);
    physmem_bootstrap_legacy(&m, 16384);
    CHECK(physmem_add_trusted(&m, 1048575, 1048576, PHYSMEM_SOURCE_SYNTHETIC));
    CHECK(sys_memory_init_model(&m, backing, sizeof(backing), 4032, verified));
    CHECK(sys_usable_mem_end() == 4032 * PAGE_SIZE);
    sys_mem_kb = 65536;
    CHECK(sys_usable_mem_end() == 4032 * PAGE_SIZE);
    /* The model path must still honour sys_reserve_top: the PEGC 8bpp
       backbuffer (H2) is its only caller and refusing it disables PEGC.
       Metadata/workspace sit above the frozen exec ceiling, so the carve
       lowers that ceiling. The hotdeploy window was retired 2026-09-09,
       so the arena now ends at real RAM. */
    CHECK(sys_reserve_top(PAGE_SIZE) == 4031 * PAGE_SIZE);
    CHECK(sys_usable_mem_end() == 4031 * PAGE_SIZE);
    CHECK(!pgalloc_alloc_n_range(1, 4031 * PAGE_SIZE, 4032 * PAGE_SIZE));
    CHECK(!pgalloc_alloc_n_pfn(1, 1048575, 1048576, &p));
    CHECK(!sys_memory_init_model(&m, backing, sizeof(backing), 4032, verified));
''', flags=('-DPHYSMEM_HOST_TEST=1', '-DPGALLOC_HOST_TEST=1'))

    def test_sys_low_stable(self):
        self.run_c('''
    u32 base;
    sys_mem_kb = 0xffffffffUL;
    /* Retired hotdeploy window (2026-09-09): the arena ends at real RAM,
       clamped to PHYSMEM_LEGACY_MAX_PFN = 16MiB. */
    CHECK(sys_usable_mem_end() == 0x1000000UL);
    base = sys_usable_mem_end();
    CHECK(!sys_reserve_top(0xffffffffUL));
    CHECK(sys_usable_mem_end() == base);
    pgalloc_init(16384);
    CHECK(sys_reserve_top(PAGE_SIZE) == base - PAGE_SIZE);
    CHECK(sys_usable_mem_end() == base - PAGE_SIZE);
    CHECK(pgalloc_alloc_n_range(1, base - PAGE_SIZE, base) == 0);
''')

    def test_dynamic_sparse_final(self):
        self.run_c('''
    struct physmem m;
    static u32 backing[65536] __attribute__((aligned(4096)));
    u32 p, before;
    CHECK(pgalloc_init_model != 0);
    physmem_bootstrap_legacy(&m, 16384);
    CHECK(physmem_add_trusted(&m, 4096, 8192, PHYSMEM_SOURCE_SYNTHETIC));
    CHECK(physmem_add_trusted(&m, 12288, 16384, PHYSMEM_SOURCE_SYNTHETIC));
    CHECK(physmem_add_trusted(&m, 1048575, 1048576, PHYSMEM_SOURCE_SYNTHETIC));
    CHECK(!pgalloc_init_model(&m, backing, sizeof(backing) - 1, 4032, verified));
    CHECK(!pgalloc_init_model(&m, backing + 1, sizeof(backing), 4032, verified));
    CHECK(!pgalloc_init_model(&m, backing, sizeof(backing), 4064, verified));
    CHECK(!pgalloc_init_model(&m, backing, sizeof(backing), 4032, 0));
    CHECK(pgalloc_init_model(&m, backing, sizeof(backing), 4032, verified));
    CHECK(pgalloc_limit_pfn() == 1048576);
    CHECK(pgalloc_alloc_n_pfn(1, 1048575, 1048576, &p) && p == 1048575);
    CHECK(pgalloc_free_n_pfn(p, 1));
    CHECK(pgalloc_alloc_n_pfn(4096, 4096, 8192, &p) && p == 4096);
    CHECK(pgalloc_free_n_pfn(p, 4096));
    CHECK(pgalloc_alloc_n_pfn(4096, 12288, 16384, &p) && p == 12288);
    before = pgalloc_free_pages();
    p = 99;
    CHECK(!pgalloc_alloc_n_pfn(0, 1048575, 1048576, &p));
    CHECK(!pgalloc_alloc_n_pfn(-1, 1048575, 1048576, &p));
    CHECK(!pgalloc_alloc_n_pfn(2147483647, 1048575, 1048576, &p));
    CHECK(!pgalloc_alloc_n_pfn(1, 4032, 4096, &p));
    CHECK(!pgalloc_alloc_n_pfn(1, 8192, 12288, &p));
    CHECK(p == 99);
    CHECK(!pgalloc_free_n_pfn(1048575, 2));
    CHECK(!pgalloc_reserve_pfn(0, 1048577));
    CHECK(!pgalloc_alloc_n_pfn(4097, 8191, 12289, &p));
    CHECK(!pgalloc_alloc_n_pfn(1, 1048575, 1048577, &p));
    CHECK(!pgalloc_alloc_n_range(1, 0xfffff000UL, 0));
    CHECK(!pgalloc_free_n_pfn(3968, 64));
    CHECK(!pgalloc_init_model(&m, backing, sizeof(backing), 3968, verified));
    CHECK(pgalloc_free_pages() == before);
    CHECK(host_if == 0x202 && saves == restores);
''', flags=('-DPHYSMEM_HOST_TEST=1', '-DPGALLOC_HOST_TEST=1'), physical_core=True)

    def test_legacy_exec_child_claim_releases_exact_ab(self):
        self.run_c('''
    u32 a, b, baseline, total, gap, p;
    int na, nb, pass;
    sys_mem_kb = 16384;
    pgalloc_init(sys_mem_kb);
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
        CHECK(pgalloc_alloc_n_range(1, gap, b) == gap);
        pgalloc_mark_used(a, na);
        pgalloc_mark_used(b, nb);
        pgalloc_mark_used(a, na); /* nested exec's idempotent marks */
        pgalloc_mark_used(b, nb);
        CHECK(pgalloc_free_pages() == baseline - na - nb - 1);
        p = 99;
        CHECK(!pgalloc_alloc_n_pfn(1, a / PAGE_SIZE, (a / PAGE_SIZE) + na, &p));
        CHECK(!pgalloc_alloc_n_pfn(1, b / PAGE_SIZE, (b / PAGE_SIZE) + nb, &p));
        CHECK(p == 99);
        pgalloc_free_n(a, na);
        pgalloc_free_n(b, nb);
        CHECK(pgalloc_free_pages() == baseline - 1);
        CHECK(pgalloc_total_pages() == total);
        CHECK(pgalloc_free_n_pfn(gap / PAGE_SIZE, 1));
        CHECK(pgalloc_free_pages() == baseline);
        CHECK(host_if == (pass ? 2U : 0x202U) && saves == restores);
    }
''', exec_claim=True)

    def test_legacy_shlib_failed_load_releases_one_mib(self):
        self.run_c('''
    u32 baseline, total;
    int pages, pass;
    pgalloc_init(16384);
    baseline = pgalloc_free_pages();
    total = pgalloc_total_pages();
    pages = (int)(MEM_SHLIB_SIZE / PAGE_SIZE);
    CHECK(MEM_SHLIB_BASE == 0x400000UL && pages == 256);
    for (pass = 0; pass < 2; pass++) {
        host_if = pass ? 2 : 0x202;
        /* shlib_init: claim before vfs_read, free on failed load/validation. */
        pgalloc_mark_used(MEM_SHLIB_BASE, pages);
        CHECK(pgalloc_free_pages() == baseline - pages);
        pgalloc_free_n(MEM_SHLIB_BASE, pages);
        CHECK(pgalloc_free_pages() == baseline);
        CHECK(pgalloc_total_pages() == total);
        CHECK(host_if == (pass ? 2U : 0x202U) && saves == restores);
    }
''')

    def test_mark_live_and_permanent_mixture(self):
        self.run_c('''
    u32 p, baseline, total;
    pgalloc_init(16384);
    p = PGALLOC_BASE / PAGE_SIZE;
    CHECK(pgalloc_reserve_pfn(p + 1, p + 2));
    total = pgalloc_total_pages();
    baseline = pgalloc_free_pages();
    CHECK(pgalloc_alloc_n_range(1, (p + 2) * PAGE_SIZE, (p + 3) * PAGE_SIZE));
    pgalloc_mark_used(p * PAGE_SIZE, 4);
    pgalloc_mark_used(p * PAGE_SIZE, 4);
    CHECK(pgalloc_free_pages() == baseline - 3);
    CHECK(pgalloc_total_pages() == total);
    CHECK(!pgalloc_free_n_pfn(p, 4)); /* permanent/live mixed free is atomic */
    CHECK(pgalloc_free_pages() == baseline - 3);
    CHECK(!pgalloc_reserve_pfn(p, p + 4)); /* live conflict is atomic */
    CHECK(pgalloc_total_pages() == total);
    CHECK(pgalloc_free_n_pfn(p, 1));
    CHECK(pgalloc_free_n_pfn(p + 2, 2));
    CHECK(!pgalloc_free_n_pfn(p + 1, 1));
    CHECK(pgalloc_free_pages() == baseline);
    pgalloc_mark_used(0, (int)p + 1); /* boot-ineligible pages never resurrect */
    CHECK(pgalloc_free_pages() == baseline - 1);
    CHECK(pgalloc_total_pages() == total);
    CHECK(!pgalloc_free_n_pfn(0, (int)p + 1));
    CHECK(pgalloc_free_n_pfn(p, 1));
    CHECK(pgalloc_free_pages() == baseline);
''')

    def test_mark_metadata_and_final_page(self):
        self.run_c('''
    struct physmem m;
    static u32 backing[65536] __attribute__((aligned(4096)));
    u32 baseline, total, p;
    physmem_bootstrap_legacy(&m, 16384);
    CHECK(physmem_add_trusted(&m, 1048575, 1048576, PHYSMEM_SOURCE_SYNTHETIC));
    CHECK(pgalloc_init_model(&m, backing, sizeof(backing), 4032, verified));
    baseline = pgalloc_free_pages();
    total = pgalloc_total_pages();
    /* 窓の撤去 (2026-09-09) で metadata が [3968,4032) -> [4032,4096) に上がった。 */
    pgalloc_mark_used(4031 * PAGE_SIZE, 65); /* live + metadata */
    pgalloc_mark_used(8192 * PAGE_SIZE, 1); /* UNKNOWN below high-water */
    CHECK(pgalloc_free_pages() == baseline - 1);
    CHECK(!pgalloc_free_n_pfn(4031, 65));
    CHECK(!pgalloc_free_n_pfn(4032, 64));
    CHECK(pgalloc_free_n_pfn(4031, 1));
    pgalloc_mark_used(0xfffff000UL, 1);
    pgalloc_mark_used(0xfffff000UL, 1);
    CHECK(pgalloc_free_pages() == baseline - 1);
    CHECK(pgalloc_free_n_pfn(1048575, 1));
    CHECK(pgalloc_free_pages() == baseline);
    CHECK(pgalloc_total_pages() == total);
    p = 99;
    CHECK(!pgalloc_alloc_n_pfn(1, 4032, 4096, &p));
    CHECK(!pgalloc_alloc_n_pfn(1, 8192, 8193, &p));
    CHECK(p == 99);
''', flags=('-DPHYSMEM_HOST_TEST=1', '-DPGALLOC_HOST_TEST=1'))

    def test_permanent_and_atomic_free(self):
        self.run_c('''
    u32 p, before;
    pgalloc_init(16384);
    p = PGALLOC_BASE;
    /* Permanent reservation is explicit, not the legacy releasable claim. */
    CHECK(pgalloc_reserve_pfn(p / PAGE_SIZE, p / PAGE_SIZE + 1));
    before = pgalloc_free_pages();
    pgalloc_free_page(p);
    CHECK(pgalloc_free_pages() == before);
    CHECK(pgalloc_alloc_n_range(2, p + PAGE_SIZE, p + 3 * PAGE_SIZE) == p + PAGE_SIZE);
    before = pgalloc_free_pages();
    pgalloc_free_n(p, 3);
    CHECK(pgalloc_free_pages() == before);
    pgalloc_free_n(p + PAGE_SIZE, 3);
    CHECK(pgalloc_free_pages() == before);
    pgalloc_free_n(p + PAGE_SIZE, 2);
    CHECK(pgalloc_free_pages() == before + 2);
    pgalloc_init(8192);
    CHECK(pgalloc_free_pages() == before + 2);
    CHECK(host_if == 0x202 && saves == restores);
''')

if __name__ == '__main__':
    unittest.main()
