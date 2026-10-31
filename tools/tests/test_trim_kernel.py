"""g1: real appslot/map/heap/gui/mem_stat with fault injection and USER frames.

Normal control always precedes mutations; only named runtime failures are RED.
The shared f9/f13 fixture simulates MMU/IRQ and external resource side effects.
"""
CHECK = dict(name='trim-kernel-host', tier=1,
    covers=['tools/tests/font_guard_inactive.h', 'arch/x86/arch_cpu.h', 'arch/x86/x86_flags.h', 'arch/x86/arch_io.h', 'drivers/input/kbd.h', 'exec/access_walk.c', 'exec/appmem.c', 'exec/appmem.h', 'exec/appmem_map.c', 'exec/appmem_unmap.c', 'exec/appslot.c', 'exec/appslot.h', 'exec/exec.c', 'exec/exec_image.inc', 'exec/exec_argv.inc', 'exec/exec_kapi_init.inc', 'exec/ring3_str.c', 'exec/exec.h', 'exec/exec_heap.c', 'exec/exec_heap.h', 'exec/lease.c', 'exec/redir_access.c', 'fs/fd_redirect.h', 'include/appmem_types.h', 'include/cpu.h', 'include/io.h', 'include/memmap.h', 'include/pc98.h', 'include/redir_access.h', 'include/sys.h', 'include/tvram.h', 'include/types.h', 'kapi/kapi_generated.c', 'kapi/kapi_sys.c', 'kernel/sqlite_engine.c', 'arch/x86/kernel/gdt.h', 'kernel/gui.c', 'kernel/gui.h', 'kernel/ime/ime.h', 'mm/kmalloc.c', 'mm/kmalloc.h', 'kdebug/kselftest.c', 'kdebug/kselftest/appmem.inc', 'mm/paging.c', 'mm/paging.h', 'mm/paging_app.c', 'mm/paging_app.h', 'mm/pgalloc.c', 'mm/pgalloc.h', 'mm/physmem.c', 'mm/physmem.h', 'arch/x86/kernel/tss.h', 'lib/kstring.h', 'platform/pc98/platform_io.h', 'sdk/include/os32/os32_gui_shared.h', 'sdk/include/os32/os32_kapi_shared.h', 'sdk/kapi.json', 'tools/tests/appmem_map_host.c', 'tools/tests/exec_heap_host.h', 'tools/tests/host32.py', 'tools/tests/host_arch/arch_cpu.h', 'tools/tests/host_arch/arch_io.h', 'tools/tests/mem_stat_host.c', 'tools/tests/mutpar.py', 'tools/tests/pgalloc_host_fixture.h', 'tools/tests/test_appmem_map.py', 'tools/tests/test_mem_stat.py', 'tools/tests/trim_kernel_host.c', 'userland/shell/cmd_sys.c', 'userland/gshell/src/trim.rs', 'userland/rust/trim_back_rs/src/cache.rs', 'exec/owner_diag.h', 'init/kernel.c', 'kernel/kstack_hw.c', 'tools/tests/kdebug_cli_host.c', 'userland/shell/cmd_base.c', 'init/sysconfig.c', 'tools/tests/kernel_layout.py', 'init/kaudit_boot.c', 'mm/memory_boot.c', 'tools/tests/input_fixture_host.h', 'kernel/input_service.c', 'include/pointer_service.h', 'include/input_service.h', 'include/kbd.h', 'tools/tests/kernel_includes.py', 'init/kernel_boot_cpu.inc', 'init/kernel_boot_memory.inc', 'init/kernel_gfx_config.inc', 'kdebug/kselftest/abort_admit.inc', 'kdebug/kselftest/app_band_pde.inc', 'kdebug/kselftest/bootinfo.inc', 'kdebug/kselftest/bootlog.inc', 'kdebug/kselftest/caller_boot.inc', 'kdebug/kselftest/con_sink_render_gate.inc', 'kdebug/kselftest/cpu_calibrate.inc', 'kdebug/kselftest/dma8237.inc', 'kdebug/kselftest/dma_pool.inc', 'kdebug/kselftest/fd_redirect_access_guard.inc', 'kdebug/kselftest/gfx_ledger.inc', 'kdebug/kselftest/gfx_owner.inc', 'kdebug/kselftest/h4_backing.inc', 'kdebug/kselftest/heap.inc', 'kdebug/kselftest/ime_render_gate.inc', 'kdebug/kselftest/irq_dynamic.inc', 'kdebug/kselftest/kapi_layout.inc', 'kdebug/kselftest/kbd_cmd.inc', 'kdebug/kselftest/kbd_inject.inc', 'kdebug/kselftest/kcg_display_return.inc', 'kdebug/kselftest/kprintf.inc', 'kdebug/kselftest/kprintf_attr.inc', 'kdebug/kselftest/ledger.inc', 'kdebug/kselftest/map_user_keep.inc', 'kdebug/kselftest/mem.inc', 'kdebug/kselftest/memmap.inc', 'kdebug/kselftest/memmap_pool_user.inc', 'kdebug/kselftest/pcm.inc', 'kdebug/kselftest/pit_setup.inc', 'kdebug/kselftest/pool_model.inc', 'kdebug/kselftest/ring3_pd.inc', 'kdebug/kselftest/ring3_wm_guard.inc', 'kdebug/kselftest/slack_cycle.inc', 'kdebug/kselftest/str.inc', 'kdebug/kselftest/time_now.inc', 'kdebug/kselftest/tramp_user_str.inc', 'kdebug/kselftest/utoa.inc'],
    needs=['make:all'], budget=3.17668)

