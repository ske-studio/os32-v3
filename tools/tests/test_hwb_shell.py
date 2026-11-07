"""HW_BASIC_DESIGN §§2-7 real shell wiring and emitted serial records."""
CHECK = dict(name='hwb-shell-host', tier=1, budget=8.0, needs=[], covers=[
    'userland/shell/cmd_hw.c', 'userland/shell/hwb*.c', 'userland/shell/hwb.h',
    'userland/shell/shell.h', 'userland/shell/main.c', 'sdk/include/os32/*.h', 'include/types.h',
    'include/pci_dev_contract.h', 'kdebug/probe_wire.h', 'kdebug/hwb_wire.h', 'userland/tests/hw/*.hwb',
    'tools/rshell_hwb.py', 'tools/tests/hwb_shell_host.c', 'tools/tests/hwb_fake.h',
    'tools/tests/hwb_host.py', 'tools/tests/host32.py', 'tools/tests/mutpar.py',
    'tools/tests/kernel_includes.py'])
import json
import re
from argparse import Namespace
from pathlib import Path
import sys
import tempfile
import hwb_host

ROOT = hwb_host.ROOT
sys.path.insert(0, str(ROOT / 'tools'))
import rshell_hwb

FIXTURE = 'tools/tests/hwb_shell_host.c'
MUTATIONS = [
    ('log-to-serial', 'userland/shell/cmd_hw.c',
     'if (!hw_log_active || run != hw_log_run)', 'if (1)', 'log-file'),
    ('drop-file-terminal', 'userland/shell/cmd_hw.c',
     "for (const char *q = line; *q; q++) hw_log_append(*q);",
     "for (const char *q = line; !terminal && *q; q++) hw_log_append(*q);", 'log-file'),
    ('drop-initial', 'userland/shell/cmd_hw.c',
     'argv[path], initial, argc - first);', 'argv[path], initial, 0);', 'log-file'),
    ('drop-release', 'userland/shell/cmd_hw.c',
     'if (hw_session.stopped) hw_release();',
     'if (hw_session.stopped && hw_session.rc == HWB_IO) hw_release();', 'log-file'),
]


def inspect_records(output):
    runs = {}
    for row in output.splitlines():
        if row.startswith('HW '):
            run = row.split()[1]
            runs.setdefault(run, []).append(row)
    failures = []
    unresolved_ops = set()
    for run, rows in runs.items():
        body = '\n'.join(rows)
        terminal, errors = rshell_hwb.inspect(body)
        if errors:
            failures.extend(run + ': ' + e for e in errors)
        if any('addr=UNRESOLVED' in row for row in rows):
            if terminal != 'ARG':
                raise AssertionError('unresolved regression must stop with ARG: ' + body)
            for row in rows:
                if 'addr=UNRESOLVED' in row:
                    unresolved_ops.add(next(t for t in row.split() if t.startswith('op=')))
                if ' phase=end ' in row and 'addr=UNRESOLVED' in row:
                    corrupt = body.replace(row, row.replace('rc=ARG', 'rc=OK'))
                    if not rshell_hwb.inspect(corrupt)[1]:
                        raise AssertionError('unresolved successful end accepted: ' + row)
            # Feed the actual cmd_hw/exec/log output through capture and status
            # handling as well: ARG must propagate as 2, with the log preserved.
            with tempfile.TemporaryDirectory(prefix='hwb-capture-') as folder:
                capture = Path(folder) / 'capture'
                args = Namespace(line=['mmioread8', 'phys', 'missing', '->', 'x'],
                                 out=str(capture), timeout=1)
                sent = []
                def send(line, timeout):
                    sent.append(line)
                    return ('2\n' if line == 'echo $?' else body), None
                status = rshell_hwb.run(args, send, out=lambda text: None)
                if status != 2 or capture.read_text() != body or sent != [
                        'hw mmioread8 phys missing -> x', 'echo $?']:
                    raise AssertionError('real unresolved capture failed: ' + body)
        # Removing a begin leaves an end-only record (supported by collector).
        # Removing every end must leave its begin pending at the next record.
        # Check that local transition (renumbered from 1) rather than rescan
        # the entire run for every deletion: palette loops emit 28k+ rows.
        # The complete unmodified run was checked above.
        for i, row in enumerate(rows):
            if ' phase=end ' in row:
                if any(' op=mmioblock' + w + ' ' in row for w in ('8', '16', '32')):
                    continue  # Blocks intentionally emit exactly one end record.
                if i == 0 or ' phase=begin ' not in rows[i-1]:
                    raise AssertionError('end lacks adjacent begin: ' + row)
                begin = re.sub(r' seq=\d+ ', ' seq=1 ', rows[i-1])
                following = re.sub(r' seq=\d+ ', ' seq=2 ', rows[i+1])
                context = [begin, following]
                if i + 1 != len(rows) - 1:
                    context.append(rows[-1])
                _, missing = rshell_hwb.inspect('\n'.join(context))
                if not any(e.startswith('missing end seq=1 ') for e in missing):
                    raise AssertionError('missing line accepted: ' + row)
        _, missing = rshell_hwb.inspect('\n'.join(rows[:-1]))
        if 'missing terminal' not in missing:
            raise AssertionError('missing terminal accepted: ' + rows[-1])
    if failures:
        raise AssertionError('serial integration:\n' + '\n'.join(failures))
    if not runs:
        raise AssertionError('no real records')
    if unresolved_ops != {'op=mmioread8', 'op=mmiowrite8', 'op=poll'}:
        raise AssertionError('missing real unresolved regressions: ' + repr(unresolved_ops))


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
        before, file_records = result.stdout.split('FILE_BEGIN\n', 1)
        file_records, after = file_records.split('FILE_END\n', 1)
        if rshell_hwb.inspect(file_records)[1]:
            raise AssertionError('invalid real file records')
        inspect_records(before + after)
        if '--mutate' in sys.argv:
            return int(bool(hwb_host.mutations(FIXTURE, MUTATIONS, flags=flags)))
        return 0


def run(ctx):
    import os
    import subprocess
    return subprocess.call([sys.executable, '-B', 'tools/tests/test_hwb_shell.py',
                            *(['--mutate'] if ctx.mutate else [])], cwd=ctx.root,
                           env=dict(os.environ, HOST32_RUNNERS=' '.join(ctx.runners)), close_fds=False)


if __name__ == '__main__':
    sys.exit(main())
