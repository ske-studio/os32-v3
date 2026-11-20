#!/usr/bin/env python3
CHECK = dict(name='machcfg-host', tier=1, needs=[], budget=5,
    covers=['include/machcfg_contract.h', 'include/types.h', 'include/callconv.h',
            'kernel/machcfg_root.c', 'include/config.h', 'lib/kstring.h',
            'init/sysconfig.c', 'init/sysconfig.h', 'fs/vfs.h', 'lib/kstring_c.c',
            'tools/tests/kernel_includes.py', 'kernel/machcfg.c', 'platform/pc98/machcfg.c', 'tools/tests/machcfg_host.c',
            'include/kprintf.h', 'include/console.h', 'sdk/include/os32/*.h',
            'platform/pc98/kernel.c', 'platform/pc98/platform_console.h', 'lib/kprintf.c', 'lib/kprintf_format.inc',
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
    import kernel_includes
    import host32
    root = Path(ctx.root)
    match = re.search(r'^#define\s+SYSCONFIG_MAX\s+(\d+)\s*$',
                      (root / 'init/sysconfig.c').read_text(), re.M)
    if not match:
        raise RuntimeError('SYSCONFIG_MAX definition missing')
    source = (root / 'kernel/machcfg.c').read_text()
    header = (root / 'include/machcfg_contract.h').read_text()
    pc98 = (root / 'platform/pc98/machcfg.c').read_text()
    loader = (root / 'kernel/machcfg_root.c').read_text()
    boot = (root / 'platform/pc98/kernel.c').read_text()
    calls = ('kernel_boot_cpu(mem_kb, &boot);', 'machcfg_begin(platform_machcfg_seed());',
             'vfs_mount("/", boot.root_dev, boot.root_fs);', 'boot_platform_run(BOOT_AUTO_MOUNT, &boot);',
             'machcfg_load_root();', 'machcfg_report("pc98", 0, 0);', 'boot_platform_run(BOOT_CLOCK, &boot);')

    def boot_order(body):
        positions = [body.find(call) for call in calls]
        return all(body.count(call) == 1 for call in calls) and positions == sorted(positions)

    if not boot_order(boot):
        raise RuntimeError('PC-98 boot configuration call order')
    if ctx.mutate:
        mount = 'rc = ' + calls[2]
        if boot.count(mount) != 1:
            raise RuntimeError('mount mutation point changed')
        early = boot.replace(calls[4], '').replace(mount, calls[4] + '\n        ' + mount)
        if early.count(calls[4]) != 1:
            raise RuntimeError('relocated load count changed')
        if boot_order(early):
            raise RuntimeError('load before mount survived')
        print('load before mount RED')
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
        parser = (root / 'init/sysconfig.c').read_text()
        def check(src, hdr, cfg=parser, platform=pc98, root_src=loader):
            (tmp / 'machcfg_root.c').write_text(root_src)
            (tmp / 'sysconfig.c').write_text(cfg)
            (tmp / 'pc98.c').write_text(platform)
            (tmp / 'machcfg.c').write_text(src)
            (tmp / 'machcfg_contract.h').write_text(hdr)
            exe = tmp / 'test'
            cmd = ['gcc', '-m32', '-std=gnu11', '-Wall', '-Wextra', '-Werror',
                   '-ffreestanding', '-fno-pie', '-no-pie', '-nostdlib',
                   '-DTEST_SYSCONFIG_MAX=' + match.group(1), '-I' + str(tmp),
                   '-I' + str(root / 'include'), str(tmp / 'machcfg.c'), str(tmp / 'pc98.c'),
                   str(tmp / 'machcfg_root.c'),
                   '-I' + str(root / 'platform/pc98'), str(tmp / 'format.c'),
                   *kernel_includes.include_flags(root), str(tmp / 'sysconfig.c'),
                   str(root / 'lib/kstring_c.c'), str(root / 'tools/tests/machcfg_host.c'), '-o', str(exe)]
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
            for name, old, new in [
                ('perf-default', '"perf.profile", -1', '"perf.profile", 0'),
                ('nap-default', '"ppc7450.nap", -1', '"ppc7450.nap", 0'),
                ('perf-range', 'x <= 2', 'x <= 3'),
                ('nap-range', 'x <= 1', 'x <= 2'),
                ('oversize', 'n < 0 ? MACHCFG_FILE_BIG : MACHCFG_FILE_NONE', 'MACHCFG_FILE_NONE'),
                ('temp-end', '\n    sysconfig_temp_end();', ''),
            ]:
                if loader.count(old) != 1:
                    raise RuntimeError('mutation point changed: ' + name)
                phase, result = check(source, header, root_src=loader.replace(old, new))
                if (phase, result) != ('run', 21):
                    print(name, phase, result)
                    return 1
                print(name, 'runtime RED')
            for name, old, new in [
                ('bare-cr-length', 'end < n && length', 'length'),
                ('bare-cr-integer', r"end - s >= 2 && *s == '\r' && s[1] == '\n'",
                 r"s < end && *s == '\r'"),
            ]:
                if parser.count(old) != 1:
                    raise RuntimeError('mutation point changed: ' + name)
                phase, result = check(source, header, cfg=parser.replace(old, new))
                if (phase, result) != ('run', 20):
                    print(name, phase, result)
                    return 1
                print(name, 'runtime RED')
            for name, old, new in mutations:
                if source.count(old) != 1:
                    raise RuntimeError('mutation point changed: ' + name)
                phase, result = check(source.replace(old, new), header)
                if phase != 'run' or result == 0:
                    print(name, 'did not fail at runtime:', phase, result)
                    return 1
                print(name, 'RED')
            for field in ('perf_request', 'nap7450', 'accepts_nap7450'):
                old = '.' + field + ' = 0'
                if pc98.count(old) != 1:
                    raise RuntimeError('PC-98 mutation point changed: ' + field)
                phase, result = check(source, header, platform=pc98.replace(old, '.' + field + ' = 1'))
                if phase != 'run' or result == 0:
                    print(field, 'did not fail at runtime:', phase, result)
                    return 1
                print('pc98-' + field, 'RED')
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
