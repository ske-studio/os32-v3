#!/usr/bin/env python3
"""Build the PowerPC USER shell and verify its guarded OS32X image."""
CHECK = dict(covers=['build/ppc-user.mk', 'tools/ppc/test_sh_build.py'],
             inputs=['Makefile', 'build/**', 'sdk/**', 'userland/**', 'include/**',
                     'drivers/**', 'lib/**', 'kdebug/**', 'hwdev/**', 'tools/ppc/test_sh_build.py'])
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'sdk'))
import os32x_hdr as hdr


def exercise(root, mutant=False):
    result = subprocess.run(['make', '-j1', 'ARCH=ppc', 'PLATFORM=powermac', 'sh'],
                            cwd=root, capture_output=True, text=True)
    out = root / 'build/out/ppc-user'
    if mutant:
        assert result.returncode != 0 and 'missing validated link inputs' in result.stderr
        assert (out / 'sh.elf').is_file() and (out / 'sh.raw').is_file()
        assert not (out / 'sh.inputs.json').exists()
        return
    assert result.returncode == 0, result.stdout + result.stderr
    elf = hdr.Elf32(out / 'sh.elf')
    fields = struct.unpack('<16I', (out / 'sh.bin').read_bytes()[:64])
    assert fields[3] & 0x40 and fields[10] == 0x80100000
    assert fields[10] + fields[4] == elf.e_entry == elf.symbols['_start']
    nm = subprocess.run(['/home/hight/opt/cross-ppc/bin/powerpc-elf-nm', '-u', out / 'sh.elf'],
                        capture_output=True, text=True, check=True)
    assert not nm.stdout.strip(), nm.stdout
    assert (out / 'sh.inputs.json').is_file()


def main():
    with tempfile.TemporaryDirectory() as td:
        root = Path(td)
        shutil.copy(ROOT / 'Makefile', root)
        for name in ('build', 'sdk', 'userland', 'include', 'drivers', 'lib', 'kdebug', 'hwdev'):
            shutil.copytree(ROOT / name, root / name,
                            ignore=shutil.ignore_patterns('out', '*.o', '*.d', '*.bin', '*.elf', '*.raw', 'target'))
        exercise(root)
        if '--mutate' in sys.argv:
            rule = root / 'build/ppc-user.mk'
            source = rule.read_text()
            old = 'python3 sdk/link_guard.py --target ppc-powermac $(LD)'
            assert source.count(old) == 1
            rule.write_text(source.replace(old, '$(LD)'))
            shutil.rmtree(root / 'build/out/ppc-user')
            exercise(root, True)
    print('PPC sh build: OK')


if __name__ == '__main__':
    main()
