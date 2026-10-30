"""T3d1b: generated pair file + product loader/lookup through actual SDK/KAPI.

The generator's --pairs output is read into the fake VFS input. The independent
flat oracle still comes from the original header. Product sources contain no
static pair table. Runtime mutants are optional (--mutate, PM only).
"""
import argparse
import pathlib
import re
import subprocess
import tempfile

import host32

ROOT = pathlib.Path(__file__).resolve().parents[2]
PRODUCT = 'kernel/console/unicode_file.c'
TABLE = 'lib/unicode_jis_table.h'
SDK = 'tools/tests/unicode_lookup_sdk.c'
FLAGS = ['-m32', '-march=i386', '-std=gnu11', '-O2', '-ffreestanding',
         '-fno-pie', '-fno-stack-protector', '-Wall', '-Wextra', '-Werror']

# Every mutant must compile, then fail at the named runtime assertion.
MUTANTS = [
    ('order', PRODUCT,
     'le16_rd(pairs + (i - 1) * UNICODE_FILE_PAIR_BYTES) >=\n'
     '            le16_rd(pairs + i * UNICODE_FILE_PAIR_BYTES)',
     '0', 'FAIL load-rejected'),
    ('duplicate', PRODUCT, 'UNICODE_FILE_PAIR_BYTES) >=',
     'UNICODE_FILE_PAIR_BYTES) >', 'FAIL load-rejected'),
    ('stat-size', PRODUCT, 'st.st_size != UNICODE_FILE_BYTES',
     'st.st_size < UNICODE_FILE_BYTES', 'FAIL load-rejected'),
    ('read-size', PRODUCT, 'rc != UNICODE_FILE_BYTES',
     'rc < 0', 'FAIL diagnostic-reason'),
    ('missing-reason', PRODUCT, '? "missing" : "io"',
     '? "io" : "io"', 'FAIL diagnostic-reason'),
    ('page-return', PRODUCT,
     'pgalloc_free_n_owner(LEDGER_OWNER_KERNEL, phys / MEM_PAGE_SIZE,\n'
     '                             UNICODE_FILE_PAGES);',
     '(void)phys;', 'FAIL failed-pages'),
    ('free-owner', PRODUCT, 'pgalloc_free_n_owner(LEDGER_OWNER_KERNEL,',
     'pgalloc_free_n_owner(LEDGER_OWNER_KERNEL + 1,', 'FAIL free-owner-pages'),
    ('early-publication', PRODUCT, '    for (u32 i = 1;',
     '    unicode_pairs = pairs;\n    for (u32 i = 1;', 'FAIL free-publication'),
    ('first-endpoint', PRODUCT, 'u32 lo = 0, hi = UNICODE_FILE_PAIRS;',
     'u32 lo = 1, hi = UNICODE_FILE_PAIRS;', 'FAIL first direct'),
    ('last-endpoint', PRODUCT, 'hi = UNICODE_FILE_PAIRS;',
     'hi = UNICODE_FILE_PAIRS - 1;', 'FAIL last direct'),
    ('missing-value', PRODUCT, '    return 0;\n}',
     '    return 0x2121;\n}', 'FAIL missing direct'),
    ('outside-value', PRODUCT, 'if (cp > 0xFFFF) return 0;',
     'if (cp > 0xFFFF) return 0x2121;', 'FAIL fallback-absent'),
    ('kana-arithmetic', PRODUCT, 'JIS_HIRAGANA_BASE + (cp - UCS_HIRAGANA_FIRST)',
     'JIS_HIRAGANA_BASE + 1 + (cp - UCS_HIRAGANA_FIRST)', 'FAIL fallback-kana'),
    ('sdk-bypass', 'lib/utf8.c', 'return kapi->unicode_to_jis(cp);',
     'extern u16 unicode_file_lookup(u32); return unicode_file_lookup(cp);',
     'FAIL sdk-kapi'),
]


def entries(text):
    return [tuple(int(x, 16) for x in m) for m in
            re.findall(r'\{0x([0-9A-Fa-f]{4}),0x([0-9A-Fa-f]{4})\}', text)]


def byte_array(name, data):
    return 'static const u8 ' + name + '[] = {' + ','.join(map(str, data)) + '};\n'


def product_build_rules():
    """Execute the product rules with an existing old artifact and newer inputs."""
    text = (ROOT / 'build/programs.mk').read_text()
    rules = text[text.index('unicode_bin:'):text.index('\nfep_dic:')]
    dependencies = re.search(r'^tools/gen_unicode: (.+)$', rules, re.M).group(1).split()
    with tempfile.TemporaryDirectory(prefix='os32-unicode-build-') as folder:
        work = pathlib.Path(folder)
        for path in dependencies:
            dest = work / path
            dest.parent.mkdir(parents=True, exist_ok=True)
            dest.write_bytes((ROOT / path).read_bytes())
        out = work / 'out/unicode.bin'
        out.parent.mkdir()
        out.write_bytes(bytes(131072))
        (work / 'Makefile').write_text('BUILD_OUT := out\n' + rules)
        def make(*args):
            return subprocess.run(['make', *args, 'unicode_bin'], cwd=work,
                                  capture_output=True, text=True, check=True)
        make()
        expected = out.read_bytes()
        assert len(expected) == 28252, 'existing old product artifact must regenerate'
        make('-q')  # Up to date: do not recreate it on every build.
        for dependency in ('tools/gen_unicode.c', 'lib/unicode_jis_table.h',
                           'kernel/console/unicode_file.h'):
            out.write_bytes(bytes(131072))
            result = make('-W', dependency)
            assert './tools/gen_unicode --pairs out/unicode.bin' in result.stdout
            assert out.read_bytes() == expected, dependency + ' did not regenerate'
    deploy = (ROOT / 'build/deploy.mk').read_text()
    prereqs = re.search(r'^deploy-kernel: (.+)$', deploy, re.M).group(1).split()
    assert 'unicode_bin' in prereqs, 'kernel deployment must build the pair file'
    print('PASS product regeneration (existing/source/table/format) and deploy dependency')


