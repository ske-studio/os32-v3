#!/usr/bin/env python3
"""Expectation origin: PPC_FP_NEWLIB_DESIGN §8 F1〜F5; runner fixtures.
These observations test classification, not privileged guest acceptance.
"""
import argparse
import os
import shutil
import subprocess
import sys
from pathlib import Path
import tempfile

ROOT = Path(__file__).resolve().parents[2]
CHECK = dict(name='ppc-fp-accept-host', env=['ppc-cross', 'qemu-ppc'], tier=1, budget=10, needs=[],
    covers=['tools/tests/test_ppc_fp_accept.py', 'tools/accept/ppc_fp.py',
            'sys/arch/ppc/exec_enter.S', 'sys/arch/ppc/exec_resume.S', 'sys/arch/ppc/kernel_vectors.S',
            'sys/arch/ppc/fp_context.S', 'sys/arch/ppc/kernel_trap.c',
            'build/ppc-user.mk', 'build/targets/ppc-powermac.mk',
            'userland/ppc/fp_probe_main.c', 'userland/ppc/fp_probe.S',
            'tools/tests/ppc_fp_accept_host.c', 'sdk/include/os32/**'],
    basis=['PPC_FP_NEWLIB_DESIGN §8 F1〜F5'])
SOURCE = ROOT/'tools/accept/ppc_fp.py'


def load(source):
    ns = {'__file__': str(SOURCE), '__name__': 'fp_accept_fixture'}
    exec(compile(source, str(SOURCE), 'exec'), ns)
    return ns


def fixtures(ns):
    v = ns['verdict']
    fault = 'FATAL trap vector=0x800 reason=0x11 addr=0x0\n'
    assert v('normal', b'F1=2\nFP-exit=0\n', b'0\n', '') == 'GREEN'
    assert v('F4', b'F4-before\n', b'1\n', fault) == 'kill'
    assert v('F1-entry', b'', b'1\n', fault) == 'kill'
    assert v('F4-retry', b'', None, '', True) == 'timeout'
    for case, code in [('F2-fpr17', 17), ('F2-fpscr', 33), ('F5-init', 5)]:
        value = str(code).encode()
        assert v(case, b'FP-exit='+value+b'\n', value+b'\n', '') == value.decode()
        assert v(case, b'FP-exit=43\n', b'43\n', '') == 'wrong-reason'
        assert v(case, b'FP-exit='+value+b'\n', value+b'\n', fault) == 'wrong-reason'
        assert v(case, b'FP-exit='+value+b'\n', value+b'\n', fault.replace('0x800', '0x700')) == 'wrong-reason'
    for case in ('normal', 'F1-entry', 'F2-fpr17', 'F4', 'F5-init'):
        assert v(case, b'', None, '', True) == 'wrong-reason'
    for diagnostic in ('', fault.replace('0x800', '0x700'), fault.replace('0x11', '0x1')):
        assert v('F4', b'F4-before\n', b'1\n', diagnostic) == 'wrong-reason'
    for body in (b'', b'F4-before\nFP-exit=0\n', b'F4-before\nF4-after\n'):
        assert v('F4', body, b'1\n', fault) == 'wrong-reason'
    assert v('F4-retry', b'', None, fault, True) == 'wrong-reason'
    assert v('F4-retry', b'', None, '', False) == 'wrong-reason'
    assert v('F1-entry', b'F1=2\n', b'1\n', fault) == 'wrong-reason'


