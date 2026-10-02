"""Boot adapter against real ILP32 sys/pgalloc/paging, no emulator.

  python3 -B tools/tests/test_memory_boot.py            # 肯定側 (unittest)
  python3 -B tools/tests/test_memory_boot.py --mutate   # 否定側 + 肯定側

T1a (docs/tasks/v3/TASK_T1_LEDGER.md §4-1): legacy の pgalloc_init への
fallback は撤去した。8MB から 2GB 超の申告まで、全部の構成がモデル経路で
ONLINE になり、台帳の置き場は backing の 2 択 (ARENA_TOP / FIXED、§3-3)。

--mutate は否定側。§4-1 の変異 2 本 (置き場の境界を 1 ページずらす /
sys_frozen_exec を workspace_first に戻す) と、FIXED / ARENA_TOP の境界・
backing の写像・FIXED の RAM 拒否を壊した版で、試験が RED になることを見る。
変異は一時ディレクトリの写しに当てる (実物のソースは書き換えない)。
"""
import host32
import pathlib
import re
import subprocess
import sys
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[2]

# (名前, 当てるファイル, 置き換え前, 置き換え後, 落とすべき場面 (case, kb))
MUTATIONS = [
    # §4-1 変異 1: 置き場の境界を 1 ページずらす (DMA プールの上側ガードを食う)。
    ("ledger-base-shift", "include/memmap.h",
     "#define MEM_LEDGER_META_BASE   0x2F9000UL",
     "#define MEM_LEDGER_META_BASE   0x2F8000UL", ("fixed", 8192)),
    # 同じく上端を 1 ページずらす (カーネルスタックのガードを食う)。
    ("ledger-end-shift", "include/memmap.h",
     "#define MEM_LEDGER_META_END    0x2FB000UL",
     "#define MEM_LEDGER_META_END    0x2FC000UL", ("fixed", 8192)),
    # §4-1 変異 2: exec の上端を workspace の位置から決める旧式に戻す (B2)。
    ("frozen-exec-ws", "kernel/sys.c",
     "    sys_frozen_exec = pgalloc_arena_end() * PAGE_SIZE;\n    sys_frozen_end = top * PAGE_SIZE;\n    sys_model_staged = 1;",
     "    sys_frozen_exec = l->workspace_first * PAGE_SIZE;\n    sys_frozen_end = top * PAGE_SIZE;\n    sys_model_staged = 1;",
     ("fixed", 8192)),
    # FIXED / ARENA_TOP の境界を 1 PFN ずらす (3,074 PFN が FIXED に落ちる)。
    ("arena-boundary", "kernel/memory_boot.c",
     "        top - pages - ws_pages >= MEM_PHYS_WORKSPACE_FLOOR / PAGE_SIZE) {",
     "        top - pages - ws_pages > MEM_PHYS_WORKSPACE_FLOOR / PAGE_SIZE) {",
     ("arena", 12296)),
    # D11 (T1a の Codex P3、T1b で追加): プローブの上限を外す (2GB を越えて数える)。
    ("detect-cap-removed", "kernel/memory_boot.c",
     "    if (reported > (MEM_PHYS_RAM_CEILING - MEM_HIGH_RAM_BASE) / MEM_1MB)\n"
     "        reported = (MEM_PHYS_RAM_CEILING - MEM_HIGH_RAM_BASE) / MEM_1MB;\n",
     "", ("detect_cap", 16384)),
    # exec の最小域の fail-stop の境界を 1 ページずらす (0x58F000 で起動してしまう)。
    ("minimum-off-by-page", "kernel/sys.c",
     "    } else if (l->kind == PGALLOC_BACKING_FIXED) {\n        if (top < minimum) goto done;",
     "    } else if (l->kind == PGALLOC_BACKING_FIXED) {\n        if (top + 1 < minimum) goto done;",
     ("minimum_short", 5692)),
    # 同梱域の申告の範囲検査を外す (同梱域の外の申告を受け付けてしまう)。
    ("bundle-range-blind", "kernel/memory_boot.c",
     "    if (first >= end || first < lo || end > hi) return 0;",
     "    if (first >= end) return 0;", ("bundle", 16384)),
    # FIXED のとき backing を張り忘れる (verify が落ちて 8MB が起動しない)。
    ("backing-unmapped", "kernel/memory_boot.c",
     "        if (!paging_map_ledger_backing()) return 0;\n",
     "", ("fixed", 8192)),
    # FIXED の metadata / workspace が RAM (RESERVED でない) でも受け付けてしまう。
    ("fixed-meta-on-ram", "kernel/pgalloc.c",
     "            !all_reserved(m, first, first + pages)) goto done;",
     "            0) goto done;", ("fixed_reject", 8192)),
    ("fixed-ws-on-ram", "kernel/pgalloc.c",
     "                !all_reserved(&next, ws_first, ws_end)) goto done;",
     "                0) goto done;", ("fixed_reject", 8192)),
]


