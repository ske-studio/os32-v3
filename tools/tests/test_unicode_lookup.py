"""T3d0: all BMP against real lib/utf8.c; mutations run only with --mutate.

The flat LE oracle uses the same expansion as tools/gen_unicode.c. It is
constructed from the original header, never from a mutated candidate table.
Image measurements (T3d0) are recorded in ~/os32-tmp/evidence/2026-10-17/t3d0_measure/.
"""
import argparse
import pathlib
import re
import subprocess
import tempfile

import host32

ROOT = pathlib.Path(__file__).resolve().parents[2]
CANDIDATE = 'tools/tests/unicode_lookup_candidate.c'
TABLE = 'lib/unicode_jis_table.h'
SDK = 'tools/tests/unicode_lookup_sdk.c'
FLAGS = ['-m32', '-march=i386', '-std=gnu11', '-O2', '-ffreestanding',
         '-fno-pie', '-fno-stack-protector', '-Wall', '-Wextra', '-Werror']

# Every mutant must compile, then fail at the named runtime assertion.
MUTANTS = [
    ('order', TABLE, '{0x00A7,0x2178},{0x00A8,0x212F}',
     '{0x00A8,0x212F},{0x00A7,0x2178}', 'FAIL table-order'),
    ('duplicate', TABLE, '{0x00A8,0x212F}', '{0x00A7,0x212F}', 'FAIL table-order'),
    ('first-endpoint', CANDIDATE, 'u32 lo = 0, hi = UNICODE_JIS_TABLE_SIZE;',
     'u32 lo = 1, hi = UNICODE_JIS_TABLE_SIZE;', 'FAIL first direct'),
    ('last-endpoint', CANDIDATE, 'hi = UNICODE_JIS_TABLE_SIZE;',
     'hi = UNICODE_JIS_TABLE_SIZE - 1;', 'FAIL last direct'),
    ('missing-value', CANDIDATE, '    return 0;\n}',
     '    return 0x2121;\n}', 'FAIL missing direct'),
    ('outside-value', CANDIDATE, 'if (cp > 0xFFFF) return 0;',
     'if (cp > 0xFFFF) return 0x2121;', 'FAIL outside direct'),
    ('kana-arithmetic', CANDIDATE, '0x2421 + (cp - 0x3041)',
     '0x2422 + (cp - 0x3041)', 'FAIL kana direct'),
    ('sdk-bypass', SDK, 'return unicode_mock_api->unicode_to_jis(cp);',
     'extern u16 t3d0_unicode_to_jis(u32); return t3d0_unicode_to_jis(cp);',
     'FAIL sdk-kapi'),
]


def entries(text):
    return [tuple(int(x, 16) for x in m) for m in
            re.findall(r'\{0x([0-9A-Fa-f]{4}),0x([0-9A-Fa-f]{4})\}', text)]


def run_case(runner, mutation=None):
    paths = [CANDIDATE, TABLE, SDK, 'tools/tests/unicode_lookup_mock.h',
             'tools/tests/unicode_lookup_host.c']
    texts = {p: (ROOT / p).read_text() for p in paths}
    pairs = entries(texts[TABLE])
    assert len(pairs) == 7063
    flat = bytearray(65536 * 2)
    for cp, jis in pairs:
        flat[cp * 2:cp * 2 + 2] = jis.to_bytes(2, 'little')
    if mutation:
        _, path, old, new, _ = mutation
        assert texts[path].count(old) == 1, mutation[0]
        texts[path] = texts[path].replace(old, new)
    with tempfile.TemporaryDirectory(prefix='os32-unicode-lookup-') as tmp:
        tmp = pathlib.Path(tmp)
        for path, body in texts.items():
            (tmp / pathlib.Path(path).name).write_text(body)
        with (tmp / 'unicode_lookup_candidate.c').open('a') as f:
            f.write('''
/* Host-only introspection; no pointer is returned to the SDK. */
int t3d0_table_order(void) {
    for (u32 i = 1; i < UNICODE_JIS_TABLE_SIZE; i++)
        if (unicode_jis_table[i-1].unicode >= unicode_jis_table[i].unicode) return 0;
    return 1;
}
u32 t3d0_table_bytes(void) { return sizeof(unicode_jis_table); }
''')
        (tmp / 'unicode_lookup_oracle.h').write_text(
            'static const u8 oracle_flat[] = {' + ','.join(map(str, flat)) + '};\n' +
            'static const u16 oracle_cp[] = {' + ','.join(str(cp) for cp, _ in pairs) + '};\n')
        includes = ['-I' + str(p) for p in
                    (tmp, ROOT / 'include', ROOT / 'lib', ROOT / 'sdk/include/os32')]
        objects = []
        for name in ('unicode_lookup_candidate', 'unicode_lookup_sdk', 'unicode_lookup_host', 'utf8'):
            source = ROOT / 'lib/utf8.c' if name == 'utf8' else tmp / (name + '.c')
            obj = tmp / (name + '.o')
            subprocess.run(['gcc', *FLAGS, *includes, '-c', str(source), '-o', str(obj)], check=True)
            objects.append(str(obj))
        # One private read-only 28,252-byte pair table in the kernel object.
        nm = subprocess.check_output(['nm', '-S', objects[0]], text=True)
        tables = [line.split() for line in nm.splitlines() if line.endswith(' unicode_jis_table')]
        assert len(tables) == 1 and tables[0][2] == 'r' and int(tables[0][1], 16) == 28252, nm
        sdk_nm = subprocess.check_output(['nm', '-u', objects[1]], text=True)
        if not mutation or mutation[0] != 'sdk-bypass':
            assert sdk_nm.split() == ['U', 'unicode_mock_api'], sdk_nm
        exe = tmp / 'fixture'
        subprocess.run(['gcc', '-m32', '-nostdlib', '-static', '-no-pie',
                        '-Wl,-z,noexecstack', *objects, '-o', str(exe)], check=True)
        return host32.run([str(exe)], runner=runner, capture_output=True, text=True, timeout=120)


@host32.control_session
def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--runner', choices=['native', 'qemu'], default='qemu')
    p.add_argument('--mutate', action='store_true')
    a = p.parse_args()
    host32.begin_control(a.mutate, a.runner, ROOT)
    with host32.control(a.mutate, a.runner, ROOT):
        result = run_case(a.runner)
        print(result.stdout + result.stderr, end='')
        assert result.returncode == 0, result.returncode
    if a.mutate:
        for m in MUTANTS:
            result = run_case(a.runner, m)
            assert result.returncode == 1 and m[4] in result.stdout, (m[0], result)
            print('RED ' + m[0] + ': ' + m[4])
        print(f'PASS {len(MUTANTS)}/{len(MUTANTS)} runtime mutants')


if __name__ == '__main__':
    main()
