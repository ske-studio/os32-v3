"""Six i386 division ABI helpers, checked against independent Python integers."""
CHECK = dict(name='divdi3-host', tier=1,
    covers=['lib/divdi3.c', 'tools/tests/divdi3_host.c',
            'tools/tests/host32.py', 'tools/tests/kernel_includes.py'],
    needs=[], budget=3.5)

import argparse
import os
from pathlib import Path
import random
import subprocess
import tempfile
import host32
from kernel_includes import include_flags

ROOT = Path(__file__).resolve().parents[2]
MUTATIONS = (
    ('omit quotient bit', 'quotient |= 1ULL << bit;', 'quotient |= 0ULL << bit;'),
    ('wrong signed remainder', 'if (value < 0) rest = 0ULL - rest;',
     'if (divisor < 0) rest = 0ULL - rest;'),
)


def vectors():
    rng = random.Random(0xD1D3)
    mask = (1 << 64) - 1
    # All word boundaries, small / equal / larger divisors, and wide quotients.
    boundaries = sorted({0, 1, 2, 3, mask, *(x + delta for bit in (31, 32, 33, 63)
                        for x in (1 << bit,) for delta in (-1, 0, 1))})
    unsigned = [(n, d) for n in boundaries for d in boundaries if d]
    unsigned += [(rng.getrandbits(64), rng.randrange(1, 1 << 64)) for _ in range(2048)]
    unsigned += [(rng.getrandbits(64), rng.randrange(1, 1 << 32)) for _ in range(2048)]
    # Signed divisions truncate toward zero; Python // by itself floors.
    signed_bounds = sorted({-(1 << 63), (1 << 63) - 1,
                            *(sign * x for x in boundaries if x < 1 << 63
                              for sign in (-1, 1))})
    signed = [(n, d) for n in signed_bounds for d in signed_bounds if d]
    signed += [(rng.randrange(-(1 << 63), 1 << 63),
                rng.randrange(-(1 << 63), 1 << 63) or 1) for _ in range(2048)]
    text = []
    for name, cases in [('unsigned', unsigned), ('signed', signed)]:
        text.append(f'static const struct {{ u64 n, d, q, r; }} {name}_cases[] = {{')
        for n, d in cases:
            if name == 'signed' and n == -(1 << 63) and d == -1:
                continue  # C signed overflow is undefined, as is division by 0.
            q = abs(n) // abs(d)
            if (n < 0) != (d < 0):
                q = -q
            r = n - q * d
            text.append('{' + ', '.join(f'{x & mask}ULL' for x in (n, d, q, r)) + '},')
        text.append('};')
    return '\n'.join(text)


def execute(mutation=None):
    with tempfile.TemporaryDirectory(prefix='divdi3-') as td:
        work = Path(td)
        source = (ROOT / 'lib/divdi3.c').read_text()
        if mutation:
            assert source.count(mutation[1]) == 1, mutation[0]
            source = source.replace(mutation[1], mutation[2])
        (work / 'divdi3.c').write_text(source)
        (work / 'divdi3_vectors.h').write_text(vectors())
        cc = str(Path(os.environ['CROSS_DIR']) / 'bin/i386-elf-gcc')
        nm = str(Path(os.environ['CROSS_DIR']) / 'bin/i386-elf-nm')
        flags = ['-std=gnu11', '-march=i386', '-O2', '-Wall', '-Wextra', '-Werror',
                 '-ffreestanding', '-fno-pie', '-fno-stack-protector', *include_flags(ROOT)]
        obj = work / 'divdi3.o'
        subprocess.run([cc, *flags, '-c', str(work / 'divdi3.c'), '-o', str(obj)], check=True)
        # Recursion into compiler runtime helpers would defeat this implementation.
        undefined = subprocess.check_output([nm, '-u', str(obj)], text=True)
        assert not undefined.strip(), undefined
        exe = work / 'division'
        subprocess.run([cc, *flags, '-nostdlib', '-I' + str(work),
                        str(ROOT / 'tools/tests/divdi3_host.c'), str(obj),
                        '-o', str(exe)], check=True)
        return host32.run([str(exe)], runner='qemu').returncode


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--mutate', action='store_true')
    args = parser.parse_args()
    assert execute() == 0
    print('GREEN six division helpers: boundaries, signs, wide quotients, deterministic random pairs', flush=True)
    if args.mutate:
        for mutation in MUTATIONS:
            rc = execute(mutation)
            assert rc > 0, (mutation[0], rc)
            print(f'RED {mutation[0]}: runtime rc={rc}', flush=True)


def run(ctx):
    return subprocess.call(['python3', '-B', 'tools/tests/test_divdi3.py',
                            *(['--mutate'] if ctx.mutate else [])], cwd=ctx.root)


if __name__ == '__main__':
    main()
