"""期待の出所: PPC_RESET_DESIGN §3・§7。
NetBSD sys/arch/macppc/dev/cuda.c:325–361,559–735,887–899,
pmu.c:475–545,620–624,775–784; OpenBSD pm_direct.c:294–322。
"""
import argparse
from pathlib import Path
import subprocess
import tempfile

import host32

CHECK = dict(name='powermac-reset', tier=1, needs=[], budget=20, covers=[
    'drivers/macintosh/via.c', 'drivers/macintosh/via.h',
    'include/types.h', 'include/callconv.h',
    'tools/tests/test_powermac_reset.py', 'tools/tests/via_host.c',
    'tools/tests/host32.py',
], basis=['docs/decisions/ppc/PPC_RESET_DESIGN.md §3・§7'])

ROOT = Path(__file__).resolve().parents[2]
DRIVER = ROOT / 'drivers/macintosh'
CASES = ('Cuda reset/wrap', 'PMU reset', 'Cuda pending', 'Cuda collision',
         'Cuda reply cap', 'Cuda stalled', 'PMU busy timeout', 'PMU idle timeout',
         'PMU free timeout', 'Cuda frame limit', 'Cuda overall deadline',
         'Cuda restart with unconsumed reply', 'Cuda fake start reply',
         'Cuda receive predicate')
MUTATIONS = (
    ('cuda-pseudo', 'CUDA_PSEUDO = 0x01', 'CUDA_PSEUDO = 0x02', 15),
    ('cuda-reset', 'CUDA_RESET = 0x11', 'CUDA_RESET = 0x12', 15),
    ('pmu-reset', 'PMU_RESET_CPU = 0xd0', 'PMU_RESET_CPU = 0xd1', 25),
    ('tip-before-sr', 'write8(v, SR, out[0]);\n            update(v, PORT_B, TIP, PB4);',
     'update(v, PORT_B, TIP, PB4);\n            write8(v, SR, out[0]);', 13),
    ('missing-ack', 'write8(v, SR, out[i]);\n                cuda_toggle_ack(v);',
     'write8(v, SR, out[i]);\n                (void)0;', 12),
    ('pb3-polarity', '(read8(v, PORT_B) & PB3) == 0', '(read8(v, PORT_B) & PB3) != 0', 12),
    ('stale-ifr', 'if (!wait_bit(v, start, IFR, SR_DONE, true))\n                    break;',
     'if (false)\n                    break;', 13),
    ('fake-start', 'return 1; /* Fake start: no frame was received. */', 'return 0;', 134),
    ('null-reply', 'rc || !in || !cap', 'rc || !cap', 142),
    ('zero-cap', 'rc || !in || !cap', 'rc || !in', 143),
    ('prepare-delay', 'if (!delay(v, start, CUDA_SETTLE))\n        goto timeout;\n    update(v, PORT_B, TIP, 0);',
     'if (false)\n        goto timeout;\n    update(v, PORT_B, TIP, 0);', 11),
    ('collision-drop', 'rc = 1; /* Collision, go back to input before receiving. */',
     'rc = 0;', 44),
    ('edge-deadline', '|| current - begin >= ticks',
     '|| (ticks == 0 && current - begin >= ticks)', 67),
    ('overall-deadline', 'current - start >= v->io.tb_hz',
     'current - start >= 2ULL * v->io.tb_hz', 117),
)


def observe(folder, source):
    (folder / 'via.c').write_text(source)
    exe = folder / 'via_host'
    host32.build(['gcc', '-m32', '-O2', '-std=gnu11', '-Wall', '-Wextra', '-Werror',
                  '-ffreestanding', '-fno-pie', '-no-pie', '-fno-stack-protector',
                  '-nostdlib', '-static', '-I' + str(folder), '-I' + str(DRIVER),
                  '-I' + str(ROOT / 'include'), str(ROOT / 'tools/tests/via_host.c'),
                  '-o', str(exe)], check=True)
    return host32.run([str(exe)], timeout=5).returncode


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--mutate', action='store_true')
    args = parser.parse_args()
    source = (DRIVER / 'via.c').read_text()
    with tempfile.TemporaryDirectory(prefix='via-host-') as temp:
        folder = Path(temp)
        rc = observe(folder, source)
        assert rc == 0, f'VIA control failed (rc={rc})'
        print('GREEN ' + ', '.join(CASES))
        if args.mutate:
            for name, old, new, expected in MUTATIONS:
                assert source.count(old) == 1, f'{name}: mutation point changed'
                rc = observe(folder, source.replace(old, new))
                assert rc == expected, f'{name}: rc={rc}, expected runtime RED {expected}'
                print(f'RED {name} ({CASES[rc // 10 - 1]}, rc={rc})')


def run(ctx):
    return subprocess.call(['python3', '-B', str(Path(__file__).resolve()),
                            *(['--mutate'] if ctx.mutate else [])], cwd=ctx.root)


if __name__ == '__main__':
    main()
