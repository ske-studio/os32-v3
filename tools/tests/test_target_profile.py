#!/usr/bin/env python3
"""Capabilities reject before I/O; a staged SDK guard selects its own ar."""
import argparse
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import tempfile
ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'sdk'))
import target_profile as T


def command(*args):
    return subprocess.run([sys.executable, '-B', *map(str,args)], capture_output=True, text=True)


def check(root):
    assert T.require('x86-pc98', 'module_codec')['elf_identity'] == (1,1,3)
    for name in ('ppc-powermac', 'unknown'):
        for capability in ('module_codec','layout','link_guard'):
            try:
                T.require(name,capability)
            except ValueError:
                pass
            else:
                raise AssertionError('unsupported capability accepted')
        output = root / 'forbidden-output'
        result = command(ROOT/'tools/mkmod.py', '--target',name,'--elf','absent',
                         '--metadata','absent','--vmkernel','absent','--kapi-abi-gen','1',
                         '--layout-gen','1','-o',output)
        assert result.returncode and 'target' in result.stderr and 'No such file' not in result.stderr
        result = command(ROOT/'tools/gen_memmap.py','--target',name,'--layout-header',output)
        assert result.returncode and 'target' in result.stderr
        result = command(root/'sdk/link_guard.py','--target',name,'absent-linker','-o',output)
        assert result.returncode and 'target' in result.stderr and 'Traceback' not in result.stderr
        assert not output.exists()
    for machine,endian in ((20,1),(3,2)):
        elf = bytearray(52)
        elf[:7] = bytes([127,69,76,70,1,endian,1])
        elf[18:20] = machine.to_bytes(2,'little' if endian==1 else 'big')
        path=root/'alien.elf';path.write_bytes(elf)
        try:
            T.check_elf_identity(path,T.PROFILES['x86-pc98'])
        except ValueError:
            pass
        else:
            raise AssertionError('alien machine/endian accepted')
    # Staging contains only sibling SDK modules; no repository sys.path fallback.
    result = command(root/'sdk/link_guard.py','--target','x86-pc98','--ar','private-ar','/bin/true')
    assert result.returncode == 0, result.stderr


if __name__ == '__main__':
    parser=argparse.ArgumentParser()
    parser.add_argument('--mutate',action='store_true')
    args=parser.parse_args()
    with tempfile.TemporaryDirectory() as directory:
        root=Path(directory)
        (root/'sdk').mkdir()
        for name in ('link_guard.py','target_profile.py','os32x_hdr.py','os32_generations.py'):
            shutil.copyfile(ROOT/'sdk'/name,root/'sdk'/name)
        check(root)
        if args.mutate:
            original=T.PROFILES['ppc-powermac']['module_codec']
            T.PROFILES['ppc-powermac']['module_codec']=True
            try:
                check(root)
            except AssertionError:
                print('RED: unsupported PPC module capability rejected at runtime')
            else:
                raise SystemExit('mutation survived')
            finally:
                T.PROFILES['ppc-powermac']['module_codec']=original
    print('PASS target profile')
