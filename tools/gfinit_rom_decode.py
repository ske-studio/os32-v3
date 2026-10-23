#!/usr/bin/env python3
"""Notebook diagnostic: python3 tools/gfinit_rom_decode.py /path/to/private.rom
Runs kdebug/gfinit_decode.c (host build, same flags as test_gfinit.py) on a private
ROM copy and prints the decode counters and failing pc. The ROM is turned into a C
array only inside a temporary directory; no ROM bytes are printed or written into
the repository. Does not disassemble anything.
"""
import pathlib
import subprocess
import sys
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools/tests'))
import host32  # noqa: E402

FLAGS = ['-std=gnu11', '-Wall', '-Wextra', '-Werror', '-m32', '-march=i386',
         '-ffreestanding', '-fno-pie', '-fno-stack-protector']


def main(argv):
    if len(argv) != 2:
        print(__doc__.splitlines()[0])
        return 2
    rom = pathlib.Path(argv[1]).read_bytes()
    with tempfile.TemporaryDirectory(prefix='gfrom-') as td:
        header = pathlib.Path(td) / 'gf_private_rom.h'
        body = ','.join(str(b) for b in rom)
        header.write_text('static const u8 gf_private_rom[%d] = {%s};\n' % (len(rom), body))
        exe = pathlib.Path(td) / 'decode'
        subprocess.run(['gcc', *FLAGS, '-O2', '-nostdlib', '-static', '-no-pie',
                        '-I' + str(ROOT / 'include'), '-DGF_ROM_HEADER="%s"' % header,
                        str(ROOT / 'tools/tests/gfinit_romfile_host.c'), '-o', str(exe)], check=True)
        return host32.run([str(exe)], timeout=60).returncode


if __name__ == '__main__':
    raise SystemExit(main(sys.argv))
