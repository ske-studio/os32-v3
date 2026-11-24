"""Exercise the L2 clock and file reader against the linked core and original fmgen."""
from pathlib import Path
import subprocess
import tempfile

CHECK = dict(name='sndl-fmtime-host', tier=1, needs=[], budget=25,
             covers=['userland/lib/snd/fmvoice.[ch]', 'tools/tests/sndl_fmvoice_host.cpp',
                     'lib/os32_opna/src/*.rs', 'lib/os32_opna/opna*.h',
                     'lib/os32_opna/upstream/**'],
             basis=['docs/decisions/sound/AV_HAL_DESIGN.md §7-4/§8/§12',
                    'docs/decisions/sound/SYNTH_OPNA_DESIGN.md §3/§9'])


def run(ctx):
    root = Path(ctx.root)
    core = root / 'lib/os32_opna'
    snd = root / 'userland/lib/snd'
    original = (snd / 'fmvoice.c').read_text()
    upstream = core / 'upstream'
    with tempfile.TemporaryDirectory() as tmp:
        work = Path(tmp)
        (work / 'compiler.h').touch()
        objects = []
        for src in [root / 'tools/tests/sndl_fmvoice_host.cpp',
                    *(upstream / f'fmgen_{part}.cpp' for part in ('fmgen', 'opna', 'fmtimer', 'psg'))]:
            obj = work / (src.stem + '.o')
            subprocess.run(['g++', '-O2', '-std=c++17', '-fwrapv', '-DSUPPORT_FMGEN',
                            '-DMAX_PATH=260', '-ffunction-sections', '-fdata-sections', '-I', work,
                            '-I', upstream, '-I', snd, '-c', src, '-o', obj], check=True)
            objects.append(obj)
        archive = work / 'core.a'
        subprocess.run(['rustc', '--edition=2021', '--crate-type=staticlib', '-O', '-Cpanic=abort',
                        core / 'src/lib.rs', '-o', archive], check=True)
        mutants = [('frame clamp', 'frame = frames-1', 'frame = 0'),
                   ('CSM applied', 'opna_apply_csm(v->state)', '(void)0'),
                   ('RIFF form', '!tag(h+8,"WAVE")', '0'),
                   ('chunk padding', 'n + (n & 1)', 'n'),
                   ('late timer rewind', 'while (before(v->tm_us, until))',
                    'if (before(until,v->tm_us)) { v->tm_us=until; }\n    while (before(v->tm_us, until))')]
        for label, old, new in [('baseline', '', '')] + (mutants if ctx.mutate else []):
            if old and original.count(old) != 1:
                raise ValueError(f'{label}: mutation point changed')
            src = work / 'fmvoice.c'
            src.write_text(original.replace(old, new) if old else original)
            obj = work / 'adapter.o'
            subprocess.run(['cc', '-O2', '-std=gnu11', '-Wall', '-Wextra', '-Werror',
                            '-I', snd, '-c', src, '-o', obj], check=True)
            exe = work / 'check'
            subprocess.run(['g++', '-Wl,--gc-sections', '-Wl,--wrap=opna_write',
                            '-Wl,--wrap=opna_render', '-Wl,--wrap=opna_apply_csm',
                            *objects, obj, archive, '-o', exe], check=True)
            result = subprocess.run([exe], cwd=work, capture_output=True, text=True)
            if old and result.returncode == -6 and 'Assertion' in result.stderr:
                print('RED', label)
            elif old or result.returncode:
                raise ValueError(f'{label}: {result.stderr}')
            else:
                print('PASS timing, timer/read ordering, WAV inputs and stereo PCM vs fmgen')
    return 0
