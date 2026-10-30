"""Compile the real shell command with only its KAPI endpoints replaced."""
CHECK = dict(name='recover-cli-host', tier=1,
    covers=['userland/shell/cmd_recover.c', 'userland/shell/shell.h', 'kdebug/recover_wire.h', 'tools/tests/recover_cli_host.c', 'tools/tests/mutpar.py'],
    needs=[], budget=1.3162)

import os
from pathlib import Path
import subprocess
import sys
import tempfile
import mutpar

ROOT = Path(__file__).resolve().parents[2]
SOURCE = "userland/shell/cmd_recover.c"
MUTATIONS = [
    ("route_cli_approval_unchecked", '!str_eq(argv[3], "--approve")', '0'),
]

def command(binary):
    return [os.environ.get("HOST_CC", "gcc"), "-std=gnu11", "-Wall", "-Wextra", "-Werror",
            str(ROOT / "tools/tests/recover_cli_host.c"), "-o", str(binary)]

def one(item):
    name, before, after = item
    source = (ROOT / SOURCE).read_text()
    if before and source.count(before) != 1:
        return name + ': missing/ambiguous anchor', 1
    with tempfile.TemporaryDirectory(prefix="recover-cli-mut-") as temp:
        binary = Path(temp) / "cli"
        try:
            mutpar.build_in_tree(ROOT, temp,
                {SOURCE: source.replace(before, after, 1) if before else source},
                [command(binary)], capture_output=True)
        except subprocess.CalledProcessError as exc:
            return name + ': COMPILE FAIL ' + str(exc.stderr), 1
        result = subprocess.run([str(binary)], capture_output=True, timeout=30)
        return name + (': GREEN' if result.returncode == 0 else ': RED'), int(result.returncode == 0)

def main():
    with tempfile.TemporaryDirectory(prefix="recover-cli-") as temp:
        binary = Path(temp) / "cli"
        subprocess.run(command(binary), check=True)
        subprocess.run([str(binary)], check=True)
    if '--mutate' in sys.argv:
        return int(bool(mutpar.run_with_control(one, MUTATIONS, ('control', '', ''))))
    return 0

if __name__ == '__main__':
    raise SystemExit(main())


def run(ctx) -> int:
    import os
    import shlex
    import subprocess
    base_env = dict(os.environ, HOST32_RUNNERS=' '.join(ctx.runners))
    env = dict(base_env)
    argv = ['python3', '-B', 'tools/tests/test_recover_cli.py', *shlex.split(('--mutate' if ctx.mutate else ''))]
    rc = subprocess.call(argv, cwd=ctx.root, env=env, close_fds=False)
    if rc:
        return rc
    return 0
