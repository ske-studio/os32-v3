"""Layer 2 component hosts: isolated links and runtime mutation controls."""
CHECK = dict(name='gf-command-host', tier=1, needs=['make:build/out/geforce/geforce_imports.h'], budget=90.0,
    covers=['build/programs.mk', 'drivers/base/dev.c', 'fs/vfs.c', 'kernel/console/console.c', 'kernel/module/kdebug_call.c', 'kernel/module/module_image.c', 'kernel/module/module_runtime.c', 'kernel/port_service.c', 'lib/kprintf.c', 'lib/kprintf_attr.c', 'platform/pc98/boot_names.c', 'platform/pc98/console.c', 'platform/pc98/console_attr.c', 'tools/tests/module_image_host.c', 'tools/tests/test_module_image.py', 'userland/shell/cmd_gfinit.c', 'hwdev/gf/**', 'drivers/video/geforce/geforce_dma.c',
            'kdebug/gf_accel_test.h', 'kdebug/gf_command_test.c', 'kdebug/gf_command_test.h',
            'kdebug/gf_command_wire.h', 'kdebug/recover.h',
            'drivers/video/geforce/geforce_module.h', 'drivers/pci/pci.h',
            'include/io.h', 'lib/serial_diag.h', 'lib/crc32_core.inc',
            'tools/tests/gf_command*', 'tools/tests/test_gf_command.py',
            'tools/tests/host32.py', 'tools/tests/kernel_includes.py',
            'tools/tests/mutpar.py', 'tools/tests/test_gfinit_owner.py',
            'tools/tests/gfinit_owner*', 'tools/tests/test_module_runtime.py',
            'tools/tests/module_runtime_host.c', 'tools/tests/gf1_dispatch_host.c',
            'tools/tests/geforce_module_host.c',
            'drivers/video/geforce/geforce_body.c',
            'drivers/video/geforce/geforce_module.c', 'hwdev/gf/foundation_test.c',
            'kdebug/gfinit.c', 'build/out/geforce/*'])

import argparse
import os
from pathlib import Path
import re
import subprocess
import tempfile
import struct

import mutpar

import host32
from kernel_includes import include_flags

