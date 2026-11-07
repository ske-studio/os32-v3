"""A10 service admission and independent thermal lifetime, production assembly."""
CHECK = dict(name='gf5-service-live-host', tier=1, budget=90.0,
             needs=['make:build/out/geforce/geforce_imports.h'],
             covers=['tools/tests/**', 'hwdev/**', 'drivers/video/geforce/**',
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
    ('boot-publication', '.state_boot_target=service_state_boot_target',
     '.state_boot_target=(true ? 0 : service_state_boot_target)'),
    ('boot-wrapper-busy', 'return rc ? rc : gf5_state_boot_target(g,dst,boot);',
     'g->busy=true; return rc ? rc : gf5_state_boot_target(g,dst,boot);'),
    *[(name+'-deadline', 'SERVICE_TIMED_OP('+name+',', 'SERVICE_OP('+name+',')
      for name in ('tile_set', 'tile_clear', 'ram_prog', 'perf_select')],
    ('running', '!g->ch->f->running', 'false'),
    ('busy', 'if (g->busy || (service_slot && !*service_slot)) return GF5_EBUSY;', 'if (false) return GF5_EBUSY;'),
    ('thermal-fault', 'return g->fan.sensor_setup || !service_ready(g);',
     'return !g->ch->fault.latched && (g->fan.sensor_setup || !service_ready(g));'),
    ('window', '!g->display->legacy->bar0_map', 'false'),
    ('save-invalidate', 'gf5_plan_invalidate(g);\n    *service_slot=0;\n    rc=gf5_state_save(g,out);',
     '*service_slot=0;\n    rc=gf5_state_save(g,out);'),
    ('management', 'if (management) {', 'if (management && false) {'),
]


def run_case(tree, directory):
    exe = directory/'service-live'
    fixture = tree/'tools/tests/gf5_service_live_host.c'
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
    with tempfile.TemporaryDirectory(prefix='gf5-service-live-') as temporary:
        directory = Path(temporary)
        tree = directory/'tree'
        display.gf4_tree(tree)
        result = run_case(tree, directory)
        assert result.returncode == 0, (result.returncode, result.stdout, result.stderr)
        print('service-live GREEN', flush=True)
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
    args = [sys.executable, '-B', 'tools/tests/test_gf5_service_live.py']
    if ctx.mutate:
        args.append('--mutate')
    return subprocess.call(args, cwd=ctx.root, env=env, close_fds=False)


if __name__ == '__main__':
    main()
