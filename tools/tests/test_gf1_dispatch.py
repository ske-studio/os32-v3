"""CW module dispatch, diagnostic lease adapter and page/IRQ operations."""
CHECK = dict(name='gf1-dispatch-host', tier=1,
    covers=['hwdev/gf/foundation_test.c', 'hwdev/gf/gf1_test.h', 'hwdev/gf/gf2_test.h',
            'drivers/video/geforce/geforce_module.h',
            'include/gfx_hal.h', 'include/gfx_format.h',
            'kernel/module/module_runtime.h', 'kernel/module/module_image.h',
            'hwdev/gf/foundation.h', 'hwdev/gf/internal.h',
            'hwdev/gf/display/*.h', 'hwdev/gf_load_state.h',
            'kdebug/gfinit_bus.h', 'kdebug/kdebug_window.h', 'kdebug/recover.h',
            'kdebug/gfinit_decode.h', 'include/types.h', 'include/memmap.h',
            'include/io.h', 'include/video_service.h', 'include/pci_dev_contract.h',
            'arch/x86/arch_io.h', 'tools/tests/gf1_dispatch_host.c',
            'tools/tests/test_gf1_dispatch.py', 'tools/tests/gf_foundation_width_host.h', 'tools/tests/host32.py',
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
            subprocess.run(['gcc', *flags, '-DGF_WIDTH_TRACE', '-fsanitize=kernel-address', '-nostdlib', '-static', '-no-pie',
                            '-DGF1_DISPATCH_SOURCE="' + str(source) + '"',
                            str(ROOT / 'tools/tests/gf1_dispatch_host.c'),
                            '-o', str(exe)], check=True)

        build(SOURCE)
        for runner in os.environ.get('HOST32_RUNNERS', 'qemu').split():
            result = host32.run([str(exe)], runner=runner)
            assert result.returncode == 0, (runner, result.returncode)
            print('PASS CW prepare/lease/dispatch/page/HEADS/revoke; real MMIO byte/word/dword width/address/count (' + runner + ')', flush=True)
        if '--mutate' in sys.argv:
            original = SOURCE.read_text()
            mutants = [
                ('f->io.read=diagnostic_read;f->io.write=diagnostic_write;',
                 'f->io.read=diagnostic_read;f->io.write=diagnostic_write;f->io.opaque=f;',
                 'lease opaque replaced'),
                ('if (f->revoked) return -1;\n    if (width==1) *value=',
                 'if (false) return -1;\n    if (width==1) *value=', 'revoked adapter read'),
                ('if (f->revoked) return -1;\n    if (width==1) *(volatile',
                 'if (false) return -1;\n    if (width==1) *(volatile', 'revoked adapter write'),
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
                ('q->op>GF1T_DISPLAY_START', 'q->op>GF1T_REVOKE', 'display operations rejected'),
                ('rc=f==gf1_module_ops.production ?', 'rc=true ?', 'diagnostic display dispatch'),
                ('geforce_foundation_display(q->op==GF1T_DISPLAY_START)',
                 'geforce_foundation_display(q->op==GF1T_DISPLAY_STOP)', 'display direction swapped'),
                ('if (width==1) *value=*(volatile u8 *)(f->bar0+off);', 'if (width==1) *value=*(volatile u32 *)(f->bar0+off);', 'diagnostic u8 read widened'),
                ('if (width==1) *value=*(volatile u8 *)(f->bar0+off);', 'if (width==1) *value=(u8)*(volatile u32 *)(f->bar0+off);', 'diagnostic masked overread'),
                ('if (width==1) *(volatile u8 *)(f->bar0+off)=(u8)value;', 'if (width==1) *(volatile u32 *)(f->bar0+off)=(u32)value;', 'diagnostic u8 write widened'),
                ('if (width==2) *value=*(volatile u16 *)(f->bar0+off);', 'if (width==2) *value=*(volatile u32 *)(f->bar0+off);', 'diagnostic u16 read widened'),
                ('if (width==2) *(volatile u16 *)(f->bar0+off)=(u16)value;', 'if (width==2) *(volatile u32 *)(f->bar0+off)=(u32)value;', 'diagnostic u16 write widened'),
            ]
            for old, new, label in mutants:
                assert original.count(old) == 1, label
                text = original.replace(old, new, 1)
                for header in ('gf1_test.h', '../../kdebug/gfinit_bus.h',
                               '../../include/memmap.h', '../../include/io.h',
                               '../../drivers/video/geforce/geforce_module.h'):
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