import argparse
import pathlib
import re
import struct
import subprocess
import tempfile

import host32
import test_mem_stat as base

ROOT = pathlib.Path(__file__).resolve().parents[2]
TARGET_SRCS = base.TARGET_SRCS + [
    'kernel/gui.c', 'kernel/gui.h', 'kdebug/kselftest/appmem.inc', 'fs/fd_redirect.h',
    'sdk/include/os32/os32_gui_shared.h', 'tools/tests/trim_kernel_host.c',
    'tools/tests/test_trim_kernel.py', 'tools/tests/test_mem_stat.py',
]

# Each substitution applies to exactly one product input, never the assertions.
MUTANTS = [
    ('fallback-removed', 'exec/exec_heap.c',
     'if (rc) {\n            if (appmem_map(as, &as->appmem, &as->appmem_layout, bytes, 0,',
     'if (rc) {\n            return 0;\n            if (appmem_map(as, &as->appmem, &as->appmem_layout, bytes, 0,',
     'TOPDOWN 16 pages'),
    ('saturation-stops-mark', 'exec/appslot.c',
     'if (appslot_trim_epoch != ~(u32)0) appslot_trim_epoch++;',
     'if (appslot_trim_epoch == ~(u32)0) return;\n    appslot_trim_epoch++;',
     'epoch saturation'),
    ('mark-excludes-wait-poll', 'exec/appslot.c',
     'a->state != APP_STATE_WAIT_KEY &&\n            a->state != APP_STATE_WAIT_POLL',
     'a->state != APP_STATE_WAIT_KEY', 'back states marked'),
    ('enova-marks', 'exec/appmem_map.c',
     'int rc = appmem_prepare(table, layout, bytes, hint, map_flags, kind, extent_flags, &plan);\n    if (rc) return rc;',
     'int rc = appmem_prepare(table, layout, bytes, hint, map_flags, kind, extent_flags, &plan);\n    if (rc) { if (rc == APPMEM_ENOVA) appslot_trim_request_as(as); return rc; }',
     'prepare errors do not mark'),
    ('mark-requester', 'exec/appslot.c', 'id == requester || ', '', 'requester excluded'),
    ('mark-cui', 'exec/appslot.c', '!a->gui || ', '', 'ineligible excluded'),
    ('mark-running', 'exec/appslot.c',
     'a->state != APP_STATE_PARKED && a->state != APP_STATE_WAIT_KEY &&\n            a->state != APP_STATE_WAIT_POLL) continue;',
     'a->state != APP_STATE_RUNNING && a->state != APP_STATE_PARKED && a->state != APP_STATE_WAIT_KEY &&\n            a->state != APP_STATE_WAIT_POLL) continue;',
     'ineligible excluded'),
    ('remark-changes-epoch', 'exec/appslot.c', ' || a->trim_pending', '', 'pending coalesces'),
    ('epoch-wrap', 'exec/appslot.c', 'if (appslot_trim_epoch != ~(u32)0) appslot_trim_epoch++;',
     'appslot_trim_epoch++;', 'epoch saturation'),
    ('start-keeps-bit', 'exec/appslot.c', 'a->trim_pending = 0;\n    a->trim_epoch = 0;',
     '/* retain both fields */', 'start clears trim'),
    ('done-any-epoch', 'exec/appslot.c', ' && a->trim_epoch == epoch',
     ' && (epoch || !epoch)', 'stale immutable'),
    ('done-trims-on-stale', 'exec/exec.c',
     'if (!appslot_trim_done(c.app_id, epoch)) return OS32_ERR_STALE;',
     'if (!appslot_trim_done(c.app_id, epoch)) { exec_heap_user_trim(slot->as); return OS32_ERR_STALE; }',
     'stale immutable'),
    ('done-other-as', 'exec/exec.c', 'slot->as != c.as || ', '', 'other AS refused'),
    ('done-from-wm', 'exec/exec.c', ' || ring3_wm_depth)', ')', 'context refused'),
    ('done-forwards-to-wm', 'kernel/gui.c',
     'if (op == GUI_OP_TRIM_DONE) return exec_trim_done(arg);', '', 'stale immutable'),
    ('done-keeps-bit', 'exec/appslot.c', '        a->trim_pending = 0;',
     '        /* keep pending */', 'DONE accepted'),
    ('mark-cpl0', 'exec/appslot.c', '!a->cpl3 || ', '', 'ineligible excluded'),
    ('mark-poisoned', 'exec/appslot.c', 'a->as->appmem_poisoned || ', '', 'ineligible excluded'),
]


