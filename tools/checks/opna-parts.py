"""Differential traces against the bundled, independently compiled fmgen."""
from pathlib import Path
import shutil
import subprocess
import tempfile

CHECK = dict(name='opna-parts', tier=1, needs=[], budget=12,
             covers=['lib/os32_opna/upstream/**', 'lib/os32_opna/src/*.rs',
                     'tools/tests/opna_parts/*', 'tools/checks/opna-parts.py'],
             basis=['docs/decisions/sound/SYNTH_OPNA_DESIGN.md §3'])


def trace():
    ops = []

    def emit(op, reg=0, val=0):
        ops.append(f'{op} {reg} {val}\n')

    def write(reg, val):
        emit('W', reg, val)

    for p in range(3):
        write(0x2d+p, 0)
        write(0x24, 251)
        write(0x25, 3)
        write(0x26, 255)
        for ctl in [0x8f, 0x3f, 0x0a, 0x05, 0]:
            write(0x27, ctl)
            for delay in [1, 17, 32767, 94, 512, 32767]:
                emit('T', 0, delay)
                emit('S')
    for mask in [0, 1, 2, 3]:
        write(0x110, mask)
        write(0x27, 0x3f)
        for _ in range(8):
            emit('T', 0, 32767)
        for enables in [0, 1, 2, 31]:
            write(0x29, enables)
            emit('Q')
            emit('S')
        write(0x110, 128)
    for mode in [0, 2]:
        emit('X')
        write(0x110, 0)
        write(0x101, 0xc0 | mode)
        write(0x104, 0)
        write(0x10c, 3)
        write(0x100, 0x60)
        for i in range(128):
            write(0x108, (i*53+174) & 255)
        for plane in range(8):
            for offset in range(16):
                emit('B', plane*0x8000+offset)
        write(0x102, 0)
        write(0x100, 0x20)
        for i in range(131):
            emit('D')
            if i % 4 == 0:
                emit('S')
        for repeat in [0, 16]:
            for delta in [0x1278, 0xe7f2]:
                write(0x100, 1)
                write(0x109, delta & 255)
                write(0x10a, delta >> 8)
                write(0x10b, 255)
                write(0x100, 0xa0 | repeat)
                for frames in [1, 11, 19, 256, 61]:
                    emit('P', 0, frames)
                    emit('S')
    emit('X')
    write(0x101, 0x80)
    write(0x110, 0)
    write(0x10a, 240)
    write(0x10b, 239)
    write(0x100, 128)
    emit('P', 0, 9)
    for i in range(65538):
        write(0x108, (i*11+9) & 255)
        if i >= 65516 and i <= 65524:
            emit('S')
    for _ in range(214):
        emit('P', 0, 256)
    emit('S')
    emit('X')
    # Install voice zero last: fmgen's all-or-none loader leaves it null on failure.
    for i in [1, 2, 3, 4, 5, 0]:
        emit('I', i, 10900+7211*i)
        write(0x18+i, (1+i%3)*64+31-i)
        if i != 0:
            write(0x10, 63)
            emit('P', 0, 17)
    write(0x11, 59)
    for i in range(6):
        emit('V', i, 89+i*5)
        write(0x10, 1<<i)
        emit('P', 0, 7+i)
    emit('G', 0, 120)
    write(0x10, 63)
    emit('M', 0, 13<<10)
    emit('P', 0, 31)
    write(0x10, 0x8e)
    emit('M')
    emit('P', 0, 256)
    emit('X')
    write(0x10, 63)
    write(0x11, 63)
    write(0x18, 255)
    emit('P', 0, 32)
    return ''.join(ops).encode()


def run(ctx):
    root = Path(ctx.root)
    core = root / 'lib/os32_opna'
    upstream = core / 'upstream'
    fixtures = root / 'tools/tests/opna_parts'
    with tempfile.TemporaryDirectory() as tmp:
        work = Path(tmp)
        (work / 'compiler.h').touch()
        subprocess.run(['g++', '-std=c++17', '-DSUPPORT_FMGEN', '-DMAX_PATH=260',
                        '-I', work, '-I', upstream, '-ffunction-sections', '-fdata-sections',
                        '-Wl,--gc-sections', fixtures / 'oracle.cpp',
                        *(upstream / f'fmgen_{n}.cpp' for n in ('fmgen','opna','psg','fmtimer')),
                        '-o', work / 'oracle'], check=True)
        inputs = trace()
        def output(binary):
            return subprocess.check_output([work / binary], input=inputs)
        expected = output('oracle')
        for name in ('state','tables','adpcm','rhythm','timer'):
            shutil.copy(core / 'src' / f'{name}.rs', work)
        shutil.copy(fixtures / 'driver.rs', work)

        def compare():
            subprocess.run(['rustc', '--edition=2021', work / 'driver.rs', '-o', work / 'driver'], check=True)
            return output('driver') == expected

        if not compare():
            actual = output('driver').splitlines()
            index = next(i for i, pair in enumerate(zip(expected.splitlines(), actual)) if pair[0] != pair[1])
            offset = 0
            for cmd in inputs.splitlines():
                op, _, val = cmd.split()
                count = 2 * int(val) if op == b'P' else int(op in (b'D', b'S', b'T', b'B', b'Q'))
                if offset + count > index:
                    raise ValueError(f'{cmd!r} at output {index}: fmgen {expected.splitlines()[index]!r}, Rust {actual[index]!r}')
                offset += count
            raise ValueError('output length mismatch')
        print(f'{expected.count(bytes([10]))} fmgen output lines matched')
        if ctx.mutate:
            for name, old, new in [
                ('adpcm','a.output + n','a.output - n'),
                ('adpcm','a.fifo_count < 65536 - 16','a.fifo_count <= 65536 - 16'),
                ('rhythm','32 + db','31 + db'),
                ('timer','self.reg_a[1] & 3','self.reg_a[1] & 2'),
            ]:
                path = work / f'{name}.rs'
                src = path.read_text()
                if src.count(old) != 1:
                    raise ValueError(f'missing unique {name} mutation: {old}')
                path.write_text(src.replace(old,new))
                if compare():
                    raise ValueError(f'survived: {old}')
                print(f'RED {name}: {old}')
                path.write_text(src)
    return 0
