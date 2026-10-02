"""Actual paging.c/pgalloc.c/physmem.c, ILP32; only privileged asm replaced.

Covers TASK_T2_APPBAND: sparse high PTs and the actual kselftest ledger
procedure on 8/17MB pools; retains the legacy physical byte-budget tests.
"""
import host32
import pathlib
import subprocess
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[2]
FLAGS = ['-m32', '-march=i386', '-std=gnu11', '-ffreestanding', '-fno-pie',
         '-fno-stack-protector', '-Wall', '-Wextra', '-Werror']
import sys
MUTATIONS = [
 ('single-PDE', '    as->app_pde_count = MEM_APP_BAND_MAX_PDES;', '    as->app_pde_count = 1;'),
 ('master-USER', '        pd[pdi] |= PTE_USER;', '        page_directory[pdi] |= PTE_USER;'),
 ('PT-destroy-leak', '        if (as->app_pt_phys[k])\n            pgalloc_free_n_owner', '        if (0)\n            pgalloc_free_n_owner'),
 ('partial-PT-leak', '        while (n) {', '        while (0) {'),
 ('kselftest-old-AS-count', 'PDE_COUNT * sizeof(u32) / PAGE_SIZE + data_pages', '(PDE_COUNT + PTE_COUNT) * sizeof(u32) / PAGE_SIZE + data_pages'),
]
def run(mutation=None):
    with tempfile.TemporaryDirectory(prefix='os32-appband-') as tmp:
        tmp = pathlib.Path(tmp)
        source = (ROOT / 'kernel/paging.c').read_text()
        kselftest = (ROOT / 'kernel/kselftest.c').read_text()
        ledger = kselftest[kselftest.index('static u32 ledger_persist_total(void)'):kselftest.index('/*  gfx の予約・BB・SURFACE')]
        ledger = ledger[:ledger.rfind('/* ------------------------------------------------------------------------ */')]
        # This fixture tests ledger allocation/reclaim, not caller copies.
        # The real bounded boot helper is executed by test_access_walk.py.
        boot_start = ledger.index('static void test_caller_boot(')
        boot_end = ledger.index('static void test_ledger(void)')
        ledger = ledger[:boot_start] + ledger[boot_end:]
        boot_call = '            if (phys) test_caller_boot(&as, phys);\n'
        assert ledger.count(boot_call) == 1
        ledger = ledger.replace(boot_call, '')
        if mutation:
            old, new = mutation[1:]
            if mutation[0].startswith('kselftest-'):
                if old not in ledger: raise RuntimeError('missing mutant: ' + mutation[0])
                ledger = ledger.replace(old, new, 1)
            else:
                if old not in source: raise RuntimeError('missing mutant: ' + mutation[0])
                source = source.replace(old, new, 1)
        (tmp / 'paging_host_source.c').write_text(source)
        (tmp / 'kselftest_ledger_source.c').write_text(ledger)
        allocator = (ROOT / 'kernel/pgalloc.c').read_text()
        allocator = allocator.replace('irq_save()', '0').replace('irq_restore(flags)', '(void)flags')
        (tmp / 'pgalloc_host_source.c').write_text(allocator)
        # arch/x86 + platform/pc98: include/io.h / include/cpu.h は契約だけで、
        # 実装は固定名 arch_io.h / arch_cpu.h / platform_io.h を引く (順序 3・5)。
        # build/config.mk の INC_COMMON と同じものをここでも渡す。
        includes = ['-I' + str(ROOT / p)
                    for p in ('include', 'arch/x86', 'platform/pc98',
                              'kernel', 'lib')] + ['-I' + str(tmp)]
        # ホスト実行のときだけ arch_cpu.h を差し替える (CR0 / CR3 は CPL=3 の
        # プロセスでは触れない)。-I の順で本物より先に見つかるよう先頭に置く。
        host_includes = ['-I' + str(ROOT / 'tools/tests/host_arch')] + includes
        exe = tmp / 'app_band_pde'
        build = subprocess.run(['gcc', *FLAGS, '-DPHYSMEM_HOST_TEST=1', '-nostdlib', '-static', '-no-pie',
                        *host_includes, str(ROOT / 'tools/tests/app_band_pde_host.c'),
                        str(ROOT / 'kernel/physmem.c'), '-o', str(exe)], capture_output=bool(mutation))
        if build.returncode:
            if mutation: return 'compile'
            raise RuntimeError('host compile failed')
        result = host32.run([str(exe)], capture_output=bool(mutation), timeout=60)
        if mutation:
            if mutation[0].startswith('kselftest-'):
                if result.returncode != 1 or b'ledger:AS alloc\n' not in result.stdout:
                    return 'wrong-failure' if result.returncode else 'green'
            return 'runtime' if result.returncode else 'green'
        if result.returncode: raise RuntimeError('host execution failed')
        subprocess.run(['i386-elf-gcc', *FLAGS, '-O2', *includes, '-c',
                        str(ROOT / 'kernel/paging.c'), '-o', str(tmp / 'paging.o')], check=True)
        print('HOST ILP32 + TARGET GNU11 PASS')

run()
if '--mutate' in sys.argv:
    for mutation in MUTATIONS:
        result = run(mutation)
        print(mutation[0] + ': ' + result)
        if result != 'runtime': raise SystemExit(1)
    print('5/5 runtime RED; compile failures 0')