def run_case(case='default', kb=16384, defines=(), mutation=None):
    with tempfile.TemporaryDirectory(prefix='os32-memory-boot-') as tmp:
        d = pathlib.Path(tmp)
        texts = {}
        for rel in ('kernel/paging.c', 'kernel/pgalloc.c', 'kernel/sys.c',
                    'kernel/memory_boot.c', 'include/memmap.h'):
            texts[rel] = (ROOT / rel).read_text()
        if mutation:
            _, rel, old, new, _ = mutation
            if old not in texts[rel]:
                raise SystemExit('変異 %s の当て先が見つからない: %s' % (mutation[0], rel))
            texts[rel] = texts[rel].replace(old, new, 1)
        for unit in ('paging', 'pgalloc', 'sys'):
            s = texts[f'kernel/{unit}.c']
            s = s.replace('irq_save()', 'host_irq_save()').replace('irq_restore(flags)', 'host_irq_restore(flags)')
            (d / f'{unit}_host_source.c').write_text(s)
        # 写しの memmap.h を -I の先頭に置く (変異を当てたときだけ中身が違う)。
        (d / 'memmap.h').write_text(texts['include/memmap.h'])
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
        mb = texts['kernel/memory_boot.c']
        if case == 'detect_cap':
            # BIOS ワークと書き込み検証をホストの配列で受ける (memory_boot_host.c)。
            if mb.count('P2V_BOOT(') != 4:
                raise AssertionError('P2V_BOOT probe sites changed')
            mb = mb.replace('P2V_BOOT(', 'host_boot_ptr(')
        (d / 'memory_boot_host_source.c').write_text(mb)
        cmd = ['gcc', '-m32', '-march=i386', '-std=gnu11', '-Wall', '-Wextra', '-Werror', '-ffreestanding', '-fno-pie', '-fno-stack-protector', '-nostdlib', '-static', '-no-pie', '-ffunction-sections', '-Wl,--gc-sections', f'-DTEST_{case.upper()}', f'-DTEST_KB={kb}UL'] + list(defines)
        # arch/x86 + platform/pc98: include/io.h / include/cpu.h は契約
        # だけで、実装は固定名 arch_io.h / arch_cpu.h / platform_io.h を
        # 引く (順序 3・5)。CR0 / CR3 を触る arch_cpu.h だけは、ホストでは
        # tools/tests/host_arch/ の実装が先に見つかるようにする。
        cmd += ['-I' + str(d), '-I' + str(ROOT / 'tools/tests/host_arch')]
        cmd += ['-I' + str(ROOT / p) for p in ('include', 'arch/x86', 'platform/pc98', 'kernel', 'lib', 'drivers', 'sdk/include/os32')]
        # physmem.c も写しから組む ("memmap.h" を同じ写しで引かせるため)。
        physmem = d / 'physmem.c'
        physmem.write_text((ROOT / 'kernel/physmem.c').read_text())
        build = subprocess.run(cmd + [str(ROOT / 'tools/tests/memory_boot_host.c'), str(physmem), '-o', str(d / 'test')],
                               capture_output=bool(mutation))
        if build.returncode != 0:
            if mutation:
                return 'compile'
            raise subprocess.CalledProcessError(build.returncode, cmd)
        out = host32.run([str(d / 'test')], timeout=120, capture_output=bool(mutation))
        if mutation:
            return 'ok' if out.returncode == 0 else 'fail'
        if out.returncode != 0:
            raise subprocess.CalledProcessError(out.returncode, str(d / 'test'))
        return 'ok'


