#!/usr/bin/env python3
"""Compare the freestanding shell string implementation with host libc."""
import argparse
import ctypes as c
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / 'userland/ppc/sh/string.c'
NAMES = 'strlen strcmp strncmp strncpy strcat strncat memcpy memmove memset'.split()


def check(source, directory):
    lib = directory / 'string.so'
    flags = ['-D' + name + '=sh_' + name for name in NAMES]
    subprocess.run(['gcc', '-Wall', '-Wextra', '-Werror', '-fno-builtin', '-shared', '-fPIC',
                    '-I' + str(ROOT / 'userland/ppc/sh/include'), *flags,
                    str(source), '-o', str(lib)], check=True)
    actual, expected = c.CDLL(str(lib)), c.CDLL(None)
    for name in NAMES:
        a, b = getattr(actual, 'sh_' + name), getattr(expected, name)
        if name == 'strlen':
            a.restype = b.restype = c.c_size_t
            for s in (b'', b'abc', b'\xffx'):
                assert a(s) == b(s), name
        elif name in ('strcmp', 'strncmp'):
            for x, y in ((b'', b'a'), (b'ab', b'ab'), (b'\xff', b'\x01'), (b'abc', b'abd')):
                for n in range(5):
                    args = (x, y, n) if name == 'strncmp' else (x, y)
                    av, bv = a(*args), b(*args)
                    assert (av > 0) - (av < 0) == (bv > 0) - (bv < 0), name
        else:
            a.restype = b.restype = c.c_void_p
            for n in range(9):
                x, y = c.create_string_buffer(b'abcde', 32), c.create_string_buffer(b'abcde', 32)
                if name == 'memmove':
                    for dest, src in ((1, 0), (0, 1), (0, 0)):
                        x.raw = y.raw = b'abcdefgh' + bytes(24)
                        ra = a(c.byref(x, dest), c.byref(x, src), n)
                        rb = b(c.byref(y, dest), c.byref(y, src), n)
                        assert ra == c.addressof(x) + dest and rb == c.addressof(y) + dest
                        assert x.raw == y.raw, name
                    continue
                if name in ('strncpy', 'strncat', 'memcpy'):
                    args_a, args_b = (x, b'xy\0zzzzz', n), (y, b'xy\0zzzzz', n)
                elif name == 'memset':
                    args_a, args_b = (x, 0xe1, n), (y, 0xe1, n)
                else:
                    args_a, args_b = (x, b'xy'), (y, b'xy')
                assert a(*args_a) == c.addressof(x) and b(*args_b) == c.addressof(y)
                assert x.raw == y.raw, name


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--mutate', action='store_true')
    args = parser.parse_args()
    with tempfile.TemporaryDirectory() as tmp:
        directory = Path(tmp)
        check(SOURCE, directory)
        if args.mutate:
            text = SOURCE.read_text()
            anchor = 'if (d > s) {'
            assert text.count(anchor) == 1
            mutant = directory / 'mutant.c'
            mutant.write_text(text.replace(anchor, 'if (d < s) {'))
            # A distinct shared object prevents the loader reusing the original.
            sub = directory / 'mutant'
            sub.mkdir()
            try:
                check(mutant, sub)
            except AssertionError as exc:
                assert str(exc) == 'memmove'
                print('memmove direction: RED')
            else:
                raise AssertionError('memmove mutant survived')
    print('ppc shell string: OK')


if __name__ == '__main__':
    main()
