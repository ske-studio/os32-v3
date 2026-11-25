"""Link the PCM registry, caps and DSP providers without libc or hardware."""
from pathlib import Path
import subprocess
import tempfile

CHECK = dict(name='snd2-reg-host', tier=1, needs=[], budget=3,
    covers=['tools/tests/snd2_reg_host.c', 'sound/hal/snd2*.c',
            'sound/hal/snd2*.h', 'include/machcfg_contract.h', 'include/av_contract.h', 'lib/kstring.h',
            'include/types.h', 'kapi/kapi_snd2*.c', 'include/user_read_contract.h',
            'exec/exec.h', 'include/memmap.h', 'sdk/include/os32/*.h'],
    basis=['docs/decisions/sound/AV_HAL_DESIGN.md §3, §13-2'])

MUTATIONS = [
    ('reg', 'if (!caps.fmt[i].rate)', 'if (!caps.fmt[0].rate)'),
    ('reg', 'caps.ring_frames > SND2_MAX_RING_FRAMES', 'caps.ring_frames > SND2_MAX_RING_FRAMES + 1'),
    ('reg', '#define SND2_MAX_DEV 16', '#define SND2_MAX_DEV 15'),
    ('reg', 'return OS32_ERR_FULL;', 'return OS32_ERR_INVAL;'),
    ('caps', 'p < c->period_min', 'p + 1 < c->period_min'),
    ('reg', 'd->info.flags |= SND2_DEV_FAULTED;', 'd->info.flags |= 0;'),
    ('caps', 'l >= c->ring_frames / p', 'l > c->ring_frames / p'),
    ('caps', 'v1 ? SND2_STG_FRAMES', 'v1 ? 4096'),
    ('kapi_snd2', '(unsigned long long)frames * stride', 'frames * stride'),
    ('kapi_snd2', 'check_read(in, (u32)bytes);', 'check_read(in, 1);'),
    ('kapi_snd2', '(u32)out, (u32)bytes', '(u32)out, 1'),
    ('kapi_snd2_chip', 'snd2_dev_count() + chips', 'snd2_dev_count()'),
    ('kapi_snd2_chip', 'index - pcm', 'index'),
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
            for name in ('reg', 'caps', 'dsp', 'kapi_snd2', 'kapi_snd2_chip'):
                src = hal / f'snd2_{name}.c'
                if name.startswith('kapi_'):
                    src = root / 'kapi' / (name + '.c')
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
                            '-ffunction-sections', '-Wl,--gc-sections', '-Ikapi', '-Iexec', '-Isdk/include/os32', start, *sources,
                            'tools/tests/snd2_reg_host.c', '-o', binary],
                           cwd=root, check=True)
            rc = subprocess.run(['qemu-i386', binary]).returncode
            if (case is None and rc != 0) or (case and not 0 < rc < 126):
                raise RuntimeError(f'{case}: probe exited {rc}')
            if case:
                print(f'RED {case[1]} (probe assertion offset {rc})')
    return 0