def build_fixture(tmp, sources):
    cc = base.build_fixture(tmp)
    fn = base.function
    fixture = (tmp / 'mem_fixture.c').read_text()
    # Replace all slot doubles by the whole product translation unit. g_cur's
    # host name is shared with the existing MMU fixture, without changing code.
    start = fixture.index('static AppSlot g_slot[')
    end = fixture.index('static u32 ring3_abort_count', start)
    fixture = fixture[:start] + '''static AppSlot *g_cur_app;
#define g_cur current_slot
#define appslot_trim_request_as trim_product_request
#include "trim_slot_source.c"
#undef appslot_trim_request_as
#undef g_cur
''' + fixture[end:]
    for sig in ('AppSlot *appslot_get(', 'int appslot_cur(',
                'void appslot_resume_commit(', 'void appslot_mark_scheduled('):
        fixture = fixture.replace(fn(fixture, sig), '')
    fixture = fixture.replace('#include "abort_clear_source.c"', '')
    fixture = fixture.replace('int res_owner_get(void) { return current_slot; }', '''
int res_owner_get(void) { return current_slot; }
void res_owner_set(int id) { current_slot = id; }
void fd_redirect_clear_state(FdRedirectState *s) {
    u8 *p = (u8 *)s; for (u32 i = 0; i < sizeof(*s); i++) p[i] = 0;
}
void fd_redirect_save(FdRedirectState *s) { fd_redirect_clear_state(s); }
void fd_redirect_restore(const FdRedirectState *s) { (void)s; }
''')
    # Successful rollback injection, in contrast to f10's quarantine injection.
    fixture = fixture.replace('if (heap_alloc_rollback_fail) integration_free_fail = phys / PAGE_SIZE;', '')
    (tmp / 'mem_fixture.c').write_text(fixture)
    heap = (tmp / 'exec_heap_host.h').read_text()
    heap = heap.replace(fn(heap, 'AppSlot *appslot_at('), '')
    (tmp / 'exec_heap_host.h').write_text(heap)
    (tmp / 'trim_slot_source.c').write_text(sources['exec/appslot.c'])
    (tmp / 'trim_exec_source.c').write_text(fn(sources['exec/exec.c'], 'i32 exec_trim_done('))
    (tmp / 'trim_gui_source.c').write_text(sources['kernel/gui.c'])
    stat = sources['kapi/kapi_sys.c']
    (tmp / 'mem_stat_source.c').write_text(stat[stat.index('STATIC_ASSERT(sizeof(MemStat)'):stat.index('static void build_time_copy(')])
    setters = ('appslot_trim_epoch', 'appslot_trim_request_count', 'appslot_trim_mark_count',
               'appslot_trim_done_count', 'appslot_trim_done_reject_count',
               'appslot_reclaim_count', 'appslot_last_reclaim_id')
    (tmp / 'trim_selftest_source.c').write_text(
        ''.join('static void kdi_set_' + name + '(' + ('int' if name.endswith('_id') else 'u32') +
                ' value) { ' + name + ' = value; }\n' for name in setters) +
        fn(sources['kdebug/kselftest/appmem.inc'], 'static void test_appmem('))
    (tmp / 'heap_source.c').write_text(sources['exec/exec_heap.c'])
    return cc


