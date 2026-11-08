"""Execute tri_burst7 single-store wait pacing, comparison, cleanup and log budget."""
CHECK = dict(name='tri-burst7', tier=1, needs=[], budget=240, covers=[
    'tools/tests/test_tri_burst7.py', 'tools/tests/tri_burst7_host.c',
    'userland/tests/hw/tri_burst7.hwb', 'tools/tests/hwb_fake.h',
    'tools/tests/hwb_host.py', 'tools/tests/host32.py', 'tools/tests/kernel_includes.py',
    'tools/tests/mutpar.py', 'userland/shell/hwb.h', 'userland/shell/hwb_probe.c',
    'userland/shell/hwb_parse.c', 'userland/shell/hwb_exec.c', 'userland/shell/hwb_log.c',
    'kdebug/probe_wire.h', 'kdebug/hwb_wire.h', 'include/types.h',
    'sdk/include/os32/os32_kapi_shared.h'])
import json
from pathlib import Path
import tempfile
import sys
import hwb_host


def trial(mutation=None):
    script = (hwb_host.ROOT / 'userland/tests/hw/tri_burst7.hwb').read_text(encoding='ascii')
    if mutation:
        name, before, after = mutation
        assert script.count(before) == 1, name
        script = script.replace(before, after)
    code = (hwb_host.ROOT / 'tools/tests/tri_burst7_host.c').read_text().replace('SCRIPT_DATA', json.dumps(script))
    with tempfile.TemporaryDirectory(prefix='tri-burst7-') as directory:
        fixture = Path(directory) / 'host.c'
        exe = Path(directory) / 'host'
        fixture.write_text(code)
        hwb_host.build(hwb_host.ROOT, fixture, exe)
        # Five fault models cover the complete status/wait matrix.
        result = hwb_host.host32.run([str(exe)], cwd=hwb_host.ROOT,
                                     capture_output=True, text=True, timeout=240)
        print(result.stdout, end='', flush=True)
        if mutation:
            if result.returncode != 1 or 'FAIL:tri-burst7:' not in result.stdout:
                raise RuntimeError(name + ': expected runtime RED')
            print(name + ': RED runtime')
        elif result.returncode:
            raise RuntimeError('tri-burst7 control failed')
        else:
            hwb_host.build(hwb_host.ROOT, fixture, Path(directory) / 'target.o', cross=True)


def main():
    trial()
    if '--mutate' in sys.argv:
        for mutation in [
            ('trial-count', 'let count = 1024', 'let count = 512'),
            ('wait-one', 'let waits = 1', 'let waits = 2'),
            ('wait-two', 'let waits = 2', 'let waits = 1'),
            ('wait-four', 'let waits = 4', 'let waits = 2'),
            ('wait-print', 'print "waits" waits', 'print "waits" 0'),
            ('wait-port', 'iowrite8 port 0x5F 0', 'iowrite8 port 0x5E 0'),
            ('wait-value', 'iowrite8 port 0x5F 0', 'iowrite8 port 0x5F 1'),
            ('wait-skip', 'if left == 0 goto single_next', 'if left != 0 goto single_next'),
            ('status-read', 'mmioread8 v + 0x3DA -> discard', 'mmioread8 v + 0x3CC -> discard'),
            ('v6-read', 'if variant == 6 goto wait_begin', 'if variant == 0 goto wait_begin'),
            ('seed', '(seed + i)', '(seed + i + 1)'),
            ('offset', 'mmiowrite32 f + (i << 2) (seed + i)', 'mmiowrite32 f + (i << 3) (seed + i)'),
            ('count', 'print "mismatch-count" bad', 'print "mismatch-count" 0'),
            ('range', 'print "match-start" off', 'print "match-start" 1'),
            ('vram', 'mmiowrite32 f + (i << 2) original[i]', 'mmiowrite32 f + (i << 2) 0'),
            ('cr23', '0x23 | ((save[852]) << 8)', '0x23 | (0 << 8)'),
        ]:
            trial(mutation)
    return 0


def run(ctx):
    import os
    import subprocess
    return subprocess.call([sys.executable, '-B', __file__,
                            *(['--mutate'] if ctx.mutate else [])], cwd=ctx.root,
                           env=dict(os.environ, HOST32_RUNNERS=' '.join(ctx.runners)), close_fds=False)


if __name__ == '__main__':
    sys.exit(main())
