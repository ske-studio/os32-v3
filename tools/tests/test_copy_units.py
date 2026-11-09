"""H1: execute production x86 and PPC copy providers without host libc."""
import argparse
from pathlib import Path
import subprocess
import tempfile
import host32

CHECK = dict(name='copy-units-host', tier=1, needs=[], budget=10, covers=[
    'include/copy_units.h', 'arch/x86/arch_copy.h', 'arch/ppc/arch_copy.h',
    'tools/tests/host_arch/arch_copy.h', 'tools/tests/test_copy_units.py',
    'tools/tests/host32.py'])
ROOT = Path(__file__).resolve().parents[2]
PROBE = r'''
#include "copy_units.h"
static int probe(void) {
    unsigned char src[48], dst[48];
    unsigned counts[] = {0, 1, 3, 7};
    for (unsigned width = 2; width <= 4; width += 2)
    for (unsigned offset = 0; offset < 4; ++offset)
    for (unsigned n = 0; n < 4; ++n) {
        unsigned units = counts[n], start = 4 + offset;
        for (unsigned i = 0; i < 48; ++i) { src[i] = i * 17 + 3; dst[i] = 0xa5; }
        if (width == 2) arch_copy16(dst + start, src + start, units);
        else arch_copy32(dst + start, src + start, units);
        for (unsigned i = 0; i < 48; ++i) {
            unsigned char expected = i >= start && i < start + units * width ? src[i] : 0xa5;
            if (dst[i] != expected || src[i] != (unsigned char)(i * 17 + 3)) return 1;
        }
    }
    return 0;
}
void _start(void) {
    int code = probe();
    __asm__ volatile("int $0x80" : : "a"(1), "b"(code));
    for (;;) {}
}
'''


def observe(folder, provider):
    (folder / 'arch_copy.h').write_text(provider)
    (folder / 'probe.c').write_text(PROBE)
    exe = folder / 'probe'
    host32.build(['gcc', '-m32', '-O2', '-std=gnu11', '-Wall', '-Wextra', '-Werror',
                  '-ffreestanding', '-fno-pie', '-no-pie', '-fno-stack-protector',
                  '-nostdlib', '-static', '-I' + str(folder), '-I' + str(ROOT / 'include'),
                  str(folder / 'probe.c'), '-o', str(exe)], check=True)
    return host32.run([str(exe)]).returncode


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--mutate', action='store_true')
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix='copy-units-') as temp:
        folder = Path(temp)
        for arch in ('x86', 'ppc'):
            source = (ROOT / 'arch' / arch / 'arch_copy.h').read_text()
            assert observe(folder, source) == 0
            print('PASS ' + arch + ' units/unaligned/canaries/bytes')
            if args.mutate:
                for width in (16, 32):
                    if arch == 'x86':
                        old = '"rep movsw"' if width == 16 else '"rep movsl"'
                        new = '"rep movsb"'
                    else:
                        old = 'static inline void arch_copy' + str(width) + '(void *dst, const void *src, unsigned int units) {'
                        new = old + '\n    units /= ' + str(width // 8) + ';'
                    assert source.count(old) == 1
                    assert observe(folder, source.replace(old, new)) == 1
                    print('RED ' + arch + ' copy' + str(width) + ' units-as-bytes')
            assert observe(folder, source) == 0
        provider = '#include "' + str(ROOT / 'tools/tests/host_arch/arch_copy.h') + '"\n'
        assert observe(folder, provider) == 0
        print('PASS host_arch production C provider')


def run(ctx):
    return subprocess.call(['python3', '-B', str(Path(__file__).resolve()),
                            *(['--mutate'] if ctx.mutate else [])], cwd=ctx.root)


if __name__ == '__main__':
    main()
