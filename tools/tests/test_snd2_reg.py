"""Link the PCM registry, caps and DSP providers without libc or hardware."""
from pathlib import Path
import subprocess
import tempfile

CHECK = dict(name='snd2-reg-host', tier=1, needs=[], budget=3,
    covers=['tools/tests/snd2_reg_host.c', 'sound/hal/snd2*.c',
            'sound/hal/snd2*.h', 'include/av_contract.h', 'lib/kstring.h',
            'include/types.h', 'sdk/include/os32/*.h'],
    basis=['docs/decisions/sound/AV_HAL_DESIGN.md §3, §13-2'])

MUTATIONS = [
    ('reg', 'd->info.flags |= SND2_DEV_FAULTED;', 'd->info.flags |= 0;'),
    ('caps', 'l >= c->ring_frames / p', 'l > c->ring_frames / p'),
    ('caps', 'v1 ? 8192', 'v1 ? 4096'),
    ('dsp', 'frames > info.max_frames', 'frames >= info.max_frames'),
    ('dsp', 'i >= info.param_count', 'i > info.param_count'),
]


def run(ctx):
    root = ctx.root
    hal = root / 'sound/hal'
    with tempfile.TemporaryDirectory() as tmp:
        tmp = Path(tmp)
        start = tmp / 'start.S'
        start.write_text('.globl _start\n_start:\n call probe\n mov %eax,%ebx\n mov $1,%eax\n int $0x80\n')
        for case in [None] + (MUTATIONS if ctx.mutate else []):
            sources = []
            for name in ('reg', 'caps', 'dsp'):
                src = hal / f'snd2_{name}.c'
                if case and name == case[0]:
                    body = src.read_text()
                    assert body.count(case[1]) == 1
                    src = tmp / src.name
                    src.write_text(body.replace(case[1], case[2]))
                sources.append(src)
            binary = tmp / 'probe'
            subprocess.run(['gcc', '-m32', '-std=gnu11', '-Wall', '-Werror',
                            '-ffreestanding', '-nostdlib', '-static', '-fno-pie',
                            '-Wl,-z,noexecstack', '-I', hal, '-Iinclude', '-Ilib',
                            '-Isdk/include/os32', start, *sources,
                            'tools/tests/snd2_reg_host.c', '-o', binary],
                           cwd=root, check=True)
            rc = subprocess.run(['qemu-i386', binary]).returncode
            if (case is None and rc != 0) or (case and not 0 < rc < 126):
                raise RuntimeError(f'{case}: probe exited {rc}')
            if case:
                print(f'RED {case[1]} (assertion at line {rc})')
    return 0
