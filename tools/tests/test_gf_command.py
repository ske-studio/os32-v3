"""Layer 2 component hosts: isolated links and runtime mutation controls."""
CHECK = dict(name='gf-command-host', tier=1, needs=[], budget=90.0,
    covers=['hwdev/gf/**', 'drivers/video/geforce/geforce_dma.c',
            'tools/tests/gf_command*', 'tools/tests/test_gf_command.py',
            'tools/tests/host32.py', 'tools/tests/kernel_includes.py'])

import argparse
import os
from pathlib import Path
import re
import subprocess
import tempfile

import host32
from kernel_includes import include_flags

ROOT = Path(__file__).resolve().parents[2]
ORACLE = Path('/home/hight/os32-tmp/refs/geforce6200/nouveau/drivers/gpu/drm/'
              'nouveau/nvkm/engine/gr/ctxnv40.c')
# A2 report: only the tested component is real; dependencies live in the
# fixture header. B7 includes fault.c itself and must never link it again.
COMPONENTS = {
    'b0': ('hwdev/gf/fifo.c', True),
    'b1': ('hwdev/gf/push.c', False),
    'b2': ('hwdev/gf/dmaobj.c', False),
    'b3': ('hwdev/gf/ramht.c', True),
    'b4': ('hwdev/gf/grctx.c', False),
    'b5': ('hwdev/gf/gr.c', False),
    'b6': ('hwdev/gf/fence.c', True),
    'b7': ('hwdev/gf/fault.c', False),
    'b8': ('hwdev/gf/command_platform.c', True),
    'b9': ('drivers/video/geforce/geforce_dma.c', True),
    't2a1': ('hwdev/gf/command_test.c', True),
}
# Each old string must occur once in the original product source. Keep these
# independent of the oracle and fixture; mutate one product site per build.
MUTATIONS = {
    # PM/A3 consolidates the T2a1 mutation tuples from its report.
    't2a1': [],
    'b0': [
        ('ramfc-r-base', 'im->vram_bytes - RAMFC_WINDOW_BYTES +',
         'im->vram_bytes - im->reserved +'),
        ('fb-enable-omitted', 'TRY(wr(ch, FB_PCI_ENABLE, 0x80000000U));',
         '(void)0;'),
        ('fb-control-omitted', 'return wr(ch, FB_PCI_CONTROL, 1);',
         'return GF2_OK;'),
    ],
    'b1': [
        ('put-word-byte', 'ch->B + put * sizeof(u32)', 'ch->B + put'),
        ('free-minus-one', 'ch->free = get - ch->cur - 1U;',
         'ch->free = get - ch->cur;'),
        ('readback-omitted', '(void)ch->va[0];', '(void)0;'),
    ],
    'b2': [
        ('pci-va', 'base = (start & DMA_PAGE_MASK) | flags2;',
         'base = ((u32)c & DMA_PAGE_MASK) | flags2;'),
        ('pci-target', 'DMA_PCI_FLAGS = 0x00023000U',
         'DMA_PCI_FLAGS = 0x00003000U'),
        ('limit-plus-one', 'gf_inst_write32(c->f, &obj->inst, 4, limit - start)',
         'gf_inst_write32(c->f, &obj->inst, 4, limit - start + 1)'),
    ],
    'b3': [
        ('probe-hole', 'free_slot = slot;', 'free_slot = slot; break;'),
        ('nv04-valid-bit', '(inst_off >> 4);', '(inst_off >> 4) | 0x80000000U;'),
    ],
    'b4': [
        # The 1540 fixture has two set bits; NV4a requires three VS units.
        ('vs-popcount', 'VS_NR = 3,', 'VS_NR = 2,'),
    ],
    'b5': [
        ('ctx-control-400140', '0x400144U', '0x400140U'),
        ('omit-ctx-self', 'gf_inst_write32(f, &ch->grctx, 0, ch->grctx.off >> 4)',
         '0'),
    ],
    'b6': [
        ('deadline-wrap', '(now - deadline_us)', '(deadline_us - now)'),
        ('seq-wrap', '(observed - fence.seq)', '(fence.seq - observed)'),
        ('reference-method', 'GF2_REFERENCE_METHOD,', '0x0054U,'),
        ('no-kick', 'rc = gf2_kick(ch);', 'rc = GF2_OK;'),
        ('init-routing', 'if (ch->state == GF2_RUN)',
         'if (ch->state == GF2_INIT)'),
        ('get-put-completion',
         'GF2_FENCE_WIDTH, &observed)) return GF2_EIO;\n'
         '        if ((i32)(observed - fence.seq) >= 0) return GF2_OK;',
         'GF2_FENCE_WIDTH, &observed)) return GF2_EIO;\n'
         '        if (io->read(io->opaque, GF2_FENCE_BAR,\n'
         '                     GF2_USER_BASE + GF2_CHID * GF2_USER_STRIDE + GF2_USER_GET,\n'
         '                     GF2_FENCE_WIDTH, &observed)) return GF2_EIO;\n'
         '        if (observed == ch->B + 4U * ch->put) return GF2_OK;'),
    ],
    'b7': [
        ('fifo-delta', 'u32 fifo_delta = now.fifo - ch->seen.fifo;',
         'u32 fifo_delta = 0;'),
        ('graph-delta', 'u32 graph_delta = now.graph - ch->seen.graph;',
         'u32 graph_delta = 0;'),
        ('irq-close', 'rc |= gf_irq_enable(ch->f, false, false, ch->heads);',
         'rc |= 0;'),
        ('first-fixed', 'merge(&ch->fault.latest, &now);',
         'merge(&ch->fault.first, &now);'),
        ('graph-gate', 'rc |= close_gate(ch->f, FAULT_GRAPH_FIFO);', 'rc |= 0;'),
    ],
    'b8': [
        ('map-offset-ignored', 'extent.base += off;', 'extent.base += 0U;'),
    ],
    'b9': [
        ('alloc-rc-lost', 'return rc;', 'return 0;'),
    ],
}


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