class MemoryBoot(unittest.TestCase):
    def run_case(self, case='default', kb=16384, defines=()):
        run_case(case, kb, defines)

    def test_fixed_paging_reservation_all_ram_shapes(self):
        for kb in (8192, 17408, 65536):
            with self.subTest(kb=kb):
                self.run_case('fixed_paging', kb)

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

    def test_legacy_allocator_is_gone(self):
        # D32 / T1a: legacy の口は製品のどこにも残っていない。
        alloc = (ROOT / 'kernel/pgalloc.c').read_text()
        header = (ROOT / 'kernel/pgalloc.h').read_text()
        self.assertNotRegex(alloc, r'\bvoid\s+pgalloc_init\s*\(')
        self.assertNotRegex(header, r'\bpgalloc_init\s*\(')
        self.assertNotIn('legacy_metadata', alloc)
        self.assertNotIn('PGALLOC_BASE', header + alloc)
        paging = (ROOT / 'kernel/paging.c').read_text()
        body = paging.split('static u32 *reserve_table(void)', 1)[1].split('\n}', 1)[0]
        self.assertNotIn('pgalloc_alloc_n_pfn', body)
        self.assertIn('pgalloc_alloc_pt', body)
        self.assertNotIn('pgalloc_init(', (ROOT / 'kernel/memory_boot.c').read_text())

    def test_high_ram_source_is_memory_boot_only(self):
        # D32: 高位 RAM の登録源は memory_boot_detect (0594h + 書き込み検証) だけ。
        # physmem_add_trusted を呼ぶ製品のソースは memory_boot.c だけで、その
        # 呼び出しは PHYSMEM_SOURCE_MACHINE を渡す 1 か所だけ。
        callers = []
        # 1 本の文字列で持つ (tools/check_select.py の入力の推定が、裸の
        # ディレクトリ名を -I の探索先と取り違えないように)。
        for top in 'kernel drivers gfx exec fs kapi lib net arch platform'.split():
            base = ROOT / top
            if not base.is_dir():
                continue
            for f in sorted(base.rglob('*.c')):
                text = f.read_text(errors='replace')
                if re.search(r'\bphysmem_add_trusted\s*\(', text) and f.name != 'physmem.c':
                    callers.append(str(f.relative_to(ROOT)))
        self.assertEqual(callers, ['kernel/memory_boot.c'])
        text = (ROOT / 'kernel/memory_boot.c').read_text()
        calls = re.findall(r'physmem_add_trusted\s*\(([^;]*)\);', text)
        self.assertEqual(len(calls), 1)
        self.assertIn('PHYSMEM_SOURCE_MACHINE', calls[0])

    def test_table_sizing_has_no_artificial_ceiling(self):
        self.run_case('tables')

    def test_high_ram_above_16m(self):
        # 17MB (ram-15mb)・32MB・33MB・64MB・128MB
        for kb in (17408, 32768, 33792, 65536, 131072):
            with self.subTest(kb=kb):
                self.run_case('high', kb)

    def test_d11_ceiling_2gb(self):
        # 2GB 超の申告 (3GB)。RAM の登録は 2GB で頭打ち。
        self.run_case('ceiling', 3 * 1024 * 1024)

    def test_16m_safe_tail_online(self):
        self.run_case('online', 16384)

    def test_default_16m_arena_top(self):
        self.run_case('default', 16384)

    def test_fixed_backing_small_configs(self):
        # 8MB (ram-8mb)・9MB (np21x64w-9mb)・12MB・3,073 PFN (境界の FIXED 側)
        for kb in (8192, 9216, 12288, 3073 * 4):
            with self.subTest(kb=kb):
                self.run_case('fixed', kb)

    def test_arena_top_low_configs(self):
        # 3,074 PFN (境界の ARENA_TOP 側)・15MB
        for kb in (3074 * 4, 15360):
            with self.subTest(kb=kb):
                self.run_case('arena', kb)

    def test_fixed_backing_rejects_ram_and_out_of_range(self):
        self.run_case('fixed_reject', 8192)

    def test_detect_probe_cap_d11(self):
        # T1a の Codex P3 (T1b で追加): 0594h の申告が 2GB を越えてもプローブと
        # 登録は MEM_PHYS_RAM_CEILING で止まる。
        self.run_case('detect_cap', 16384)

    def test_exec_minimum_failstop_boundary(self):
        # T1a の訂正 5 (T1b で追加): 低位 RAM 0x590000 ちょうどは起動し、
        # 1 ページ足りなければ何も変えずに fail-stop。
        self.run_case('minimum', 0x590000 // 1024)
        self.run_case('minimum_short', 0x58F000 // 1024)

    def test_bundle_staging_rule(self):
        # 集積域・同梱域の規則 (§3-3 ③、T1b): 申告あり / なし / 範囲外。
        for kb in (8192, 16384):
            with self.subTest(kb=kb):
                self.run_case('bundle', kb)

    def test_ram_kb_is_the_registered_total_not_the_top(self):
        # K6-RAM decision (2). (top-of-RAM KiB from the detector, real RAM KiB).
        # 15MiB (NP21/W ExMemory 16): RAM is [0,15MiB) + [16MiB,17MiB).
        # 32MiB (ExMemory 33):        RAM is [0,15MiB) + [16MiB,33MiB).
        # 8MiB  (FIXED backing):      no hole below the top, so both agree.
        for kb, expect in ((17408, 16384), (33792, 32768), (8192, 8192)):
            with self.subTest(kb=kb):
                self.run_case('ramkb', kb, (f'-DTEST_RAM_KB_EXPECT={expect}UL',))


def mutate():
    red = 0
    compiled = 0
    compile_errors = 0
    for m in MUTATIONS:
        case, kb = m[4]
        result = run_case(case, kb, mutation=m)
        if 'compile' in result:
            compiled += 1
            print('  変異 %-18s NOT COUNTED (compile)' % m[0])
            if m[0] not in ('ledger-base-shift', 'ledger-end-shift'):
                compile_errors += 1
            continue
        ok = result != 'ok'
        print('  変異 %-18s %s' % (m[0], 'RED (%s %d: %s)' % (case, kb, result) if ok
                                   else '**GREEN — 試験が穴を見逃した**'))
        red += ok
    print('%d/%d の変異が実行時 RED; コンパイル拒否 %d 本は数えない' % (red, len(MUTATIONS) - compiled, compiled))
    return 0 if not compile_errors and red == len(MUTATIONS) - compiled else 1


if __name__ == '__main__':
    if '--mutate' in sys.argv:
        sys.argv.remove('--mutate')
        rc = mutate()
        if rc:
            sys.exit(rc)
    unittest.main()
