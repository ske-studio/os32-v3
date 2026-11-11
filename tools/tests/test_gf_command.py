"""Layer 2 component hosts: isolated links and runtime mutation controls."""
CHECK = dict(name='gf-command-host', tier=1, needs=['make:build/out/geforce/geforce_imports.h', 'make:build/out/kernel.elf'], budget=31.2,
    covers=['drivers/pci/pci_barsize.c', 'drivers/pci/pci_barsize.h', 'tools/tests/recover_gf_dev_stub.c', 'gfx/gfx_policy.h', 'gfx/gfx_osri_view.h', 'platform/pc98/bootinfo_wire.h', 'platform/pc98/bootinfo_native.h', 'include/legacy_guest.h', 'tools/tests/legacy_guest_fake.h', 'include/legacy_guest.h', 'tools/tests/legacy_guest_fake.h', 'kernel/module/debug_probe.c', 'drivers/video/cirrus/backend_cirrus.c', 'drivers/video/cirrus/cirrus_identify.c', 'drivers/video/pc98/backend_pc98.c', 'drivers/video/pc98/backend_pegc.c', 'exec/access_walk.c', 'exec/lease.c', 'exec/redir_access.c', 'exec/surface_query.c', 'gfx/gfx_core.c', 'gfx/gfx_emergency.c', 'gfx/gfx_scroll.c', 'platform/pc98/gfx_scroll.c', 'platform/pc98/gfx_init.c', 'platform/pc98/gfx_selector.c', 'platform/pc98/gfx_surfaces.c', 'kernel/console/display_restore.h', 'gfx/gfx_vram.c', 'kernel/sys.c', 'mm/physmem.c', 'mm/shlib.c', 'tools/tests/test_gfx_kernel_fb.py', 'tools/tests/gfx_kernel_fb_host.c', 'tools/tests/paging_source.py', 'tools/tests/kernel_layout.py', 'tools/tests/serial_diag_host.h', 'tools/tests/host_arch/*.h', 'mm/paging.c', 'mm/paging_*.inc', 'mm/pgalloc.c', 'mm/pgalloc_*.inc', 'tools/tests/gf_guard_fake.h', 'tools/tests/gf_startlog_host.h', 'build/programs.mk', 'drivers/base/dev.c', 'fs/vfs.c', 'kernel/console/console.c', 'kernel/module/kdebug_call.c', 'kernel/module/module_image.c', 'kernel/module/module_runtime.c', 'kernel/port_service.c', 'lib/kprintf.c', 'lib/kprintf_attr.c', 'platform/pc98/boot_names.c', 'platform/pc98/console.c', 'platform/pc98/console_attr.c', 'tools/tests/module_image_host.c', 'tools/tests/test_module_image.py', 'userland/shell/cmd_gfinit.c', 'hwdev/gf/**', 'drivers/video/geforce/geforce_dma.c',
            'kdebug/gf_accel_test.h', 'kdebug/gf_prim_bench.h', 'kdebug/gf_command_test.c', 'kdebug/gf_command_test.h',
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
            'kdebug/gfinit.c', 'build/out/geforce/*', 'tools/tests/test_gf_display.py', 'tools/tests/gf4_platform_host.c', 'drivers/video/geforce/*.c', 'drivers/video/geforce/*.h', 'hwdev/gf_load_state.c', 'kdebug/gfinit_decode.c', 'kdebug/gfinit_pll.c', 'tools/tests/gf_accel_unbound_stubs.c', 'kdebug/gfinit_decode.h', 'kdebug/gfinit_decode/*.inc', 'kdebug/gfinit_op/*.inc', 'kdebug/gfinit/*.inc', 'kdebug/gfinit_plan_c/*.inc', 'kdebug/gfinit_pll/*.inc', 'kdebug/gfinit_resources/*.inc', 'kdebug/recover.c', 'kdebug/recover_common/*.inc', 'kdebug/recover_plan/*.inc', 'kdebug/recover_gf_stage/*.inc', 'kdebug/recover_gf.c', 'kdebug/recover_gf_expand.h', 'kdebug/recover_gf.h', 'tools/tests/gfinit_host.c', 'tools/tests/gfinit_fixture.h', 'tools/tests/test_gfinit.py', 'include/types.h', 'tools/tests/recover_gf_host.c', 'tools/tests/recover_gf_inventory.h', 'kdebug/gfinit_pll.h', 'include/memmap.h', 'kdebug/gfinit_exec.c', 'kdebug/gfinit_exec.h', 'tools/tests/gfinit_exec_host.c', 'tools/tests/test_gfinit_exec.py', 'tools/tests/gfinit_pll_host.c', 'tools/tests/test_gfinit_pll.py', 'kdebug/gfinit_bus.c', 'kdebug/gfinit_bus.h', 'kdebug/kdebug_window.c', 'kdebug/kdebug_window.h', 'tools/tests/gfinit_bus_host.c', 'tools/tests/test_gfinit_bus.py', 'tools/tests/recover_hostshim/io.h', 'kdebug/gfinit_resources.c', 'kdebug/gfinit_resources.h', 'drivers/pci/pci_decode.h', 'sdk/include/os32/os32_kapi_shared.h', 'include/kprintf.h', 'tools/tests/gfinit_resources_ra266.h', 'hwdev/owner.h', 'kdebug/gfinit.h', 'kdebug/gfinit_plan.h', 'kdebug/kpeek.h', 'mm/pgalloc.h', 'mm/paging.h', 'mm/physmem.h', 'lib/kstring.h', 'arch/x86/kernel/idt.h', 'kernel/module/module.h', 'drivers/char/serial.h', 'include/bootinfo.h', 'include/sys.h', 'kdebug/generated/kdebug_import_macros.h', 'kdebug/generated/kdebug_imports.h', 'tools/tests/gfinit_owner_host.c', 'include/hwdev_private.h', 'kdebug/gfinit_wire.h', 'userland/shell/shell.h', 'userland/shell/cmd_sys.c', 'tools/tests/gfinit_output_host.h', 'include/console_attr.h', 'hwdev/hwdev_imports.def', 'kdebug/gfinit_plan.c', 'tools/tests/gfinit_plan_host.c', 'tools/tests/test_gfinit_plan.py', 'kdebug/recover_profile.c', 'tools/run_host_group.py', 'tools/tests/gfinit_romfile_host.c', 'tools/gfinit_rom_decode.py', 'include/port_service.h', 'include/console_output.h', 'kdebug/recover_cleanup.h', 'kdebug/recover_diag.h', 'include/pci_dev_contract.h', 'kernel/console/console_backend.h', 'kdebug/gfinit_mode.c', 'kdebug/gfinit_plan_c.c', 'kdebug/gfinit_plan_c.h', 'kdebug/recover_gf_c.h', 'hwdev/gfinit_mode.h', 'tools/tests/test_gfinit_plan_c.py', 'tools/tests/gfinit_plan_c_host.c', 'tools/tests/test_gfinit_mode.py', 'tools/tests/gfinit_mode_host.c', 'tools/tests/test_gfinit_budget.py', 'tools/tests/gfinit_budget_host.c', 'tools/rshell_recover.py', 'tools/lan_bridge.py', 'tools/host_image.py', 'tools/lanfs_host.py', 'tools/rshell_peek.py', 'tools/rshell_hwb.py', 'tools/rshell_serial.py', 'tools/serialfs_host.py', 'kdebug/gf_stage_state.c', 'kdebug/gf_stage_state.h', 'hwdev/gf_load_state.h', 'tools/tests/test_gf_runner.py', 'tools/tests/gf_runner_host.c', 'hwdev/gf/internal.h', 'hwdev/gf/runner.c', 'hwdev/gf/save.c', 'hwdev/gf/pll.c', 'hwdev/gf/op/*.c', 'include/video_service.h', 'hwdev/gf/scripts.h', 'hwdev/gf/display/calc.h', 'hwdev/gf/display/calc.c', 'hwdev/gf/display/timing.c', 'hwdev/gf/display/format.c', 'hwdev/gf/display/pitch.c', 'hwdev/gf/display/start.c', 'hwdev/gf/display/dac_format.c', 'hwdev/gf/display/output.c', 'hwdev/gf/display/cursor.c', 'hwdev/gf/display/fifo.c', 'hwdev/gf/display/extended.c', 'hwdev/gf/display/pll_program.c', 'hwdev/gf/display/state.h', 'hwdev/gf/quiesce_rows.h', 'hwdev/gf/display/*.c', 'hwdev/gf/display/*.h', 'hwdev/gf/service.c', 'tools/tests/gf_display_host.c', 'tools/tests/gf_display_palette_host.c', 'tools/tests/gf_display_blank_host.c', 'tools/tests/gf_display_modes_host.c', 'include/gf_production.h', 'tools/tests/gfinit_c_only_host.c', 'tools/tests/gfinit_init_host.c', 'tools/tests/gfinit_s0_host.c', 'tools/tests/gfinit_stages_host.c', 'tools/tests/test_gfinit_stages.py', 'tools/tests/gf4_*_host.c', 'tools/tests/gf4_display_fake.h', 'tools/tests/gf_accel*.[ch]', 'tools/tests/gf_command_runtime_host.h', 'build/geforce-module.mk', 'build/hwdev.mk', 'build/kernel.mk', 'include/gfx_format.h', 'include/gfx_hal.h', 'kdebug/gf_display_test.c', 'kdebug/gf_display_test.h', 'tools/module_schema.py', 'kernel/module/module_schema.h', 'hwdev/gf/*.c', 'hwdev/gf/*.h', 'hwdev/gf/rom.inc', 'tools/prototypes/geforce_imports.def'])

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
    'guard-mm': 'hwdev/gf/instmem_map.c',
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
    'c0': [('start-log', '"[gf2] start fail stage=%s rc=%d\\n"', '"[gf2] wrong stage=%s rc=%d\\n"'), ('irq-close', 'command_irq(ch, false) ||', 'command_irq(ch, true) ||'),
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
    't2b': [('cycle-recover-after-stop',
        'phase(&o, GF2T_START, "restart", session)',
        'phase(&o, GF2T_RECOVER, "restart", session)'),
     ('production-fault-tests',
        'if (o.production && (q.test == GF2_BAD_HANDLE || q.test == GF2_BAD_METHOD))',
        'if (false)'),
        ('gf3-dispatch', 'gf_accel_test(q.session)', 'gf_accel_test(0)'),
        ('prim-dispatch', 'gf_prim_bench(q.session)', 'gf_prim_bench(0)'),
     ('gf4-dispatch', 'gf_display_test(q.session)', 'gf_display_test(1)'),
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
        ('seq-wrap', 'if ((i32)(observed - fence.seq) >= 0) return GF2_OK;', 'if ((i32)(fence.seq - observed) >= 0) return GF2_OK;'),
        ('poll-seq', '*done = (i32)(observed - fence.seq) >= 0;', '*done = (i32)(fence.seq - observed) >= 0;'),
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
        ('pre-ack-gr', 'if (fault->first.first.graph_latched) {',
         'if (false) {'),
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


# H1 owns the combined A1-A3 oracle; H4/H5 retain the isolated BAR2 fixture.
GUARD_MUTATIONS = [
    ('dynamic-membership', 'mm/paging.c',
     'ledger_resource_page_mapped(k, p)',
     '(p >= ledger_resources[k].map_first && p < ledger_resources[k].map_end)',
     'FAIL !paging_master_audit'),
    ('decode-hull', 'mm/paging.c',
     'ledger_resource_page_mapped(k, p)',
     '(ledger_resource_page_mapped(k, p) || ((e & PTE_PRESENT) && p >= ledger_resources[k].decode_first && p < ledger_resources[k].decode_end))',
     'FAIL paging_master_audit'),
    ('failed-unmap-removes-registration', 'mm/paging_map.inc',
     'if (device_map_preflight(owner, rid, first, end, unmap, &slot) != 0)\n'
     '        return -1;',
     'if (device_map_preflight(owner, rid, first, end, unmap, &slot) != 0) {\n'
     '        if (unmap && !ledger_device_map_prepare(owner, rid, first, end, unmap, &slot))\n'
     '            ledger_device_map_commit(slot, 0);\n'
     '        return -1;\n    }',
     'FAIL f.mapped'),
]


def guard_mm(runners, mutate):
    """Use existing real boot/AS/lease scaffolding, with real GPU map adapters."""
    from test_gfx_kernel_fb import run_case
    def run(runner, mutation=None):
        changes = []
        if mutation:
            _, path, old, new, _ = mutation
            require((ROOT / path).read_text().count(old) == 1,
                    mutation[0] + ': anchor must be unique')
            # run_case expands paging's same-TU fragments before replacement.
            changes = [('mm/paging.c' if path == 'mm/paging_map.inc' else path, old, new)]
        def stage(tmp, sources):
            platform = ROOT / 'drivers/video/geforce/geforce_platform.c'
            body = re.sub(r'^#include "([^"]+)"',
                          lambda m: '#include "' + str((platform.parent / m[1]).resolve()) + '"'
                          if (platform.parent / m[1]).is_file() else m[0],
                          platform.read_text(), flags=re.MULTILINE)
            (tmp / 'guard_platform.c').write_text(body)
            sources.extend([ROOT / 'tools/tests/recover_gf_dev_stub.c', ROOT / 'hwdev/gf/instmem_map.c',
                            ROOT / 'hwdev/gf/command_platform.c'])
        result = run_case(None, runner, changes,
                          '#include "gf_command_guard_host.c"\n', stage=stage)
        print(result.stdout + result.stderr, end='', flush=True)
        require(result.returncode == (1 if mutation else 0),
                f'guard-mm: runner={runner} rc={result.returncode}')
        if mutation:
            require(mutation[4] in result.stdout, mutation[0] + ': wrong runtime failure')
        print('guard-mm ' + (mutation[0] + ': runtime RED' if mutation else 'control: runtime GREEN'), flush=True)
    for runner in runners:
        run(runner)
        if mutate:
            for mutation in GUARD_MUTATIONS:
                run(runner, mutation)
                run(runner)


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
                    *sources, str(ROOT / 'tools/tests/recover_gf_dev_stub.c'), '-o', str(exe)], cwd=ROOT, check=True)
    return exe


