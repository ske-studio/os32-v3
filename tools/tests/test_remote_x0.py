#!/usr/bin/env python3
"""Check the shared LAN operator constants and their consumers' C dialects."""
import argparse
import os
from pathlib import Path
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
CHECK = dict(name='remote-x0', tier=1, needs=[], budget=5,
    covers=['include/remote_control.h', 'net/host_mailbox.h', 'net/link.h',
            'tools/host_services.py', 'tools/tests/test_remote_x0.py'])

SOURCE = r'''
#include "remote_control.h"
static int same(const char *a, const char *b) {
    while (*a && *a == *b) { ++a; ++b; }
    return *a == *b;
}
int main(void) {
    if (REMOTE_SVC_RSHELL != 65534 || REMOTE_SVC_GSERIAL != 65533 ||
        REMOTE_SVC_PPC_DIAG != 65532 || REMOTE_SVC_REGISTRY_MAX != 65531 ||
        REMOTE_SVC_ALL != 65535 || REMOTE_SVC_RSHELL <= REMOTE_SVC_REGISTRY_MAX ||
        REMOTE_SVC_GSERIAL <= REMOTE_SVC_REGISTRY_MAX ||
        REMOTE_SVC_PPC_DIAG <= REMOTE_SVC_REGISTRY_MAX ||
        REMOTE_SVC_RSHELL >= REMOTE_SVC_ALL || REMOTE_SVC_GSERIAL >= REMOTE_SVC_ALL ||
        REMOTE_SVC_PPC_DIAG >= REMOTE_SVC_ALL) return 1;
    if (REMOTE_NOTIFY_EXECUTE != 1 || REMOTE_NOTIFY_PROBE != 2 ||
        REMOTE_RSHELL_COMMAND_MAX != 126 || REMOTE_GSERIAL_LINE_MAX != 128 ||
        REMOTE_NOTIFY_DATA_MAX != 236 || REMOTE_ID8_HEX_DIGITS != 8 ||
        REMOTE_RESULT_BODY_MAX != 8193) return 1;
    if (!same(REMOTE_REQ_RSH_READY, "RSH READY") || !same(REMOTE_REQ_RSH_OUT, "RSH OUT") ||
        !same(REMOTE_REQ_GS_OUT, "GS OUT") || !same(REMOTE_REQ_PDIAG_OUT, "PDIAG OUT") ||
        !same(REMOTE_REQ_LOG, "LOG") || !same(REMOTE_MODE_CUI, "cui") ||
        !same(REMOTE_MODE_GUI, "gui") || !same(REMOTE_MODE_DIAG, "diag") ||
        !same(REMOTE_STATE_IDLE, "idle") || !same(REMOTE_STATE_RUNNING, "running") ||
        !same(REMOTE_STATE_DONE, "done") || !same(REMOTE_STATE_NONE, "none")) return 1;
    return 0;
}
'''
MUTATIONS = (
    ('reserved-number', 'include/remote_control.h', '#define REMOTE_SVC_RSHELL       65534u', '#define REMOTE_SVC_RSHELL       65533u'),
    ('command-length', 'include/remote_control.h', '#define REMOTE_RSHELL_COMMAND_MAX 126', '#define REMOTE_RSHELL_COMMAND_MAX 127'),
    ('notify-length', 'net/link.h', '#define LINK_NOTIFY_DATA_MAX        236', '#define LINK_NOTIFY_DATA_MAX        235'),
)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--mutate', action='store_true')
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix='remote-x0-') as tmp:
        work = Path(tmp)
        for name in ('include/remote_control.h', 'net/host_mailbox.h', 'net/link.h'):
            target = work / name
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(ROOT / name, target)
        source = work / 'test.c'
        source.write_text(SOURCE)
        includes = ['-I' + str(work / 'include'), '-I' + str(ROOT / 'include'),
                    '-I' + str(ROOT / 'net'), '-I' + str(ROOT / 'sdk/include/os32')]
        # host_mailbox.h uses a relative SDK path.
        (work / 'sdk/include/os32').mkdir(parents=True)
        shutil.copyfile(ROOT / 'sdk/include/os32/os32_host_notice.h', work / 'sdk/include/os32/os32_host_notice.h')
        cross = str(Path(os.environ.get('CROSS_DIR', '/home/hight/opt/cross')) / 'bin/i386-elf-gcc')
        def build(cc, dialect, output, compile_only=False):
            cmd = [cc, '-std=' + dialect, '-Wall', '-Wextra', '-Werror', *includes, str(source)]
            if compile_only:
                cmd += ['-c']
            subprocess.run(cmd + ['-o', str(output)], check=True)
        for cc in ('gcc', cross):
            for dialect in ('c89', 'gnu11'):
                build(cc, dialect, work / 'check.o', True)
        exe = work / 'check'
        build('gcc', 'c89', exe)
        assert subprocess.run([str(exe)]).returncode == 0
        import sys
        sys.path.insert(0, str(ROOT / 'tools'))
        import host_services
        assert host_services.MAX_SERVICE_ID == 65531, 'registry overlaps reserved IDs'
        if args.mutate:
            for name, filename, old, new in MUTATIONS:
                path = work / filename
                original = path.read_text()
                assert original.count(old) == 1, name
                path.write_text(original.replace(old, new))
                build('gcc', 'c89', exe)
                rc = subprocess.run([str(exe)]).returncode
                assert rc == 1, (name, rc)
                print('RED ' + name + ' rc=1')
                path.write_text(original)
    print('PASS remote-x0')


def run(ctx):
    import sys
    return subprocess.call([sys.executable, '-B', 'tools/tests/test_remote_x0.py'] +
                           (['--mutate'] if ctx.mutate else []), cwd=ctx.root)


if __name__ == '__main__':
    main()
