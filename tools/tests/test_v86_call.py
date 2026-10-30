"""T7a-2a: genuine admission/staging + T2 uaccess; ILP32 CPU boundaries.
Normal controls always run before compiled runtime-RED mutations.
"""
CHECK = dict(name='v86-call-host', tier=1,
    covers=['exec/v86_call.c', 'exec/redir_access.c', 'exec/exec.c', 'kernel/v86/v86.c', 'kernel/v86/v86_module.c', 'userland/tests/kstr_v86.c', 'tools/tests/v86_call_host.c', 'tools/tests/host32.py', 'tools/tests/kernel_includes.py', 'exec/exec_image.inc', 'exec/exec_kapi_init.inc', 'kernel/v86/v86_gp_emulate.inc'],
    needs=[], budget=1.61671)

from kernel_includes import include_flags
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import host32

ROOT = Path(__file__).resolve().parents[2]
MUTANTS = [
    ('redir', 'if (!dst[i]) { ok = 1; break; }',
     'if (!dst[i] || i + 1 == cap) { ok = 1; break; }', 'missing-terminator'),
    ('call', 'call.path[i], VFS_MAX_PATH)) {',
     'call.path[i], VFS_MAX_PATH) && i == 0) {', 'second-path-copy-failure'),
    ('call', 'if (!mailbox) { *result = OS32_ERR_NOSPC; return 1; }',
     'if (0) { *result = OS32_ERR_NOSPC; return 1; }', 'boot-mailbox-allocation-failure'),
    ('call', 'i < paths;', 'i < paths && i == 0;', 'boot2-second-input-unchecked'),
    ('guest', 'api->v86_boot2("/__t4b8b_missing__", bad);',
     'api->v86_boot2("/__t4b8b_missing__", 0);', 'boot2-guest-second-input-NULL'),
    ('guest', "memset(bad, '!', 16);", "memset(bad, 0, 16);",
     'boot2-guest-second-input-terminated'),
    ('call', 'call.pinned = 0;', '(void)0;', 'pin-release'),
]


def main():
    sources = {'call': (ROOT / 'exec/v86_call.c').read_text(),
               'redir': (ROOT / 'exec/redir_access.c').read_text(),
               'guest': (ROOT / 'userland/tests/kstr_v86.c').read_text().replace(
                   '#include <string.h>', '')}
    with tempfile.TemporaryDirectory(prefix='v86-call-') as td:
        folder = Path(td)
        def run(parts, red=False):
            for key, body in parts.items():
                (folder / {'call': 'v86_call_source.c', 'redir': 'redir_source.c',
                           'guest': 'guest_host_source.c'}[key]).write_text(body)
            exe = folder / 'entry'
            subprocess.run(['gcc', '-m32', '-std=gnu11', '-Os', '-Wall', '-Wextra',
                            '-Werror', '-ffreestanding', '-fno-pie', '-fno-stack-protector',
                            '-ffunction-sections', '-fdata-sections', '-D__cdecl=',
                            *include_flags(ROOT),
                            '-I' + str(folder), '-nostdlib', '-static', '-no-pie',
                            '-Wl,--gc-sections', ROOT / 'tools/tests/v86_call_host.c',
                            '-o', exe], check=True)
            for runner in os.environ.get('HOST32_RUNNERS', 'qemu').split():
                result = host32.run([str(exe)], runner=runner, capture_output=True,
                                    text=True, timeout=host32.RUN_TIMEOUT)
                if red:
                    assert result.returncode == 1 and 'FAIL ' in result.stdout, result
                else:
                    assert result.returncode == 0 and 'GREEN ' in result.stdout, result
                print(runner, result.stdout.strip(), flush=True)
        run(sources)
        if '--mutate' in sys.argv:
            for key, old, new, label in MUTANTS:
                assert sources[key].count(old) == 1, old
                changed = dict(sources)
                changed[key] = changed[key].replace(old, new)
                run(changed, red=True)
                print('MUTATE RED (compiled runtime):', label, flush=True)
    return 0

if __name__ == '__main__':
    sys.exit(main())


def run(ctx) -> int:
    import os
    import shlex
    import subprocess
    base_env = dict(os.environ, HOST32_RUNNERS=' '.join(ctx.runners))
    env = dict(base_env)
    argv = ['python3', '-B', 'tools/tests/test_v86_call.py', *shlex.split(('--mutate' if ctx.mutate else ''))]
    rc = subprocess.call(argv, cwd=ctx.root, env=env, close_fds=False)
    if rc:
        return rc
    return 0
