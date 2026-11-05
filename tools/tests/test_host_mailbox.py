#!/usr/bin/env python3
"""B3 NOTICE: real mailbox, bounded heap, token race and ALIVE snapshots.

NET_HOSTSVC_DESIGN §5/#4/#5/#12. Wire duplicate rejection belongs to B2;
this unit verifies that stage/commit and ALIVE never duplicate delivery.
"""
import argparse
import os
from pathlib import Path
import subprocess
import sys
import tempfile

import host32
from kernel_includes import include_flags

CHECK = dict(name='host-mailbox', tier=1, needs=[], covers=[
    'net/host_mailbox.c', 'net/host_mailbox.h',
    'sdk/include/os32/os32_host_notice.h', 'sdk/include/os32/os32_kapi_shared.h',
    'tools/tests/host_mailbox_host.c', 'tools/tests/hostsvc_fixture.c',
    'tools/tests/hostsvc_fixture.h', 'tools/tests/host32.py',
    'tools/tests/kernel_includes.py', 'build/config.mk',
    'include/io.h', 'lib/kstring.h', 'mm/kmalloc.h'], budget=10)
ROOT = Path(__file__).resolve().parents[2]
MUTATIONS = (
    ('overflow-count', '            dropped++;', '            dropped += 0;'),
    ('alive-enqueue', 'if (alive && same_alive)', 'if (alive && same_alive && 0)'),
    ('commit-serial', 'if (slots[order[i]].serial == serial)',
     'if (slots[order[i]].serial == serial || 1)'),
    ('commit-generation', 'generation == generation_now', '(generation == generation_now || 1)'),
    ('alive-consumed', 'latest_valid = 0;\n    }\n    irq_restore(flags);',
     'latest_valid = 0;\n        alive_state.valid = 0;\n    }\n    irq_restore(flags);'),
    ('alive-allocation', '        alive_state.observation++;',
     '        if (slots) alive_state.observation++;'),
    ('stage-consumes', '            rc = 0;',
     '            rc = 0;\n            if (count) remove_at(0);'),
    ('commit-resets-serial', 'latest_valid = 0;\n    }\n    irq_restore(flags);',
     'latest_valid = 0;\n        serial_now = 0;\n    }\n    irq_restore(flags);'),
)


def build(work, source):
    exe = work / 'mailbox'
    result = host32.build(['gcc', '-m32', '-std=gnu11', '-O0', '-Wall', '-Wextra',
        '-Werror', '-fno-pie', '-no-pie', '-fno-stack-protector', '-ffreestanding',
        '-nostdlib', '-static', '-I' + str(ROOT), *include_flags(ROOT),
        '-DPRODUCT_SOURCE="' + str(source) + '"',
        str(ROOT / 'tools/tests/host_mailbox_host.c'),
        str(ROOT / 'tools/tests/hostsvc_fixture.c'), '-o', str(exe)],
        capture_output=True, text=True)
    if result.returncode:
        raise RuntimeError(result.stderr)
    return exe


def runs(exe):
    return [host32.run([str(exe), *args], capture_output=True, timeout=30).returncode
            for args in ([], ['heap-failure'])]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--mutate', action='store_true')
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix='host-mailbox-') as tmp:
        work = Path(tmp)
        source = ROOT / 'net/host_mailbox.c'
        # Compile real privileged IO path with the kernel compiler as well.
        cross = Path(os.environ.get('CROSS_DIR', '/home/hight/opt/cross')) / 'bin/i386-elf-gcc'
        subprocess.run([str(cross), '-std=gnu11', '-O2', '-Wall', '-Wextra', '-Werror',
            '-ffreestanding', *include_flags(ROOT), '-c', str(source),
            '-o', str(work / 'target.o')], check=True)
        assert runs(build(work, source)) == [0, 0], 'normal control failed'
        print('PASS mailbox/alive/race + allocation failure (runner=' +
              host32.selected_runner() + ')', flush=True)
        if args.mutate:
            original = source.read_text()
            for name, old, new in MUTATIONS:
                assert original.count(old) == 1, (name, 'anchor must be unique')
                mutant = work / 'host_mailbox.c'
                mutant.write_text(original.replace(old, new))
                rc = runs(build(work, mutant))
                assert all(code >= 0 for code in rc) and any(code == 1 for code in rc), (name, rc)
                print('RED ' + name + ' rc=' + str(rc), flush=True)
    return 0


def run(ctx) -> int:
    for index, runner in enumerate(ctx.runners):
        env = dict(os.environ, HOST32_RUNNERS=runner)
        argv = [sys.executable, '-B', 'tools/tests/test_host_mailbox.py']
        if ctx.mutate and index == 0:
            argv.append('--mutate')
        rc = subprocess.call(argv, cwd=ctx.root, env=env)
        if rc:
            return rc
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
