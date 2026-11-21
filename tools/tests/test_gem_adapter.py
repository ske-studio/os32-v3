"""Borrowed GEM operations and direct DMA receive with a shared poll budget."""
CHECK = dict(env=['ppc-cross'], name='gem-adapter-host', tier=2, budget=10, needs=[], covers=[
    'drivers/net/gem/gem_rx.c', 'platform/powermac/nic_gem.c', 'platform/powermac/nic_gem.h',
    'platform/powermac/lan_boot.h', 'platform/powermac/lan_service.c',
    'platform/powermac/rh_observe.h', 'net/link.h', 'include/net_eth.h', 'drivers/net/gem/gem.h', 'net/nic.h', 'include/types.h',
    'tools/tests/gem_adapter_host.c', 'tools/tests/test_gem_adapter.py',
    'tools/tests/host32.py', 'include/callconv.h',
    'sdk/include/os32/os32_kapi_shared.h',
    'sdk/include/os32/os32_kapi_generated.h',
    'sdk/include/os32/os32_generations.h', 'sdk/include/os32/os32_gfx2.h',
    'sdk/include/os32/os32_host_notice.h', 'sdk/include/os32/os32_ls.h',
    'sdk/include/os32/os32_surface.h', 'sdk/include/os32/os32_ver_info.h'],
    basis=['AC-NET-1', 'AC-NET-2'],
    pins=['LAN_BULK_DESIGN'],
)
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
        rx_source = (ctx.root / "drivers/net/gem/gem_rx.c").read_text()
        def exercise(text, good, link=False, driver=None):
            rx = work / "gem_rx.c"
            rx.write_text(driver if driver is not None else rx_source)
            source.write_text(text)
            subprocess.run(['gcc', '-m32', '-std=gnu11', '-Wall', '-Wextra',
                '-nostdinc', '-I'+str(ctx.root / 'drivers/net/gem'), str(rx), '-DKBOOT_LINK='+str(int(link)), '-Werror', '-fno-pie', '-nostdlib', '-static', '-no-pie',
                *includes, '-DGEM_ADAPTER_SOURCE="' + str(source) + '"',
                str(ctx.root / 'tools/tests/gem_adapter_host.c'),
                '-o', str(exe)], check=True)
            rc = host32.run([str(exe)], runner='qemu').returncode
            assert rc >= 0 and (rc == 0) == good, rc
        exercise(original, True)
        exercise(original, True, link=True)
        print('GREEN tx/rx/mac/credit, tick bounds, acquire/release', flush=True)
        if ctx.mutate:
            anchor = 'pm_rh_stage(PM_RH_LINK_HELLO_SENT);'
            assert original.count(anchor) == 1
            exercise(original.replace(anchor, '(void)0;'), False, link=True)
            print('RED runtime HELLO sent mark', flush=True)
            for anchor, replacement in [
                ('return pending < published ? published - pending : 0;',
                 'return published; /* ignore completed descriptors */ (void)pending;'),
                ('g->io.tb(g->io.ctx), 8u)', 'g->io.tb(g->io.ctx), 32u)'),
                ('if (!pm_lan_running_gem(0)) return OS32_ERR_NOSYS;',
                 'if (0) return OS32_ERR_NOSYS;'),
                ('rx_budget = GEM_RX_SCAN_BUDGET;', 'rx_budget = 16u;')]:
                assert original.count(anchor) == 1
                exercise(original.replace(anchor, replacement), False)
                print('RED runtime: ' + anchor, flush=True)
            anchor = 'if (g->rx_head % 4 == 0)'
            assert rx_source.count(anchor) == 1
            exercise(original, False, driver=rx_source.replace(anchor, 'if (g->rx_head % 8 == 0)'))
            print('RED runtime delayed refill kick', flush=True)
            # Restore poll-time staging and serve only those four packets.
            staged = original.replace('static u32 rx_budget;', """static u32 rx_budget;
static u8 staged[4][1514];
static u32 lengths[4], next, count;""").replace('rx_budget = GEM_RX_SCAN_BUDGET;', """rx_budget = GEM_RX_SCAN_BUDGET;
    while (count < 4) {
        u32 tail = (next + count) % 4;
        if (gem_rx_some(g, staged[tail], 1514, &lengths[tail], &rx_budget)) break;
        count++;
    }""").replace('if (!rx_budget) return OS32_ERR_AGAIN;', """if (!count) return OS32_ERR_AGAIN;
    *len = lengths[next];
    if (*len > cap) return OS32_ERR_NOSPC;
    for (u32 i = 0; i < *len; i++) ((u8 *)frame)[i] = staged[next][i];
    next = (next + 1) % 4; count--;
    return 0;""")
            exercise(staged, False)
            print('RED runtime four-slot staging', flush=True)
        cc = Path(os.environ.get('PPC_CROSS_DIR', '/home/hight/opt/cross-ppc')) / 'bin/powerpc-elf-gcc'
        # CI check-fast has no PPC toolchain (same rule as link-platform-host); ppc-compile requires it.
        if not cc.is_file():
            if os.environ.get('OS32_REQUIRE_PPC') == '1':
                print('FAIL PowerPC object: toolchain unavailable: ' + str(cc), flush=True)
                return 1
            print('SKIP PowerPC object: toolchain unavailable: ' + str(cc), flush=True)
            return 0
        subprocess.run([str(cc), '-std=gnu11', '-Wall', '-Wextra', '-Werror',
            '-ffreestanding', '-D__cdecl=', *includes, '-c',
            str(ctx.root / 'platform/powermac/nic_gem.c'),
            '-o', str(work / 'ppc.o')], check=True)
        print('GREEN PowerPC standalone object', flush=True)
    return 0
