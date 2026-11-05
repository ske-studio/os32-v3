#!/usr/bin/env python3
"""3b restore: shared device fake, full script trace and every-access faults.

Oracle: trident_mode.hwb:499-519,1497-1585 (MIT notices there and in
state_restore.c); vgaHW.c:369-409,845-892. The committed script is the
fixture: unroll its calls/loops, split ONLY VGA packed writes into the
reference's two byte writes, select current/saved mono/color bases per the
3a contract. No reference checkout, product parsing or product-generated
expectations. To regenerate expectations, run this suite after an approved
script/contract change. Full production bind/lifecycle assembly belongs to
3c/3d; this standalone 3b tree has declarations only for those components.
"""
CHECK = dict(name='trident-restore', tier=1, needs=[], budget=30,
    covers=['drivers/video/trident/state_restore.c',
            'drivers/video/trident/trident.h', 'tools/tests/trident_restore_host.c',
            'tools/tests/trident_device_fake.c', 'tools/tests/trident_device_fake.h',
            'userland/tests/hw/trident_mode.hwb', 'tools/tests/host32.py',
            'tools/tests/kernel_includes.py', 'build/config.mk', 'include/types.h',
            'include/memmap.h', 'gfx/palette.h', 'include/pegc.h'])
import argparse
from pathlib import Path
import re
import struct
import subprocess
import tempfile

import host32
from kernel_includes import include_flags

ROOT = Path(__file__).resolve().parents[2]
PRODUCT = 'drivers/video/trident/state_restore.c'
HOST = 'tools/tests/trident_restore_host.c'
FAKE = 'tools/tests/trident_device_fake.c'
SCRIPT = 'userland/tests/hw/trident_mode.hwb'
FLAGS = ['-m32', '-march=i386', '-std=gnu11', '-O2', '-Wall', '-Wextra',
         '-Werror', '-ffreestanding', '-fno-pie', '-fno-stack-protector',
         '-nostdlib', '-static', '-no-pie', '-Wl,-e,_start']
# name, unique product anchor, replacement. All must compile then runtime RED.
MUTATIONS = (
    ('restore-xor', 's[TRI_SAVE_SR0E] ^ 0x02', 's[TRI_SAVE_SR0E] ^ 0x00'),
    ('dac-last', 'i < TRI_DAC_BYTES;', 'i < TRI_DAC_BYTES - 1u;'),
    ('gr6', 's[TRI_SAVE_GR + i]', 's[TRI_SAVE_GR + (i == 6 ? 5 : i)]'),
    ('cr3a', 's[TRI_SAVE_CR3A]', 's[TRI_SAVE_CR2A]'),
    ('active', 'if (!d->active) return TRI_OK;', 'if (d->active) return TRI_OK;'),
    ('valid', 'if (!d->saved.valid) return TRI_E_STATE;',
     'if (!d->saved.valid) return TRI_OK;'),
    ('current-base', '(misc & 1u) ? TRI_CR_COLOR : TRI_CR_MONO',
     '(misc & 1u) ? TRI_CR_MONO : TRI_CR_COLOR'),
    ('saved-base', '(s[TRI_SAVE_MISC] & 1u) ? TRI_CR_COLOR : TRI_CR_MONO',
     '(s[TRI_SAVE_MISC] & 1u) ? TRI_CR_MONO : TRI_CR_COLOR'),
    ('mclk-unsaved', 'if (TRI_SAVE_MCLK)', 'if (!TRI_SAVE_MCLK)'),
    ('vga-width', 'TRY(trident_write8(d, TRI_SPACE_REGS, port, index));\n'
     '    return trident_write8(d, TRI_SPACE_REGS, port + 1u, value);',
     'return trident_indexed_write(d, port, index, value);'),
    ('dac-delay', 'base + 6u, &ignored));\n    }',
     'base + 6u, &ignored));\n        TRY(trident_read8(d, TRI_SPACE_REGS, base + 6u, &ignored));\n    }'),
    ('exit-rc', 'return trident_protect(d, base, false);',
     'int rc = trident_protect(d, base, false); return rc ? TRI_OK : rc;'),
)


