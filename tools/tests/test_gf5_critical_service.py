"""T0c critical pause ownership and independent thermal lifetime, production assembly."""
CHECK = dict(name='gf5-critical-service-host', tier=1, budget=16.1,
             needs=['make:build/out/geforce/geforce_imports.h'],
             covers=['drivers/pci/pci_barsize.c', 'drivers/pci/pci_barsize.h', 'tools/tests/**', 'hwdev/**', 'drivers/video/geforce/**',
                     'include/**', 'kdebug/**', 'mm/pgalloc.h', 'mm/physmem.h',
                     'mm/paging.h', 'lib/kstring.h', 'drivers/pci/pci.h',
                     'drivers/pci/pci_decode.h', 'drivers/char/serial.h',
                     'drivers/base/dev.c', 'kernel/module/**',
                     'kernel/console/console_backend.h', 'arch/x86/kernel/idt.h',
                     'sdk/include/os32/os32_kapi_shared.h', 'fs/fd_redirect.h',
                     'fs/vfs.c', 'platform/pc98/boot_names.c'])
from pathlib import Path
import argparse
import subprocess
import tempfile
import host32
from kernel_includes import include_flags
import test_gf_display as display

ROOT = Path(__file__).resolve().parents[2]
PRODUCT = 'hwdev/gf/feature5_service.c'
MUTATIONS = [
    ('critical-drain', 'rc=g->links.drain_pause(g->links.owner);', 'rc=0;'),
    ('critical-resume', 'rc=g->links.unpause(g->links.owner);', 'rc=0;'),
    ('critical-owner', 'if (!live) return;', 'if (!live && false) return;'),
    ('critical-saved', '!g->display->ready || g->display->changing || g->display->saved_closed)', '!g->display->ready || g->display->changing)'),
    ('critical-stop', 'gf5_stop_request(g,g->last_rc,g->temperature,rc);', '(void)g;'),
]



def run_case(tree, directory):
    exe = directory/'critical-service'
    fixture = tree/'tools/tests/gf5_critical_service_host.c'
    sources = display.gf4_sources(tree, 'scanlog', fixture)
    flags = ['gcc', '-m32', '-march=i386', '-std=gnu11', '-O2', '-Wall', '-Wextra',
             '-Werror', '-Werror=vla', '-Wno-unused-parameter', '-ffreestanding',
             '-fno-pie', '-fno-stack-protector', '-ffunction-sections', '-fdata-sections',
             '-nostdlib', '-static', '-no-pie', '-Wl,--gc-sections',
             '-D__KERNEL_BUILD__', '-DGF_PRODUCTION_MODULE', '-DGF4_REAL_DISPLAY',
             '-I'+str(tree/'tools/tests/host_arch'), *include_flags(tree),
             '-I'+str(tree), '-I'+str(ROOT/'build/out/geforce')]
    subprocess.run([*flags, str(fixture), *[str(tree/p) for p in sources],
                    '-o', str(exe)], check=True)
    return host32.run([str(exe)], capture_output=True, timeout=180)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--mutate', action='store_true')
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix='gf5-critical-service-') as temporary:
        directory = Path(temporary)
        tree = directory/'tree'
        display.gf4_tree(tree)
        result = run_case(tree, directory)
        assert result.returncode == 0, (result.returncode, result.stdout, result.stderr)
        print('critical-service GREEN', flush=True)
        if args.mutate:
            product = tree/PRODUCT
            original = product.read_text()
            for name, old, new in MUTATIONS:
                assert original.count(old) == 1, (name, original.count(old))
                product.write_text(original.replace(old, new))
                result = run_case(tree, directory)
                assert result.returncode != 0, (name, 'survived')
                print(name+' runtime RED', flush=True)
                product.write_text(original)
                result = run_case(tree, directory)
                assert result.returncode == 0, (name, 'restored control', result.stderr)
                print(name+' restored GREEN', flush=True)


def run(ctx) -> int:
    import os
    import sys
    env = dict(os.environ, HOST32_RUNNERS=' '.join(ctx.runners))
    args = [sys.executable, '-B', 'tools/tests/test_gf5_critical_service.py']
    if ctx.mutate:
        args.append('--mutate')
    return subprocess.call(args, cwd=ctx.root, env=env, close_fds=False)


if __name__ == '__main__':
    main()
