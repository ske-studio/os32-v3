"""B2 real IRQ component: shared registration, bounded W1C and tick recovery."""
CHECK = dict(name='gf1-irq-host', tier=1,
    covers=['hwdev/gf/irq.c', 'hwdev/gf/*.h', 'hwdev/gf/display/*.h',
            'hwdev/gf_load_state.h', 'kdebug/gfinit_decode.h', 'kdebug/recover.h',
            'kernel/irq/irq.h', 'kernel/irq/irq_math.h', 'drivers/pci/pci.h',
            'drivers/pci/pci_decode.h', 'include/types.h', 'include/io.h',
            'lib/serial_diag.h', 'include/video_service.h',
            'include/pci_dev_contract.h', 'tools/tests/gf1_irq_host.c',
            'tools/tests/test_gf1_irq.py', 'tools/tests/host32.py',
            'tools/tests/kernel_includes.py'],
    needs=[], budget=3.7)

from pathlib import Path
import os
import subprocess
import sys
import tempfile
import host32
from kernel_includes import include_flags

ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / 'hwdev/gf/irq.c'


def main():
    flags = ['-m32', '-march=i386', '-std=gnu11', '-O2', '-Wall', '-Wextra',
             '-Werror', '-Werror=vla', '-ffreestanding', '-fno-pie',
             '-fno-stack-protector', *include_flags(ROOT),
             '-I' + str(SOURCE.parent)]
    with tempfile.TemporaryDirectory(prefix='gf1-irq-') as td:
        td = Path(td)
        cross = Path(os.environ['CROSS_DIR']) / 'bin/i386-elf-gcc'
        subprocess.run([str(cross), *flags, '-D__KERNEL_BUILD__', '-c',
                        str(SOURCE), '-o', str(td / 'irq.o')], check=True)
        exe = td / 'host'

        def build(source):
            subprocess.run(['gcc', *flags, '-nostdlib', '-static', '-no-pie',
                            '-DGF1_IRQ_SOURCE="' + str(source) + '"',
                            str(ROOT / 'tools/tests/gf1_irq_host.c'),
                            '-o', str(exe)], check=True)

        build(SOURCE)
        for runner in os.environ.get('HOST32_RUNNERS', 'qemu').split():
            result = host32.run([str(exe)], runner=runner)
            assert result.returncode == 0, (runner, result.returncode)
            print('PASS B2 shared IRQ/PMC/FIFO/GR/W1C/VBLANK/snapshot/tick (' + runner + ')', flush=True)
        if '--mutate' in sys.argv:
            original = SOURCE.read_text()
            mutants = [
                ('if (!f->stats.first.fifo_latched) {', 'if (1) {',
                 'first FIFO status overwritten'),
                ('if (!f->stats.first.graph_latched) {', 'if (1) {',
                 'first GR trap overwritten'),
                ('if (wr(f, GF_IRQ_PMC_ENABLE, 1)) goto io_error; /* gf1 rearm anchor */',
                 '/* lost tick rearm */', 'tick PMC rearm removed'),
                ('#define GF_IRQ_GRAPH_ENABLE 0x40013cU',
                 '#define GF_IRQ_GRAPH_ENABLE 0x400140U', 'wrong PGRAPH enable address'),
                ('if (wr(f, GF_IRQ_CRTC_STATUS + stride, GF_IRQ_VBLANK)) return -1;',
                 '/* lost observed PCRTC ACK */', 'PCRTC ACK removed'),
                ('if (enable & GF_IRQ_VBLANK) f->stats.vblank[h]++;',
                 'f->stats.vblank[h]++;', 'disabled head counted'),
                ('if (poll) mask = f->wanted_fifo;',
                 'if (poll) mask = 0;', 'requested FIFO recovery lost'),
                ('if (poll) status &= f->wanted_graph;',
                 'if (poll) status = 0;', 'requested GR recovery lost'),
                ('if (f->running) (void)drain(f, true);',
                 '(void)drain(f, true);', 'stopped tick touches map'),
                ('*out = f->stats;', '*out = f->stats; out->vblank[0] = 0;', 'snapshot counter discarded'),
            ]
            for old, new, label in mutants:
                assert original.count(old) == 1, label
                source = td / 'mutant.c'
                source.write_text(original.replace(old, new, 1))
                build(source)  # A compile error is never counted as RED.
                result = host32.run([str(exe)], runner='qemu')
                assert result.returncode != 0, label
                print('MUTATE runtime RED: ' + label, flush=True)
    return 0


if __name__ == '__main__':
    raise SystemExit(main())


def run(ctx) -> int:
    env = dict(os.environ, HOST32_RUNNERS=' '.join(ctx.runners))
    argv = ['python3', '-B', 'tools/tests/test_gf1_irq.py']
    if ctx.mutate:
        argv.append('--mutate')
    return subprocess.call(argv, cwd=ctx.root, env=env, close_fds=False)
