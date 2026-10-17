#!/usr/bin/env python3
"""T3c2 observer tests; guest reclamation/STOP are accepted separately by PM."""
import contextlib
import io
import json
import os
from pathlib import Path
import struct
import subprocess
import sys
import tempfile

import host32

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools'))
import memmap_audit_live as live
from gen_memmap import Macros, read_defines


def audit_cases(tmp):
    # Independent symbols; addresses in the observer must follow this layout.
    symbols = {'__bss_end': 0x140000, '__kernel_resident_end': 0x140000,
               '__ledger_fixed_start': 0x13e000, '__ledger_fixed_end': 0x140000,
               'ring3_tramp_raw': 0x135123}
    macros = Macros(read_defines(ROOT / 'include/memmap.h'), symbols)
    k = {name: macros.get(name) for name in macros.defines}
    checks = live.checks(k, symbols)
    assert ('KAPI trampoline', 0x136000, 0x136fff, 1, 1) in checks
    shell = [c for c in checks if c[0].startswith('shell')]
    assert shell[-1][2] == k['MEM_SHELL_BAND_END']
    assert shell[0][1] == k['MEM_SHELL_LOAD_ADDR']
    # Fill captures with the expected P/US values, then independently alter guards.
    ptes = {}
    for _, lo, hi, present, user in checks:
        for address in range(lo & ~4095, hi + 1, 4096):
            ptes[address] = address | present | 2 | ((user or 0) << 2)
    with contextlib.redirect_stdout(io.StringIO()):
        assert sum(live.show(ptes, lo, hi, p, u) for _, lo, hi, p, u in checks) == 0
        for guard in ('MEM_STACK_GUARD', 'MEM_SHELL_GUARD'):
            address = k[guard]
            saved = ptes[address]
            ptes[address] |= 1
            assert live.show(ptes, address, address + 4095, 0, None) == 1
            ptes[address] = saved
        # A missing PT and a USER shell page both have to fail, including upper end.
        address = k['MEM_SHELL_BAND_END'] & ~4095
        assert live.show({}, address, address + 4095, 1, 0) == 1
        ptes[address] |= 4
        assert live.show(ptes, address, address + 4095, 1, 0) == 1
    for i in range(k['MEM_FIXED_BOOT_PT_COUNT']):
        words = [ptes.get((i * 1024 + j) * 4096, 0) for j in range(1024)]
        (tmp / f'pt{i}.json').write_text(json.dumps({'hex': struct.pack('<1024I', *words).hex()}))
    loaded = live.pte_all(tmp, k['MEM_FIXED_BOOT_PT_COUNT'])
    assert loaded[address] == ptes[address]
    assert subprocess.run([sys.executable, str(ROOT / 'tools/memmap_audit_live.py'), '--help'],
                          capture_output=True).returncode == 0
    print('PASS live audit: current constants, whole shell band, guards, missing PT, explicit directory')


def guest_observer(tmp):
    cross = Path(os.environ['CROSS_DIR'])
    gccinc = subprocess.check_output(['gcc', '-m32', '-print-file-name=include'], text=True).strip()
    exe = tmp / 'observer'
    subprocess.run(['gcc', '-m32', '-march=i386', '-std=gnu11', '-O2',
                    '-ffreestanding', '-fno-builtin', '-fno-pie', '-fno-stack-protector',
                    '-U_FORTIFY_SOURCE', '-D_FORTIFY_SOURCE=0',
                    '-ffunction-sections', '-fdata-sections', '-nostdlib', '-static', '-no-pie',
                    '-nostdinc', '-isystem', gccinc, '-isystem', str(cross / 'i386-elf/include'),
                    '-I' + str(ROOT / 'include'), '-I' + str(ROOT / 'sdk/include/os32'),
                    '-I' + str(ROOT / 'userland/lib'), '-Wall', '-Wextra', '-Werror',
                    '-Wl,--gc-sections', str(ROOT / 'tools/tests/layout_cycle_host.c'), '-o', str(exe)],
                   check=True)
    host32.run([str(exe)], check=True, timeout=60)
    print('PASS guest observer: 6 modes x20, nested --once, sentinel corruption, 8 resource leaks, wrong result; fd1 summaries, leaf allocation failures, invalid mode, short write; dbg guards')


def old_admission(tmp):
    # Reuse the existing real predicate fixture, including old memory generation.
    exe = tmp / 'admission'
    subprocess.run(['gcc', '-std=gnu11', '-Wall', '-Wextra', '-Werror', '-D__cdecl=',
                    '-I' + str(ROOT / 'sdk/include/os32'),
                    str(ROOT / 'tools/tests/os32x_layout_host.c'), '-o', str(exe)], check=True)
    subprocess.run([str(exe)], check=True)
    print('PASS existing admission fixture: old shell/shlib generation rejected, current accepted')


def main():
    for name in ('userland/gshell/Cargo.toml', 'userland/gshell/src/lib.rs'):
        assert '0x300000' not in (ROOT / name).read_text(), name
    assert '/tmp/claude-' not in (ROOT / 'tools/memmap_audit_live.py').read_text()
    dbg = (ROOT / 'userland/lib/rt/dbgserial.c').read_text()
    for old in ('0x1FB000', '0x200000', '0x370000', '0x380000'):
        assert old not in dbg, old
    with tempfile.TemporaryDirectory(prefix='os32-t3c2-') as td:
        tmp = Path(td)
        audit_cases(tmp)
        guest_observer(tmp)
        old_admission(tmp)
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
