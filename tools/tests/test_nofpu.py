"""Audit the linked no-FPU product, its isolated acceptance image and media."""
from pathlib import Path
import os
import re
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]
BIN = Path(os.environ.get('CROSS_DIR', '/home/hight/opt/cross')) / 'bin'


def output(tool, *args):
    return subprocess.check_output([str(BIN / ('i386-elf-' + tool)),
                                    *map(str, args)], text=True)


def audit(elf, allowed=()):
    assert not output('nm', '-u', elf).strip(), f'unresolved references: {elf}'
    link_map = elf.with_suffix('.map')
    if link_map.exists():
        assert 'libgcc' not in link_map.read_text(), link_map
    function = ''
    count = 0
    for line in output('objdump', '-d', '--no-show-raw-insn', elf).splitlines():
        label = re.match(r'^[0-9a-f]+ <([^>]+)>:', line)
        if label:
            function = label[1]
        insn = re.match(r'^\s*[0-9a-f]+:\s+([a-z][a-z0-9]*)\b', line)
        if insn and (insn[1].startswith('f') or insn[1] == 'wait'):
            assert function in allowed, (elf, function, line)
            count += 1
    return count


def build_flags(forced=False):
    # Query the same graph as the product; no caller MAKEFLAGS propagation.
    env = dict(os.environ)
    for key in ('MAKEFLAGS', 'MFLAGS', 'MAKEOVERRIDES', 'GNUMAKEFLAGS'):
        env.pop(key, None)
    return subprocess.check_output(
        ['make', '-rsf', '-', 'flags', 'TEST_NOFPU_BUILD=' + str(int(forced)), 'KERNEL_CFLAGS_EXTRA=-DKAPI_PROFILE'],
        input='include build/config.mk\nflags:\n\t@$(info $(CFLAGS_BASE)):\n',
        cwd=ROOT, env=env, text=True)


def main():
    out = ROOT / 'build/out'
    allowed = {line.split()[0] for line in
               output('nm', '-P', '--defined-only', out/'obj/arch/x86/kernel/x87.o').splitlines()
               if line.split()[1] in ('T', 't')}
    for directory in (out, out/'testnofpu'):
        count = audit(directory/'kernel.elf', allowed)
        assert count, 'hardware guarded instructions disappeared'
        link_map = (directory/'kernel.map').read_text()
        assert 'libgcc' not in link_map
        assert 'x87_emu_dispatch.o' in link_map and 'extF80_add.o' in link_map
        for module in sorted(directory.glob('*.elf')):
            if module.name != 'kernel.elf':
                audit(module)
        # Module ELF files live in their own build subdirectories.
        for module in sorted(directory.glob('*/*.elf')):
            if module.parent.name in ('r1', 'testnofpu'):
                continue
            audit(module)
        size = (directory/'vmkernel.lz4').stat().st_size
        assert size <= 520192, size
        print(f'{directory.relative_to(ROOT)}: x87.c={count}, outside=0, '
              f'undefined=0 libgcc=0, VK32={size}, margin={520192-size}')
    assert (out/'testnofpu/sqlite-proto/sqlite.elf').is_file(), 'acceptance SQLite must be isolated'
    normal, forced = build_flags(), build_flags(True)
    assert '-DOS32_X87_EMU_LINKED' in normal
    assert '-DOS32_TEST_NOFPU' not in normal
    assert '-DOS32_TEST_NOFPU' in forced
    for directory, test in ((out, False), (out/'testnofpu', True)):
        # These flags are captured by the actual build, not just the make query.
        flags = (directory/'config/df-guard.flags').read_text()
        assert ('-DOS32_TEST_NOFPU' in flags) == test
    sys.path.insert(0, str(ROOT/'tools/tests'))
    from test_packages import fat12_files
    notices = {'/sys/sfloat.txt': ROOT/'lib/softfloat/COPYING.txt',
               '/sys/x87emu.txt': ROOT/'arch/x86/kernel/x87_emu_vendor/LICENSE-I386C-FPU.TXT'}
    for image in ('os32_boot.img', 'os32_boot144.img'):
        files, free, total = fat12_files(str(ROOT/'images'/image))
        assert free >= 65536, (image, free)
        for guest, source in notices.items():
            assert files[guest.upper()] == source.read_bytes(), guest
        print(f'{image}: free={free}/{total}, margin over 64KiB={free-65536}')
    if '--mutate' in sys.argv:
        with tempfile.TemporaryDirectory(prefix='nofpu-audit-') as d:
            source, obj = Path(d)/'bad.c', Path(d)/'bad.o'
            for name, body in (
                ('forbidden-x87', 'void bad(void) { __asm__ volatile("fninit"); }'),
                ('undefined-helper', 'extern void __bad_helper(void); void bad(void) { __bad_helper(); }')):
                source.write_text(body)
                subprocess.run([str(BIN/'i386-elf-gcc'), '-march=i386', '-ffreestanding',
                                '-c', str(source), '-o', str(obj)], check=True)
                try:
                    audit(obj)
                except AssertionError:
                    print(f'{name}: audit RED')
                else:
                    raise AssertionError(name + ' survived')


if __name__ == '__main__':
    main()
