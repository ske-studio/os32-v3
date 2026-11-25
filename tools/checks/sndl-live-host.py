"""Run the PCM and chip interfaces with controlled playback and real software FM."""
from pathlib import Path
import os
import subprocess
import tempfile

CHECK = dict(name='sndl-live-host', tier=1, needs=['make:all'], budget=8,
             covers=['tools/tests/sndl_live_host.c', 'userland/lib/snd/**', 'build/os32snd.mk',
                     'lib/os32_opna/**', 'sdk/include/os32/*.h'],
             basis=['docs/decisions/sound/AV_HAL_DESIGN.md §7-1, §7-3, 追記 §3'])

CASES = [
    ('pump', 'generations[i] != sndl_mix_generation(voice)', '0'),
    ('pump', 'scratch + 2*sent', 'scratch'),
    ('pump', 'pos.state == SND2_ST_IDLE && pos.cap', 'pos.state == SND2_ST_RUN && pos.cap'),
    ('pump', 'played - s->offset - s->count', 'played + 1 - s->offset - s->count'),
    ('pump', 'how = SND2_CLOSE_ABORT;', 'how = SND2_CLOSE_DRAIN;'),
    ('pump', 'if (refs > 1)', 'if (refs > 2)'),
    ('pump', 'if (how == SND2_CLOSE_ABORT || rc) sndl_mix_abort()', 'if (0) sndl_mix_abort()'),
    ('chip', '&& mode != SND2_DEV_FM_HW) rc = software', ') rc = software'),
    ('chip', 'rc && mode != SND2_DEV_FM_SOFT', 'rc'),
    ('chip', '(r.flags & SNDL_REQ_PREFER_SOFT)', '0'),
    ('chip', '(w->reg >= 0x10 && w->reg <= 0x1d)', '0'),
    ('chip', '(adpcm ? 12 : 0)', '0'),
]


def run(ctx):
    root = Path(ctx.root)
    snd = root / 'userland/lib/snd'
    cc = Path(os.environ['CROSS_DIR']) / 'bin/i386-elf-gcc'
    archive = root / 'build/out/opna/pic/target/i386-os32-snd/release/libos32_opna.a'
    with tempfile.TemporaryDirectory() as tmp:
        work = Path(tmp)
        for unit, old, new in [('', '', '')] + (CASES if ctx.mutate else []):
            sources = []
            for name in ('pump', 'chip', 'mix', 'render', 'fmvoice', 'panic'):
                src = snd / (name + '.c')
                if unit == name:
                    body = src.read_text()
                    assert body.count(old) == 1, old
                    src = work / src.name
                    src.write_text(body.replace(old, new))
                sources.append(src)
            exe = work / 'probe'
            subprocess.run([cc, '-march=i386', '-O2', '-ffreestanding', '-nostdlib',
                            '-fno-stack-protector', '-ffunction-sections', '-Wl,--gc-sections',
                            '-Isdk/include/os32', '-Iuserland/lib/snd', *sources,
                            'tools/tests/sndl_live_host.c', archive, '-lgcc', '-o', exe],
                           cwd=root, check=True)
            rc = subprocess.run(['qemu-i386', exe], timeout=8).returncode
            if old and 0 < rc < 200:
                print(f'RED {old} (assertion line {rc})')
            elif old or rc:
                raise RuntimeError(f'{old or "baseline"} exited {rc}')
            else:
                print('PASS scratch, start, positions, close, chip negotiation, read and shared PCM')
    return 0
