#!/usr/bin/env python3
"""USB 1A: three compilers, two byte orders and A1/A2 guard mutations."""
import argparse
import os
import shutil
from pathlib import Path
import subprocess
import sys
import tempfile

import host32

ROOT = Path(__file__).resolve().parents[2]
# The declaration lists transitive
# repository headers and the runner actually used by the executable fixtures.
CHECK = dict(name='usb-contract', tier=1, needs=[], budget=5, covers=['include/endian_dma.h', 'arch/*/arch_endian_dma.h',
                     'include/usb_platform.h', 'drivers/usb/*.[ch]',
                     'tools/check_usb_portable.py', 'include/types.h', 'include/callconv.h', 'include/endian_le.h',
                     'tools/tests/usb_contract_host.c', 'tools/tests/test_usb_contract.py',
                     'tools/tests/host32.py', 'tools/tests/cc_cache.py'])


def command(argv, **kwargs):
    subprocess.run(argv, cwd=ROOT, check=True, **kwargs)


def guard(root, source, expected):
    path = root / 'drivers/usb/ohci_init.c'
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(source)
    result = subprocess.run([sys.executable, '-B', 'tools/check_usb_portable.py',
                             '--root', str(root)], cwd=ROOT, capture_output=True, text=True)
    assert result.returncode == expected, result.stdout + result.stderr
    if expected:
        print('RED:', result.stdout.strip())


def check(mutate):
    with tempfile.TemporaryDirectory(prefix='usb-contract-') as directory:
        tmp = Path(directory)
        flags = ['-std=gnu11', '-Wall', '-Wextra', '-Werror', '-Wvla',
                 '-Werror=implicit-function-declaration', '-Iinclude', '-I.']
        src = 'tools/tests/usb_contract_host.c'
        cross = Path(os.environ.get('CROSS_DIR', '/home/hight/opt/cross'))
        ppc = Path(os.environ.get('USB_PPC_CROSS_DIR', '/home/hight/opt/cross-ppc'))
        for cc, arch in [(str(cross / 'bin/i386-elf-gcc'), 'x86'),
                         (str(ppc / 'bin/powerpc-elf-gcc'), 'ppc'), ('gcc', 'x86')]:
            extra = ['-m32'] if cc == 'gcc' else []
            command([cc, *flags, *extra, '-Iarch/' + arch, '-O2', '-c', src,
                 '-o', str(tmp / (arch + '-' + Path(cc).name + '.o'))])
            print('compile:', cc, arch)
        for arch in ('x86', 'ppc'):
            exe = tmp / arch
            swap = ['-DUSB_TEST_SWAP'] if arch == 'ppc' else []
            command(['gcc', *flags, '-m32', '-Iarch/x86', *swap, '-O2', src, '-o', str(exe)])
            result = host32.run([str(exe)])
            assert result.returncode == 0, (arch, result.returncode)
            print('runtime:', arch, 'OK')
        guard(tmp, '#include "usb_platform.h"\n#include "drivers/usb/ohci_regs.h"\n', 0)
        guard(tmp, '/* #include "bad.h" .raw */\nconst char *s = ".raw";\n', 0)
        # Shells are excluded from the include rule, but not raw access.
        shell = tmp / 'drivers/usb/usb_module.c'
        shell.write_text('#include "mm/dma_pool.h"\n')
        guard(tmp, '', 0)
        if mutate:
            # Copy only the accessor dependency closure. Mutations never touch
            # the working tree; each compiles and must fail at runtime.
            copy = tmp / 'mutant'
            for rel in ('include/endian_dma.h', 'include/types.h', 'include/callconv.h',
                        'arch/x86/arch_endian_dma.h', 'arch/ppc/arch_endian_dma.h'):
                dest = copy / rel
                dest.parent.mkdir(parents=True, exist_ok=True)
                shutil.copyfile(ROOT / rel, dest)
            mutations = [
                ('ppc', 'return __builtin_bswap32(usb_test_native_load(p));',
                 'return usb_test_native_load(p);'),
                ('ppc', 'unsigned int n = __builtin_bswap32((unsigned int)v);',
                 'unsigned int n = (unsigned int)v;'),
                ('x86', 'return p->raw;', 'return p->raw ^ 1u;'),
                ('x86', 'p->raw = (unsigned int)v;', 'p->raw = (unsigned int)v ^ 1u;')]
            for arch, anchor, replacement in mutations:
                header = copy / ('arch/' + arch + '/arch_endian_dma.h')
                original = header.read_text()
                assert original.count(anchor) == 1, anchor
                header.write_text(original.replace(anchor, replacement))
                exe = tmp / 'bad-accessor'
                command(['gcc', '-I' + str(copy / 'include'),
                         '-I' + str(copy / 'arch/x86'), *flags, '-m32', '-O2',
                         *(['-DUSB_TEST_SWAP'] if arch == 'ppc' else []),
                         src, '-o', str(exe)])
                result = host32.run([str(exe)])
                assert result.returncode in (1, 2, 3, 4), (anchor, result.returncode)
                print('RED runtime:', arch, anchor, 'rc=' + str(result.returncode))
                header.write_text(original)
            header = tmp / 'drivers/usb/ohci_priv.h'
            header.write_text('#include "drivers/usb/usb_core.h"\n')
            guard(tmp, '', 0)
            header.write_text('#include "mm/dma_pool.h"\n')
            guard(tmp, '', 1)  # A2: headers also enforce the boundary
            header.unlink()
            guard(tmp, '#include "mm/dma_pool.h"\n', 1)  # A2
            guard(tmp, 'void f(void) { ed->headp.raw = x; }\n', 1)  # A1
            guard(tmp, 'void f(void) { p->raw = 0; }\n', 1)
            guard(tmp, '# include \\\n"mm/dma_pool.h"\n', 1)
            guard(tmp, '#include PLATFORM_HEADER\n', 1)
            guard(tmp, '#include "drivers/usb/../../mm/dma_pool.h"\n', 1)
            guard(tmp, '', 0)
            shell.write_text('void f(void) { ed->headp.raw = 0; }\n')
            guard(tmp, '', 1)
    command([sys.executable, '-B', 'tools/check_usb_portable.py'])


def run(ctx):
    argv = [sys.executable, '-B', 'tools/tests/test_usb_contract.py']
    if ctx.mutate:
        argv.append('--mutate')
    return subprocess.call(argv, cwd=ctx.root,
                           env=dict(os.environ, HOST32_RUNNERS=' '.join(ctx.runners)),
                           close_fds=False)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--mutate', action='store_true')
    check(parser.parse_args().mutate)
