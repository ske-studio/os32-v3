"""Device broker core: real sys/pgalloc ILP32, no device I/O or mapping."""
import unittest
import pathlib
import subprocess
import tempfile
import test_pgalloc_model as harness

PRE = '''
#ifndef SYS_DEVICE_MAX_SPANS
#define SYS_DEVICE_MAX_SPANS 16
#define SYS_DEVICE_MMIO 1
#define SYS_DEVICE_RAM 2
#define SYS_DEVICE_IDLE 1
#define SYS_DEVICE_RAM_MAPPED 2
struct sys_device_span { u32 first, end, kind; };
struct sys_device_capability { u32 flags, mapped_first, mapped_end; };
extern int sys_device_reserve_core(u32, const struct sys_device_span *, u32,
    const struct sys_device_capability *) __attribute__((weak));
#endif
'''

class Broker(unittest.TestCase):
    def test_real_online(self):
        root = harness.ROOT
        with tempfile.TemporaryDirectory(prefix='os32-device-stage-') as tmp:
            tmp = pathlib.Path(tmp)
            for unit in ('paging', 'pgalloc', 'sys'):
                source = (root / f'kernel/{unit}.c').read_text()
                source = source.replace('irq_save()', 'host_irq_save()').replace('irq_restore(flags)', 'host_irq_restore(flags)')
                (tmp / f'{unit}_host_source.c').write_text(source)
            cmd = ['gcc', '-m32', '-march=i386', '-std=gnu89', '-Wall', '-Wextra', '-Werror', '-Wdeclaration-after-statement', '-ffreestanding', '-fno-pie', '-fno-stack-protector', '-nostdlib', '-static', '-no-pie', '-ffunction-sections', '-Wl,--gc-sections', '-DPHYSMEM_HOST_TEST=1']
            # arch/x86 + platform/pc98: include/io.h / include/cpu.h は契約
            # だけで、実装は固定名 arch_io.h / arch_cpu.h / platform_io.h を
            # 引く (順序 3・5)。CR0 / CR3 を触る arch_cpu.h だけは、ホストでは
            # tools/tests/host_arch/ の実装が先に見つかるようにする。
            cmd += ['-I' + str(root / 'tools/tests/host_arch')]
            cmd += ['-I' + str(root / p) for p in ('include', 'arch/x86', 'platform/pc98', 'kernel', 'lib', 'drivers', 'sdk/include/os32')] + ['-I' + str(tmp)]
            subprocess.run(cmd + [str(root / 'tools/tests/device_reservation_stage_host.c'), str(root / 'kernel/physmem.c'), '-o', str(tmp / 'test')], check=True)
            subprocess.run([str(tmp / 'test')], check=True, timeout=60)

    def run_c(self, body, **kw):
        # Reuse the real-source IRQ/bitmap invariant harness. Its entry calls
        # our body after file-scope weak declarations (also supports RED).
        for initial_if in (0x202, 2):
            source = ('extern int device_test(void); return device_test(); }\n'
                      + PRE + '\nint device_test(void) {\n' + body
                      + f'\nCHECK(host_if == {initial_if} && saves == restores);\n')
            # Set IF before the first operation, without declarations-after-code.
            source = source.replace('return device_test();',
                                    f'host_if = {initial_if}; return device_test();', 1)
            harness.Integration().run_c(source, **kw)

    def test_atomic_ram_and_later_live_collision(self):
        self.run_c('''
    struct physmem m;
    static u32 backing[2048] __attribute__((aligned(4096)));
    struct sys_device_span s[2] = {{4096, 4224, SYS_DEVICE_MMIO},
                                  {4500, 4502, SYS_DEVICE_MMIO}};
    struct sys_device_capability cap = {SYS_DEVICE_IDLE, 0, 0};
    u32 total, free, p, i, old[512];
    physmem_bootstrap_legacy(&m, 16384);
    CHECK(physmem_add_trusted(&m, 4096, 8192, PHYSMEM_SOURCE_SYNTHETIC));
    CHECK(pgalloc_init_model(&m, backing, sizeof(backing), 4030, verified));
    /* Ownership fixture only: mapping/publish is covered by highram_stage.
     * Broker must work on the actual allocator, not a changed model copy. */
    online = 1;
    CHECK(pgalloc_alloc_n_pfn(1, 4501, 4502, &p));
    total = pgalloc_total_pages(); free = pgalloc_free_pages();
    for (i = 0; i < 512; i++) old[i] = backing[i];
    CHECK(!sys_device_reserve_core(1, s, 2, &cap));
    CHECK(device_claims == 0);
    for (i = 0; i < 512; i++) CHECK(old[i] == backing[i]);
    CHECK(total == pgalloc_total_pages() && free == pgalloc_free_pages());
    CHECK(pgalloc_free_n_pfn(p, 1));
    CHECK(sys_device_reserve_core(1, s, 2, &cap));
    CHECK(pgalloc_total_pages() == total - 130);
    CHECK(pgalloc_free_pages() == free + 1 - 130);
    CHECK(!pgalloc_free_n_pfn(4096, 128));
    pgalloc_mark_used(4096 * PAGE_SIZE, 128);
    CHECK(!pgalloc_alloc_n_pfn(1, 4096, 4224, &p));
    CHECK(sys_device_reserve_core(1, s, 2, &cap));
    CHECK(pgalloc_total_pages() == total - 130);
''', flags=('-DPHYSMEM_HOST_TEST=1', '-DPGALLOC_HOST_TEST=1'))

    def test_fixed_arena_and_permanent_provenance(self):
        self.run_c('''
    struct sys_device_span s = {3840, 3968, SYS_DEVICE_MMIO};
    struct sys_device_capability cap = {SYS_DEVICE_IDLE, 0, 0};
    u32 total, i;
    const u32 fixed[] = {0, 1024, 1280, 3711, 3967, 4032};
    sys_mem_kb = 16384; pgalloc_init(sys_mem_kb);
    total = pgalloc_total_pages();
    for (i = 0; i < sizeof(fixed)/sizeof(fixed[0]); i++) {
        s.first = fixed[i]; s.end = s.first + 1;
        CHECK(!sys_device_reserve_core(1, &s, 1, &cap));
    }
    CHECK(total == pgalloc_total_pages() && device_claims == 0);
''')
        self.run_c('''
    struct physmem m;
    static u32 backing[2048] __attribute__((aligned(4096)));
    struct sys_device_span s = {4030, 4031, SYS_DEVICE_MMIO};
    struct sys_device_capability cap = {SYS_DEVICE_IDLE, 0, 0};
    struct pgalloc_layout l = {backing, sizeof(backing), 4030, 4000, 4030};
    u32 total;
    physmem_bootstrap_legacy(&m, 16384);
    CHECK(physmem_add_trusted(&m, 4096, 8192, PHYSMEM_SOURCE_SYNTHETIC));
    CHECK(physmem_exclude(&m, 6000, 6001, PHYSMEM_RESERVED));
    CHECK(physmem_exclude(&m, 6002, 6003, PHYSMEM_MMIO));
    CHECK(pgalloc_init_layout(&m, &l, verified));
    CHECK(!sys_device_reserve_core(1, &s, 1, &cap)); /* BOOTSTRAP */
    online = 1;
    total = pgalloc_total_pages();
    CHECK(!sys_device_reserve_core(1, &s, 1, &cap)); /* metadata */
    s.first = 4000; s.end = 4030;
    CHECK(!sys_device_reserve_core(1, &s, 1, &cap)); /* whole workspace */
    s.first = 6000; s.end = 6001;
    CHECK(!sys_device_reserve_core(1, &s, 1, &cap));
    s.first = 6002; s.end = 6003;
    CHECK(!sys_device_reserve_core(1, &s, 1, &cap));
    CHECK(pgalloc_reserve_pfn(6100, 6101));
    s.first = 6100; s.end = 6101;
    CHECK(!sys_device_reserve_core(1, &s, 1, &cap));
    CHECK(pgalloc_total_pages() == total - 1 && device_claims == 0);
''', flags=('-DPHYSMEM_HOST_TEST=1', '-DPGALLOC_HOST_TEST=1'))

    def test_normalized_set_capacity_and_bounds(self):
        self.run_c('''
    struct sys_device_span s[3] = {{4096,4608,SYS_DEVICE_MMIO},
        {3936,3944,SYS_DEVICE_MMIO}, {4200,4300,SYS_DEVICE_MMIO}};
    struct sys_device_capability cap = {SYS_DEVICE_IDLE,0,0};
    u32 i, total;
    sys_mem_kb = 8192; pgalloc_init(sys_mem_kb);
    CHECK(sys_device_reserve_core(1,s,3,&cap)); /* Xe10 + duplicate subset */
    CHECK(device_claims == 2);
    s[0].first = 3936; s[0].end = 3944;
    s[1].first = 4096; s[1].end = 4608;
    CHECK(sys_device_reserve_core(1,s,2,&cap)); /* order independent */
    CHECK(!sys_device_reserve_core(1,s,1,&cap));
    for (i = 2; i < SYS_DEVICE_MAX_SPANS; i++) {
        s[0].first = 5000 + i * 2; s[0].end = s[0].first + 1;
        CHECK(sys_device_reserve_core(i,s,1,&cap));
    }
    total = pgalloc_total_pages();
    s[0].first = PHYSMEM_MAX_PFN - 1; s[0].end = PHYSMEM_MAX_PFN;
    CHECK(!sys_device_reserve_core(100,s,1,&cap)); /* full */
    CHECK(pgalloc_total_pages() == total && device_claims == SYS_DEVICE_MAX_SPANS);
''')
        self.run_c('''
    struct sys_device_span s = {PHYSMEM_MAX_PFN - 1,PHYSMEM_MAX_PFN,SYS_DEVICE_MMIO};
    struct sys_device_capability cap = {SYS_DEVICE_IDLE,0,0};
    CHECK(!sys_device_reserve_core(1,&s,1,&cap));
    sys_mem_kb = 8192; pgalloc_init(sys_mem_kb);
    CHECK(!sys_device_reserve_core(0,&s,1,&cap));
    CHECK(!sys_device_reserve_core(1,0,1,&cap));
    CHECK(!sys_device_reserve_core(1,&s,0,&cap));
    CHECK(!sys_device_reserve_core(1,&s,0xffffffffUL,&cap));
    CHECK(!sys_device_reserve_core(1,&s,1,0));
    cap.flags = 0;
    CHECK(!sys_device_reserve_core(1,&s,1,&cap));
    cap.flags = SYS_DEVICE_IDLE;
    s.end++;
    CHECK(!sys_device_reserve_core(1,&s,1,&cap));
    s.end = s.first;
    CHECK(!sys_device_reserve_core(1,&s,1,&cap));
    s.end--;
    CHECK(!sys_device_reserve_core(1,&s,1,&cap));
    s.end = PHYSMEM_MAX_PFN;
    CHECK(sys_device_reserve_core(1,&s,1,&cap));
    CHECK(device_ledger[0].span.end == PHYSMEM_MAX_PFN);
''')

    def test_bb_ram_capability_and_immutable_kind(self):
        self.run_c('''
    struct physmem m;
    static u32 backing[2048] __attribute__((aligned(4096)));
    struct sys_device_span s[2] = {{9000,9128,SYS_DEVICE_MMIO},
                                  {4096,4171,SYS_DEVICE_RAM}};
    struct sys_device_capability cap = {SYS_DEVICE_IDLE,4096,4171};
    u32 total, p;
    physmem_bootstrap_legacy(&m, 16384);
    CHECK(physmem_add_trusted(&m,4096,8192,PHYSMEM_SOURCE_SYNTHETIC));
    CHECK(pgalloc_init_model(&m,backing,sizeof(backing),4030,verified));
    online = 1;
    total = pgalloc_total_pages();
    CHECK(!sys_device_reserve_core(1,s,2,&cap)); /* no mapping capability */
    cap.flags |= SYS_DEVICE_RAM_MAPPED;
    cap.mapped_end--;
    CHECK(!sys_device_reserve_core(1,s,2,&cap));
    cap.mapped_end++;
    s[1].first = 8500; s[1].end = 8501;
    cap.mapped_first = 8500; cap.mapped_end = 8501;
    CHECK(!sys_device_reserve_core(1,s,2,&cap)); /* unknown is not BB RAM */
    s[1].first = cap.mapped_first = 4096;
    s[1].end = cap.mapped_end = 4171;
    CHECK(sys_device_reserve_core(1,s,2,&cap));
    CHECK(pgalloc_total_pages() == total - 75 && pgalloc_free_pages() == total - 75);
    CHECK(sys_device_reserve_core(1,s,2,&cap));
    CHECK(!sys_device_reserve_core(2,s,2,&cap));
    s[1].kind = SYS_DEVICE_MMIO;
    CHECK(!sys_device_reserve_core(1,s,2,&cap));
    CHECK(!pgalloc_free_n_pfn(4096,75));
    pgalloc_mark_used(4096 * PAGE_SIZE,75);
    CHECK(!pgalloc_alloc_n_pfn(1,4096,4171,&p));
    CHECK(pgalloc_total_pages() == total - 75);
''', flags=('-DPHYSMEM_HOST_TEST=1', '-DPGALLOC_HOST_TEST=1'))

    def test_late_sys_arena_and_unknown_permanent(self):
        self.run_c('''
    struct sys_device_span s = {3000,3001,SYS_DEVICE_MMIO};
    struct sys_device_capability cap = {SYS_DEVICE_IDLE,0,0};
    sys_mem_kb = 8192; pgalloc_init(sys_mem_kb);
    sys_mem_kb = 16384; /* do not allow a stale boot snapshot to under-protect */
    CHECK(!sys_device_reserve_core(1,&s,1,&cap));
''')
        self.run_c('''
    struct physmem m;
    static u32 backing[2048] __attribute__((aligned(4096)));
    struct sys_device_span s = {8500,8501,SYS_DEVICE_MMIO};
    struct sys_device_capability cap = {SYS_DEVICE_IDLE,0,0};
    physmem_bootstrap_legacy(&m,16384);
    CHECK(physmem_add_trusted(&m,9000,9001,PHYSMEM_SOURCE_SYNTHETIC));
    CHECK(pgalloc_init_model(&m,backing,sizeof(backing),4030,verified));
    online = 1;
    CHECK(pgalloc_reserve_pfn(8500,8501));
    CHECK(!sys_device_reserve_core(1,&s,1,&cap));
''', flags=('-DPHYSMEM_HOST_TEST=1', '-DPGALLOC_HOST_TEST=1'))

    def test_capacity_rejects_whole_transaction(self):
        self.run_c('''
    struct physmem m;
    static u32 backing[2048] __attribute__((aligned(4096)));
    struct sys_device_span s[2] = {{10000,10001,SYS_DEVICE_MMIO},
                                  {11000,11001,SYS_DEVICE_MMIO}};
    struct sys_device_capability cap = {SYS_DEVICE_IDLE | SYS_DEVICE_RAM_MAPPED,4096,4097};
    u32 i, total, p;
    physmem_bootstrap_legacy(&m,16384);
    CHECK(physmem_add_trusted(&m,4096,8192,PHYSMEM_SOURCE_SYNTHETIC));
    CHECK(pgalloc_init_model(&m,backing,sizeof(backing),4030,verified));
    online = 1;
    for (i = 1; i < SYS_DEVICE_MAX_SPANS; i++) {
        CHECK(sys_device_reserve_core(i,s,1,&cap));
        s[0].first += 2; s[0].end += 2;
    }
    total = pgalloc_total_pages();
    s[0].first = 4096; s[0].end = 4097; s[0].kind = SYS_DEVICE_RAM;
    CHECK(!sys_device_reserve_core(100,s,2,&cap));
    CHECK(device_claims == SYS_DEVICE_MAX_SPANS - 1);
    CHECK(pgalloc_total_pages() == total && pgalloc_free_pages() == total);
    CHECK(pgalloc_alloc_n_pfn(1,4096,4097,&p));
    CHECK(pgalloc_free_n_pfn(p,1));
    CHECK(sys_device_reserve_core(100,s,1,&cap)); /* last slot still available */
    CHECK(device_claims == SYS_DEVICE_MAX_SPANS && pgalloc_total_pages() == total - 1);
    CHECK(sys_device_reserve_core(100,s,1,&cap)); /* retry at capacity */
''', flags=('-DPHYSMEM_HOST_TEST=1', '-DPGALLOC_HOST_TEST=1'))

    def test_optional_pegc_unknown_exact_retry(self):
        self.run_c('''
    struct sys_device_span s = { 3840, 3968, SYS_DEVICE_MMIO };
    struct sys_device_capability cap = { SYS_DEVICE_IDLE, 0, 0 };
    u32 total;
    CHECK(sys_device_reserve_core != 0);
    sys_mem_kb = 8192;
    pgalloc_init(sys_mem_kb);
    total = pgalloc_total_pages();
    /* 8MiB。窓の撤去 (2026-09-09) で末尾 64 ページが戻り 1984 -> 2048。 */
    CHECK(total == 2048 - 1024);
    CHECK(sys_device_reserve_core(1, &s, 1, &cap));
    CHECK(sys_device_reserve_core(1, &s, 1, &cap));
    CHECK(pgalloc_total_pages() == total && pgalloc_free_pages() == total);
    CHECK(sys_usable_mem_end() == 2048 * PAGE_SIZE);
    CHECK(!sys_device_reserve_core(2, &s, 1, &cap));
    s.end--;
    CHECK(!sys_device_reserve_core(1, &s, 1, &cap));
''')

if __name__ == '__main__':
    unittest.main()
