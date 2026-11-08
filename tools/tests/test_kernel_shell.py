"""Execute the real boot shell loop, before and after E02b extraction.

Only device/exec boundaries are faked; product control flow is compiled intact.
Mutation compile failures and signals are errors, never runtime RED evidence.
"""
CHECK = dict(name='kernel-shell-host', tier=1,
    covers=['init/kernel.c', 'tools/tests/kernel_shell_host.c', 'tools/tests/host32.py', 'tools/tests/kernel_includes.py', 'tools/tests/host_arch/arch_cpu.h', 'tools/tests/kernel_shell_arch/arch_io.h', 'init/kernel_*.inc', 'include/config.h', 'include/pc98.h', 'include/types.h', 'exec/exec.h', 'mm/shlib.h', 'mm/paging.h', 'sdk/include/os32/os32_kapi_shared.h'],
    needs=[], budget=3.1)

from pathlib import Path
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
    paths = [ROOT / 'init/kernel.c', *sorted((ROOT / 'init').glob('kernel_*.inc'))]
    return {path.name: path.read_text() for path in paths}


def build(work, sources):
    for name, source in sources.items():
        (work / name).write_text(source)
    exe = work / 'shell'
    host32.build(['gcc', '-m32', '-std=gnu11', '-O0', '-Wall', '-Wextra',
                  '-Werror', '-Wno-unused-function', '-Wno-unused-variable',
                  '-Wno-unused-but-set-variable', '-D__KERNEL_BUILD__',
                  '-static', '-fno-pie', '-no-pie', '-nostdlib',
                  '-ffreestanding', '-fno-builtin', '-fno-stack-protector',
                  '-ffunction-sections', '-fdata-sections', '-Wl,--gc-sections',
                  '-I' + str(work), '-I' + str(ROOT / 'tools/tests/kernel_shell_arch'),
                  '-I' + str(ROOT / 'tools/tests/host_arch'), '-I' + str(ROOT / 'init'),
                  *include_flags(ROOT),
                  str(ROOT / 'tools/tests/kernel_shell_host.c'), '-o', str(exe)],
                 check=True)
    return exe


def run_cases(exe):
    results = []
    for case in CASES:
        result = host32.run([str(exe), case], capture_output=True, text=True, timeout=10)
        assert result.returncode == 0 or (result.returncode == 1 and
                                          result.stdout.startswith("FAIL ")), (
            case, result.returncode, result.stdout, result.stderr)
        results.append((case, result.returncode, result.stdout + result.stderr))
    return results


def main():
    source = product_source()
    with tempfile.TemporaryDirectory(prefix='kernel-shell-') as td:
        work = Path(td)
        results = run_cases(build(work, source))
        assert all(rc == 0 for _, rc, _ in results), results
        print(f'GREEN {len(results)} real shell-loop cases (runner={host32.selected_runner()})', flush=True)
        if '--mutate' in sys.argv:
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
            if 'kernel_gui_shlib_ready(void)' in source['kernel_gui_shlib.inc']:
                mutations += [
                    ('gui-result', 'is_gui && !kernel_gui_shlib_ready()',
                     'is_gui && kernel_gui_shlib_ready()'),
                    ('fdd-layout-argument', 'kernel_shell_fdd_fallback(exec_layout_rejected());',
                     'kernel_shell_fdd_fallback(0);'),
                ]
            # Explicit identity control uses the very same build/runtime path.
            marker = 'gui_fault_streak++;'
            assert sum(text.count(marker) for text in source.values()) == 1
            assert all(rc == 0 for _, rc, _ in run_cases(build(work, source)))
            print('GREEN identity control', flush=True)
            for label, old, new in mutations:
                matches = [name for name, text in source.items() if old in text]
                assert len(matches) == 1 and source[matches[0]].count(old) == 1, (label, matches)
                changed = dict(source)
                changed[matches[0]] = changed[matches[0]].replace(old, new, 1)
                results = run_cases(build(work, changed))
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