def oracle(current, saved):
    lines = (ROOT / SCRIPT).read_text().splitlines()
    labels = {line.strip()[6:]: i for i, line in enumerate(lines)
              if line.strip().startswith('label ')}
    values = [(i * 37 + 11) & (63 if i < 768 else 255) for i in range(852)]
    values[768] = 0xA0 | saved
    env = dict(save=values, savemclk=0, cb=0x3D4 if saved else 0x3B4,
               savedseq=0x15, t=0, flip=0)
    events = [(False, 1, 0x3CC, 0)]  # frozen contract's current-base selection
    current_base = 0x3D4 if current else 0x3B4
    stack = []
    pc = labels['state_restore'] + 1
    def value(expr):
        return eval(expr, {'__builtins__': {}}, env)
    while True:
        line = lines[pc].strip()
        source_line = pc + 1
        pc += 1
        if not line or line.startswith(('#', 'label ')):
            continue
        if line.startswith('call '):
            stack.append(pc)
            pc = labels[line[5:]] + 1
        elif line == 'return':
            if not stack:
                break
            pc = stack.pop()
        elif line.startswith('let '):
            name, expr = line[4:].split(' = ')
            env[name] = value(expr)
        elif line.startswith('if '):
            condition, target = line[3:].split(' goto ')
            if value(condition):
                pc = labels[target] + 1
        else:
            match = re.fullmatch(r'mmio(write|read)(8|16) v \+ (0x[0-9A-Fa-f]+|cb|\([^)]*\)) (.*)', line)
            if not match:
                raise AssertionError(f'unhandled script line {source_line}: {line}')
            kind, bits, address, expr = match.groups()
            off = value(address)
            # Entry protect uses current base, then TridentRestore uses it
            # until VGA writes the saved MISC. Script assumed color here.
            if off == 0x3DA and source_line < 520:
                off = current_base + 6
            if off == 0x3D4 and 1500 <= source_line <= 1534:
                off = current_base
            if kind == 'read':
                target = expr.removeprefix('-> ')
                if target == 'savedseq':
                    env[target] = 0x15 if source_line < 520 else values[824]
                else:
                    env[target] = 0
                events.append((False, int(bits) // 8, off, 0))
            else:
                v = value(expr)
                if bits == '16' and 1539 <= source_line <= 1550:
                    events.extend([(True, 1, off, v & 255), (True, 1, off + 1, v >> 8)])
                else:
                    events.append((True, int(bits) // 8, off, v))
    return events


def check_trace(data):
    pos = 0
    event = struct.Struct('<IIIIBB2xi')  # frozen ILP32 fake event layout
    for current in range(2):
        for saved in range(2):
            count, = struct.unpack_from('<I', data, pos)
            pos += 4
            expected = oracle(current, saved)
            assert count == len(expected), (current, saved, count, len(expected))
            for i, (write, width, off, v) in enumerate(expected):
                space, actual_off, actual_v, elements, actual_width, actual_write, rc = event.unpack_from(data, pos)
                pos += event.size
                assert (space, actual_off, elements, actual_width, actual_write, rc) == (0, off, 1, width, write, 0), (current, saved, i, off)
                if write:
                    assert actual_v == v, (current, saved, i, hex(off), actual_v, v)
    assert pos == len(data)


def run(source, directory):
    product = directory / 'state_restore.c'
    product.write_text(source)
    binary = directory / 'restore-host'
    subprocess.run(['gcc', *FLAGS, '-I' + str(ROOT),
                    '-I' + str(ROOT / 'drivers/video/trident'), *include_flags(ROOT),
                    str(product), str(ROOT / HOST), str(ROOT / FAKE), '-o', str(binary)],
                   check=True, capture_output=True)
    result = host32.run([str(binary)], capture_output=True)
    return result


@host32.control_session
def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--mutate', action='store_true')
    args = parser.parse_args()
    source = (ROOT / PRODUCT).read_text()
    with tempfile.TemporaryDirectory(prefix='trident-restore-') as tmp:
        directory = Path(tmp)
        with host32.control(args.mutate, None, ROOT):
            result = run(source, directory)
            assert result.returncode == 0, f'normal runtime rc={result.returncode}'
            check_trace(result.stdout)
            print('GREEN: four mono/color traces, all DAC components, every-access failures, inactive/invalid gates')
        if args.mutate:
            for name, old, new in MUTATIONS:
                assert source.count(old) == 1, f'{name}: anchor must be unique'
                result = run(source.replace(old, new), directory)
                rejected = result.returncode != 0
                if not rejected:
                    try:
                        check_trace(result.stdout)
                    except AssertionError:
                        rejected = True
                assert rejected, f'{name}: survived runtime'
                print(f'RED: {name} (compiled, runtime rc={result.returncode})')


if __name__ == '__main__':
    main()
