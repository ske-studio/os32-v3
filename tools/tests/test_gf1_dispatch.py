"""CW module dispatch, diagnostic lease adapter and page/IRQ operations."""
CHECK = dict(name='gf1-dispatch-host', tier=1,
    covers=['hwdev/gf/foundation_test.c', 'hwdev/gf/gf1_test.h',
            'hwdev/gf/foundation.h', 'hwdev/gf/internal.h',
            'hwdev/gf/display/*.h', 'hwdev/gf_load_state.h',
            'kdebug/gfinit_bus.h', 'kdebug/kdebug_window.h', 'kdebug/recover.h',
            'kdebug/gfinit_decode.h', 'include/types.h', 'include/memmap.h',
            'include/io.h', 'include/video_service.h', 'include/pci_dev_contract.h',
            'arch/x86/arch_io.h', 'tools/tests/gf1_dispatch_host.c',
            'tools/tests/test_gf1_dispatch.py', 'tools/tests/host32.py',
            'tools/tests/kernel_includes.py'], needs=[], budget=30.0)

import os
from pathlib import Path
import subprocess
import sys
import tempfile
import host32
from kernel_includes import include_flags

ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / 'hwdev/gf/foundation_test.c'


def main():
    flags = ['-m32', '-march=i386', '-std=gnu11', '-O2', '-Wall', '-Wextra',
             '-Werror', '-Werror=vla', '-ffreestanding', '-fno-pie',
             '-fno-stack-protector', *include_flags(ROOT), '-I' + str(ROOT),
             '-I' + str(SOURCE.parent)]
    with tempfile.TemporaryDirectory(prefix='gf1-dispatch-') as td:
        td = Path(td)
        cross = Path(os.environ['CROSS_DIR']) / 'bin/i386-elf-gcc'
        subprocess.run([str(cross), *flags, '-D__KERNEL_BUILD__', '-c',
                        str(SOURCE), '-o', str(td / 'dispatch.o')], check=True)
        exe = td / 'host'

        def build(source):
            subprocess.run(['gcc', *flags, '-nostdlib', '-static', '-no-pie',
                            '-DGF1_DISPATCH_SOURCE="' + str(source) + '"',
                            str(ROOT / 'tools/tests/gf1_dispatch_host.c'),
                            '-o', str(exe)], check=True)

        build(SOURCE)
        for runner in os.environ.get('HOST32_RUNNERS', 'qemu').split():
            result = host32.run([str(exe)], runner=runner)
            assert result.returncode == 0, (runner, result.returncode)
            print('PASS CW prepare/lease/dispatch/page/HEADS/revoke (' + runner + ')', flush=True)
        if '--mutate' in sys.argv:
            original = SOURCE.read_text()
            mutants = [
                ('f->io.read=diagnostic_read;f->io.write=diagnostic_write;',
                 'f->io.read=diagnostic_read;f->io.write=diagnostic_write;f->io.opaque=f;',
                 'lease opaque replaced'),
                ('if (f->revoked) return -1;\n    *value=',
                 'if (false) return -1;\n    *value=', 'revoked adapter read'),
                ('if (f->revoked) return -1;\n    *(volatile',
                 'if (false) return -1;\n    *(volatile', 'revoked adapter write'),
                ('if (f->revoked && q->op!=GF1T_STOP',
                 'if (false && q->op!=GF1T_STOP', 'revoked mutable dispatch'),
                ('unsigned flags=irq_save();', 'unsigned flags=0;', 'HEADS CLI removed'),
                ('reply->bytes+=sizeof(u32);', 'reply->bytes+=1;', 'page byte count'),
                ('reply->bad_off=off;rc=GF1_EIO;', 'reply->bad_off=0;rc=GF1_EIO;',
                 'first mismatch offset'),
                ('gf_inst_free(f,&page);', '(void)page;', 'page release omitted'),
                ('reply->command=f->expected_command;', 'reply->command=0;', 'expected command omitted'),
                ('(f->revoked?16U:0)', '(f->revoked?0U:0)', 'revoked output flag'),
                ('gf_irq_snapshot(f,&reply->stats);', '/* snapshot omitted */', 'snapshot omitted'),
            ]
            for old, new, label in mutants:
                assert original.count(old) == 1, label
                text = original.replace(old, new, 1)
                for header in ('gf1_test.h', '../../kdebug/gfinit_bus.h',
                               '../../include/memmap.h', '../../include/io.h'):
                    text = text.replace('#include "' + header + '"',
                                        '#include "' + str((SOURCE.parent / header).resolve()) + '"')
                mutant = td / 'mutant.c'
                mutant.write_text(text)
                build(mutant)
                result = host32.run([str(exe)], runner='qemu')
                assert result.returncode != 0, label
                print('MUTATE runtime RED: ' + label, flush=True)
    return 0


if __name__ == '__main__':
    raise SystemExit(main())


def run(ctx):
    args = ['python3', '-B', 'tools/tests/test_gf1_dispatch.py']
    if ctx.mutate:
        args.append('--mutate')
    return subprocess.call(args, cwd=ctx.root,
                           env=dict(os.environ, HOST32_RUNNERS=' '.join(ctx.runners)),
                           close_fds=False)
