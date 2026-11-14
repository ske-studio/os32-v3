"""Shared environment gate for the independently registered PPC tests."""
import os
from pathlib import Path
import shutil
import subprocess
import time


def run_test(ctx, source, dependencies):
    cross = Path(os.environ.get('PPC_CROSS_DIR', '/home/hight/opt/cross-ppc'))
    required = [str(cross / ('bin/powerpc-elf-' + tool))
                for tool in ('gcc', 'nm', 'objdump', 'ld', 'objcopy')]
    required += dependencies
    missing = [tool for tool in required
               if not (Path(tool).is_file() if tool.startswith('/') else shutil.which(tool))]
    if missing:
        print(('FAIL' if os.environ.get('OS32_REQUIRE_PPC') == '1' else 'SKIP')
              + ' ' + source + ': unavailable: ' + ', '.join(missing), flush=True)
        return 1 if os.environ.get('OS32_REQUIRE_PPC') == '1' else 0
    # Every test may invoke a host QEMU runner; honor the shared-machine load gate.
    while os.getloadavg()[0] >= 4:
        print('Waiting for load < 4 before ' + source, flush=True)
        time.sleep(10)
    env = dict(os.environ)
    for key in ('BUILD_OUT', 'KERNEL_CFLAGS', 'INC_KERNEL', 'C_STD',
                'SQLITE_PROTO_OUT', 'MAKEFLAGS', 'MFLAGS', 'MAKELEVEL'):
        env.pop(key, None)
    return subprocess.call(['python3', '-B', source] +
                           (['--mutate'] if ctx.mutate else []), cwd=ctx.root, env=env)
