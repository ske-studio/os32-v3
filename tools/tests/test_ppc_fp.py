#!/usr/bin/env python3
"""Expectation origin: PPC_FP_NEWLIB_DESIGN §8 F1〜F5・B1.

qemu-ppc executes real FPR/FPSCR operations with only the privileged MSR
wrapper removed. It does not prove privileged entry, IRQ or slot lifecycle;
those observations remain in the PM's system-QEMU acceptance plan.
"""
import argparse
import importlib.util
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]
CHECK = dict(name='ppc-fp', env=['ppc-cross'], tier=2, budget=60, needs=[],
    covers=['sys/arch/ppc/arch_kernel_fp.h', 'tools/tests/test_ppc_fp.py', 'tools/tests/ppc_fp_guard_host.c', 'userland/ppc/fp_probe.S',
            'sys/arch/ppc/fp_context.S', 'arch/ppc/kernel_fp.[ch]',
            'sys/arch/ppc/arch_exec_storage.h', 'sys/arch/ppc/ppc_oea.h',
            'sys/arch/ppc/exec_enter.S', 'sys/arch/ppc/exec_resume.S',
            'sys/arch/ppc/kernel_vectors.S', 'sys/arch/ppc/kernel_trap.c',
            'arch/ppc/ppc_*.h', 'sys/arch/ppc/pd_walk.c', 'include/**', 'sys/kern/mm/pgalloc.h',
            'sys/kern/irq/irq.h', 'sys/kern/irq/irq_math.h',
            'sys/platform/powermac/platform_irq_limits.h', 'build/arch/ppc.mk',
            'build/targets/ppc-powermac.mk', 'tools/ppc/kboot_audit.py', 'tools/ppc/sqlite_attributes.py',
            'lib/sqlite3/sqlite3.c', 'lib/sqlite3/os32_sqlite_config.h',
            'tools/ppc/test_user_trap*', 'tools/ppc/test_exec_enter.py',
            'tools/accept/ppc_fp.py'],
    basis=['PPC_FP_NEWLIB_DESIGN §8 F1〜F5・B1'])


def call(args):
    return subprocess.run([str(x) for x in args], check=True, capture_output=True, text=True).stdout


