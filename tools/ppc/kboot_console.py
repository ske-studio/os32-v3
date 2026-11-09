#!/usr/bin/env python3
"""Product console/log/ESCC/kprintf; ILP32 runtime and PPC native-va_list link audit."""
import argparse
import os
from pathlib import Path
import subprocess
import tempfile

CHECK = dict(name='ppc-kboot-console', budget=5, needs=[], covers=[
    'platform/powermac/kboot_console.c', 'platform/powermac/log.c',
    'platform/powermac/kboot_log.h', 'platform/powermac/escc.c',
    'platform/powermac/ofw.h', 'platform/powermac/ppc_layout.h',
    'arch/ppc/ppc_boot.h', 'include/console.h', 'include/kprintf.h',
    'include/types.h', 'include/callconv.h', 'lib/kstring.h',
    'lib/kstring_c.c', 'lib/kprintf.c', 'sdk/include/os32/os32_kapi_shared.h',
    'sdk/include/os32/os32_kapi_generated.h',
    'sdk/include/os32/os32_ver_info.h', 'sdk/include/os32/os32_host_notice.h',
    'sdk/include/os32/os32_generations.h', 'sdk/include/os32/os32_surface.h',
    'sdk/include/os32/os32_ls.h',
    'tools/ppc/kboot_console.py', 'tools/ppc/kboot_console_host.c',
    'build/arch/ppc.mk'])
ROOT = Path(__file__).resolve().parents[2]
UNITS = ['platform/powermac/kboot_console.c', 'platform/powermac/log.c',
         'platform/powermac/escc.c', 'lib/kprintf.c', 'lib/kstring_c.c']
MUTATIONS = [
    ('write-length', 'pm_log_write(buf, size);', 'pm_log_write(buf, size + 1);', 41),
    ('string-end', 'kstrlen(str), color', 'kstrlen(str) + 1, color', 42),
    ('lf-expansion', 'pm_log_write(buf, size);',
     "for (u32 i = 0; i < size; i++) { if (buf[i] == '\\n') pm_log_write(\"\\r\", 1); pm_log_write(buf + i, 1); }", 43),
    ('screen-success', 'int console_get_cursor_x(void) { return -1; }',
     'int console_get_cursor_x(void) { return 0; }', 44),
]
INCLUDES = ['-I' + str(ROOT / p) for p in
            ('include', 'lib', 'arch/ppc', 'platform/powermac')]
FLAGS = ['-std=gnu11', '-O2', '-ffreestanding', '-fno-builtin', '-fno-pie',
         '-fno-stack-protector', '-Wall', '-Wextra', '-Werror']


def exercise(directory, name, before=None, after=None):
    source = (ROOT / UNITS[0]).read_text()
    if before is not None:
        assert source.count(before) == 1, 'mutation anchor must be unique'
        source = source.replace(before, after, 1)
    copy = directory / 'kboot_console.c'
    copy.write_text(source)
    binary = directory / name
    subprocess.run(['gcc', '-m32', *FLAGS, '-no-pie', '-nostdlib', '-static',
                    *INCLUDES, str(copy),
                    *(str(ROOT / p) for p in UNITS[1:]),
                    str(ROOT / 'tools/ppc/kboot_console_host.c'), '-o', str(binary)],
                   check=True, timeout=60)
    result = subprocess.run(['qemu-i386', str(binary)], timeout=10)
    # Only the specific fixture assertion exit is RED. Signals, timeout,
    # compile errors, and other assertion failures are infrastructure/test errors.
    return result.returncode


def cross_audit(directory):
    prefix = '/home/hight/opt/cross-ppc/bin/powerpc-elf-'
    machine = ['-mcpu=7450', '-m32', '-mbig-endian', '-msoft-float',
               '-mno-altivec', '-msdata=none', '-nostdlib', '-fcommon',
               '-fsigned-char', '-fno-short-enums', '-D__cdecl=']
    objects = []
    for unit in UNITS:
        obj = directory / (Path(unit).stem + '.o')
        subprocess.run([prefix + 'gcc', *machine, *FLAGS, *INCLUDES,
                        '-c', str(ROOT / unit), '-o', str(obj)], check=True, timeout=60)
        objects.append(str(obj))
    linked = directory / 'console-linked.o'
    subprocess.run([prefix + 'ld', '-r', *objects, '-o', str(linked)], check=True, timeout=60)
    undefined = subprocess.check_output([prefix + 'nm', '-u', str(linked)], text=True)
    symbols = {line.split()[-1] for line in undefined.splitlines()}
    assert symbols == {'ppc_load8', 'ppc_store8'}, undefined
    # Only the existing external MMIO accessor boundary remains unresolved.
    print('PPC native va_list compile/link GREEN; undefined: ' + ', '.join(sorted(symbols)), flush=True)
    for option, filename in [('-h', 'console-elf.txt'), ('-r', 'console-relocs.txt')]:
        (directory / filename).write_text(subprocess.check_output(
            [prefix + 'readelf', option, str(linked)], text=True))
    (directory / 'console-disasm.txt').write_text(subprocess.check_output(
        [prefix + 'objdump', '-dr', str(linked)], text=True))


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--mutate', action='store_true')
    parser.add_argument('--cross-dir', type=Path, help='retain PPC audit objects and inspection output here')
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix='kboot-console-', dir=os.environ['TMPDIR']) as tmp:
        directory = Path(tmp)
        assert exercise(directory, 'baseline') == 0, 'baseline failed'
        print('baseline GREEN (ILP32, qemu-i386)', flush=True)
        if args.mutate:
            for name, before, after, verdict in MUTATIONS:
                rc = exercise(directory, name, before, after)
                assert rc == verdict, f'{name}: expected assertion rc={verdict}, got {rc}'
                print(f'{name} runtime RED rc={rc}', flush=True)
        target = args.cross_dir or directory
        target.mkdir(parents=True, exist_ok=True)
        cross_audit(target)
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
