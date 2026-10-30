"""L18 immutable pre-move MMIO traces; verification needs no git.

Only individual volatile stores are instrumented; formatting/loops remain
product code. Both positive and mutation builds use an isolated header tree.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import shutil
import subprocess
import tempfile
import host32

ROOT = Path(__file__).resolve().parents[2]
FIXTURE = ROOT / 'tools/tests/fixtures/dep_dir_d'
SOURCES = ('init/boot_display.c', 'arch/x86/kernel/isr_handlers.c',
           'arch/x86/kernel/double_fault.c', 'kernel/v86/v86_mem.c', 'platform/pc98/emergency.c')
HEADER_DIRS = ('kernel', 'platform/pc98', 'include', 'arch/x86', 'lib', 'sdk/include/os32')
HARNESS = 'tools/tests/dep_dir_d_emergency_host.c'
MUTATIONS = (
    ('boot-width', 'platform/pc98/emergency.c',
     '*(volatile u8 *)P2V_IO(TVRAM_ATTR + offset) = color;',
     '*(volatile u16 *)P2V_IO(TVRAM_ATTR + offset) = color;'),
    ('fault-width', 'platform/pc98/emergency.c',
     '((volatile u16 *)P2V_IO(TVRAM_ATTR))[pos] = (u16)attr;',
     '((volatile u8 *)P2V_IO(TVRAM_ATTR))[pos * TVRAM_CELL_BYTES] = attr;'),
)


def function(text, name):
    match = re.search(r'^(?:static )?void ' + name + r'\([^)]*\)\n\{', text, re.M)
    assert match, name
    pos, depth = match.end(), 1
    while depth:
        depth += (text[pos] == '{') - (text[pos] == '}')
        pos += 1
    return text[match.start():pos] + '\n'


def kernel_list():
    # Query make without building, as check-build-no-deploy does. The broad
    # selector owns build-rule changes; no build/*.mk in this check's shard.
    result = subprocess.run(['make', '-qp'], cwd=ROOT, capture_output=True, text=True)
    assert result.returncode in (0, 1), result.stderr
    return result.stdout


def once(text, old, new):
    assert text.count(old) == 1, old
    result = text.replace(old, new, 1)
    assert result.count(old) == 0 and result.count(new) == 1
    return result


def instrument(text):
    # Width comes from the product lvalue, including width mutants.
    text = re.sub(r'\*\(volatile (u8|u16) \*\)P2V_IO\(([^\n;]+)\) = ([^;]+);',
                  lambda m: f'store({m[2]}, sizeof({m[1]}), {m[3]});', text)
    text = re.sub(r'\(\(volatile (u8|u16) \*\)P2V_IO\((TVRAM_BASE|TVRAM_ATTR)\)\)\[([^\]]+)\] = ([^;]+);',
                  lambda m: f'store({m[2]} + ({m[3]}) * sizeof({m[1]}), sizeof({m[1]}), {m[4]});', text)
    for name, address in (('TVRAM_CHAR', 'TVRAM_BASE'), ('TVRAM_ATRP', 'TVRAM_ATTR'),
                          ('text', 'TVRAM_BASE'), ('attr', 'TVRAM_ATTR')):
        text = re.sub(r'\b' + name + r'\[([^\]]+)\] = ([^;]+);',
                      lambda m: f'store({address} + ({m[1]}) * sizeof(u16), sizeof(u16), {m[2]});', text)
    text = re.sub(r'    volatile u16 \*(?:text|attr) = P2V_IO\(TVRAM_(?:BASE|ATTR)\);\n', '', text)
    assert not re.search(r'P2V_IO|TVRAM_CHAR\[|TVRAM_ATRP\[|text\[|attr\[', text)
    return text.replace('for (;;) _stop();', 'host_stop(); return;')


def pieces(parts):
    boot, isr, df, v86 = (parts[p] for p in SOURCES[:4])
    result = '#define EXC_HEX_PREFIX_COLS 2\n#define EXC_ATTR_LEGACY 0x07\n'
    result += '#define HEX32_DIGITS 8\n#define HEX_NIBBLE_BITS 4\n#define HEX_NIBBLE_MASK 15\n'
    result += '\n'.join(re.findall(r'enum \{.*?};', df + v86, re.S)) + '\n'
    if 'platform/pc98/emergency.c' in parts:
        result += re.sub(r'^#include[^\n]*\n', '', parts[SOURCES[4]], flags=re.M)
    result += function(boot, 'tvram_print')
    for name in ('tvram_puts_at', 'tvram_put_hex32'):
        result += function(isr, name)
    # hex_at has a char * return-free signature accepted by the shared slicer.
    result += function(df, 'hex_at') + function(df, 'double_fault_halt')
    start = v86.index('    if (v86_restore_mismatch) {')
    end = v86.index('    /* Guest permissions', start)
    result += 'static void v86_diagnostic(void) {\n' + v86[start:end] + '}\n'
    return instrument(result)


def build(work, parts, omit_header=False):
    for directory in HEADER_DIRS:
        for source in (ROOT / directory).glob('*.h'):
            dest = work / source.relative_to(ROOT)
            dest.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(source, dest)
    if omit_header:
        (work / 'include/platform_emergency.h').unlink()
    for rel, source in parts.items():
        dest = work / rel
        dest.parent.mkdir(parents=True, exist_ok=True)
        dest.write_text(source)
    (work / 'emergency_source.inc').write_text(pieces(parts))
    shutil.copyfile(ROOT / HARNESS, work / 'harness.c')
    exe = work / 'emergency'
    cmd = ['gcc', '-m32', '-march=i386', '-std=gnu11', '-Wall', '-Wextra', '-Werror',
           '-ffreestanding', '-fno-pie', '-fno-stack-protector', '-nostdlib', '-static', '-no-pie']
    cmd += ['-I' + str(work), *('-I' + str(work / d) for d in HEADER_DIRS)]
    subprocess.run([*cmd, str(work / 'harness.c'), '-o', str(exe)], check=True)
    return exe


def trace(exe):
    result = host32.run([str(exe)], runner='qemu', capture_output=True)
    assert result.returncode == 0, result
    return result.stdout


def resident_check(text):
    block = re.search(r'^C_KERNEL = (.*)$', text, re.M).group(1)
    assert block.split().count('platform/pc98/emergency.c') == 1


def verify():
    parts = {p: (ROOT / p).read_text() for p in SOURCES}
    expected = (FIXTURE / 'emergency.trace').read_bytes()
    manifest = json.loads((FIXTURE / 'emergency.json').read_text())
    assert hashlib.sha256(expected).hexdigest() == manifest['trace_sha256']
    assert hashlib.sha256((ROOT / HARNESS).read_bytes()).hexdigest() == manifest['harness_sha256']
    resident_check(kernel_list())
    for _, rel, old, new in MUTATIONS:
        once(parts[rel], old, new)
    with tempfile.TemporaryDirectory(prefix='dep-dir-d-emergency-') as td:
        assert trace(build(Path(td), parts)) == expected
    print('PASS L18 fixed boot/exception/DF/V86 width/order/high-byte traces; resident membership')
    return parts, expected


def mutation_builds(parts, expected, execute=False):
    for name, rel, old, new in MUTATIONS:
        changed = dict(parts)
        changed[rel] = once(parts[rel], old, new)
        with tempfile.TemporaryDirectory(prefix='dep-dir-d-emergency-mut-') as td:
            exe = build(Path(td), changed)
            if execute:
                assert trace(exe) != expected, name
        print('PASS -Werror mutation preparation ' + name)


def mutations(parts, expected):
    mutation_builds(parts, expected, execute=True)
    text = kernel_list()
    try:
        resident_check(re.sub(r'^C_KERNEL = (.*)$',
                              lambda m: m[0].replace('platform/pc98/emergency.c ', '', 1),
                              text, count=1, flags=re.M))
    except AssertionError:
        pass
    else:
        raise AssertionError('resident omission escaped')
    with tempfile.TemporaryDirectory(prefix='dep-dir-d-header-') as td:
        try:
            build(Path(td), parts, omit_header=True)
        except subprocess.CalledProcessError:
            print('PASS isolated manifest header omission rejected (not runtime kill)')
        else:
            raise AssertionError('original header leaked into copy')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--mutate', action='store_true')
    parser.add_argument('--compile-mutations', action='store_true')
    args = parser.parse_args()
    parts, expected = verify()
    if args.compile_mutations:
        mutation_builds(parts, expected)
    if args.mutate:
        mutations(parts, expected)


if __name__ == '__main__':
    main()
