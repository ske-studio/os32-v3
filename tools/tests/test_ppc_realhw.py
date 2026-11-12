#!/usr/bin/env python3
"""Exercise distribution limits and real-hardware transcript assessment."""
CHECK = dict(name='ppc-realhw', tier=2, budget=10, needs=['make:all'],
             covers=['tools/ppc/realhw_image.py', 'tools/ppc/realhw_run.py',
                     'tools/tests/test_ppc_realhw.py','tools/ppc/sc_run.py', 'tools/ppc/sd_run.py',
                     'tools/ppc/sb_run.py', 'tools/ppc/kboot_run.py',
                     'tools/ppc/kboot_accept.py', 'tools/ppc/run.py',
                     'tools/ppc_lan_host.py', 'tools/ppc/*.py', 'tools/*host*.py',
                     'tools/lan*.py', 'tools/serialfs_host.py', 'arch/ppc/*.[cSh]',
                     'init/ppc_kernel.c', 'platform/powermac/*.[ch]',
                     'userland/ppc/*', 'build/**/*.mk', 'build/out/ppc-user/*'])
import os
from pathlib import Path
import struct
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT/'tools/ppc'))
import realhw_run


def fixture(note=True):
    header = struct.pack('>16sHHIIIIIHHHHHH', b'\x7fELF\x01\x02\x01'+bytes(9), 2, 20, 1,
                         0x1000, 52, 0, 0, 52, 32, 2 if note else 1, 40, 0, 0)
    load = struct.pack('>IIIIIIII', 1, 0, 0x1000, 0x1000, 160, 192, 5, 1)
    notehdr = struct.pack('>IIIIIIII', 4, 128, 0, 0, 20, 20, 4, 4)
    data = bytearray(160)
    data[:52] = header
    data[52:84] = load
    if note:
        data[84:116] = notehdr
        data[128:148] = struct.pack('>III4sI', 4, 4, 1, b'OF\0\0', 7)
    return data


def check(script, image, base, end):
    return subprocess.run([sys.executable, '-B', str(script), str(image), '--load-base', hex(base),
                           '--avail-end', hex(end)], stdout=subprocess.PIPE, stderr=subprocess.PIPE).returncode


def main():
    with tempfile.TemporaryDirectory() as d:
        tmp = Path(d)
        image = tmp/'image'; image.write_bytes(fixture())
        missing = tmp/'missing'; missing.write_bytes(fixture(False))
        script = ROOT/'tools/ppc/realhw_image.py'
        assert check(script, image, 0x8000, 0x9000) == 0
        assert check(script, image, 0x10c0, 0x9000) != 0
        assert check(script, image, 0x8000, 0x809f) != 0
        assert check(script, missing, 0x8000, 0x9000) != 0
        assert subprocess.run([sys.executable, '-B', str(script), str(image)], stdout=subprocess.PIPE).returncode != 0
        class Host:
            boot = bytes(16)
        evidence = {'terminal': 'os32> '+realhw_run.EXPECTED}
        assert realhw_run.run_rh(Host(), 'nest', evidence)['pass']
        evidence['terminal'] = evidence['terminal'].replace('exit=5', 'exit=6')
        try:
            realhw_run.run_rh(Host(), 'nest', evidence)
        except RuntimeError:
            pass
        else:
            raise AssertionError('RH_USER_EXIT survived')
        realhw_run.run_rh(Host(), 'exact', {'terminal': 'cat x\ny\nsh> ', 'command': 'cat x', 'expected': 'y\n'})
        if '--mutate' in sys.argv:
            source = script.read_text()
            for label, old, new, base, end in [
                ('load-base', 'pa + memsz >= load_base', 'False', 0x10c0, 0x9000),
                ('filesz', 'len(data) > avail_end - load_base', 'False', 0x8000, 0x809f),
            ]:
                assert source.count(old) == 1
                mutant = tmp/(label+'.py'); mutant.write_text(source.replace(old, new))
                assert check(mutant, image, base, end) == 0, label
                print('RED '+label+' runtime accepted invalid ELF')
    print('PASS realhw ELF and transcript')


if __name__ == '__main__':
    main()


def run(ctx):
    return subprocess.call([sys.executable, '-B', 'tools/tests/test_ppc_realhw.py',
                            *(['--mutate'] if ctx.mutate else [])], cwd=ctx.root)
