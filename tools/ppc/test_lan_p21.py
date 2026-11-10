#!/usr/bin/env python3
"""PLN1 codec golden vectors and runtime mutation checks."""
import pathlib
import subprocess
import sys
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools/tests'))
import host32

CHECK = {'id': 'ppc-lan-p21', 'covers': ['platform/powermac/lan_wire.c'],
         'inputs': ['tools/ppc/lan_p21_fixture.c', 'platform/powermac/lan_boot.h',
                    'include/types.h', 'lib/kstring.h']}
SOURCE = ROOT / 'platform/powermac/lan_wire.c'
MUTATIONS = [
    ('seq-le', 'put_be(dst + PLN1_OFF_SEQ, m->seq, 8);', 'for (i = 0; i < 8; ++i) dst[PLN1_OFF_SEQ + i] = (u8)(m->seq >> (8 * i));', 2),
    ('header', 'get_be(src + PLN1_OFF_HEADER_BYTES, 2) != PLN1_HEADER_BYTES ||', '0 ||', 19),
    ('limit', 'len > PLN1_PAYLOAD_MAX || len > bytes - PLN1_HEADER_BYTES)', 'len > bytes - PLN1_HEADER_BYTES)', 21),
    ('reserved', 'get_be(src + PLN1_OFF_RESERVED, 4) != 0 ||', '0 ||', 20),
    ('clear', 'memset(m, 0, sizeof(*m));', '(void)m;', 17),
    ('nonce', 'if (m->host_nonce[i]) return LAN_EINVAL;', 'if (0) return LAN_EINVAL;', 11),
]


def exercise(source, directory):
    path = pathlib.Path(directory)
    (path / 'wire.c').write_text(source)
    binary = path / 'fixture'
    subprocess.run(['gcc', '-Wall', '-Wextra', '-Werror', '-m32', '-ffreestanding', '-fno-pie', '-no-pie',
                    '-nostdlib', '-static', '-fno-builtin', '-std=gnu11',
                    '-I', str(ROOT), '-I', str(ROOT / 'include'),
                    '-I', str(ROOT / 'platform/powermac'), str(path / 'wire.c'),
                    str(ROOT / 'tools/ppc/lan_p21_fixture.c'), '-o', str(binary)],
                   check=True, timeout=60)
    return subprocess.run(host32.command([str(binary)]), capture_output=True,
                          text=True, timeout=60)


def main():
    source = SOURCE.read_text()
    with tempfile.TemporaryDirectory() as temp:
        result = exercise(source, temp)
        if result.returncode != 0 or result.stdout != 'REACHED\nPASS\n':
            raise RuntimeError(f'baseline: {result.returncode} {result.stdout} {result.stderr}')
        print('baseline PASS')
        if '--mutate' in sys.argv:
            for name, old, new, expected in MUTATIONS:
                if source.count(old) != 1:
                    raise RuntimeError(f'{name}: anchor count {source.count(old)}')
                result = exercise(source.replace(old, new), temp)
                if result.returncode != expected or result.stdout != f'REACHED\nFAIL {expected:02d}\n':
                    raise RuntimeError(f'{name}: unexpected {result.returncode} {result.stdout} {result.stderr}')
                print(f'{name} RED (rc={expected}, REACHED, FAIL {expected:02d})')


if __name__ == '__main__':
    main()
