#!/usr/bin/env python3
"""Build the PPC USER acceptance application through its make entry point."""
CHECK = dict(covers=['build/ppc-user.mk', 'userland/ppc/hello.c', 'userland/ppc/fault.c',
                     'tools/ppc/test_hello_build.py'],
             inputs=['Makefile', 'build/**', 'sdk/**', 'userland/ppc/hello.c', 'userland/ppc/fault.c',
                     'tools/ppc/test_hello_build.py'])
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
    result = subprocess.run(['make', '-j1', 'ARCH=ppc', 'PLATFORM=powermac', 'hello', 'ppc-fault'],
                            cwd=root, capture_output=True, text=True)
    out = root / 'build/out/ppc-user'
    if mutant:
        assert result.returncode != 0 and 'missing validated link inputs' in result.stderr
        assert (out / 'hello.elf').is_file() and (out / 'hello.raw').is_file()
        assert not (out / 'hello.inputs.json').exists()
        return
    assert result.returncode == 0, result.stdout + result.stderr
    elf = hdr.Elf32(out / 'hello.elf')
    fields = struct.unpack('<16I', (out / 'hello.bin').read_bytes()[:64])
    assert fields[3] & 0x40 and fields[10] == 0x80100000
    assert fields[10] + fields[4] == elf.e_entry == elf.symbols['_start']
    nm = subprocess.run(['/home/hight/opt/cross-ppc/bin/powerpc-elf-nm', '-u', out / 'hello.elf'],
                        capture_output=True, text=True, check=True)
    assert not nm.stdout.strip(), nm.stdout
    fault = struct.unpack('<16I', (out / 'fault.bin').read_bytes()[:64])
    assert fault[3] & 0x40 and fault[10] == fields[10]
    receipt = (out / 'hello.inputs.json').is_file()
    assert receipt, 'guarded link receipt missing'


def main():
    with tempfile.TemporaryDirectory() as td:
        root = Path(td)
        shutil.copy(ROOT / 'Makefile', root)
        shutil.copytree(ROOT / 'build', root / 'build', ignore=shutil.ignore_patterns('out'))
        shutil.copytree(ROOT / 'sdk', root / 'sdk')
        (root / 'userland/ppc').mkdir(parents=True)
        for app in ('hello', 'fault'):
            shutil.copy(ROOT / f'userland/ppc/{app}.c', root / 'userland/ppc')
        exercise(root)
        if '--mutate' in sys.argv:
            rule = root / 'build/ppc-user.mk'
            source = rule.read_text()
            old = 'python3 sdk/link_guard.py --target $(SDK_TARGET) $(LD)'
            assert source.count(old) == 1
            rule.write_text(source.replace(old, '$(LD)'))
            shutil.rmtree(root / 'build/out/ppc-user')
            exercise(root, True)
    print('PPC hello build: OK')


if __name__ == '__main__':
    main()
