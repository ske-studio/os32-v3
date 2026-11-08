"""Execute tri_burst6 grouped pacing, comparison, cleanup and log budget."""
CHECK = dict(name='tri-burst6', tier=1, needs=[], budget=240, covers=[
    'tools/tests/test_tri_burst6.py', 'tools/tests/tri_burst6_host.c',
    'userland/tests/hw/tri_burst6.hwb', 'tools/tests/hwb_fake.h',
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
    script = (hwb_host.ROOT / 'userland/tests/hw/tri_burst6.hwb').read_text(encoding='ascii')
    if mutation:
        name, before, after = mutation
        assert script.count(before) == 1, name
        script = script.replace(before, after)
    code = (hwb_host.ROOT / 'tools/tests/tri_burst6_host.c').read_text().replace('SCRIPT_DATA', json.dumps(script))
    with tempfile.TemporaryDirectory(prefix='tri-burst6-') as directory:
        fixture = Path(directory) / 'host.c'
        exe = Path(directory) / 'host'
        fixture.write_text(code)
        hwb_host.build(hwb_host.ROOT, fixture, exe)
        # Five complete pacing matrices exceed the usual one-script 60s budget.
        result = hwb_host.host32.run([str(exe)], cwd=hwb_host.ROOT,
                                     capture_output=True, text=True, timeout=240)
        print(result.stdout, end='', flush=True)
        if mutation:
            if result.returncode != 1 or 'FAIL:tri-burst6:' not in result.stdout:
                raise RuntimeError(name + ': expected runtime RED')
            print(name + ': RED runtime')
        elif result.returncode:
            raise RuntimeError('tri-burst6 control failed')
        else:
            hwb_host.build(hwb_host.ROOT, fixture, Path(directory) / 'target.o', cross=True)


def main():
    trial()
    if '--mutate' in sys.argv:
        for mutation in [
            ('v3-count', 'let count = 256', 'let count = 512'),
            ('v4-group', 'let group = 2', 'let group = 4'),
            ('v5-group', 'let variant = 5\n let group = 4', 'let variant = 5\n let group = 8'),
            ('block-increment', 'mmioblock32 f + off group data inc', 'mmioblock32 f + off group data'),
            ('block-offset', 'let off = off + (group << 2)', 'let off = off + (group << 1)'),
            ('last-read', 'off + ((group - 1) << 2)', 'off + (group << 2)'),
            ('status-read', 'label grouped_store\n mmioblock32 f + off group data inc\n if variant == 5 goto grouped_readback\n mmioread8 v + 0x3DA -> discard',
             'label grouped_store\n mmioblock32 f + off group data inc\n if variant == 5 goto grouped_readback\n mmioread8 v + 0x3CC -> discard'),
            ('group-print', 'print "group" group', 'print "group" 0'),
            ('depth', 'call set_depth\n print "depth-bpp" depth', 'log "skip depth"\n print "depth-bpp" depth'),
            ('seed', 'let data = data + group', 'let data = data + 1'),
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
