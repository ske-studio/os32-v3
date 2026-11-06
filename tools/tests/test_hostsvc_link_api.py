"""A11 real API/deadline hooks; no libc, explicit ILP32 runner."""
CHECK = dict(name='hostsvc-link-api', tier=1, needs=[], budget=5,
    covers=['net/nic.h', 'net/link.c', 'net/link.h', 'net/link_host_api.inc',
            'net/link_rx.inc', 'net/link_selftest.inc', 'lib/le_internal.h',
            'include/types.h', 'include/net_eth.h', 'include/kprintf.h',
            'lib/kstring.h', 'drivers/net/ne2000/ne2000.h',
            'kernel/irq/tick.h', 'platform/pc98/tick_order.h',
            'sdk/include/os32/os32_kapi_shared.h', 'build/config.mk',
            'tools/tests/kernel_includes.py', 'tools/tests/host32.py',
            'tools/tests/hostsvc_link_api_host.c'])
import argparse
from pathlib import Path
import shutil
import subprocess
import tempfile
import host32
from kernel_includes import include_flags
ROOT = Path(__file__).resolve().parents[2]
ANCHOR = 'if ((u32)(tick_count - d->progress_tick) < LINK_HOST_SILENCE_TICKS) continue;'

def build(folder, mutant=False):
    folder.mkdir()
    shutil.copytree(ROOT / 'net', folder / 'net')
    api = folder / 'net/link_host_api.inc'
    text = api.read_text()
    assert text.count(ANCHOR) == 1
    if mutant:
        api.write_text(text.replace(ANCHOR, 'if (1) continue;'))
    exe = folder / 'probe'
    subprocess.run(['gcc', '-std=gnu11', '-m32', '-O2', '-Wall', '-Wextra', '-Werror',
        '-ffreestanding', '-nostdlib', '-static', '-fno-pie', '-no-pie',
        '-fno-stack-protector', '-ffunction-sections', '-fdata-sections',
        '-Wl,--gc-sections', '-I' + str(folder), *include_flags(ROOT),
        str(ROOT / 'tools/tests/hostsvc_link_api_host.c'), '-o', str(exe)], check=True)
    return exe

def main():
    p = argparse.ArgumentParser(); p.add_argument('--mutate', action='store_true')
    args = p.parse_args()
    with tempfile.TemporaryDirectory(prefix='hostsvc-api-') as tmp:
        folder = Path(tmp)
        host32.run([str(build(folder / 'control'))], check=True)
        print('GREEN: eight slots, isolated expiry, progress, wrap, close/reuse, stale, IF')
        if args.mutate:
            rc = host32.run([str(build(folder / 'mutant', True))]).returncode
            assert rc == 1, rc
            print('RED runtime: deadline disabled (compiled successfully)')
if __name__ == '__main__': main()
def run(ctx):
    return subprocess.call(['python3', '-B', __file__, *(['--mutate'] if ctx.mutate else [])], cwd=ctx.root)
