#!/usr/bin/env python3
"""Compile the isolated G1 module against the real generated KAPI host adapter."""
import argparse
import os
from pathlib import Path
import subprocess
import sys
import tempfile

import os32api_host

ROOT = Path(__file__).resolve().parents[2]
CHECK = dict(name='remote-g1', tier=1, needs=[], budget=10, covers=[
    'userland/gshell/src/gserial_link.rs', 'userland/gshell/host/gserial_link_tests.rs',
    'userland/gshell/build.rs', 'include/remote_control.h', 'net/link.h',
    'exec/shell_service.h', 'sdk/include/os32/os32_gui_shared.h',
    'sdk/include/os32/os32_kapi_shared.h', 'sdk/include/os32/os32_host_notice.h', 'sdk/rust/os32api/**',
    'tools/tests/os32api_host.py', 'tools/tests/test_remote_g1.py'])


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--mutate', action='store_true')
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix='remote-g1-') as tmp:
        out = Path(tmp)
        os32api_host.build(out)
        env = dict(os.environ, OUT_DIR=str(out), CARGO_MANIFEST_DIR=str(ROOT/'userland/gshell'))
        subprocess.run(['rustc', '--edition=2021', str(ROOT/'userland/gshell/build.rs'), '-o', str(out/'remote-contract-gen')], check=True)
        subprocess.run([str(out/'remote-contract-gen')], env=env, check=True, stdout=subprocess.DEVNULL)
        source = ROOT/'userland/gshell/src/gserial_link.rs'
        tests = ROOT/'userland/gshell/host/gserial_link_tests.rs'

        def run(product, name):
            root = out/(name+'.rs')
            module = out/(name+'_product.rs')
            module.write_text(product.read_text() + f'\n#[cfg(test)] #[path="{tests}"] mod tests;\n')
            root.write_text(f'#[path="{module}"] mod link;\n')
            exe = out/name
            subprocess.run(['rustc', '--edition=2021', '--test', str(root), '-L', str(out),
                            '--extern', 'os32api='+str(out/'libos32api.rlib'), '-o', str(exe)], env=env, check=True)
            return subprocess.run([str(exe), '--test-threads=1'], capture_output=True, text=True)

        control = run(source, 'control')
        print(control.stdout, end='')
        assert control.returncode == 0, control.stderr
        if args.mutate:
            original = source.read_text()
            for name, old, new in [
                ('notice-drop-reused', 'if self.h >= 0 && self.sending_notice { self.notice_drop = 0; }', 'if false { self.notice_drop = 0; }'),
                ('stale-keeps-id', 'self.last = None; self.done = false; self.frozen = false; self.len = 0;', 'self.done = false; self.frozen = false; self.len = 0;'),
                ('again-no-ready', 'if rc == OS32_ERR_AGAIN && now.wrapping_sub(self.retry)', 'if false && now.wrapping_sub(self.retry)'),
                ('generation-no-reset-ready', 'if reset_ready {', 'if false {'),
                ('wire-too-short', 'len > WIRE_MAX', 'len > REMOTE_GSERIAL_LINE_MAX as usize'),
                ('other-service', 'n.service != REMOTE_SVC_GSERIAL as u16', 'false'),
                ('missing-end', 'if has_end(&self.body[..self.len], &self.id)', 'if true'),
            ]:
                assert original.count(old) == 1, ('non-unique mutation anchor', name)
                mutant = out/(name+'_source.rs')
                mutant.write_text(original.replace(old, new))
                result = run(mutant, name)
                assert result.returncode == 101 and 'test result: FAILED' in result.stdout, (name, result.stdout, result.stderr)
                print(f'RED {name} rc={result.returncode}')
    return 0


def run(ctx):
    cmd = [sys.executable, '-B', 'tools/tests/test_remote_g1.py']
    if ctx.mutate:
        cmd.append('--mutate')
    return subprocess.call(cmd, cwd=ctx.root)


if __name__ == '__main__':
    raise SystemExit(main())
