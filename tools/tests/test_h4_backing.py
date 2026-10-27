"""T3b2/b3: execute the kdebug fixture with real ILP32 allocator/page walks.

Normal controls cover all PFNs, strict >32MiB, the ordinary low allocation,
partial allocation/map failure and reclaim, I/O fault/cleanup controls, the
actual DMA allocator and FDC setup at this kernel ELF's BSS address. --mutate is for PM verification;
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
              'exec/access_walk.c', 'kernel/physmem.c', 'kernel/dma_pool.c',
              'kernel/dma_pool_math.c', 'lib/dma_contract.c',
              'drivers/fdc.c', 'drivers/fdc_decide.c']
MUTANTS = [
    ('VA-only high classification', 'pfns[i] > H4_PHYS_BOUNDARY / PAGE_SIZE',
     'va > H4_PHYS_BOUNDARY'),
    ('32MiB accepted as strictly above', 'pfns[i] > H4_PHYS_BOUNDARY / PAGE_SIZE',
     'pfns[i] >= H4_PHYS_BOUNDARY / PAGE_SIZE'),
    ('write omitted', 'vfs_write_fd(fd, p, n)', '(int)n'),
    ('read omitted', 'vfs_read_fd(fd, p, n)', '(int)n'),
    ('comparison omitted', 'ok = h4_buffer_entry(as, buffer, H4_BUFFER_BYTES);', 'ok = 1;'),
    ('poison omitted', 'mode == 2 ? 0xff : 0', '0'),
    ('remove omitted', 'int rc = vfs_rm(path);', 'int rc = VFS_OK;'),
    ('I/O failure ignored', 'if (root_io >= 0 && host_io >= 0) result = root_io > 0 || host_io > 0;',
     '(void)root_io; (void)host_io; result = 1;'),
    ('missing backend counted', 'if (available) check(ok, "h4: I/O roundtrip and cleanup");',
     'check(ok, "h4: I/O roundtrip and cleanup");'),
    ('stale file removal omitted', 'if (vfs_rm(path) != VFS_OK) goto out;',
     'if (0) goto out;'),
    ('partial DMA allocation accepted', 'int complete = count == before;',
     'int complete = 1;'),
    ('DMA range ignored', 'check(valid, "h4: DMA allocation range/bank");',
     'check(1, "h4: DMA allocation range/bank");'),
    ('DMA free omitted', 'dma_free(&buffers[i]) == 0', '1'),
    ('owner reclaim omitted', 'ledger_reclaim_owner(owner, &left)', '(left = 0, (void)left, 1)'),
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
                           ('dma_pool', 'kernel/dma_pool.c'),
                           ('access_walk', 'exec/access_walk.c')):
            (tmp / (name + '_host_source.c')).write_text((ROOT / path).read_text())
        fdc = (ROOT / 'drivers/fdc.c').read_text()
        fdc = fdc[fdc.index('static u8 s_fdbuf['):fdc.index('static u8 *fdc_dma_buf(void)')]
        fdc = fdc.replace('static u8 s_fdbuf[FDC_BUF_BYTES];',
                          '#define s_fdbuf ((u8 *)P2V(host_fdc_base))')
        (tmp / 'fdc_setup_source.c').write_text(fdc)
        symbols = {}
        for line in subprocess.check_output(
                ['nm', '--defined-only', str(ROOT / 'build/out/kernel.elf')], text=True).splitlines():
            fields = line.split()
            if len(fields) == 3:
                symbols[fields[2]] = int(fields[0], 16)
        fdc_flags = [f'-DHOST_FDC_BSS={symbols["s_fdbuf"]}',
                     f'-DHOST_BSS_START={symbols["__bss_start"]}']
        print(f'[h4] io fdc linked BSS base={symbols["s_fdbuf"]:08X}')
        exe = tmp / 'h4'
        command = ['gcc', *linker_flags(), *fdc_flags, '-m32', '-march=i386', '-std=gnu11',
                   '-O2', '-Wall', '-Wextra', '-Werror', '-ffreestanding',
                   '-fno-pie', '-fno-stack-protector', '-ffunction-sections',
                   '-fdata-sections', '-DPHYSMEM_HOST_TEST=1', '-nostdlib',
                   '-static', '-no-pie', '-Wl,--gc-sections', '-I' + str(tmp),
                   *['-I' + str(ROOT / p) for p in
                     ('tools/tests/host_arch', 'include', 'arch/x86', 'platform/pc98',
                      'kernel', 'exec', 'fs', 'lib', 'drivers', 'sdk/include/os32')],
                   str(ROOT / 'tools/tests/h4_backing_host.c'),
                   *[str(ROOT / path) for path in ('kernel/physmem.c',
                       'kernel/dma_pool_math.c',
                       'lib/dma_contract.c', 'drivers/fdc_decide.c')],
                   '-o', str(exe)]
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
