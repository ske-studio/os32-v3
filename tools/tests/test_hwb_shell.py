"""HW_BASIC_DESIGN §§2-7 real shell wiring and emitted serial records."""
CHECK = dict(name='hwb-shell-host', tier=1, budget=8.0, needs=[], covers=[
    'userland/shell/cmd_hw.c', 'userland/shell/hwb*.c', 'userland/shell/hwb.h',
    'userland/shell/shell.h', 'userland/shell/main.c', 'sdk/include/os32/*.h', 'include/types.h',
    'include/pci_dev_contract.h', 'kdebug/hwb_wire.h', 'userland/tests/hw/*.hwb',
    'tools/rshell_hwb.py', 'tools/tests/hwb_shell_host.c', 'tools/tests/hwb_fake.h',
    'tools/tests/hwb_host.py', 'tools/tests/host32.py', 'tools/tests/mutpar.py',
    'tools/tests/kernel_includes.py'])
import json
from pathlib import Path
import sys
import tempfile
import hwb_host

ROOT = hwb_host.ROOT
sys.path.insert(0, str(ROOT / 'tools'))
import rshell_hwb

FIXTURE = 'tools/tests/hwb_shell_host.c'
MUTATIONS = [
    ('drop-initial', 'userland/shell/cmd_hw.c',
     'argv[2], initial, argc - 3);', 'argv[2], initial, 0);', 'load-lifetime'),
    ('drop-release', 'userland/shell/cmd_hw.c',
     'if (hw_session.stopped) hw_release();',
     'if (hw_session.stopped && hw_session.rc == HWB_IO) hw_release();', 'adapters'),
]


def inspect_records(output):
    runs = {}
    for row in output.splitlines():
        if row.startswith('HW '):
            run = row.split()[1]
            runs.setdefault(run, []).append(row)
    failures = []
    for run, rows in runs.items():
        body = '\n'.join(rows)
        _, errors = rshell_hwb.inspect(body)
        # Keep this as a real failing integration check until PM merges P9.
        if errors:
            failures.extend(run + ': ' + e for e in errors)
        # Removing a begin leaves an end-only record (supported by collector).
        # Removing any end or terminal must make the capture incomplete.
        for i, row in enumerate(rows):
            if ' phase=end ' in row or (' op=end ' in row and ' phase=' not in row):
                _, missing = rshell_hwb.inspect('\n'.join(rows[:i] + rows[i+1:]))
                if not missing:
                    raise AssertionError('missing line accepted: ' + row)
    if failures:
        raise AssertionError('serial integration:\n' + '\n'.join(failures))
    if not runs:
        raise AssertionError('no real records')


def main():
    with tempfile.TemporaryDirectory(prefix='hwb-shell-') as folder:
        tmp = Path(folder)
        # Only external libc declarations are replaced, as in sh_shell tests.
        (tmp / 'string.h').write_text('int strcmp(const char *, const char *);\n')
        scripts = ['struct script_data { const char *name, *source; u32 bytes; };',
                   'static const struct script_data scripts_data[] = {']
        for path in sorted((ROOT / 'userland/tests/hw').glob('*.hwb')):
            data = path.read_bytes().decode('ascii')
            scripts.append('{%s, %s, %d},' % (json.dumps(path.name), json.dumps(data), len(data)))
        (tmp / 'hwb_shell_scripts.h').write_text('\n'.join(scripts) + '\n};\n')
        flags = ['-I' + str(tmp), '-I' + str(ROOT / 'sdk/include/os32'), '-D__OS32_USERLAND__']
        hwb_host.build(ROOT, FIXTURE, tmp / 'host', flags=flags)
        result = hwb_host.execute(tmp / 'host')
        print(result.stdout, end='', flush=True)
        if result.returncode:
            raise AssertionError('shell control failed: ' + result.stderr)
        hwb_host.build(ROOT, FIXTURE, tmp / 'cross.o', cross=True, flags=flags)
        print('GREEN host ILP32, libc-free; i386-elf compile PASS', flush=True)
        serial_error = None
        try:
            inspect_records(result.stdout)
        except AssertionError as error:
            serial_error = error
        # Mutants are independent of the known P9 formatter merge. Always run
        # normal control first and retain the strict serial result afterwards.
        rc = 0
        if '--mutate' in sys.argv:
            rc = int(bool(hwb_host.mutations(FIXTURE, MUTATIONS, flags=flags)))
        if serial_error is not None:
            raise serial_error
        return rc


def run(ctx):
    import os
    import subprocess
    return subprocess.call([sys.executable, '-B', 'tools/tests/test_hwb_shell.py',
                            *(['--mutate'] if ctx.mutate else [])], cwd=ctx.root,
                           env=dict(os.environ, HOST32_RUNNERS=' '.join(ctx.runners)), close_fds=False)


if __name__ == '__main__':
    sys.exit(main())
