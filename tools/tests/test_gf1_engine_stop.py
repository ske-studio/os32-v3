"""Engine reset pulses and failure containment against fake MMIO."""
CHECK = dict(name='gf1-engine-stop', tier=1,
    covers=['hwdev/gf/engine_stop.c', 'hwdev/gf/foundation.h',
            'hwdev/gf/internal.h', 'kdebug/recover.h', 'include/types.h',
            'tools/tests/gf1_engine_stop_host.c', 'tools/tests/test_gf1_engine_stop.py',
            'tools/tests/host32.py', 'tools/tests/kernel_includes.py'],
    needs=[], budget=2.8)

from pathlib import Path
import os
import subprocess
import sys
import tempfile
import host32
from kernel_includes import include_flags

ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / 'hwdev/gf/engine_stop.c'


def main():
    original = SOURCE.read_text()
    mutations = [('FIFO pulse omitted', 'value &= ~engines[i];', 'value &= ~(engines[i] & ~RECOVER_PMC_ENGINE_FIFO);'),
                 ('readback ignored', 'observed != value)\n            return GF1_ESTOP;\n        value |=', '0)\n            return GF1_ESTOP;\n        value |=')]
    with tempfile.TemporaryDirectory(prefix='gf1-stop-') as td:
        def run(source):
            exe = Path(td) / 'host'
            subprocess.run(['gcc', '-m32', '-march=i386', '-std=gnu11', '-Wall', '-Wextra',
                            '-Werror', '-ffreestanding', '-fno-pie', '-fno-stack-protector',
                            '-nostdlib', '-static', '-no-pie', *include_flags(ROOT), '-I' + str(ROOT / 'hwdev/gf'),
                            str(ROOT / 'tools/tests/gf1_engine_stop_host.c'), str(source),
                            '-o', str(exe)], cwd=ROOT, check=True)
            return host32.run([str(exe)], cwd=ROOT, runner='qemu').returncode
        assert run(SOURCE) == 0
        print('PASS engine stop fake MMIO', flush=True)
        if '--mutate' in sys.argv:
            for label, old, new in mutations:
                assert original.count(old) == 1, label
                mutant = Path(td) / 'engine_stop.c'
                mutant.write_text(original.replace(old, new, 1))
                assert run(mutant) != 0, label
                print('MUTATE RED ' + label, flush=True)
    return 0


if __name__ == '__main__':
    raise SystemExit(main())


def run(ctx):
    env = dict(os.environ, HOST32_RUNNERS=' '.join(ctx.runners))
    args = ['python3', '-B', 'tools/tests/test_gf1_engine_stop.py']
    if ctx.mutate:
        args.append('--mutate')
    return subprocess.call(args, cwd=ctx.root, env=env, close_fds=False)
