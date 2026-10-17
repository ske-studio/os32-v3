"""T3b2: execute the kdebug fixture with real ILP32 allocator/page walks.

Normal controls cover all PFNs, strict >32MiB, the ordinary low allocation,
partial allocation/map failure and reclaim. --mutate is for PM verification;
compilation errors never count as RED.
"""
import argparse
from pathlib import Path
import subprocess
import tempfile

import host32
from kernel_layout import linker_flags

ROOT = Path(__file__).resolve().parents[2]
TARGET_SRC = ['kdebug/kselftest.c', 'kernel/paging.c', 'kernel/pgalloc.c',
              'exec/access_walk.c', 'kernel/physmem.c']
MUTANTS = [
    ('VA-only high classification', 'pfns[i] > H4_PHYS_BOUNDARY / PAGE_SIZE',
     'va > H4_PHYS_BOUNDARY'),
    ('32MiB accepted as strictly above', 'pfns[i] > H4_PHYS_BOUNDARY / PAGE_SIZE',
     'pfns[i] >= H4_PHYS_BOUNDARY / PAGE_SIZE'),
    ('owner reclaim omitted', 'ledger_reclaim_owner(owner, &left)', '(left = 0, 1)'),
]


def fixture_source():
    source = (ROOT / 'kdebug/kselftest.c').read_text()
    dispatch = source.split('int kselftest_run_post_exec(void)', 1)[1].split('\n}', 1)[0]
    assert dispatch.count('    test_h4_backing();') == 1
    start = source.index('/* T3b2: H-4(a) backing fixture.')
    return source[start:source.index('/* T3b2 end */', start)]


def run(body, runner):
    with tempfile.TemporaryDirectory(prefix='h4-backing-') as directory:
        tmp = Path(directory)
        (tmp / 'h4_backing_source.c').write_text(body)
        for name, path in (('paging', 'kernel/paging.c'),
                           ('pgalloc', 'kernel/pgalloc.c'),
                           ('access_walk', 'exec/access_walk.c')):
            (tmp / (name + '_host_source.c')).write_text((ROOT / path).read_text())
        exe = tmp / 'h4'
        command = ['gcc', *linker_flags(), '-m32', '-march=i386', '-std=gnu11',
                   '-O2', '-Wall', '-Wextra', '-Werror', '-ffreestanding',
                   '-fno-pie', '-fno-stack-protector', '-ffunction-sections',
                   '-fdata-sections', '-DPHYSMEM_HOST_TEST=1', '-nostdlib',
                   '-static', '-no-pie', '-Wl,--gc-sections', '-I' + str(tmp),
                   *['-I' + str(ROOT / p) for p in
                     ('tools/tests/host_arch', 'include', 'arch/x86', 'platform/pc98',
                      'kernel', 'exec', 'fs', 'lib', 'drivers', 'sdk/include/os32')],
                   str(ROOT / 'tools/tests/h4_backing_host.c'),
                   str(ROOT / 'kernel/physmem.c'), '-o', str(exe)]
        host32.build(command, check=True, capture_output=True, text=True)
        return host32.run([str(exe)], runner=runner, capture_output=True,
                          text=True, timeout=120)


@host32.control_session
def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--mutate', action='store_true')
    parser.add_argument('--runner', choices=['native', 'qemu'], default='qemu')
    args = parser.parse_args()
    host32.begin_control(args.mutate, args.runner, ROOT)
    body = fixture_source()
    with host32.control(args.mutate, args.runner, ROOT) as normal:
        if normal:
            result = run(body, args.runner)
            print(result.stdout + result.stderr, end='')
            assert result.returncode == 0, result.returncode
    if args.mutate:
        for name, old, new in MUTANTS:
            assert body.count(old) == 1, name
            result = run(body.replace(old, new), args.runner)
            assert result.returncode == 1 and 'FAIL:' in result.stdout, (name, result)
            print('RED (runtime):', name)


if __name__ == '__main__':
    try:
        main()
    except subprocess.CalledProcessError as exc:
        print(exc.stdout or '', exc.stderr or '')
        raise
