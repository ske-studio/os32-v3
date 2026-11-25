"""A0y public event conversion on a 32-bit host."""
from pathlib import Path
import subprocess
import tempfile

import host32

CHECK = dict(name='input-export-host', tier=1, budget=5, needs=[], covers=[
    'sys/hal/input/in2_export.c', 'sys/hal/input/in2_export.h', 'sys/hal/input/in2_int.h',
    'sys/include/types.h', 'sys/include/callconv.h', 'sys/include/kbd.h', 'sdk/include/os32/os32_in2.h',
    'tools/tests/input_export_host.c', 'tools/tests/test_input_export.py', 'tools/tests/host32.py'],
    basis=['INPUT_HAL_DESIGN §8 A0y', 'AV_HAL_DESIGN §5-2', 'AV0_V90_ADDENDUM §5'])


def run(ctx):
    source = (ctx.root / 'sys/hal/input/in2_export.c').read_text()
    with tempfile.TemporaryDirectory() as directory:
        unit = Path(directory) / 'in2_export.c'
        binary = Path(directory) / 'test'

        def execute(code):
            unit.write_text(code)
            subprocess.run(['gcc', '-m32', '-O2', '-ffreestanding', '-fno-stack-protector',
                            '-nostdlib', '-static', '-no-pie', '-Iinput/hal', '-Iinclude',
                            '-Isdk/include/os32', unit, 'tools/tests/input_export_host.c',
                            '-o', binary], cwd=ctx.root, check=True)
            return host32.run([str(binary)], runner='qemu').returncode

        assert execute(source) == 0
        if ctx.mutate:
            fields = ('time_ms seq dev type flags u.key.sc u.key.down u.key.mods '
                      'u.move.dx u.move.dy u.btn.down u.btn.changed u.wheel.delta '
                      'u.pad.buttons u.pad.changed u.abs.x u.abs.y u.lost.reason '
                      'u.lost.count').split()
            mutations = [('*out = (IN2_Event){0};', ''), ('out->seq = seq;', 'out->seq = event->generation;'),
                         ('event->y ? U16_MAX : (u16)event->x', '(u16)event->x')]
            for field in fields:
                anchor, = (line for line in source.splitlines()
                           if line.strip().startswith('out->' + field + ' = '))
                mutations.append((anchor, ''))
            for old, new in mutations:
                assert source.count(old) == 1
                assert 1 <= execute(source.replace(old, new)) <= 11, old
                print('RED runtime:', old.strip())
    return 0
