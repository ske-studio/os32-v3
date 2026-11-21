"""Run one inventory partition in the PPC shard's toolchain environment."""
import os
from pathlib import Path
import subprocess
import sys
import tempfile


def run_group(ctx, group):
    env = os.environ.copy()
    for key in ('BUILD_OUT', 'KERNEL_CFLAGS', 'INC_KERNEL', 'C_STD',
                'SQLITE_PROTO_OUT', 'MAKEFLAGS', 'MFLAGS', 'MAKELEVEL'):
        env.pop(key, None)
    out = Path(tempfile.mkdtemp(prefix='root-lz4-' + group + '-'))
    env['TMPDIR'] = str(out)
    print(f'inventory evidence={out}', flush=True)
    subprocess.run([sys.executable, '-B', 'tools/ppc/root_lz4_inventory.py',
                    '--group', group, '--out', str(out)], cwd=ctx.root, env=env, check=True)
    if group == 'boot':
        subprocess.run([sys.executable, '-B', 'tools/ppc/root_lz4_layout_host.py',
                        *(['--mutate'] if ctx.mutate else [])], cwd=ctx.root, env=env, check=True)
    return 0
