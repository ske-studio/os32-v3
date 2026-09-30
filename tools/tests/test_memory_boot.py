"""Boot adapter against real ILP32 sys/pgalloc/paging, no emulator."""
import pathlib
import subprocess
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[2]

class MemoryBoot(unittest.TestCase):
    def run_case(self, case='legacy', kb=8192, defines=()):
        with tempfile.TemporaryDirectory(prefix='os32-memory-boot-') as tmp:
            d = pathlib.Path(tmp)
            for unit in ('paging', 'pgalloc', 'sys'):
                s = (ROOT / f'kernel/{unit}.c').read_text()
                s = s.replace('irq_save()', 'host_irq_save()').replace('irq_restore(flags)', 'host_irq_restore(flags)')
                (d / f'{unit}_host_source.c').write_text(s)
            kernel = (ROOT / 'kernel/kernel.c').read_text()
            start = kernel.index('    if (!memory_boot_init(mem_kb))')
            end = kernel.index('    shm_init();', start) + len('    shm_init();')
            # 失敗時の行き止まりは io.h の _stop() (= cli; hlt)。ホストでは
            # 実行できないので観測用の host_failstop() に差し替える。
            gate = kernel[start:end].replace('for (;;) { _stop(); }', 'host_failstop();')
            (d / 'kernel_boot_gate.c').write_text(
                'static void __attribute__((unused)) host_kernel_boot(u32 mem_kb) {\n'
                '#define kprintf(...) ((void)0)\n#define shm_init() die(7)\n' + gate +
                '\n#undef kprintf\n#undef shm_init\n}\n')
            adapter = ROOT / 'kernel/memory_boot.c'
            (d / 'memory_boot_host_source.c').write_text(adapter.read_text() if adapter.exists() else '')
            cmd = ['gcc', '-m32', '-march=i386', '-std=gnu89', '-Wall', '-Wextra', '-Werror', '-Wdeclaration-after-statement', '-ffreestanding', '-fno-pie', '-fno-stack-protector', '-nostdlib', '-static', '-no-pie', '-ffunction-sections', '-Wl,--gc-sections', f'-DTEST_{case.upper()}', f'-DTEST_KB={kb}UL'] + list(defines)
            # arch/x86 + platform/pc98: include/io.h / include/cpu.h は契約
            # だけで、実装は固定名 arch_io.h / arch_cpu.h / platform_io.h を
            # 引く (順序 3・5)。CR0 / CR3 を触る arch_cpu.h だけは、ホストでは
            # tools/tests/host_arch/ の実装が先に見つかるようにする。
            cmd += ['-I' + str(ROOT / 'tools/tests/host_arch')]
            cmd += ['-I' + str(ROOT / p) for p in ('include', 'arch/x86', 'platform/pc98', 'kernel', 'lib', 'drivers', 'sdk/include/os32')] + ['-I' + str(d)]
            subprocess.run(cmd + [str(ROOT / 'tools/tests/memory_boot_host.c'), str(ROOT / 'kernel/physmem.c'), '-o', str(d / 'test')], check=True)
            subprocess.run([str(d / 'test')], check=True, timeout=60)

    def test_huge_hint_no_promotion(self):
        self.run_case('online', 0xffffffff)

    def test_no_fallback_after_bootstrap_or_stage_failure(self):
        for case in ('bootstrap_fail', 'stage_fail'):
            with self.subTest(case=case):
                self.run_case(case, 16384)

    def test_actual_pte_verification_before_write(self):
        for case in ('metadata_pte', 'workspace_pte'):
            with self.subTest(case=case):
                self.run_case(case, 16384)

    def test_kernel_boot_order_and_failstop(self):
        s = (ROOT / 'kernel/kernel.c').read_text()
        gate = 'if (!memory_boot_init(mem_kb))'
        self.assertIn(gate, s)
        start = s.index(gate)
        downstream = min(s.index(f'{name}();') for name in
                         ('shm_init', 'kselftest_run', 'shlib_init'))
        # K6-RAM: detection decides the paging extent and the table sizes, so it
        # must run before paging_init, which must run before the model gate.
        self.assertLess(s.index('mem_kb = memory_boot_detect(mem_kb);'),
                        s.index('paging_init(mem_kb);'))
        self.assertLess(s.index('paging_init(mem_kb);'), start)
        self.assertLess(start, downstream)
        self.assertNotIn('pgalloc_init(mem_kb)', s)
        # 割り込みを禁じたまま止まること (io.h の _stop() = cli; hlt)。
        # 眠って起きる _halt() ではいけない — 先へ進んでしまう。
        self.assertIn('for (;;) { _stop(); }', s[start:downstream])
        self.assertIn('kernel/memory_boot.c', (ROOT / 'build/kernel.mk').read_text())

    def test_table_sizing_has_no_artificial_ceiling(self):
        self.run_case('tables')

    def test_high_ram_above_16m(self):
        for kb in (32768, 131072):
            with self.subTest(kb=kb):
                self.run_case('high', kb)

    def test_16m_safe_tail_online(self):
        self.run_case('online', 16384)

    def test_8m_legacy_preinit(self):
        self.run_case()

    def test_ram_kb_is_the_registered_total_not_the_top(self):
        # K6-RAM decision (2). (top-of-RAM KiB from the detector, real RAM KiB).
        # 15MiB (NP21/W ExMemory 16): RAM is [0,15MiB) + [16MiB,17MiB).
        # 32MiB (ExMemory 33):        RAM is [0,15MiB) + [16MiB,33MiB).
        # 8MiB  (legacy path):        no hole below the top, so both agree.
        for kb, expect in ((17408, 16384), (33792, 32768), (8192, 8192)):
            with self.subTest(kb=kb):
                self.run_case('ramkb', kb, (f'-DTEST_RAM_KB_EXPECT={expect}UL',))

if __name__ == '__main__':
    unittest.main()