def execute(tmp, sources, runner):
    cc = build_fixture(tmp, sources)
    objects = []
    for i, name in enumerate(('exec/appmem.c', 'exec/appmem_map.c', 'exec/appmem_unmap.c',
                              'mm/paging_app.c', 'mm/physmem.c')):
        src, obj = tmp / f'part{i}.c', tmp / f'part{i}.o'
        src.write_text(sources[name])
        extra = ['-Dpgalloc_alloc_phys=map_alloc', '-Dpgalloc_free_n_owner=map_free'] if name == 'mm/paging_app.c' else []
        host32.build(cc + extra + ['-c', str(src), '-o', str(obj)],
                     check=True, capture_output=True, text=True)
        objects.append(str(obj))
    out = tmp / 'trim'
    host32.build(cc + ['-nostdlib', '-static', '-no-pie', '-Wl,--gc-sections',
        str(ROOT / 'tools/tests/trim_kernel_host.c'), *objects, '-o', str(out)],
        check=True, capture_output=True, text=True)
    return host32.run([str(out)], runner=runner, capture_output=True, text=True,
                      timeout=host32.RUN_TIMEOUT)



def memstat_wire_layout(tmp, header):
    """Compile the real ILP32 header; read compiler-emitted offsetof values."""
    (tmp / 'os32_kapi_shared.h').write_text(header)
    fields = ('pressure_epoch', 'trim_pending_mask', 'trim_epoch', 'flags',
              'extents[1]', 'extents[3]', 'extents[4]', 'exec_heap_cur_end',
              'exec_heap_base', 'exec_heap_size')
    values = ['sizeof(MemStat)'] + [f'__builtin_offsetof(MemStat, {f})' for f in fields]
    src, obj, raw = tmp / 'layout.c', tmp / 'layout.o', tmp / 'layout.bin'
    src.write_text('#include "os32_kapi_shared.h"\n'
                   'const u32 layout[] = {' + ', '.join(values) + '};\n')
    host32.build(['gcc', '-m32', '-std=gnu11', '-Werror', '-I' + str(ROOT / 'sdk/include/os32'), '-c', str(src),
                  '-o', str(obj)], check=True, capture_output=True, text=True)
    subprocess.run(['objcopy', '--dump-section', f'.rodata={raw}', str(obj)],
                   check=True, capture_output=True, text=True)
    layout = struct.unpack('<' + 'I' * len(values), raw.read_bytes())
    trim = (ROOT / 'userland/gshell/src/trim.rs').read_text()
    cache = (ROOT / 'userland/rust/trim_back_rs/src/cache.rs').read_text()
    def constant(source, name):
        return int(re.search(r'const ' + name + r': usize = (\d+)', source)[1])
    expected = (constant(trim, 'MEMSTAT_SIZE'), 120,
                constant(trim, 'PENDING_OFFSET'), constant(trim, 'EPOCH_OFFSET'),
                *(4 * constant(cache, name) for name in
                  ('FLAGS', 'INITIAL', 'ARENA', 'LARGE', 'CUR', 'EXEC_BASE', 'EXEC_SIZE')))
    # WORDS is expressed as bytes / 4 in the fixture.
    words = re.search(r'const WORDS: usize = (\d+) / 4;', cache)
    assert words and int(words[1]) <= layout[0], 'fixture MemStat size'
    assert all(x + 4 <= int(words[1]) for x in expected[4:]), 'fixture MemStat prefix bounds'
    if expected[0] > layout[0] or layout[1:] != expected[1:] or any(x + 4 > expected[0] for x in expected[1:4]):
        print(f'FAIL: MemStat wire offsets: header={layout} consumers={expected}')
        return 1
    print('PASS: MemStat wire offsets size=132 pressure=120 pending=124 epoch=128; Rust fixture fields match')
    return 0


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--mutate', action='store_true')
    p.add_argument('--runner', choices=('native', 'qemu'), default='native')
    args = p.parse_args()
    paths = ['exec/appslot.c', 'exec/exec.c', 'kernel/gui.c', 'kdebug/kselftest/appmem.inc',
             'kapi/kapi_sys.c', 'exec/exec_heap.c', 'exec/appmem.c', 'exec/appmem_map.c',
             'exec/appmem_unmap.c', 'mm/paging_app.c', 'mm/physmem.c']
    sources = {name: (ROOT / name).read_text() for name in paths}
    with tempfile.TemporaryDirectory(prefix='trim-kernel-') as directory:
        tmp = pathlib.Path(directory)
        header = (ROOT / 'sdk/include/os32/os32_kapi_shared.h').read_text()
        assert memstat_wire_layout(tmp, header) == 0
        if args.mutate:
            old = 'u32 pressure_epoch, trim_pending_mask;'
            assert header.count(old) == 1
            rc = memstat_wire_layout(tmp, header.replace(old, 'u32 trim_pending_mask, pressure_epoch;'))
            assert rc != 0, 'MemStat field swap survived'
            print(f'RED: memstat-pressure-pending-swapped rc={rc} -> FAIL: MemStat wire offsets')
        (tmp / 'os32_kapi_shared.h').write_text(header)
        control = execute(tmp, sources, args.runner)
        print(control.stdout, end='')
        assert control.returncode == 0, (control.returncode, control.stdout, control.stderr)
        print('PASS runner=' + args.runner)
        if args.mutate:
            for name, path, old, new, label in MUTANTS:
                assert sources[path].count(old) == 1, (name, sources[path].count(old))
                changed = dict(sources); changed[path] = sources[path].replace(old, new)
                result = execute(tmp, changed, args.runner)
                assert result.returncode == 1 and 'FAIL: ' + label + '\n' in result.stdout, (
                    name, result.returncode, result.stdout, result.stderr)
                print('RED:', name, 'rc=1 -> FAIL: ' + label)
    return 0


if __name__ == '__main__':
    try:
        raise SystemExit(main())
    except subprocess.CalledProcessError as error:
        print((error.stdout or '') + (error.stderr or ''))
        raise


def run(ctx) -> int:
    import os
    import shlex
    import subprocess
    base_env = dict(os.environ, HOST32_RUNNERS=' '.join(ctx.runners))
    if not ctx.runners:
        return 1
    for index, runner in enumerate(ctx.runners):
        print('HOST32_RUNNERS=' + runner, flush=True)
        argv = ['python3', '-B', 'tools/tests/test_trim_kernel.py', '--runner', runner]
        if ctx.mutate and index == 0:
            argv.append('--mutate')
        rc = subprocess.call(argv, cwd=ctx.root, env=base_env, close_fds=False)
        if rc:
            return rc
    return 0