def relocated(text, source):
    """A temporary mutant keeps the real source's quoted include paths."""
    def replace(match):
        header = source.parent / match[1]
        return '#include "' + str(header.resolve()) + '"'
    return re.sub(r'^#include "([^"]+)"', replace, text, flags=re.MULTILINE)


def build(component, source, directory, flags):
    _, libc = COMPONENTS[component]
    extra = [] if libc else ['-nostdlib']
    if component == 'b4':
        require(ORACLE.is_file() and ORACLE.with_suffix('.h').is_file(),
                'B4 requires the independent c03c1a1 MIT ctxnv40.c/.h oracle')
        core = directory / 'core'
        core.mkdir(exist_ok=True)
        (core / 'gpuobj.h').write_text('/* Oracle services supplied by B4 host. */\n')
        extra += ['-I' + str(directory), '-DGF2_B4_ORACLE="' + str(ORACLE) + '"']
    if component == 'b7':
        extra += ['-DGF2_FAULT_SOURCE="' + str(source) + '"']
        sources = []
    else:
        sources = [str(source)]
    exe = directory / 'host'
    # check=True is deliberate: a compile/link failure never kills a mutant.
    subprocess.run(['gcc', *flags, *extra,
                    str(ROOT / ('tools/tests/gf_command_' + component + '_host.c')),
                    *sources, '-o', str(exe)], cwd=ROOT, check=True)
    return exe


def execute(exe, runners, label, green):
    for runner in runners:
        result = host32.run([str(exe)], runner=runner, cwd=ROOT)
        require((result.returncode == 0) == green,
                f'{label}: runner={runner} rc={result.returncode}, '
                f'expected runtime {"GREEN" if green else "RED"}')
        print(f'{label}: runtime {"GREEN" if green else "RED"} '
              f'(runner={runner}, rc={result.returncode})', flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--mutate', action='store_true')
    parser.add_argument('--component', choices=tuple(COMPONENTS),
                        help='run one independently linked component')
    args = parser.parse_args()
    runners = os.environ.get('HOST32_RUNNERS', 'qemu').split()
    require(bool(runners) and all(r in ('native', 'qemu') for r in runners),
            'HOST32_RUNNERS must select native and/or qemu')
    flags = ['-m32', '-march=i386', '-std=gnu11', '-O2', '-Wall', '-Wextra',
             '-Werror', '-Werror=vla', '-ffreestanding', '-fno-pie',
             '-fno-stack-protector', '-static', '-no-pie',
             *include_flags(ROOT), '-I' + str(ROOT)]
    selected = (args.component,) if args.component else COMPONENTS
    with tempfile.TemporaryDirectory(prefix='gf-command-') as tmp:
        for component in selected:
            source = ROOT / COMPONENTS[component][0]
            if not source.is_file():
                require(not args.component,
                        f'{component}: requested component has not been added')
                continue  # Unstarted B components are not registered yet (§9).
            host = ROOT / ('tools/tests/gf_command_' + component + '_host.c')
            require(host.is_file(), f'{component}: added component is missing its host')
            directory = Path(tmp) / component
            directory.mkdir()
            original = source.read_text()
            exe = build(component, source, directory, flags)
            execute(exe, runners, component + ' control', True)
            if not args.mutate:
                continue
            mutant = directory / source.name
            for name, old, new in MUTATIONS[component]:
                label = component + '-' + name
                require(original.count(old) == 1, label + ': anchor must be unique')
                # The previous restored GREEN is the next unchanged control.
                mutant.write_text(relocated(original.replace(old, new, 1), source))
                try:
                    exe = build(component, mutant, directory, flags)
                    execute(exe, runners, label, False)
                finally:
                    # Never edit the worktree. Restore the temporary source and
                    # rebuild, even when a mutant fails to compile or survives.
                    mutant.write_text(relocated(original, source))
                    exe = build(component, mutant, directory, flags)
                    execute(exe, runners, label + ' restored', True)
    return 0


if __name__ == '__main__':
    raise SystemExit(main())


def run(ctx):
    args = ['python3', '-B', 'tools/tests/test_gf_command.py']
    if ctx.mutate:
        args.append('--mutate')
    return subprocess.call(args, cwd=ctx.root,
                           env=dict(os.environ, HOST32_RUNNERS=' '.join(ctx.runners)),
                           close_fds=False)
