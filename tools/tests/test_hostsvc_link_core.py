"""A8 real link.c TX/credit/tick deadline integration, libc-free ILP32."""
CHECK = dict(name='hostsvc-link-core', tier=1, needs=[], budget=5,
    covers=['net/link.c', 'net/nic.h', 'net/link.h', 'net/link_host_api.inc',
            'net/link_rx.inc', 'net/link_selftest.inc', 'lib/le_internal.h',
            'include/types.h', 'include/net_eth.h', 'include/kprintf.h',
            'lib/kstring.h', 'drivers/net/ne2000/ne2000.h',
            'sdk/include/os32/os32_kapi_shared.h', 'build/config.mk',
            'tools/tests/kernel_includes.py', 'tools/tests/host32.py',
            'tools/tests/hostsvc_link_core_host.c'])
import argparse
from pathlib import Path
import shutil
import subprocess
import tempfile
import host32
from kernel_includes import include_flags
ROOT = Path(__file__).resolve().parents[2]
ANCHOR = '    link_host_deadlines();'


def build(folder, mutant=False):
    folder.mkdir()
    shutil.copytree(ROOT / 'net', folder / 'net')
    core = folder / 'net/link.c'
    source = core.read_text()
    assert source.count(ANCHOR) == 1
    if mutant:
        core.write_text(source.replace(ANCHOR, '    /* deadline hook omitted */'))
    exe = folder / 'probe'
    subprocess.run(['gcc', '-std=gnu11', '-m32', '-march=i386', '-O2',
        '-Wall', '-Wextra', '-Werror', '-ffreestanding', '-nostdlib', '-static',
        '-fno-pie', '-no-pie', '-fno-stack-protector', '-ffunction-sections',
        '-fdata-sections', '-Wl,--gc-sections', '-I' + str(folder),
        *include_flags(ROOT), str(ROOT / 'tools/tests/hostsvc_link_core_host.c'),
        '-o', str(exe)], check=True)
    return exe


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--mutate', action='store_true')
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix='hostsvc-core-') as tmp:
        folder = Path(tmp)
        host32.run([str(build(folder / 'control'))], check=True)
        print('GREEN A8 shared TX/credit, init without tick registration, deadline before RTO/wrap/isolation/reuse')
        if args.mutate:
            rc = host32.run([str(build(folder / 'mutant', True))]).returncode
            assert rc == 1, rc
            print('RED runtime: A8 deadline hook omitted (compiled successfully)')


if __name__ == '__main__':
    main()


def run(ctx):
    return subprocess.call(['python3', '-B', __file__,
        *(['--mutate'] if ctx.mutate else [])], cwd=ctx.root)