ROOT = Path(__file__).resolve().parents[2]
# A2 report: only the tested component is real; dependencies live in the
# fixture header. B7/T2b include their product source and must not link it twice.
COMPONENTS = {
    'c0': 'hwdev/gf/command.c',
    't2a2-link': 'hwdev/gf/foundation_test.c',
    't2a2-bind': 'drivers/video/geforce/geforce_body.c',
    't2b-owner': 'kdebug/gfinit.c',
    'b0': 'hwdev/gf/fifo.c',
    'b1': 'hwdev/gf/push.c',
    'b2': 'hwdev/gf/dmaobj.c',
    'b3': 'hwdev/gf/ramht.c',
    'b4': 'hwdev/gf/grctx.c',
    'b5': 'hwdev/gf/gr.c',
    'b6': 'hwdev/gf/fence.c',
    'b7': 'hwdev/gf/fault.c',
    'b8': 'hwdev/gf/command_platform.c',
    'b9': 'drivers/video/geforce/geforce_dma.c',
    't2a1': 'hwdev/gf/command_test.c',
    't2b': 'kdebug/gf_command_test.c',
}
# Each old string must occur once in the original product source. Keep these
# independent of the oracle and fixture; mutate one product site per build.
MUTATIONS = {
    'c0': [('irq-close', 'command_irq(ch, false) ||', 'command_irq(ch, true) ||'),
     # Retain the parameter use so -Werror does not replace the runtime oracle.
     ('reset-success', 'return ch->last_rc = result;', '(void)result; return ch->last_rc = GF2_OK;'),
     ('early-staging-free',
      'gf_foundation_stop(ch->f))',
      '(gf2_dma_put(ch), gf_foundation_stop(ch->f)))'),
     ('early-inst-free',
      'gf_engine_stop(ch->f) || release_instances(ch)',
      'release_instances(ch) || gf_engine_stop(ch->f)')],
    't2a1': [('bad-handle', '#define TEST_BAD_HANDLE 0xdeadbeefU', '#define TEST_BAD_HANDLE 0x80000002U'),
     ('bad-method', '#define TEST_BAD_METHOD 0x1ffcU', '#define TEST_BAD_METHOD 0x0100U'),
     ('object-epoch',
      'if (!test_object.handle || test_object_epoch != ch->epoch)',
      'if (!test_object.handle)'),
     ('diagnostic-only',
      'if (ch != &diagnostic || ch->state != GF2_RUN) return GF2_ESTATE;',
      'if (ch->state != GF2_RUN) return GF2_ESTATE;'),
     ('prepare-retention', 'if (diagnostic.state != GF2_OFF) return GF2_ESTATE;', '(void)0;'),
     ('revoked-access',
      'if (f->revoked) return reply->rc = GF2_EFAULT;',
      'if (false) return reply->rc = GF2_EFAULT;'),
     ('fault-wait',
      'return wait_fault(ch, deadline);',
      '(void)wait_fault; return reference(ch, deadline);'),
     ('reference-wait', 'if (!rc) rc = gf2_fence_wait(ch, fence, deadline);', '(void)fence;'),
     ('deadline-wrap',
      'if ((i32)(now - deadline) >= 0) return GF2_ETIME;',
      'if (now >= deadline) return GF2_ETIME;'),
     ('bad-kick', 'rc = gf2_kick(ch);', 'rc = GF2_OK;')],
    't2a2-link': [('t2a2-command-link', '.command=&gf2_module_ops', '.command=0')],
    't2a2-bind': [('t2a2-production-bind',
      'gf2_test_bind_production(&production.foundation,&production.chan);',
      '(void)0;')],
    't2b': [('gf3-dispatch', 'gf_accel_test(q.session)', 'gf_accel_test(0)'),
     ('reference-count',
      '#define GF2_REFERENCE_COUNT 100000U',
      '#define GF2_REFERENCE_COUNT 1000U'),
     ('get-put-completion',
      '(i32)(o->reply.reference - o->reply.seq) < 0) return GF2_EIO;',
      'o->reply.get != o->reply.put) return GF2_EIO;'),
     ('wrap-check', 'o->wraps >= 2U', 'true'),
     ('recover-epoch', 'o->reply.epoch == o->previous_epoch', 'false'),
     ('fault-required',
      'rc == GF2_EFAULT && o->reply.state == GF2_FAULT &&',
      'rc == GF2_OK && o->reply.state == GF2_FAULT &&'),
     ('display-crc', 'o.crc_before != o.crc_after', 'false'),
     ('deadline', 'now + GF2_TIMEOUT_US', 'now'),
     ('fault-subchannel',
      '(p->gr_method >> 16) & GF2_SUBCHANNEL_MAX',
      '(p->gr_trap >> 16) & GF2_SUBCHANNEL_MAX')],
    't2b-owner': [('clock-frozen', 'int rc = sys_time_now(&lo, &hi);', 'int rc = 0; lo = hi = 0; (void)hi;'),
     ('revoked-layer1-stop',
      'if (command_rc && !owner->foundation.revoked) return command_rc;',
      'if (command_rc) return command_rc;')],

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
    extra = ['-nostdlib']
    if component in ('t2a2-bind', 't2b-owner'):
        return build_integration(component, source, directory)
    if component == 'c0':
        extra += ['-DGF2_COMMAND_SOURCE="' + str(source) + '"']
        sources = [str(ROOT / 'hwdev/gf/fence.c')]
    elif component == 't2a2-link':
        extra += ['-DGF1_DISPATCH_SOURCE="' + str(source) + '"']
        sources = []
    elif component == 'b7':
        extra += ['-DGF2_FAULT_SOURCE="' + str(source) + '"']
        sources = []
    elif component == 't2b':
        extra += ['-DGF2_T2B_SOURCE="' + str(source) + '"']
        sources = []
    else:
        sources = [str(source)]
    exe = directory / 'host'
    # check=True is deliberate: a compile/link failure never kills a mutant.
    subprocess.run(['gcc', *flags, *extra,
                    str(ROOT / ('tools/tests/gf1_dispatch_host.c' if component == 't2a2-link'
                                else 'tools/tests/gf_command_' + component + '_host.c')),
                    *sources, '-o', str(exe)], cwd=ROOT, check=True)
    return exe


def build_integration(component, source, directory):
    """Reuse the established owner/module oracles in a copied source tree."""
    exe = directory / 'host'
    if component == 't2b-owner':
        from test_gfinit_owner import command
        cmd = command(exe)
    else:
        from test_module_runtime import FLAGS, INCLUDES
        cmd = ['gcc', *FLAGS, '-Wno-unused-parameter', *INCLUDES,
               '-Igfx', '-Ibuild/out/geforce', '-I' + str(ROOT / 'tools/tests'),
               str(ROOT / 'tools/tests/geforce_module_host.c'),
               *[str(ROOT / f) for f in ('kernel/module/module_runtime.c',
                  'kernel/module/module_image.c', 'fs/vfs.c', 'drivers/base/dev.c')],
               '-nostdlib', '-static', '-no-pie', '-Wl,--gc-sections', '-o', str(exe)]
    copied = Path(tempfile.mkdtemp(prefix='integration-', dir=directory))
    mutpar.build_in_tree(ROOT, copied, {COMPONENTS[component]: source.read_text()}, [cmd])
    return exe


def execute(exe, runners, label, green, component):
    data = None
    if component == 't2a2-bind':
        from test_module_runtime import image
        data = struct.pack('<II', 0, 0) + image()
    for runner in runners:
        result = host32.run([str(exe)], runner=runner, cwd=ROOT, input=data)
        require((result.returncode == 0) == green,
                f'{label}: runner={runner} rc={result.returncode}, '
                f'expected runtime {"GREEN" if green else "RED"}')
        print(f'{label}: runtime {"GREEN" if green else "RED"} '
              f'(runner={runner}, rc={result.returncode})', flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--mutate', action='store_true')
    parser.add_argument('--component', choices=tuple(COMPONENTS),
                        help='run one component or its established integration oracle')
    parser.add_argument('--oracle', type=Path,
                        help='also compare B4 fixture with local c03c1a1 ctxnv40.c/.h')
    args = parser.parse_args()
    runners = os.environ.get('HOST32_RUNNERS', 'qemu').split()
    require(bool(runners) and all(r in ('native', 'qemu') for r in runners),
            'HOST32_RUNNERS must select native and/or qemu')
    flags = ['-m32', '-march=i386', '-std=gnu11', '-O2', '-Wall', '-Wextra',
             '-Werror', '-Werror=vla', '-ffreestanding', '-fno-pie',
             '-fno-stack-protector', '-static', '-no-pie',
             *include_flags(ROOT), '-I' + str(ROOT)]
    selected = (args.component,) if args.component else COMPONENTS
    if args.oracle:
        require('b4' in selected, '--oracle requires the B4 component')
        from gf_command_b4_fixture import FIXTURE, generate
        require(generate(args.oracle) == FIXTURE.read_text(),
                'B4 frozen expectations differ from the independent MIT oracle')
        print('b4 fixture: independent c03c1a1 oracle GREEN', flush=True)
    with tempfile.TemporaryDirectory(prefix='gf-command-') as tmp:
        for component in selected:
            source = ROOT / COMPONENTS[component]
            if not source.is_file():
                require(not args.component,
                        f'{component}: requested component has not been added')
                continue  # Unstarted B components are not registered yet (§9).
            host = ROOT / {'t2a2-link': 'tools/tests/gf1_dispatch_host.c',
                           't2a2-bind': 'tools/tests/geforce_module_host.c',
                           't2b-owner': 'tools/tests/gfinit_owner_host.c'}.get(
                               component, 'tools/tests/gf_command_' + component + '_host.c')
            require(host.is_file(), f'{component}: added component is missing its host')
            directory = Path(tmp) / component
            directory.mkdir()
            original = source.read_text()
            exe = build(component, source, directory, flags)
            execute(exe, runners, component + ' control', True, component)
        # All selected normal controls complete before mutation dispatch.
        if args.mutate:
            failures = []
            for component in selected:
                source = ROOT / COMPONENTS[component]
                directory = Path(tmp) / component
                original = source.read_text()
                mutant = directory / source.name
                for name, old, new in MUTATIONS[component]:
                    label = component + '-' + name
                    require(original.count(old) == 1, label + ': anchor must be unique')
                    def temporary(text):
                        return text if component in ('t2a2-bind', 't2b-owner') else relocated(text, source)
                    mutant.write_text(temporary(original.replace(old, new, 1)))
                    try:
                        exe = build(component, mutant, directory, flags)
                        execute(exe, runners, label, False, component)
                    except (subprocess.CalledProcessError, RuntimeError) as error:
                        failures.append(label)
                        print(f'{label}: ERROR {error}', flush=True)
                    finally:
                        mutant.write_text(temporary(original))
                        exe = build(component, mutant, directory, flags)
                        execute(exe, runners, label + ' restored', True, component)
            require(not failures, 'mutation failures: ' + ', '.join(failures))
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
