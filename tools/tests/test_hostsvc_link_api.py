"""A11 real API/deadline hooks; no libc, explicit ILP32 runner."""
CHECK = dict(name='hostsvc-link-api', tier=1, needs=[], budget=3.6,
    covers=['net/nic.h', 'net/link.c', 'net/link.h', 'net/link_notify.h', 'net/link_host_api.inc',
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

MUTATIONS = [(ANCHOR, "if (1) continue;", "deadline disabled"),
    ('tok_eq(req, a0, b0, "os32.clip")', "0", "named clip body disabled"),
    ('tok_eq(req, a0, b0, "os32.print")', "0", "named print body disabled")]

def build(folder, mutation=None):
    folder.mkdir()
    shutil.copytree(ROOT / 'net', folder / 'net')
    api = folder / 'net/link_host_api.inc'
    text = api.read_text()
    if mutation:
        old, new, _ = mutation
        assert text.count(old) == 1
        api.write_text(text.replace(old, new))
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
            for i, mutation in enumerate(MUTATIONS):
                rc = host32.run([str(build(folder / f'mutant{i}', mutation))]).returncode
                assert rc == 1, (mutation[2], rc)
                print('RED runtime:', mutation[2], '(compiled successfully)')
if __name__ == '__main__': main()
def run(ctx):
    return subprocess.call(['python3', '-B', __file__, *(['--mutate'] if ctx.mutate else [])], cwd=ctx.root)
