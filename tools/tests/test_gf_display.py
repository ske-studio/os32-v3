"""P3 calc versus base source, executed rows versus C plan, real service faults."""
from pathlib import Path
import subprocess
import tempfile
import host32
import test_gfinit_plan_c

ROOT = Path(__file__).resolve().parents[2]
BASE = '13bc5619'


def previous_calc():
    def source(name):
        return subprocess.check_output(['git', 'show', f'{BASE}:{name}'], cwd=ROOT, text=True)
    text = source('kdebug/gfinit_mode.c').split('void gfinit_mode_palette')[0]
    for name in ('output', 'pll_program'):
        text = text.replace(f'#include "gfinit_mode/{name}.inc"', source(f'kdebug/gfinit_mode/{name}.inc'))
    return text.replace('../hwdev/gfinit_mode.h', str(ROOT / 'hwdev/gfinit_mode.h')).replace(
        'gfinit_mode_calc', 'gf_previous_calc').replace('mode_bit', 'previous_mode_bit').replace(
        'mode_dcb_head_a_or0', 'previous_mode_dcb_head_a_or0')


def check_rows(text):
    rows, actual, delays, resets = {}, [], {}, []
    colors = bytes.fromhex('000000 ff0000 00ff00 0000ff ffffff 00ffff ff00ff ffff00') + bytes(744)
    groups = 0
    collecting = False
    for line in text.splitlines():
        if line.startswith('variant='):
            rows = {}
        elif line.startswith('row=') and not collecting:
            ordinal, kind, reg, index, width, value, count, bound = map(int, line[4:].split())
            assert bound == 1
            if kind != 10:  # Upper layer route belongs to P5.
                rows[ordinal] = kind, reg, index, width, value, count
        elif line.startswith('adapter variant='):
            collecting = True
            actual, delays, resets = [], {}, []
        elif line.startswith('bus='):
            ordinal, write, reg, width, value = map(int, line[4:].split())
            if write:
                actual.append((ordinal, reg, width, value))
            else:
                assert reg not in (0x6013c0, 0x6813c9)
                if reg == 0x6013da:
                    resets.append(ordinal)
        elif line.startswith('delay='):
            ordinal, us = map(int, line[6:].split())
            delays[ordinal] = delays.get(ordinal, 0) + us
        elif line == 'adapter end':
            expected = []
            for ordinal, (kind, reg, index, width, value, count) in rows.items():
                if kind in (1, 2, 3):
                    expected += [(ordinal, reg, 1, index), (ordinal, reg+1, width, value)]
                elif kind == 4:
                    expected += [(ordinal, reg, 1, index), (ordinal, reg, 1, value)]
                elif kind == 9:
                    assert count == len(colors)
                    expected += [(ordinal, reg, 1, v) for v in colors]
                else:
                    expected.append((ordinal, reg, width, value))
            assert len(actual) == len(expected), (len(actual), len(expected))
            for i, (got, want) in enumerate(zip(actual, expected)):
                assert got == want, (groups, i, got, want)
            assert resets == [i for i, r in rows.items() if r[0] in (4, 6)]
            assert delays == ({1: 50000, 5: 10000, 200: 10000} if 1 in rows else {5: 10000, 200: 10000})
            collecting = False
            groups += 1
    assert groups == 3
    assert 'DISPLAY PASS' in text and 'FAIL' not in text
    print('PASS adapter: all writes match executed C plan (3 cases), AR/DAC aliases and FP/DPMS waits')
    for line in text.splitlines():
        if line.startswith('PASS '):
            print(line)


def main():
    with tempfile.TemporaryDirectory(prefix='gf-display-') as tmp:
        tmp = Path(tmp)
        (tmp / 'gf_previous_calc.inc').write_text(previous_calc())
        exe = tmp / 'host'
        cmd = [s.replace('gfinit_plan_c_host.c', 'gf_display_host.c')
               for s in test_gfinit_plan_c.command(exe)]
        sources = [ROOT / 'hwdev/gf/service.c',
                   *[p for p in sorted((ROOT / 'hwdev/gf/display').glob('*.c'))
                     if p.stem in ('baseline', 'checks', 'load_row', 'modeset', 'quiesce', 'palette', 'blank', 'modes')]]
        cmd += ['-I'+str(ROOT), '-I'+str(tmp), *map(str, sources)]
        subprocess.run(cmd, cwd=ROOT, check=True)
        result = host32.run([str(exe)], cwd=ROOT, timeout=180, capture_output=True, text=True)
        if result.returncode:
            print('\n'.join([line for line in result.stdout.splitlines() if 'FAIL' in line][:30]))
            return result.returncode
        check_rows(result.stdout)
    # Retain terra's focused tests: they cover partial palette updates and
    # every blank I/O failure more precisely than the integration fixture.
    with tempfile.TemporaryDirectory(prefix='gf-components-') as tmp:
        for name in ('palette', 'blank', 'modes'):
            exe = Path(tmp) / name
            cmd = ['gcc', '-m32', '-march=i386', '-std=gnu11', '-O2', '-Wall', '-Wextra',
                   '-Werror', '-ffreestanding', '-fno-pie', '-fno-stack-protector',
                   '-nostdlib', '-static', '-no-pie',
                   *['-I'+str(ROOT / d) for d in ('.', 'include', 'sdk/include/os32')],
                   str(ROOT / f'tools/tests/gf_display_{name}_host.c')]
            if name in ('blank', 'modes'):
                cmd.append(str(ROOT / f'hwdev/gf/display/{name}.c'))
            subprocess.run([*cmd, '-o', str(exe)], cwd=ROOT, check=True)
            result = host32.run([str(exe)], cwd=ROOT, timeout=60)
            if result.returncode:
                return result.returncode
            print(f'PASS component {name}')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
