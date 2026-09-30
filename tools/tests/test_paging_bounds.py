"""Actual paging.c, ILP32; only privileged asm replaced for host execution."""
import pathlib
import subprocess
import tempfile
import argparse
parser = argparse.ArgumentParser()
parser.add_argument('--rebuild', choices=['nonmaster', 'rollback', 'sparse', 'attrs', 'final'])
args = parser.parse_args()
ROOT = pathlib.Path(__file__).resolve().parents[2]
FLAGS = ['-m32', '-march=i386', '-std=gnu89', '-ffreestanding', '-fno-pie', '-fno-stack-protector', '-Wall', '-Wextra', '-Werror', '-Wdeclaration-after-statement']
with tempfile.TemporaryDirectory(prefix='os32-paging-') as tmp:
    tmp = pathlib.Path(tmp)
    source = (ROOT / 'kernel/paging.c').read_text()
    (tmp / 'paging_host_source.c').write_text(source)
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
    exe = tmp / 'paging'
    harness = 'paging_rebuild_host.c' if args.rebuild else 'paging_bounds_host.c'
    defines = ['-DPHYSMEM_HOST_TEST=1']
    if args.rebuild:
        defines.append('-DTEST_' + args.rebuild.upper())
    subprocess.run(['gcc', *FLAGS, *defines, '-nostdlib', '-static', '-no-pie', *host_includes, str(ROOT / 'tools/tests' / harness), str(ROOT / 'kernel/physmem.c'), '-o', str(exe)], check=True)
    subprocess.run([str(exe)], check=True, timeout=60)
    subprocess.run(['i386-elf-gcc', *FLAGS, '-O2', *includes, '-c', str(ROOT / 'kernel/paging.c'), '-o', str(tmp / 'paging.o')], check=True)
    print('HOST ILP32 + TARGET GNU89 PASS')
