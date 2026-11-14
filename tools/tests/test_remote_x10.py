#!/usr/bin/env python3
CHECK = dict(name='remote-x10', tier=1, needs=[], budget=12,
    covers=['userland/shell/rshell_lan.c', 'userland/shell/shell.h',
            'include/remote_control.h', 'include/con_sink.h',
            'net/host_mailbox.h', 'net/link.h', 'include/net_eth.h', 'include/types.h',
            'userland/lib/host/libos32host.h', 'sdk/include/os32/os32api.h',
            'sdk/include/os32/ls.h', 'sdk/include/os32/os32_kapi_shared.h',
            'sdk/include/os32/os32_kapi_generated.h', 'sdk/include/os32/os32_host_notice.h',
            'tools/tests/remote_x10_host.c', 'tools/tests/test_remote_x10.py',
            'tools/tests/host32.py'])
import argparse
from pathlib import Path
import subprocess
import tempfile
import sys
import host32

ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / 'userland/shell/rshell_lan.c'


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--mutate', action='store_true')
    args = parser.parse_args()
    source = SOURCE.read_text()
    # Keep quoted relative includes bound to their original source directory.
    source = source.replace('"../../include/', '"' + str(ROOT / 'include') + '/')
    source = source.replace('"../lib/host/', '"' + str(ROOT / 'userland/lib/host') + '/')
    with tempfile.TemporaryDirectory(prefix='remote-x10-') as td:
        path = Path(td)
        def case(text, expect_pass):
            src, exe = path / 'lan.c', path / 'test'
            src.write_text(text)
            subprocess.run(['cc', '-std=gnu11', '-Wall', '-Wextra', '-Werror',
                '-D__cdecl=', '-I' + str(ROOT / 'sdk/include/os32'),
                '-I' + str(ROOT / 'userland/shell'), '-I' + str(ROOT / 'include'),
                str(src), str(ROOT / 'tools/tests/remote_x10_host.c'), '-o', str(exe)], check=True)
            result = host32.run([str(exe)], capture_output=True, text=True)
            if expect_pass:
                assert result.returncode == 0, result.stdout + result.stderr
                print(result.stdout, end='')
            else:
                assert result.returncode == 1 and 'FAIL line' in result.stderr, result.stdout + result.stderr
        case(source, True)
        if args.mutate:
            for name, old, new in [
                ('duplicate ID executes', 'if (have_id && id <= last_id)', 'if (have_id && id < last_id)'),
                ('drop sent as zero', 'id, dropped, (unsigned int)result_len', 'id, dropped * 0, (unsigned int)result_len'),
                ('missing EOT', 'result[result_len++] = 0x04;', 'result[result_len++] = 0x00;')]:
                assert source.count(old) == 1, old
                case(source.replace(old, new), False)
                print('RED runtime:', name)


def run(ctx):
    return subprocess.call([sys.executable, '-B', __file__] + (['--mutate'] if ctx.mutate else []), cwd=ctx.root)


if __name__ == '__main__':
    main()
