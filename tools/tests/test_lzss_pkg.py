#!/usr/bin/env python3
"""Deterministic PKG round trips through Python and the real ILP32 guest C.

Seed 250 reproduces the supplied lzss_fail_seed.bin exactly (SHA-256 pinned).
The regression used to corrupt /b[0] in a match starting at A[17988], distance
12, length 13. Also exercise overlap periods, ring wrap, F=18 and flag tails.
"""
import argparse
import hashlib
import pathlib
import random
import sys
import tempfile
import time

import host32

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools'))
import mkpkg

PREFIX = bytes((i * 7) & 255 for i in range(9000)) + b'abc' * 3000
FAIL_SHA256 = 'a53ec4b84d5b2d20df6bcd93cd8e8009585378a11a16a8b7b95cb351d7c6a078'
FIX = '''                    c = (text_buf[(chain_pos + l) % N] if l < dist
                         else in_data[src_p + l - dist])'''
OLD = '                    c = text_buf[(chain_pos + l) % N]'


def random_bytes(seed):
    rng = random.Random(seed)
    return bytes(rng.getrandbits(8) for _ in range(5000))


def package(tmp, name, a, b, encoder=None):
    directory = tmp / name
    (directory / 'x').mkdir(parents=True)
    (directory / 'x/a').write_bytes(a)
    (directory / 'b').write_bytes(b)
    # A private module namespace for mutants; never alter the source tree.
    build = mkpkg.build_pkg if encoder is None else encoder
    blob = build('rt', 1, [('/x/a', directory / 'x/a'),
                         ('/b', directory / 'b')], True, 0)
    path = directory / 'RT.PKG'
    path.write_bytes(blob)
    _, entries = mkpkg.read_pkg(path)
    got = {g: d for g, t, d in entries if t == mkpkg.PKG_TYPE_FILE}
    return [str(path), str(directory)], got == {'/x/a': a, '/b': b}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--runner', choices=['native', 'qemu'], default='qemu')
    parser.add_argument('--mutate', action='store_true')
    args = parser.parse_args()
    start = time.monotonic()
    assert hashlib.sha256(random_bytes(250)).hexdigest() == FAIL_SHA256
    with tempfile.TemporaryDirectory(prefix='lzss-pkg-') as tmp:
        tmp = pathlib.Path(tmp)
        exe = tmp / 'guest'
        (tmp / 'string.h').write_text('/* libc primitives supplied by harness */\n')
        host32.build(['gcc', '-m32', '-std=gnu11', '-O2', '-Wall', '-Wextra',
                      '-Werror', '-D__cdecl=', '-ffreestanding', '-fno-pie',
                      '-fno-stack-protector', '-nostdlib', '-static', '-no-pie',
                      '-I' + str(tmp), '-I' + str(ROOT),
                      '-I' + str(ROOT / 'sdk/include/os32'),
                      str(ROOT / 'tools/tests/lzss_pkg_host.c'), '-o', str(exe)],
                     check=True)
        command = [str(exe)]
        for seed in range(300):
            pair, ok = package(tmp, f'seed{seed}', PREFIX, random_bytes(seed))
            assert ok, f'Python round trip seed={seed}'
            command.extend(pair)
        # Distances 1..18, multiple wraps, last flag groups of every length.
        for period in range(1, 19):
            data = bytes(range(period)) * (2 * mkpkg.N // period + 1)
            for tail in range(8):
                pair, ok = package(tmp, f'p{period}t{tail}', data,
                                   b' ' * 19 + bytes(range(240, 240 + tail)))
                assert ok, f'Python round trip period={period} tail={tail}'
                command.extend(pair)
        host32.run(command, runner=args.runner, check=True, timeout=30)
        print(f'PASS Python + guest: 300 seeds + 144 boundaries '
              f'({time.monotonic() - start:.2f}s)', flush=True)
        if args.mutate:
            source = (ROOT / 'tools/mkpkg.py').read_text()
            assert source.count(FIX) == 1, 'mutation anchor drift'
            namespace = {'__name__': 'mkpkg_mutant'}
            exec(compile(source.replace(FIX, OLD), 'mkpkg_mutant', 'exec'), namespace)
            pair, ok = package(tmp, 'mutant', PREFIX, random_bytes(250),
                               namespace['build_pkg'])
            assert not ok, 'old encoder survived Python regression'
            result = host32.run([str(exe), *pair], runner=args.runner,
                                capture_output=True, text=True, timeout=30)
            assert result.returncode == 1 and 'want[j] == bytes[off + j]' in result.stderr, result
            print('RED old encoder: both Python and guest detect seed250 corruption')
    return 0


if __name__ == '__main__':
    sys.exit(main())
