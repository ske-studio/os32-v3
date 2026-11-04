"""E4: real IRQ core + actual PCM I24 handler + diagnostic fake device."""
CHECK = dict(name='pcm-e4-host', tier=1, covers=[
    'tools/tests/pcm_e4_host.c', 'tools/tests/pcm_cs4231_host.c',
    'tools/tests/pcm_hostshim/io.h', 'tools/tests/kernel_includes.py',
    'kernel/irq/irq.c', 'kernel/irq/irq_math.c', 'kernel/irq/tick.c',
    'kernel/module/kdebug_call.c', 'drivers/sound/cs4231/pcm_module.c',
    'drivers/sound/cs4231/pcm_cs4231.c', 'drivers/sound/cs4231/pcm_cs4231_math.c',
    'drivers/sound/cs4231/pcm_e4_*.c', 'drivers/sound/cs4231/pcm_e4_*.h',
    'userland/tests/pcm_test.c', 'build/kernel.mk'], needs=[], budget=3)

from kernel_includes import include_flags
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]
IRQ = 'kernel/irq/irq_math.c'
MUTATIONS = [
    ('            round |= rc;', '            round |= rc; if (rc) break;',
     'stop the scan after the first claimant'),
    ('pass < IRQ_DISPATCH_PASSES', 'pass < 1', 'omit the second scan'),
]


def build_run(root, out):
    exe = out / 'pcm-e4-host'
    subprocess.run(['gcc', '-std=gnu11', '-Wall', '-Wextra', '-Werror',
                    '-D__cdecl=', '-DPCM_E4_DIAG',
                    '-I' + str(root / 'tools/tests/pcm_hostshim'),
                    *include_flags(root), str(root / 'tools/tests/pcm_e4_host.c'),
                    '-o', str(exe)], check=True)
    return subprocess.run([str(exe)], timeout=30).returncode


def main(mutate):
    with tempfile.TemporaryDirectory(prefix='pcm-e4-') as name:
        tmp = Path(name)
        if build_run(ROOT, tmp):
            return 1
        if mutate:
            # A private copy keeps source paths (including relative includes)
            # real; no mutation ever touches the working tree.
            tree = tmp / 'tree'
            for directory in ('tools/tests', 'kernel', 'drivers', 'arch',
                              'include', 'sdk/include', 'mm', 'lib'):
                shutil.copytree(ROOT / directory, tree / directory,
                                ignore=shutil.ignore_patterns('out', '__pycache__', '*.o', '*.d'))
            config = tree / 'build/config.mk'
            config.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(ROOT / 'build/config.mk', config)
            original = (tree / IRQ).read_text()
            for anchor, replacement, reason in MUTATIONS:
                if original.count(anchor) != 1:
                    raise RuntimeError('mutation anchor must be unique: ' + anchor)
                (tree / IRQ).write_text(original.replace(anchor, replacement))
                if not build_run(tree, tmp):
                    print('MUTANT SURVIVED: ' + reason, flush=True)
                    return 1
                print('MUTANT runtime RED: ' + reason, flush=True)
            (tree / IRQ).write_text(original)
    return 0


def run(ctx):
    return main(ctx.mutate)


if __name__ == '__main__':
    sys.exit(main('--mutate' in sys.argv))
