"""Drive the original OPNA and the C ABI with the same fixed score."""
from pathlib import Path
import shutil
import subprocess
import tempfile

CHECK = dict(name='opna-core', tier=1, needs=[], budget=25,
             covers=['lib/os32_opna/upstream/**', 'lib/os32_opna/src/*.rs',
                     'lib/os32_opna/opna*.h', 'tools/tests/opna_core/*'],
             basis=['docs/decisions/sound/SYNTH_OPNA_DESIGN.md §3/§9'])


def run(ctx):
    core = Path(ctx.root) / 'lib/os32_opna'
    upstream = core / 'upstream'
    fixture = Path(ctx.root) / 'tools/tests/opna_core/compare.cpp'
    with tempfile.TemporaryDirectory() as tmp:
        work = Path(tmp)
        shutil.copytree(core / 'src', work / 'src')
        (work / 'compiler.h').touch()
        objects = []
        for src in [fixture, *(upstream / f'fmgen_{part}.cpp' for part in
                               ('fmgen', 'opna', 'fmtimer', 'psg'))]:
            obj = work / (src.stem + '.o')
            subprocess.run(['g++', '-std=c++17', '-O2', '-fwrapv', '-DSUPPORT_FMGEN',
                            '-DMAX_PATH=260', '-ffunction-sections', '-fdata-sections',
                            '-I', core, '-I', work, '-I', upstream, '-c', src, '-o', obj], check=True)
            objects.append(obj)

        def compare():
            archive = work / 'libopna.a'
            subprocess.run(['rustc', '--edition=2021', '--crate-type=staticlib', '-O',
                            '-Cpanic=abort', work / 'src/lib.rs', '-o', archive], check=True)
            exe = work / 'compare'
            subprocess.run(['g++', '-Wl,--gc-sections', *objects, archive, '-o', exe], check=True)
            return subprocess.run([exe], capture_output=True, text=True)

        normal = compare()
        if normal.returncode:
            raise ValueError(normal.stderr)
        print('Full register trace, timer deadlines, status and stereo PCM match fmgen')
        if ctx.mutate:
            for name, old, new, diagnostic in [
                ('registers', 'key_control(15, clock)', 'key_control(0, clock)', 'PCM block'),
                ('lib', 'us -= step;', 'us -= if step == 32767 { us } else { step };', 'timer deadline'),
                ('adpcm', 'decode(a, data >> 4)', 'decode(a, data & 15)', 'PCM block'),
                ('adpcm', 'a.playing = 0;\n        s.status |= 4 & s.status_mask;',
                 'a.playing = 0;\n        s.status |= 4;', 'status'),
                ('rhythm', 'v.position.saturating_add(v.step).min(v.frames * 1024)',
                 'v.position.wrapping_add(v.step)', 'rhythm endpoint'),
            ]:
                path = work / 'src' / (name + '.rs')
                src = path.read_text()
                assert src.count(old) == 1, old
                path.write_text(src.replace(old, new))
                result = compare()
                if result.returncode == 0 or diagnostic not in result.stderr:
                    raise ValueError(f'{name} expected {diagnostic}: {result.stderr}')
                print('RED', result.stderr.strip())
                path.write_text(src)
    return 0
