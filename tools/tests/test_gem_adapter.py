"""Borrowed GEM operations and tick-only bounded hardware access."""
CHECK = dict(name='gem-adapter-host', tier=2, budget=10, needs=[], covers=[
    'platform/powermac/nic_gem.c', 'platform/powermac/nic_gem.h',
    'platform/powermac/lan_boot.h', 'platform/powermac/lan_service.c',
    'drivers/net/gem/gem.h', 'net/nic.h', 'include/types.h',
    'tools/tests/gem_adapter_host.c', 'tools/tests/test_gem_adapter.py',
    'tools/tests/host32.py', 'include/callconv.h',
    'sdk/include/os32/os32_kapi_shared.h',
    'sdk/include/os32/os32_kapi_generated.h',
    'sdk/include/os32/os32_generations.h', 'sdk/include/os32/os32_gfx2.h',
    'sdk/include/os32/os32_host_notice.h', 'sdk/include/os32/os32_ls.h',
    'sdk/include/os32/os32_surface.h', 'sdk/include/os32/os32_ver_info.h'])
import os
from pathlib import Path
import subprocess
import tempfile
import host32


def run(ctx):
    original = (ctx.root / 'platform/powermac/nic_gem.c').read_text()
    with tempfile.TemporaryDirectory(prefix='gem-adapter-') as td:
        work = Path(td)
        source, exe = work / 'nic_gem.c', work / 'probe'
        includes = ['-I' + str(ctx.root / p) for p in
                    ['.', 'include', 'platform/powermac', 'sdk/include']]
        def exercise(text, good):
            source.write_text(text)
            subprocess.run(['gcc', '-m32', '-std=gnu11', '-Wall', '-Wextra',
                '-Werror', '-fno-pie', '-nostdlib', '-static', '-no-pie',
                *includes, '-DGEM_ADAPTER_SOURCE="' + str(source) + '"',
                str(ctx.root / 'tools/tests/gem_adapter_host.c'),
                '-o', str(exe)], check=True)
            rc = host32.run([str(exe)], runner='qemu').returncode
            assert rc >= 0 and (rc == 0) == good, rc
        exercise(original, True)
        print('GREEN tx/rx/mac/credit, tick bounds, acquire/release', flush=True)
        if ctx.mutate:
            for anchor, replacement in [
                ('g->io.tb(g->io.ctx), 8u)', 'g->io.tb(g->io.ctx), 32u)'),
                ('if (!pm_lan_running_gem(0)) return OS32_ERR_NOSYS;',
                 'if (0) return OS32_ERR_NOSYS;'),
                ('u32 n = 0;', 'u32 n = 0; gem_rx(g, packet, sizeof(packet), &n);')]:
                assert original.count(anchor) == 1
                exercise(original.replace(anchor, replacement), False)
                print('RED runtime: ' + anchor, flush=True)
        cc = Path(os.environ.get('PPC_CROSS_DIR', '/home/hight/opt/cross-ppc')) / 'bin/powerpc-elf-gcc'
        subprocess.run([str(cc), '-std=gnu11', '-Wall', '-Wextra', '-Werror',
            '-ffreestanding', '-D__cdecl=', *includes, '-c',
            str(ctx.root / 'platform/powermac/nic_gem.c'),
            '-o', str(work / 'ppc.o')], check=True)
        print('GREEN PowerPC standalone object', flush=True)
    return 0
