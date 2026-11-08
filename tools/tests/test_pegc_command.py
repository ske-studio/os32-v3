"""Execute the PEGC benchmark and shell adapter against a full RAM frame."""
CHECK = dict(name='pegc-command', tier=1, needs=[], budget=30, covers=[
    'kdebug/pegc_command.c', 'kdebug/pegc_command_wire.h',
    'drivers/video/pc98/pegc_diagnostic.h', 'userland/shell/cmd_pegc.c',
    'tools/tests/pegc_command_host.c', 'tools/tests/test_pegc_command.py',
    'tools/tests/hwb_fake.h', 'tools/tests/hwb_host.py', 'tools/tests/host32.py',
    'tools/tests/kernel_includes.py', 'tools/tests/mutpar.py',
    'include/*.h', 'gfx/*.h', 'lib/*.h', 'mm/pgalloc.h', 'mm/physmem.h', 'mm/paging.h', 'exec/surface_limits.h', 'kernel/*.h',
    'kernel/time/*.h', 'userland/shell/shell.h', 'userland/shell/hwb.h',
    'kdebug/*wire.h', 'kdebug/generated/**', 'sdk/include/os32/*.h'])
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import hwb_host
from mutpar import build_in_tree

ROOT = hwb_host.ROOT
MUTATIONS = [
    ('kdebug/pegc_command.c', 'run < PEGC_RUNS', 'run < PEGC_RUNS - 1u'),
    ('kdebug/pegc_command.c', 'if (v->vram[i] != v->client[i]) ++mismatches;',
     'if (v->vram[i] == v->client[i]) ++mismatches;'),
    ('kdebug/pegc_command.c', 'v.vram[i] = saved[i];', 'v.vram[i] = 0;'),
    ('kdebug/pegc_command.c', 'loops |= 1u << bit;', 'loops |= 0u << bit;'),
    ('kdebug/pegc_command.c', 'dst[i] = v->vram[i];', 'dst[i] = 0;'),
    ('userland/shell/cmd_pegc.c', '!req.step_ns || req.start_ns > req.end_ns',
     'req.start_ns > req.end_ns'),
]

def main():
    with tempfile.TemporaryDirectory(prefix='pegc-command-') as td:
        work = Path(td)
        (work / "string.h").write_text("int strcmp(const char *, const char *);\n")
        cases = [None] + (MUTATIONS + [None] if '--mutate' in sys.argv else [])
        for index, mutation in enumerate(cases):
            changes = {}
            if mutation:
                path, old, new = mutation
                source = (ROOT / path).read_text()
                assert source.count(old) == 1
                changes[path] = source.replace(old, new)
            exe = work / str(index)
            cmd = hwb_host.build_command(ROOT, 'tools/tests/pegc_command_host.c', exe, flags=['-I'+str(work)])
            build_in_tree(ROOT, work / ('tree'+str(index)), changes, [cmd], capture_output=False)
            result = hwb_host.execute(exe)
            assert result.returncode == (1 if mutation else 0), result
            print(('RED runtime ' if mutation else 'GREEN ') + str(index), flush=True)
    return 0

def run(ctx):
    return subprocess.call([sys.executable, '-B', __file__,
        *(['--mutate'] if ctx.mutate else [])], cwd=ctx.root,
        env=dict(os.environ, HOST32_RUNNERS=' '.join(ctx.runners)))

if __name__ == '__main__':
    raise SystemExit(main())
