#!/usr/bin/env python3
CHECK = dict(name='machcfg-host', tier=1, needs=[], budget=5,
    covers=['include/machcfg_contract.h', 'include/types.h', 'include/callconv.h',
            'init/sysconfig.c', 'kernel/machcfg.c', 'tools/tests/machcfg_host.c',
            'include/kprintf.h', 'include/console.h', 'sdk/include/os32/*.h',
            'platform/pc98/platform_console.h', 'lib/kprintf.c', 'lib/kprintf_format.inc',
            'tools/tests/host32.py', 'tools/tests/cc_cache.py'],
    basis=['docs/decisions/settings/MACHCFG_DESIGN.md §2, §3, §7'])


def run(ctx):
    import os
    import re
    import subprocess
    import sys
    import tempfile
    from pathlib import Path
    sys.path.insert(0, str(ctx.root / 'tools/tests'))
    import host32
    root = Path(ctx.root)
    match = re.search(r'^#define\s+SYSCONFIG_MAX\s+(\d+)\s*$',
                      (root / 'init/sysconfig.c').read_text(), re.M)
    if not match:
        raise RuntimeError('SYSCONFIG_MAX definition missing')
    source = (root / 'kernel/machcfg.c').read_text()
    header = (root / 'include/machcfg_contract.h').read_text()
    mutations = [
        ('capability', 'if (!machine_seed.accepts_nap7450)', 'if (0)'),
        ('report-once', 'reported = 1;', ''),
        ('report-before-finish', '!finished || reported', 'reported'),
        ('report-n-a', 'if (machine_seed.accepts_nap7450 == 0)', 'if (0)'),
        ('freeze', 'if (finished)', 'if (0)'),
        ('begin-defaults', 'current = seed.defaults;', 'current = (struct machcfg){0, 0};'),
        ('capability-default', 'next.nap7450 = machine_seed.defaults.nap7450;', 'next.nap7450 = 0;'),
    ]
    with tempfile.TemporaryDirectory(dir=os.environ.get('TMPDIR')) as tmp:
        tmp = Path(tmp)
        formatter = (root / 'lib/kprintf.c').read_text().replace(
            '#include "kprintf_format.inc"', (root / 'lib/kprintf_format.inc').read_text())
        (tmp / 'format.c').write_text(formatter)
        def check(src, hdr):
            (tmp / 'machcfg.c').write_text(src)
            (tmp / 'machcfg_contract.h').write_text(hdr)
            exe = tmp / 'test'
            cmd = ['gcc', '-m32', '-std=gnu11', '-Wall', '-Wextra', '-Werror',
                   '-ffreestanding', '-fno-pie', '-no-pie', '-nostdlib',
                   '-DTEST_SYSCONFIG_MAX=' + match.group(1), '-I' + str(tmp),
                   '-I' + str(root / 'include'), str(tmp / 'machcfg.c'),
                   '-I' + str(root / 'platform/pc98'), str(tmp / 'format.c'),
                   str(root / 'tools/tests/machcfg_host.c'), '-o', str(exe)]
            built = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
            if built.returncode:
                return 'compile', built.stderr.decode()
            result = subprocess.run(host32.command([str(exe)]))
            host32.report_signal(result.returncode)
            return 'run', result.returncode
        phase, result = check(source, header)
        if phase != 'run' or result != 0:
            print('normal control failed:', phase, result)
            return 1
        print('normal control: GREEN')
        if ctx.mutate:
            for name, old, new in mutations:
                if source.count(old) != 1:
                    raise RuntimeError('mutation point changed: ' + name)
                phase, result = check(source.replace(old, new), header)
                if phase != 'run' or result == 0:
                    print(name, 'did not fail at runtime:', phase, result)
                    return 1
                print(name, 'RED')
            old = '#define MACHCFG_FILE_MAX (1024 - 1)'
            if header.count(old) != 1:
                raise RuntimeError('limit mutation point changed')
            phase, result = check(source, header.replace(old, '#define MACHCFG_FILE_MAX (1024 - 2)'))
            if phase != 'compile':
                print('limit mutation did not fail compilation')
                return 1
            print('limit RED (compile)')
    return 0


if __name__ == '__main__':
    import argparse
    from pathlib import Path
    from types import SimpleNamespace
    parser = argparse.ArgumentParser()
    parser.add_argument('--mutate', action='store_true')
    args = parser.parse_args()
    raise SystemExit(run(SimpleNamespace(root=Path(__file__).resolve().parents[2],
                                          mutate=args.mutate)))