def build_integration(component, source, directory):
    """Reuse the owner/module oracles with their real current dependencies."""
    exe = directory / 'host'
    if component == 't2b-owner':
        from test_gfinit_owner import command
        commands = [command(exe)]
    else:
        from test_module_runtime import FLAGS, INCLUDES
        from test_gf_display import gf4_sources
        flags = ['gcc', *FLAGS, '-Wno-unused-parameter', *INCLUDES,
                 '-I'+str(ROOT), '-Igfx', '-Ibuild/out/geforce',
                 '-I'+str(ROOT/'tools/tests'), '-DGF_PRODUCTION_MODULE']
        backend = directory/'real_backend.o'
        aliases = ('geforce_backend_ops', 'geforce_backend_bind',
                   'geforce_backend_wc_bind', 'geforce_backend_stop_bind', 'geforce_heat_stop_request')
        fixture = ROOT/'tools/tests/geforce_module_host.c'
        sources = gf4_sources(ROOT, 'c3', fixture)
        commands = [
            [*flags, '-DOS32_GEFORCE_MODULE',
             *['-D'+name+'=command_real_'+name for name in aliases],
             '-c', str(ROOT/'drivers/video/geforce/backend_geforce.c'),
             '-o', str(backend)],
            [*flags, str(fixture), *[str(ROOT/f) for f in sources], str(backend),
             '-nostdlib', '-static', '-no-pie', '-Wl,--gc-sections', '-o', str(exe)]]
    copied = Path(tempfile.mkdtemp(prefix='integration-', dir=directory))
    if component == 't2b-owner':
        mutpar.build_in_tree(ROOT, copied, {COMPONENTS[component]: source.read_text()}, commands)
    else:
        # Dependency discovery runs before compiling the preceding object.
        dependencies = [commands[0], [arg for arg in commands[1] if arg != str(backend)]]
        tree = mutpar.mutant_tree(ROOT, copied/'tree',
            {COMPONENTS[component]: source.read_text()}, gcc_cmds=dependencies)
        for cmd in commands:
            subprocess.run(mutpar.rebase(cmd, ROOT, tree), cwd=tree, check=True)
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
    selected = (args.component,) if args.component else tuple(COMPONENTS)
    if 'guard-mm' in selected:
        guard_mm(runners, args.mutate)
        selected = tuple(c for c in selected if c != 'guard-mm')
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
