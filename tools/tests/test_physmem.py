"""Synthetic host-only physical range tests; never evidence of machine RAM.
Run: python3 tools/tests/test_physmem.py
Uses real ILP32 types.h and GNU89 core, no libc/multilib runtime required.
"""
import pathlib
import subprocess
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[2]


class PhysmemTests(unittest.TestCase):
    def run_c(self, body, flags=('-DPHYSMEM_HOST_TEST=1',)):
        self.assertTrue((ROOT / 'kernel/physmem.c').exists(),
                        'physical range model not implemented')
        with tempfile.TemporaryDirectory(prefix='os32-physmem-') as tmp:
            src = pathlib.Path(tmp) / 'test.c'
            exe = pathlib.Path(tmp) / 'test'
            src.write_text('''#include "physmem.h"
#include "memmap.h"
#include "pegc.h"
#include "wab_xe10.h"
#define CHECK(x) do { if (!(x)) return __LINE__; } while (0)
STATIC_ASSERT(sizeof(u32) == 4, host_is_ilp32);
static int test(void) {
''' + body + '''
return 0;
}
void _start(void) {
    int result;
    result = test();
    __asm__ volatile("int $0x80" : : "a"(1), "b"(result) : "memory");
    for (;;) {}
}
''')
            result = subprocess.run([
                'gcc', '-m32', '-std=gnu89', '-Wall', '-Wextra', '-Werror',
                '-Wdeclaration-after-statement', '-ffreestanding', '-fno-builtin',
                '-fno-pie', '-fno-stack-protector', '-nostdlib', '-no-pie',
                '-I' + str(ROOT / 'include'), '-I' + str(ROOT / 'kernel'),
                str(src), str(ROOT / 'kernel/physmem.c'), '-o', str(exe)
            ] + list(flags), capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            result = subprocess.run([str(exe)])
            self.assertEqual(result.returncode, 0,
                             'C CHECK failed at line ' + str(result.returncode)
                             + '\n' + src.read_text())

    def test_non_host_builds_reject_synthetic_atomically(self):
        # Kernel wins even when the host-only switch is accidentally supplied.
        modes = (
            ('kernel', ('-D__KERNEL_BUILD__',)),
            ('default', ()),
            ('kernel_and_host', ('-D__KERNEL_BUILD__', '-DPHYSMEM_HOST_TEST=1')),
            ('host_disabled', ('-DPHYSMEM_HOST_TEST=0',)),
        )
        for mode, flags in modes:
            for source in ('PHYSMEM_SOURCE_SYNTHETIC',
                           'PHYSMEM_SOURCE_SYNTHETIC | PHYSMEM_SOURCE_MACHINE'):
                with self.subTest(mode=mode, source=source):
                    self.run_c('''
    struct physmem m, before;
    u32 i, count, pfn;
    unsigned char *a, *b;
    physmem_bootstrap_legacy(&m, 0xffffffffUL);
    CHECK(physmem_legacy_end(&m) == 4096);
    CHECK(m.ranges[1].sources == PHYSMEM_SOURCE_LEGACY);
    CHECK(physmem_add_trusted(&m, 5000, 5010, PHYSMEM_SOURCE_MACHINE));
    CHECK(physmem_exclude(&m, 5004, 5006, PHYSMEM_MMIO));
    CHECK(physmem_count(&m, 4096, PHYSMEM_MAX_PFN, PHYSMEM_RAM, &count));
    CHECK(count == 8);
    CHECK(physmem_find(&m, 4096, PHYSMEM_MAX_PFN, 4, 1, &pfn) && pfn == 5000);
    before = m;
    CHECK(!physmem_add_trusted(&m, 4096, 6000, ''' + source + '''));
    a = (unsigned char *)&m; b = (unsigned char *)&before;
    for (i = 0; i < sizeof(m); i++) CHECK(a[i] == b[i]);
    CHECK(physmem_count(&m, 4096, 5000, PHYSMEM_RAM, &count) && count == 0);
    pfn = 99;
    CHECK(!physmem_find(&m, 4096, 5000, 1, 1, &pfn));
    CHECK(pfn == 99);
    CHECK(physmem_count(&m, 4096, PHYSMEM_MAX_PFN, PHYSMEM_RAM, &count));
    CHECK(count == 8);
    CHECK(physmem_legacy_end(&m) == 4096);
    CHECK(!physmem_add_trusted(&m, 4096, 5000, PHYSMEM_SOURCE_LEGACY));
    CHECK(!physmem_add_trusted(&m, 4096, 5000, 0));
    CHECK(!physmem_add_trusted(&m, 4096, 5000, 0xffffffffUL));
    for (i = 0; i < sizeof(m); i++) CHECK(a[i] == b[i]);
''', flags=flags)

    def test_empty_model_is_unknown(self):
        self.run_c('''
    struct physmem m;
    physmem_init(&m);
    CHECK(m.count == 1);
    CHECK(m.ranges[0].first == 0);
    CHECK(m.ranges[0].end == PHYSMEM_MAX_PFN);
    CHECK(m.ranges[0].kind == PHYSMEM_UNKNOWN);
    CHECK(m.ranges[0].sources == 0);
''')

    def test_trusted_fullrange_and_checked_counts(self):
        self.run_c('''
    struct physmem m, before;
    u32 count;
    physmem_init(&m);
    CHECK(physmem_add_trusted(&m, 0, PHYSMEM_MAX_PFN,
                             PHYSMEM_SOURCE_SYNTHETIC));
    CHECK(m.count == 1);
    CHECK(physmem_count(&m, 0, PHYSMEM_MAX_PFN, PHYSMEM_RAM, &count));
    CHECK(count == 1048576UL);
    CHECK(physmem_count(&m, 1048575UL, 1048576UL, PHYSMEM_RAM, &count));
    CHECK(count == 1);
    before = m;
    CHECK(!physmem_add_trusted(&m, 0, 0, PHYSMEM_SOURCE_MACHINE));
    CHECK(!physmem_add_trusted(&m, 2, 1, PHYSMEM_SOURCE_MACHINE));
    CHECK(!physmem_add_trusted(&m, 0, 1048577UL, PHYSMEM_SOURCE_MACHINE));
    CHECK(!physmem_add_trusted(&m, 0xffffffffUL, 1, PHYSMEM_SOURCE_MACHINE));
    CHECK(!physmem_add_trusted(&m, 0, 1, 0));
    CHECK(!physmem_add_trusted(&m, 0, 1, PHYSMEM_SOURCE_LEGACY));
    CHECK(!physmem_add_trusted(&m, 0, 1, 0xffffffffUL));
    CHECK(m.count == before.count && m.ranges[0].sources == before.ranges[0].sources);
    count = 99;
    CHECK(!physmem_count(&m, 1, 0, PHYSMEM_RAM, &count));
    CHECK(!physmem_count(&m, 0, 0xffffffffUL, PHYSMEM_RAM, &count));
    CHECK(!physmem_count(&m, 0, 1, 99, &count));
    CHECK(count == 99);
''')

    def test_reserved_wins_both_orders_and_capacity_is_atomic(self):
        self.run_c(''' 
    struct physmem m, reverse, before;
    u32 i, count;
    unsigned char *a, *b;
    physmem_init(&m);
    physmem_init(&reverse);
    CHECK(physmem_add_trusted(&m, 0, PHYSMEM_MAX_PFN, PHYSMEM_SOURCE_SYNTHETIC));
    CHECK(physmem_exclude(&m, 10, 20, PHYSMEM_RESERVED));
    CHECK(physmem_exclude(&m, 15, 25, PHYSMEM_MMIO));
    CHECK(physmem_exclude(&reverse, 15, 25, PHYSMEM_MMIO));
    CHECK(physmem_exclude(&reverse, 10, 20, PHYSMEM_RESERVED));
    CHECK(physmem_add_trusted(&reverse, 0, PHYSMEM_MAX_PFN, PHYSMEM_SOURCE_SYNTHETIC));
    CHECK(physmem_add_trusted(&m, 0, PHYSMEM_MAX_PFN, PHYSMEM_SOURCE_SYNTHETIC));
    a = (unsigned char *)&m; b = (unsigned char *)&reverse;
    for (i = 0; i < sizeof(m); i++) CHECK(a[i] == b[i]);
    CHECK(physmem_count(&m, 0, 30, PHYSMEM_RAM, &count) && count == 15);
    CHECK(physmem_count(&m, 0, 30, PHYSMEM_RESERVED, &count) && count == 5);
    CHECK(physmem_count(&m, 0, 30, PHYSMEM_MMIO, &count) && count == 10);
    CHECK(!physmem_exclude(&m, 0, 1, PHYSMEM_RAM));
    CHECK(!physmem_exclude(&m, 0, 1, PHYSMEM_UNKNOWN));
    CHECK(!physmem_exclude(&m, 1, 0, PHYSMEM_MMIO));
    physmem_init(&m);
    for (i = 0; i < 31; i++)
        CHECK(physmem_exclude(&m, i * 2 + 1, i * 2 + 2, PHYSMEM_RESERVED));
    CHECK(m.count == 63);
    CHECK(physmem_exclude(&m, 63, PHYSMEM_MAX_PFN, PHYSMEM_MMIO));
    CHECK(m.count == PHYSMEM_MAX_RANGES);
    CHECK(physmem_add_trusted(&m, 62, 63, PHYSMEM_SOURCE_SYNTHETIC));
    /* A lower-priority exclusion does not split an MMIO interval. */
    CHECK(physmem_exclude(&m, 64, 65, PHYSMEM_RESERVED));
    CHECK(physmem_exclude(&m, 63, PHYSMEM_MAX_PFN, PHYSMEM_MMIO));
    physmem_init(&m);
    for (i = 0; i < 31; i++)
        CHECK(physmem_exclude(&m, i * 2 + 1, i * 2 + 2, PHYSMEM_RESERVED));
    before = m;
    CHECK(!physmem_exclude(&m, 64, 65, PHYSMEM_RESERVED));
    CHECK(!physmem_add_trusted(&m, 64, 65, PHYSMEM_SOURCE_SYNTHETIC));
    a = (unsigned char *)&m; b = (unsigned char *)&before;
    for (i = 0; i < sizeof(m); i++) CHECK(a[i] == b[i]);
''')

    def test_bootstrap_caps_hint_and_keeps_legacy_arena_separate(self):
        self.run_c('''
    struct physmem m;
    u32 count;
    physmem_bootstrap_legacy(&m, 0xffffffffUL);
    CHECK(physmem_legacy_end(&m) == 4096UL);
    CHECK(physmem_count(&m, 4096, PHYSMEM_MAX_PFN, PHYSMEM_UNKNOWN, &count));
    CHECK(count == PHYSMEM_MAX_PFN - 4096);
    CHECK(physmem_count(&m, 0, 1024, PHYSMEM_RESERVED, &count) && count == 1024);
    /* ホットデプロイ窓を撤去 (2026-09-09) したので末尾の予約帯は無い。 */
    CHECK(physmem_count(&m, 4032, 4096, PHYSMEM_RAM, &count) && count == 64);
    CHECK(m.ranges[1].sources == PHYSMEM_SOURCE_LEGACY);
    CHECK(physmem_add_trusted(&m, 4096, PHYSMEM_MAX_PFN, PHYSMEM_SOURCE_SYNTHETIC));
    CHECK(physmem_legacy_end(&m) == 4096);
    CHECK(physmem_exclude(&m, 1500, 1501, PHYSMEM_RESERVED));
    CHECK(physmem_legacy_end(&m) == 1500);
    CHECK(physmem_add_trusted(&m, 1500, 1501, PHYSMEM_SOURCE_MACHINE));
    CHECK(physmem_legacy_end(&m) == 1500);
    /* 8MB 相当。窓の撤去 (2026-09-09) で末尾 64 ページが解放され 1984 -> 2048。 */
    physmem_bootstrap_legacy(&m, 8195);
    CHECK(physmem_legacy_end(&m) == 2048);
    physmem_bootstrap_legacy(&m, 0);
    CHECK(physmem_legacy_end(&m) == 0);
    CHECK(physmem_count(&m, 0, PHYSMEM_MAX_PFN, PHYSMEM_RAM, &count) && count == 0);
    physmem_init(&m);
    CHECK(physmem_add_trusted(&m, 0, PHYSMEM_MAX_PFN, PHYSMEM_SOURCE_SYNTHETIC));
    CHECK(physmem_legacy_end(&m) == 0);
''')

    def test_find_and_metadata_reservation_never_cross_holes(self):
        self.run_c('''
    struct physmem m, before;
    u32 pfn, count, i;
    unsigned char *a, *b;
    physmem_init(&m);
    CHECK(physmem_add_trusted(&m, 10, 20, PHYSMEM_SOURCE_SYNTHETIC));
    CHECK(physmem_add_trusted(&m, 20, 30, PHYSMEM_SOURCE_MACHINE));
    CHECK(physmem_find(&m, 0, 40, 20, 1, &pfn) && pfn == 10);
    CHECK(physmem_reserve_ram(&m, 15, 17));
    CHECK(physmem_find(&m, 0, 40, 8, 8, &pfn) == 0);
    CHECK(physmem_find(&m, 0, 40, 6, 8, &pfn) && pfn == 24);
    CHECK(physmem_reserve_ram(&m, pfn, pfn + 6));
    CHECK(!physmem_reserve_ram(&m, 14, 18));
    CHECK(!physmem_reserve_ram(&m, 0, 1));
    CHECK(!physmem_reserve_ram(&m, 30, 29));
    CHECK(physmem_add_trusted(&m, 0, 40, PHYSMEM_SOURCE_SYNTHETIC));
    CHECK(physmem_count(&m, 15, 17, PHYSMEM_RESERVED, &count) && count == 2);
    pfn = 99;
    CHECK(!physmem_find(&m, 0, 40, 0, 1, &pfn));
    CHECK(!physmem_find(&m, 0, 40, 1, 0, &pfn));
    CHECK(!physmem_find(&m, 0, 40, 1, 3, &pfn));
    CHECK(!physmem_find(&m, 0, 40, 0xffffffffUL, 1, &pfn));
    CHECK(!physmem_find(&m, 0, 40, 1, 0x80000000UL, &pfn));
    CHECK(!physmem_find(&m, 40, 0, 1, 1, &pfn));
    CHECK(pfn == 99);
    physmem_init(&m);
    CHECK(physmem_add_trusted(&m, 0, PHYSMEM_MAX_PFN, PHYSMEM_SOURCE_SYNTHETIC));
    CHECK(physmem_find(&m, 0, PHYSMEM_MAX_PFN, PHYSMEM_MAX_PFN, 1, &pfn) && pfn == 0);
    CHECK(physmem_find(&m, 1048575, 1048576, 1, 1, &pfn) && pfn == 1048575);
    CHECK((pfn << PHYSMEM_PAGE_SHIFT) == 0xfffff000UL);
    CHECK(!physmem_find(&m, 1048575, 1048576, 1, 2, &pfn));
    CHECK(physmem_reserve_ram(&m, 1048575, 1048576));
    CHECK(!physmem_find(&m, 1048575, 1048576, 1, 1, &pfn));
    for (i = 0; i < 31; i++)
        CHECK(physmem_exclude(&m, i * 2 + 1, i * 2 + 2, PHYSMEM_RESERVED));
    CHECK(m.count == 64);
    before = m;
    CHECK(!physmem_reserve_ram(&m, 64, 65));
    a = (unsigned char *)&m; b = (unsigned char *)&before;
    for (i = 0; i < sizeof(m); i++) CHECK(a[i] == b[i]);
''')

    def test_sources_normalize_without_claiming_detection(self):
        self.run_c('''
    struct physmem m;
    u32 count;
    physmem_init(&m);
    CHECK(physmem_add_trusted(&m, 20, 30, PHYSMEM_SOURCE_SYNTHETIC));
    CHECK(physmem_add_trusted(&m, 10, 20, PHYSMEM_SOURCE_SYNTHETIC));
    CHECK(m.count == 3 && m.ranges[1].first == 10 && m.ranges[1].end == 30);
    CHECK(physmem_add_trusted(&m, 15, 25, PHYSMEM_SOURCE_MACHINE));
    CHECK(m.count == 5);
    CHECK(m.ranges[1].sources == PHYSMEM_SOURCE_SYNTHETIC);
    CHECK(m.ranges[2].sources == (PHYSMEM_SOURCE_MACHINE | PHYSMEM_SOURCE_SYNTHETIC));
    CHECK(m.ranges[3].sources == PHYSMEM_SOURCE_SYNTHETIC);
    CHECK(physmem_count(&m, 0, PHYSMEM_MAX_PFN, PHYSMEM_RAM, &count) && count == 20);
    CHECK(physmem_add_trusted(&m, 10, 30, PHYSMEM_SOURCE_MACHINE));
    CHECK(m.count == 3);
''')

    def test_device_apertures_are_explicit_synthetic_exclusions(self):
        # These constants are existing driver definitions, not new policy.
        # PEGC F00000 + 80000 is 512KiB. Xe10's linear aperture moved to the
        # v3 OS-assigned device band at FE000000 (2026-09-29): NP21/W decodes
        # 4MiB there, the backend draws into the first 2MiB.
        self.run_c('''
    struct physmem m;
    u32 count, pfn, guard, guard_end, xe, xe_end, xe_dec;
    physmem_bootstrap_legacy(&m, 16384);
    guard = PEGC_LINEAR_BASE / PHYSMEM_PAGE_SIZE;
    guard_end = guard + PEGC_LINEAR_SIZE / PHYSMEM_PAGE_SIZE;
    xe = WAB_XE10_LINEARWIN_BASE / PHYSMEM_PAGE_SIZE;
    xe_end = xe + WAB_XE10_LINEARWIN_SIZE / PHYSMEM_PAGE_SIZE;
    xe_dec = xe + WAB_XE10_LINEARWIN_DECODE / PHYSMEM_PAGE_SIZE;
    CHECK(PEGC_LINEAR_BASE == 0x00f00000UL);
    CHECK(PEGC_LINEAR_SIZE == 0x00080000UL);
    CHECK(WAB_XE10_LINEARWIN_BASE == 0xFE000000UL);
    CHECK(WAB_XE10_LINEARWIN_BASE == MEM_DEVICE_APERTURE_BASE);
    CHECK(WAB_XE10_LINEARWIN_SIZE == 0x00200000UL);
    CHECK(xe_dec <= MEM_DEVICE_APERTURE_END / PHYSMEM_PAGE_SIZE);
    CHECK(MEM_PHYS_RAM_CEILING == MEM_DEVICE_APERTURE_BASE);
    /* Inactive apertures must not impose a default CUI RAM restriction. */
    CHECK(physmem_count(&m, guard, 4096, PHYSMEM_RAM, &count) && count == 256);
    CHECK(physmem_add_trusted(&m, xe, PHYSMEM_MAX_PFN, PHYSMEM_SOURCE_SYNTHETIC));
    CHECK(physmem_find(&m, xe, xe_end, 512, 1, &pfn) && pfn == xe);
    /* Synthetic boot-only example, NOT safe live backend activation code. */
    CHECK(physmem_exclude(&m, guard, guard_end, PHYSMEM_MMIO));
    CHECK(physmem_exclude(&m, xe, xe_dec, PHYSMEM_MMIO));
    CHECK(physmem_add_trusted(&m, guard, guard_end, PHYSMEM_SOURCE_SYNTHETIC));
    CHECK(physmem_count(&m, guard, guard_end, PHYSMEM_MMIO, &count) && count == 128);
    CHECK(physmem_count(&m, xe, xe_dec, PHYSMEM_MMIO, &count) && count == 1024);
    CHECK(!physmem_find(&m, xe, xe_dec, 1, 1, &pfn));
    CHECK(!physmem_reserve_ram(&m, xe, xe_end));
    CHECK(physmem_find(&m, xe_dec, PHYSMEM_MAX_PFN, 1, 1, &pfn) && pfn == xe_dec);
''')


    def test_high_ram_has_no_artificial_ceiling(self):
        # K6-RAM: the only ceiling is the architecture (4GiB). Everything that
        # is subtracted must be a named device reservation, never a clamp.
        self.run_c('''
    struct physmem m;
    u32 count, hole, high, top;
    hole = MEM_SYSTEM_SPACE_BASE / PHYSMEM_PAGE_SIZE;
    high = MEM_HIGH_RAM_BASE / PHYSMEM_PAGE_SIZE;
    top = MEM_PHYS_MMIO_TOP / PHYSMEM_PAGE_SIZE;
    CHECK(hole == 0xF00 && high == 0x1000 && top == 0xFF000);
    /* 15MiB admitted by the old loader; the arena stops below the hole. */
    physmem_bootstrap_legacy(&m, 15360);
    CHECK(physmem_legacy_end(&m) == hole);
    CHECK(physmem_exclude(&m, hole, high, PHYSMEM_RESERVED));
    CHECK(physmem_exclude(&m, top, PHYSMEM_MAX_PFN, PHYSMEM_MMIO));
    /* 128MiB machine. MACHINE is the authoritative detector source. */
    CHECK(physmem_add_trusted(&m, high, 0x8000, PHYSMEM_SOURCE_MACHINE));
    CHECK(physmem_count(&m, hole, high, PHYSMEM_RAM, &count) && count == 0);
    CHECK(physmem_count(&m, hole, high, PHYSMEM_RESERVED, &count));
    CHECK(count == high - hole);
    CHECK(physmem_count(&m, high, PHYSMEM_MAX_PFN, PHYSMEM_RAM, &count));
    CHECK(count == 0x8000 - high);
    /* The legacy arena is unaffected by high RAM (exec layout is unchanged). */
    CHECK(physmem_legacy_end(&m) == hole);
    /* 4GiB machine: nothing is truncated except the top MMIO band. */
    CHECK(physmem_add_trusted(&m, high, top, PHYSMEM_SOURCE_MACHINE));
    CHECK(physmem_count(&m, 0, PHYSMEM_MAX_PFN, PHYSMEM_RAM, &count));
    CHECK(count == (hole - MEM_APP_BAND_BASE / PHYSMEM_PAGE_SIZE) + (top - high));
    CHECK(physmem_count(&m, top, PHYSMEM_MAX_PFN, PHYSMEM_MMIO, &count));
    CHECK(count == PHYSMEM_MAX_PFN - top);
    CHECK(physmem_legacy_end(&m) == hole);
''')


if __name__ == '__main__':
    unittest.main()