def settings(ns):
    with tempfile.TemporaryDirectory() as td:
        for name, count in [('normal', 0), ('F4', 2), ('F4-retry', 3),
                            ('F1-entry', 3), ('F2-fpr17', 1), ('F2-fpscr', 1), ('F5-init', 1)]:
            work = Path(td)/name; work.mkdir()
            opts = ns['settings'](work, name)
            values = dict(o.split('=', 1) for o in opts)
            assert values['KBOOT_SH'] == values['KBOOT_LINK'] == '1'
            assert values['KB_ROOT_BINS'] == str(work/'user/fp_probe.bin')
            pairs = [(values['KB_MUT_'+slot+'SRC'], values['KB_MUT_'+slot+'FILE'])
                     for slot in ('', 'EXTRA_', 'THIRD_') if 'KB_MUT_'+slot+'SRC' in values]
            assert len(pairs) == count
            assert len({src for src, _ in pairs}) == count
            for src, path in pairs:
                original = (ROOT/src).read_text()
                changed = Path(path).read_text()
                assert changed != original
            if name.startswith('F4'):
                assert pairs[0][0] == 'sys/arch/ppc/exec_enter.S'
                assert pairs[1][0] == 'sys/arch/ppc/kernel_vectors.S'
                assert '    ori 3,3,PPC_MSR_FP' not in Path(pairs[1][1]).read_text()
            if name == 'F4-retry': assert pairs[2][0] == 'sys/arch/ppc/kernel_trap.c'
            if name == 'F1-entry':
                assert [src for src, _ in pairs] == ['sys/arch/ppc/exec_enter.S',
                    'sys/arch/ppc/exec_resume.S', 'sys/arch/ppc/kernel_vectors.S']


def user_driver():
    prefix = str(Path(os.environ.get('PPC_CROSS_DIR', '/home/hight/opt/cross-ppc'))/'bin/powerpc-elf-')
    if not shutil.which(prefix+'gcc') or not shutil.which('qemu-ppc'):
        required = os.environ.get('OS32_REQUIRE_PPC') == '1'
        print(('FAIL' if required else 'SKIP')+': FP driver PPC compiler/qemu-ppc unavailable')
        return int(required)
    with tempfile.TemporaryDirectory() as td:
        work = Path(td)
        flags = ['-mcpu=750', '-msoft-float', '-mno-altivec', '-msdata=none',
                 '-ffreestanding', '-fno-builtin', '-fno-pie', '-O2', '-Wno-attributes',
                 '-I'+str(ROOT/'sdk/include/os32')]
        obj = work/'driver.o'
        subprocess.run([prefix+'gcc', *flags, '-Dmain=fp_main', '-c',
                        str(ROOT/'userland/ppc/fp_probe_main.c'), '-o', str(obj)], check=True)
        start = work/'start.S'
        start.write_text('.text\n.globl _start\n_start:\n bl test_main\n li 0,1\n sc\n')
        elf = work/'driver'
        subprocess.run([prefix+'gcc', *flags, '-nostdlib', '-static', '-Wl,-e,_start,-z,noexecstack',
                        str(start), str(obj), str(ROOT/'tools/tests/ppc_fp_accept_host.c'),
                        str(ROOT/'userland/ppc/fp_probe.S'), '-o', str(elf)], check=True)
        result = subprocess.run(['qemu-ppc', str(elf)], timeout=10)
        assert result.returncode == 0, ('USER driver', result.returncode)
        print('GREEN real USER driver: F1/F2/F5 and missing-init RED=5')
    return 0


def main():
    ap = argparse.ArgumentParser(); ap.add_argument('--mutate', action='store_true')
    args = ap.parse_args()
    rc = user_driver()
    if rc: return rc
    source = SOURCE.read_text()
    fixtures(load(source)); settings(load(source))
    print('GREEN classification and isolated mutation slots')
    if args.mutate:
        mutations = [
            ('wrong-code', "'F2-fpr17': 17", "'F2-fpr17': 18"),
            ('generic-fault', 'reason=0x11', 'reason=0x1'),
            ('timeout-anywhere', "if timeout: return 'wrong-reason'", "if timeout: return 'timeout'"),
            ('F4-resume-missing', 'edits = [MUTATIONS[0], STIMULUS]', 'edits = [MUTATIONS[0]]'),
        ]
        for name, before, after in mutations:
            assert source.count(before) == 1, name
            ns = load(source.replace(before, after))
            try:
                fixtures(ns); settings(ns)
            except AssertionError:
                print('RED '+name)
            else: raise AssertionError('SURVIVED '+name)
            fixtures(load(source)); settings(load(source))
    return 0


def run(ctx):
    import subprocess
    import sys
    return subprocess.call([sys.executable, '-B', __file__] + (['--mutate'] if ctx.mutate else []), cwd=ctx.root)


if __name__ == '__main__': raise SystemExit(main())