def run_case(runner, mutation=None):
    paths = [PRODUCT, 'kernel/console/unicode_file.h', SDK,
             'lib/utf8.c', 'lib/utf8.h', 'tools/tests/unicode_lookup_host.c']
    texts = {p: (ROOT / p).read_text() for p in paths}
    pairs = entries((ROOT / TABLE).read_text())
    assert len(pairs) == 7063
    assert all(pairs[i - 1][0] < pairs[i][0] for i in range(1, len(pairs)))
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
        generator = tmp / 'gen_unicode'
        subprocess.run(['gcc', '-std=gnu11', '-O2', '-Wall', '-Wextra', '-Werror',
                        '-I' + str(ROOT), '-I' + str(ROOT / 'include'),
                        str(ROOT / 'tools/gen_unicode.c'), '-o', str(generator)], check=True)
        # Separate temporary artifacts: never overwrite build/out/unicode.bin.
        pair_file = tmp / 'unicode-pairs.bin'
        subprocess.run([str(generator), '--pairs', str(pair_file)], check=True)
        data = pair_file.read_bytes()
        assert len(data) == 28252
        expected = b''.join(cp.to_bytes(2, 'little') + jis.to_bytes(2, 'little')
                            for cp, jis in pairs)
        assert data == expected, 'generator LE pair format'
        # The no-argument product generator uses the same pair format.
        subprocess.run([str(generator)], cwd=tmp, check=True, capture_output=True)
        assert (tmp / 'unicode.bin').read_bytes() == data, 'product pair format'
        (tmp / 'unicode_lookup_oracle.h').write_text(
            byte_array('oracle_flat', flat) + byte_array('generated_pairs', data) +
            'static const u16 oracle_cp[] = {' + ','.join(str(cp) for cp, _ in pairs) + '};\n')
        includes = ['-I' + str(p) for p in
                    (tmp, ROOT / 'include', ROOT / 'lib', ROOT / 'kernel',
                     ROOT / 'fs', ROOT / 'sdk/include/os32')]
        (tmp / 'utf8_source.inc').write_text(texts['lib/utf8.c'])
        kernel_utf8 = re.sub(r'\b(utf8_\w+|unicode_to_\w+)(?=\s*\()',
                             lambda match: 'kernel_' + match[0], texts['lib/utf8.c'])
        (tmp / 'kernel_utf8.h').write_text(re.sub(
            r'\b(utf8_\w+|unicode_to_\w+)(?=\s*\()',
            lambda match: 'kernel_' + match[0], texts['lib/utf8.h']))
        kernel_utf8 = kernel_utf8.replace('#include "utf8.h"', '#include "kernel_utf8.h"')
        kernel_utf8 = kernel_utf8.replace('#include "../kernel/console/unicode_file.h"',
                                          '#include "unicode_file.h"')
        (tmp / 'unicode_kernel.c').write_text('#define __KERNEL_BUILD__ 1\n' + kernel_utf8)
        generated = (ROOT / 'kapi/kapi_generated.c').read_text()
        wrapper = re.search(r'u16 __cdecl wrap_unicode_to_jis\(u32 cp\)\n\{.*?\n\}',
                            generated, re.S).group(0)
        (tmp / 'unicode_kapi.c').write_text(
            '#include "unicode_file.h"\nextern u32 kapi_calls;\n'
            '#define KAPI_HIT(slot) (++kapi_calls)\n' + wrapper)
        objects = []
        for name in ('unicode_file', 'unicode_lookup_sdk', 'unicode_lookup_host', 'unicode_kapi', 'unicode_kernel'):
            source = tmp / (name + '.c')
            obj = tmp / (name + '.o')
            subprocess.run(['gcc', *FLAGS, *includes, '-c', str(source), '-o', str(obj)], check=True)
            objects.append(str(obj))
        # Only a private pointer in the product object, no embedded pair table.
        nm = subprocess.check_output(['nm', '-S', objects[0]], text=True)
        assert 'unicode_jis_table' not in nm, nm
        pointers = [line.split() for line in nm.splitlines() if line.endswith(' unicode_pairs')]
        assert len(pointers) == 1 and pointers[0][2] == 'b' and int(pointers[0][1], 16) == 4, nm
        sdk_nm = subprocess.check_output(['nm', '-u', objects[1]], text=True)
        if not mutation or mutation[0] != 'sdk-bypass':
            assert sdk_nm.split() == ['U', 'os32_kapi_v63'], sdk_nm
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
    product_build_rules()
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
