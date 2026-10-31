"""Execute the real boot shell loop, before and after E02b extraction.

Only device/exec boundaries are faked; product control flow is compiled intact.
Mutation compile failures and signals are errors, never runtime RED evidence.
"""
CHECK = dict(name='kernel-shell-host', tier=1,
    covers=['init/kernel.c', 'init/kernel_gui_shlib.inc',
            'init/kernel_shell_fdd.inc', 'tools/tests/kernel_shell_host.c',
            'tools/tests/host32.py', 'tools/tests/kernel_includes.py',
            'include/config.h', 'include/pc98.h', 'include/types.h',
            'exec/exec.h', 'mm/shlib.h', 'mm/paging.h',
            'sdk/include/os32/os32_kapi_shared.h', 'init/kernel_boot_cpu.inc', 'init/kernel_boot_memory.inc', 'init/kernel_gfx_config.inc'],
    needs=[], budget=5)

from pathlib import Path
import re
import subprocess
import sys
import tempfile

import host32
from kernel_includes import include_flags

ROOT = Path(__file__).resolve().parents[2]
CASES = ('cui', 'gui', 'gui-probed', 'kcg', 'shlib-layout', 'shlib-api',
         'cui-ignores-shlib', 'load-fail', 'load-layout', 'request-again',
         'three-faults', 'success-resets', 'request-resets', 'cui-fault',
         'fdd-success', 'fdd-fault', 'fdd-fail', 'layout-first',
         'layout-second', 'layout-both', 'tick-wrap')


def product_source():
    source = (ROOT / 'init/kernel.c').read_text()
    constants = '\n'.join(re.findall(
        r'^#define (?:GUI_FAULT_STREAK_MAX|SHELL_RELOAD_DELAY) .+$', source, re.M))
    # Follow the actual includes, so the same fixture works on the original loop.
    source = re.sub(r'^#include "(kernel_(?:gui_shlib|shell_fdd)\.inc)"$',
                    lambda m: (ROOT / 'init' / m[1]).read_text(), source, flags=re.M)
    functions = re.findall(
        r'^static (?:inline void kernel_shell_loop|int kernel_gui_shlib_ready|'
        r'void kernel_shell_fdd_fallback)\([^\n]*\)\n\{.*?^}\n', source, re.M | re.S)
    assert sum('kernel_shell_loop(void)' in f for f in functions) == 1
    return constants + '\n' + '\n'.join(functions)


def build(work, source):
    (work / 'kernel_shell_source.inc').write_text(source)
    exe = work / 'shell'
    host32.build(['gcc', '-m32', '-std=gnu11', '-O0', '-Wall', '-Wextra',
                  '-Werror', '-static', '-fno-pie', '-no-pie', '-nostdlib',
                  '-ffreestanding', '-fno-builtin', '-fno-stack-protector',
                  '-I' + str(work), *include_flags(ROOT),
                  str(ROOT / 'tools/tests/kernel_shell_host.c'), '-o', str(exe)],
                 check=True)
    return exe


def run_cases(exe):
    results = []
    for case in CASES:
        result = host32.run([str(exe), case], capture_output=True, text=True, timeout=10)
        assert result.returncode in (0, 1), (case, result.returncode, result.stderr)
        results.append((case, result.returncode, result.stdout + result.stderr))
    return results


def main():
    source = product_source()
    with tempfile.TemporaryDirectory(prefix='kernel-shell-') as td:
        work = Path(td)
        results = run_cases(build(work, source))
        assert all(rc == 0 for _, rc, _ in results), results
        print(f'GREEN {len(results)} real shell-loop cases (runner={host32.selected_runner()})', flush=True)
        if '--mutate' not in sys.argv:
            return
        mutations = [
            ('load-fail-waits', 'continue;   /* ウェイト無しで即 CUI を起動 */',
             ';   /* mutation: fall through to wait */'),
            ('fault-streak', 'gui_fault_streak++;', 'gui_fault_streak += 2;'),
            ('layout-or', 'layout_bad |= exec_layout_rejected();',
             'layout_bad = exec_layout_rejected();'),
            ('layout-second', 'layout_bad |= exec_layout_rejected();',
             'layout_bad |= 0;'),
            ('consume-reset', 'gui_fault_streak = 0;   /* 別シェルへ切替',
             ';   /* 別シェルへ切替'),
        ]
        # The new call boundaries must also be observed, not merely compiled.
        if 'kernel_gui_shlib_ready(void)' in source:
            mutations += [
                ('gui-result', 'is_gui && !kernel_gui_shlib_ready()',
                 'is_gui && kernel_gui_shlib_ready()'),
                ('fdd-layout-argument', 'kernel_shell_fdd_fallback(exec_layout_rejected());',
                 'kernel_shell_fdd_fallback(0);'),
            ]
        # Explicit identity control uses the very same build/runtime path.
        marker = 'gui_fault_streak++;'
        assert source.count(marker) == 1
        assert all(rc == 0 for _, rc, _ in run_cases(build(work, source.replace(marker, marker))))
        print('GREEN identity control', flush=True)
        for label, old, new in mutations:
            assert source.count(old) == 1, (label, source.count(old))
            results = run_cases(build(work, source.replace(old, new, 1)))
            failures = [case for case, rc, _ in results if rc == 1]
            assert failures, (label, 'survived')
            print(f'RED {label}: unique marker; runtime assertions: {", ".join(failures)}', flush=True)


def run(ctx):
    import os
    return subprocess.call([sys.executable, '-B', __file__,
                            *(['--mutate'] if ctx.mutate else [])],
                           cwd=ctx.root,
                           env=dict(os.environ, HOST32_RUNNERS=' '.join(ctx.runners)))


if __name__ == '__main__':
    main()
