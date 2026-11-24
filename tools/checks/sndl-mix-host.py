import subprocess
import tempfile
from pathlib import Path

CHECK = dict(name='sndl-mix-host', tier=1, needs=[], budget=6,
             covers=['userland/lib/snd/mix.[ch]', 'sdk/include/os32/os32_snd2.h',
                     'tools/tests/sndl_mix_host.c'],
             basis=['docs/decisions/sound/AV_HAL_DESIGN.md §7-2, §12'])

MUTATIONS = [
    ('upper saturation', 's > 32767 ? 32767', 's > 32767 ? 0'),
    ('phase', 'v->phase = pos & (ONE - 1)', 'v->phase = 0'),
    ('gain', 'attenuation % 100', '0'),
    ('stop', 'v->playing = 0;\n    return 0;', 'v->playing = 1;\n    return 0;'),
    ('reset completion', 'complete(b, SNDL_RESET)', 'complete(b, SNDL_PLAYED)'),
    ('release slot', 'lookup(id)->id = 0', 'lookup(id)->playing = 0'),
]


def run(ctx):
    root = Path(ctx.root)
    folder = root / 'userland/lib/snd'
    original = (folder / 'mix.c').read_text()
    with tempfile.TemporaryDirectory() as tmp:
        src = Path(tmp) / 'mix.c'
        exe = Path(tmp) / 'mix'
        for label, anchor, replacement in [('baseline', '', '')] + (MUTATIONS if ctx.mutate else []):
            if anchor and original.count(anchor) != 1:
                raise ValueError(f'{label}: mutation point changed')
            src.write_text(original.replace(anchor, replacement) if anchor else original)
            subprocess.run(['cc', '-std=gnu11', '-Wall', '-Wextra', '-Werror',
                            '-I', str(folder), '-I', str(root / 'sdk/include/os32'),
                            str(src), str(root / 'tools/tests/sndl_mix_host.c'),
                            '-o', str(exe)], check=True)
            result = subprocess.run([exe], cwd=tmp, capture_output=True, text=True)
            if anchor and result.returncode == -6 and 'Assertion' in result.stderr:
                print('RED', label)
            elif anchor or result.returncode:
                print(label, result.stderr)
                return 1
            else:
                print(result.stdout.strip())
    return 0
