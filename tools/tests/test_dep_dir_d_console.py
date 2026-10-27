"""L16a: immutable 256-byte pre-move fixture, wrapper and isolated closure."""
import argparse
import hashlib
import json
import re
from pathlib import Path
import shutil
import subprocess
import tempfile
import host32

ROOT = Path(__file__).resolve().parents[2]
FIXTURE = ROOT / 'tools/tests/fixtures/dep_dir_d'
BACKEND = 'platform/pc98/console_attr.c'
HEADER = 'include/console_attr.h'
HARNESS = 'tools/tests/dep_dir_d_console_host.c'
FILES = (BACKEND, HEADER, HARNESS, 'lib/kprintf_attr.c', 'include/types.h',
         'include/pc98.h', 'include/kprintf.h', 'sdk/include/os32/os32_kapi_shared.h',
         'sdk/include/os32/os32_ver_info.h', 'sdk/include/os32/os32_generations.h',
         'sdk/include/os32/os32_surface.h', 'sdk/include/os32/os32_ls.h',
         'sdk/include/os32/os32_kapi_generated.h')
OLD, NEW = 'attr | TATTR_VISIBLE', 'attr | 0'


def kernel_list():
    # The broad selector owns build-rule changes, without a build/*.mk input.
    result = subprocess.run(['make', '-qp'], cwd=ROOT, capture_output=True, text=True)
    assert result.returncode in (0, 1), result.stderr
    return result.stdout


def once(text, old, new):
    assert text.count(old) == 1, old
    result = text.replace(old, new, 1)
    assert result.count(old) == 0 and result.count(new) == 1
    return result


def build(work, source, omit_header=False):
    for rel in FILES:
        dest = work / rel
        dest.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(ROOT / rel, dest)
    (work / BACKEND).write_text(source)
    if omit_header:
        (work / HEADER).unlink()
    exe = work / 'console-attr'
    subprocess.run(['gcc', '-m32', '-march=i386', '-std=gnu11', '-Wall', '-Wextra',
                    '-Werror', '-ffreestanding', '-fno-pie', '-fno-stack-protector',
                    '-nostdlib', '-static', '-no-pie', '-I' + str(work / 'include'),
                    str(work / HARNESS), str(work / 'lib/kprintf_attr.c'),
                    str(work / BACKEND), '-o', str(exe)], check=True)
    return exe


def trace(exe):
    result = host32.run([str(exe)], runner='qemu', capture_output=True)
    assert result.returncode == 0, result
    return result.stdout


def resident_check(text):
    block = re.search(r'^C_KERNEL = (.*)$', text, re.M).group(1)
    assert block.split().count(BACKEND) == 1


def verify():
    expected = (FIXTURE / 'console_attr.trace').read_bytes()
    manifest = json.loads((FIXTURE / 'console_attr.json').read_text())
    assert len(expected) == 256
    assert hashlib.sha256(expected).hexdigest() == manifest['trace_sha256']
    assert hashlib.sha256((ROOT / HARNESS).read_bytes()).hexdigest() == manifest['harness_sha256']
    resident_check(kernel_list())
    source = (ROOT / BACKEND).read_text()
    once(source, OLD, NEW)
    with tempfile.TemporaryDirectory(prefix='dep-dir-d-console-') as td:
        assert trace(build(Path(td), source)) == expected
    print('PASS L16a all 256 fixed values, wrapper, isolated headers, resident membership')
    return source, expected


def mutation_build(source, expected, execute=False):
    with tempfile.TemporaryDirectory(prefix='dep-dir-d-console-mut-') as td:
        exe = build(Path(td), once(source, OLD, NEW))
        if execute:
            assert trace(exe) != expected
    print('PASS -Werror visible-OR mutation preparation')


def mutations(source, expected):
    mutation_build(source, expected, execute=True)
    text = kernel_list()
    omitted = re.sub(r'^C_KERNEL = (.*)$',
                     lambda m: once(m[0], BACKEND + ' ', ''), text, flags=re.M)
    try:
        resident_check(omitted)
    except AssertionError:
        pass
    else:
        raise AssertionError('resident omission escaped')
    with tempfile.TemporaryDirectory(prefix='dep-dir-d-console-header-') as td:
        try:
            build(Path(td), source, omit_header=True)
        except subprocess.CalledProcessError:
            print('PASS isolated header omission rejected (not runtime kill)')
        else:
            raise AssertionError('original header leaked into copy')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--mutate', action='store_true')
    parser.add_argument('--compile-mutations', action='store_true')
    args = parser.parse_args()
    source, expected = verify()
    if args.compile_mutations:
        mutation_build(source, expected)
    if args.mutate:
        mutations(source, expected)


if __name__ == '__main__':
    main()