def load(name, path):
    spec = importlib.util.spec_from_file_location(name, ROOT/path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def once(source, old, new):
    assert source.count(old) == 1, old
    return source.replace(old, new)


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--mutate', action='store_true')
    args = ap.parse_args(argv)
    prefix = str(Path(os.environ.get('PPC_CROSS_DIR', '/home/hight/opt/cross-ppc'))/'bin/powerpc-elf-')
    missing = [x for x in (prefix+'gcc', prefix+'objdump', prefix+'ld', 'qemu-ppc', 'qemu-i386', 'gcc')
               if not shutil.which(x)]
    if missing:
        required = os.environ.get('OS32_REQUIRE_PPC') == '1'
        print(('FAIL' if required else 'SKIP')+': PPC FP dependencies unavailable: '+', '.join(missing))
        return int(required)
    asm = (ROOT/'sys/arch/ppc/fp_context.S').read_text()
    audit = load('fp_audit', 'tools/ppc/kboot_audit.py')
    entry = load('fp_entry', 'tools/ppc/test_exec_enter.py')
    assert entry.inspect(entry.SOURCE.read_text()) == 0xf032
    with tempfile.TemporaryDirectory(prefix='ppc-fp-') as td:
        work = Path(td)
        flags = ['-m32', '-mcpu=750', '-mbig-endian', '-mno-altivec', '-I'+str(ROOT/'arch/ppc')]
        attributes = load('sqlite_attributes', 'tools/ppc/sqlite_attributes.py')
        record = bytearray.fromhex('41 00000011 676e7500 01 00000009 04 09 0c 01')
        expected = record.copy()
        expected[-3] = 8
        assert attributes.neutralize(record.copy()) == expected
        if args.mutate:
            record[-1] = 2
            try:
                attributes.neutralize(record)
            except ValueError as exc:
                assert 'unexpected PPC32 ABI attributes' in str(exc)
                print('RED unexpected PPC32 ABI attribute')
            else:
                raise AssertionError('accepted unexpected PPC32 ABI attribute')
        # Real product wrapper is assembled and audited independently.
        obj = work/'fp.o'
        call([prefix+'gcc', *flags, '-c', ROOT/'sys/arch/ppc/fp_context.S', '-o', obj])
        assert not audit.forbidden(prefix+'objdump', str(obj), str(obj))[1]
        layout = work/'layout.c'
        layout.write_text('#include "exec_arch.h"\n_Static_assert(sizeof(struct arch_fp_context)==264,"size");\n'
                          '_Static_assert(_Alignof(struct arch_fp_context)==8,"align");\n')
        call([prefix+'gcc', *flags, '-I'+str(ROOT/'include'), '-c', layout, '-o', work/'layout.o'])
        # qemu-user owns MSR: run unchanged FP instructions, replacing ONLY the
        # two privileged leaf macros. Mutations still target product lines.
        user = re.sub(r'\.macro fp_enter\n.*?\.endm', '.macro fp_enter\n.endm', asm, flags=re.S)
        user = re.sub(r'\.macro fp_leave\n.*?\.endm', '.macro fp_leave\n    blr\n.endm', user, flags=re.S)
        start = work/'start.S'
        start.write_text('.text\n.globl _start\n_start:\n bl ppc_fp_probe\n li 0,1\n sc\n')
        variants = [('GREEN', user, 0)]
        if args.mutate:
            variants += [('F2-fpr17', once(user, '    stfd 17,136(3)', '    nop'), 2),
                         ('F2-fpscr', once(user, '    stfd 0,PPC_FP_FPSCR_OFFSET(3)', '    nop'), 2),
                         ('F2-save-f0', once(user, '    lfd 0,0(3)\n    fp_leave', '    nop\n    fp_leave'), 2),
                         ('F5-init', once(user, '    b arch_fp_restore', '    blr'), 5)]
        variants += [('GREEN-restored', user, 0)]
        for name, source, expected in variants:
            src = work/'fp.S'; src.write_text(source)
            elf = work/'probe'
            call([prefix+'gcc', *flags, '-DPPC_FP_CONTEXT_TEST', '-nostdlib', '-static', '-Wl,-e,_start',
                  start, ROOT/'userland/ppc/fp_probe.S', src, '-o', elf])
            rc = subprocess.run(['qemu-ppc', str(elf)], timeout=5).returncode
            assert rc == expected, (name, rc, expected)
            print(name+': runtime rc='+str(rc), flush=True)
        # Also assemble/execute the unprivileged USER helpers used by the PM.
        elf = work/'user-probe'
        call([prefix+'gcc', *flags, '-nostdlib', '-static', '-Wl,-e,_start',
              start, ROOT/'userland/ppc/fp_probe.S', '-o', elf])
        assert subprocess.run(['qemu-ppc', str(elf)], timeout=5).returncode == 0
        call([sys.executable, '-B', ROOT/'tools/accept/ppc_fp.py', '--plan'])
        # B1: actual decoded fixture is refused outside the FP leaf range.
        for name, instruction, expected in [('integer', 'nop', False),
                                           ('B1-kernel-fp', 'fadd 1,2,3', True),
                                           ('B1-kernel-load', 'lfd 1,0(3)', True),
                                           ('B1-altivec', 'vxor 0,0,0', True)]:
            src = work/'ordinary.S'
            src.write_text('.section .text.ordinary_wrapped_section,"ax"\n.globl ordinary\nordinary:\n '+instruction+'\n blr\n')
            ordinary = work/'ordinary.o'
            call([prefix+'gcc', *flags, '-c', src, '-o', ordinary])
            elf = work/'audit.elf'
            link_map = work/'audit.map'
            call([prefix+'ld', '-Map='+str(link_map), '-e', 'ordinary', ordinary, obj, '-o', elf])
            bad = audit.forbidden(prefix+'objdump', str(elf), str(obj))[1]
            assert bool(bad) == expected, (name, bad)
            ranges = audit.text_ranges(link_map, str(ordinary))
            within = audit.forbidden(prefix+'objdump', str(elf), str(obj), ranges=ranges)[1]
            assert bool(within) == (name == 'B1-altivec'), (name, within)
            print(name+': audit '+('RED' if bad else 'GREEN'), flush=True)
        guard = (ROOT/'sys/arch/ppc/kernel_fp.c').read_text()
        # Replace only the privileged MSR writer; FP save/restore models expose
        # all 264 bytes so the real guard sequencing is independently checked.
        begin = guard.index('static void set_msr(')
        end = guard.index('\nint ppc_kernel_fp_begin(', begin)
        guard = guard[:begin]+'#define set_msr fake_set_msr\n'+guard[end:]
        cases = [('F3-GREEN', guard, 0)]
        if args.mutate:
            cases += [('F3-no-restore', once(guard, '    arch_fp_restore(&g->previous);', '    (void)g;'), 3)]
        cases += [('F3-GREEN-restored', guard, 0)]
        for name, source, expected in cases:
            (work/'guard.inc').write_text(source)
            exe = work/'guard'
            call(['gcc', '-m32', '-std=gnu11', '-O2', '-Wall', '-Wextra', '-Werror',
                  '-ffreestanding', '-nostdlib', '-static', '-fno-pie', '-no-pie', '-fno-stack-protector',
                  *['-I'+str(p) for p in (work, ROOT, ROOT/'include', ROOT/'arch/ppc', ROOT/'platform/powermac')],
                  ROOT/'tools/tests/ppc_fp_guard_host.c', '-o', exe])
            rc = subprocess.run(['qemu-i386', str(exe)], timeout=5).returncode
            assert rc == expected, (name, rc, expected)
            print(name+': runtime rc='+str(rc), flush=True)
        trap = load('fp_trap', 'tools/ppc/test_user_trap.py')
        source = (ROOT/'sys/arch/ppc/kernel_trap.c').read_text()
        trap.check(source, 0)
        if args.mutate:
            # Unconditional retry is observed as a returned unchanged frame;
            # the fixture rejects it before a real guest would time out.
            retry = once(source, '    if (!f || !translated) goto fatal;',
                         '    if (!f || !translated) goto fatal;\n    if (f->vector == 0x800) return;')
            trap.check(retry, 14)
            print('F4-retry: runtime RED rc=14', flush=True)
        trap.check(source, 0)
    print('PPC FP host PASS; privileged/IRQ/lifecycle observations require system QEMU')
    return 0


def run(ctx):
    return main(['--mutate'] if ctx.mutate else [])


if __name__ == '__main__':
    sys.exit(main())
