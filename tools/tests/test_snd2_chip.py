"""Link the FM/PSG registry and ownership provider without libc or hardware."""
from pathlib import Path
import subprocess
import tempfile

CHECK = dict(name='snd2-chip-host', tier=1, needs=[], budget=3,
    covers=['tools/tests/snd2_chip_host.c', 'sound/hal/snd2*.c',
            'sound/hal/snd2*.h', 'include/av_contract.h', 'lib/kstring.h',
            'include/types.h', 'sdk/include/os32/*.h'],
    basis=['docs/decisions/sound/AV_HAL_DESIGN.md FM の確定版 §2, §5, §7'])

MUTATIONS = [
    ('chip', 'rc == OS32_ERR_STALE || rc == OS32_ERR_BUSY', 'rc == OS32_ERR_BUSY'),
    ('chip', 'rc == OS32_ERR_STALE || rc == OS32_ERR_BUSY', 'rc == OS32_ERR_STALE'),
    ('chip', 'c->info.flags |= SND2_DEV_FAULTED;', 'c->info.flags |= 0;'),
    ('chip', 'c->gen + 1', 'c->gen + 0'),
    ('chip', 'return c->be->quiet(c->ctx) < 0', '++c->gen; return c->be->quiet(c->ctx) < 0'),
    ('chip', 'quiet < 0 || close < 0', 'quiet > 0 || close < 0'),
    ('chip', 'if (c->be->open(c->ctx) < 0)', 'if (c->be->open(c->ctx) > 0)'),
    ('chip', 'if (rc != (int)n)', 'if (rc > (int)n)'),
    ('chip', 'w[i].bank >=', 'w[0].bank >='),
    ('chip', 'return OS32_ERR_BUSY;\n    if (c->owner &&', 'return 0;\n    if (c->owner &&'),
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
            for name in ('chip',):
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
                            'tools/tests/snd2_chip_host.c', '-o', binary],
                           cwd=root, check=True)
            rc = subprocess.run(['qemu-i386', binary]).returncode
            if (case is None and rc != 0) or (case and not 0 < rc < 126):
                raise RuntimeError(f'{case}: probe exited {rc}')
            if case:
                print(f'RED {case[1]} (probe assertion offset {rc})')
    return 0
