"""g1: real appslot/map/heap/gui/mem_stat with fault injection and USER frames.

Normal control always precedes mutations; only named runtime failures are RED.
The shared f9/f13 fixture simulates MMU/IRQ and external resource side effects.
"""
CHECK = dict(name='trim-kernel-host', tier=1,
    covers=['sys/include/legacy_guest.h', 'tools/tests/legacy_guest_fake.h', 'tools/tests/font_guard_inactive.h', 'sys/arch/x86/arch_cpu.h', 'sys/arch/x86/x86_flags.h', 'sys/arch/x86/arch_io.h', 'sys/platform/pc98/input/kbd.h', 'sys/kern/exec/access_walk.c', 'sys/kern/exec/appmem.c', 'sys/kern/exec/appmem.h', 'sys/kern/exec/appmem_map.c', 'sys/kern/exec/appmem_unmap.c', 'sys/kern/exec/appslot.c', 'sys/kern/exec/appslot.h', 'sys/kern/exec/exec.c', 'sys/kern/exec/ring3_str.c', 'sys/kern/exec/exec.h', 'sys/kern/exec/exec_heap.c', 'sys/kern/exec/exec_heap.h', 'sys/kern/exec/lease.c', 'sys/kern/exec/redir_access.c', 'sys/fs/fd_redirect.h', 'sys/include/appmem_types.h', 'sys/include/cpu.h', 'sys/include/io.h', 'sys/include/memmap.h', 'sys/platform/pc98/pc98.h', 'sys/include/redir_access.h', 'sys/include/sys.h', 'sys/include/text_screen.h', 'sys/include/types.h', 'sys/kapi/kapi_generated.c', 'sys/kapi/kapi_sysinfo.c', 'sys/platform/pc98/platform_mem_stat.h', 'sys/platform/powermac/platform_mem_stat.h', 'tools/tests/mem_stat_source.py', 'sys/kern/sqlite_engine.c', 'sys/arch/x86/kernel/gdt.h', 'sys/kern/gui.c', 'sys/kern/gui.h', 'sys/kern/ime/ime.h', 'sys/include/ime_control.h', 'sys/kern/mm/kmalloc.c', 'sys/kern/mm/kmalloc.h', 'sys/platform/pc98/kdebug/kselftest.c', 'sys/kern/mm/paging.c', 'sys/kern/mm/paging.h', 'sys/kern/mm/paging_app.c', 'sys/kern/mm/paging_app.h', 'sys/kern/mm/pgalloc.c', 'sys/kern/mm/pgalloc.h', 'sys/kern/mm/physmem.c', 'sys/kern/mm/physmem.h', 'sys/arch/x86/kernel/tss.h', 'sys/kern/kstring.h', 'sys/platform/pc98/platform_io.h', 'sdk/include/os32/os32_gui_shared.h', 'sdk/include/os32/os32_kapi_shared.h', 'sdk/kapi.json', 'tools/tests/appmem_map_host.c', 'tools/tests/exec_heap_host.h', 'tools/tests/host32.py', 'tools/tests/host_arch/arch_cpu.h', 'tools/tests/host_arch/arch_io.h', 'tools/tests/mem_stat_host.c', 'tools/tests/mutpar.py', 'tools/tests/pgalloc_host_fixture.h', 'tools/tests/test_appmem_map.py', 'tools/tests/test_mem_stat.py', 'tools/tests/trim_kernel_host.c', 'userland/shell/cmd_sys.c', 'userland/gshell/src/trim.rs', 'userland/rust/trim_back_rs/src/cache.rs', 'sys/kern/exec/owner_diag.h', 'sys/platform/pc98/kernel.c', 'sys/kern/kstack_hw.c', 'tools/tests/kdebug_cli_host.c', 'userland/shell/cmd_base.c', 'sys/kern/init/sysconfig.c', 'tools/tests/kernel_layout.py', 'sys/kern/init/kaudit_boot.c', 'sys/kern/mm/memory_boot.c', 'tools/tests/input_fixture_host.h', 'sys/hal/input/input_service.c', 'sys/hal/input/pointer_service.h', 'sys/hal/input/input_service.h', 'sys/include/kbd.h', 'tools/tests/kernel_includes.py', 'exec/exec_*.inc', 'tools/tests/exec_source.py', 'sdk/gen_kapi.py', 'sdk/kapi_abi_x86.py', 'sdk/target_profile.py', 'tools/tests/paging_source.py', 'sys/include/exec_arch.h', 'sys/arch/x86/arch_exec_storage.h', 'tools/tests/host_arch/arch_exec_storage.h', 'tools/tests/host_exec_context.h', 'sys/arch/x86/kernel/exec_context.c', 'sys/arch/x86/kernel/exec_entry.c', 'tools/tests/host_exec_entry.h', 'sys/arch/x86/kernel/tss.h'],
    needs=['make:all'], budget=4.7,
    kind='characterization',
)

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
    'sys/kern/gui.c', 'sys/kern/gui.h', 'sys/kern/kdebug/kselftest/appmem.inc', 'sys/fs/fd_redirect.h',
    'sdk/include/os32/os32_gui_shared.h', 'tools/tests/trim_kernel_host.c',
    'tools/tests/test_trim_kernel.py', 'tools/tests/test_mem_stat.py',
]

# Each substitution applies to exactly one product input, never the assertions.
MUTANTS = [
    ('fallback-removed', 'sys/kern/exec/exec_heap.c',
     'if (rc) {\n            if (appmem_map(as, &as->appmem, &as->appmem_layout, bytes, 0,',
     'if (rc) {\n            return 0;\n            if (appmem_map(as, &as->appmem, &as->appmem_layout, bytes, 0,',
     'TOPDOWN 16 pages'),
    ('saturation-stops-mark', 'sys/kern/exec/appslot.c',
     'if (appslot_trim_epoch != ~(u32)0) appslot_trim_epoch++;',
     'if (appslot_trim_epoch == ~(u32)0) return;\n    appslot_trim_epoch++;',
     'epoch saturation'),
    ('mark-excludes-wait-poll', 'sys/kern/exec/appslot.c',
     'a->state != APP_STATE_WAIT_KEY &&\n            a->state != APP_STATE_WAIT_POLL',
     'a->state != APP_STATE_WAIT_KEY', 'back states marked'),
    ('enova-marks', 'sys/kern/exec/appmem_map.c',
     'int rc = appmem_prepare(table, layout, bytes, hint, map_flags, kind, extent_flags, &plan);\n    if (rc) return rc;',
     'int rc = appmem_prepare(table, layout, bytes, hint, map_flags, kind, extent_flags, &plan);\n    if (rc) { if (rc == APPMEM_ENOVA) appslot_trim_request_as(as); return rc; }',
     'prepare errors do not mark'),
    ('mark-requester', 'sys/kern/exec/appslot.c', 'id == requester || ', '', 'requester excluded'),
    ('mark-cui', 'sys/kern/exec/appslot.c', '!a->gui || ', '', 'ineligible excluded'),
    ('mark-running', 'sys/kern/exec/appslot.c',
     'a->state != APP_STATE_PARKED && a->state != APP_STATE_WAIT_KEY &&\n            a->state != APP_STATE_WAIT_POLL) continue;',
     'a->state != APP_STATE_RUNNING && a->state != APP_STATE_PARKED && a->state != APP_STATE_WAIT_KEY &&\n            a->state != APP_STATE_WAIT_POLL) continue;',
     'ineligible excluded'),
    ('remark-changes-epoch', 'sys/kern/exec/appslot.c', ' || a->trim_pending', '', 'pending coalesces'),
    ('epoch-wrap', 'sys/kern/exec/appslot.c', 'if (appslot_trim_epoch != ~(u32)0) appslot_trim_epoch++;',
     'appslot_trim_epoch++;', 'epoch saturation'),
    ('start-keeps-bit', 'sys/kern/exec/appslot.c', 'a->trim_pending = 0;\n    a->trim_epoch = 0;',
     '/* retain both fields */', 'start clears trim'),
    ('done-any-epoch', 'sys/kern/exec/appslot.c', ' && a->trim_epoch == epoch',
     ' && (epoch || !epoch)', 'stale immutable'),
    ('done-trims-on-stale', 'sys/kern/exec/exec.c',
     'if (!appslot_trim_done(c.app_id, epoch)) return OS32_ERR_STALE;',
     'if (!appslot_trim_done(c.app_id, epoch)) { exec_heap_user_trim(slot->as); return OS32_ERR_STALE; }',
     'stale immutable'),
    ('done-other-as', 'sys/kern/exec/exec.c', 'slot->as != c.as || ', '', 'other AS refused'),
    ('done-from-wm', 'sys/kern/exec/exec.c', ' || ring3_wm_depth)', ')', 'context refused'),
    ('done-forwards-to-wm', 'sys/kern/gui.c',
     'if (op == GUI_OP_TRIM_DONE) return exec_trim_done(arg);', '', 'stale immutable'),
    ('done-keeps-bit', 'sys/kern/exec/appslot.c', '        a->trim_pending = 0;',
     '        /* keep pending */', 'DONE accepted'),
    ('mark-cpl0', 'sys/kern/exec/appslot.c', '!a->cpl3 || ', '', 'ineligible excluded'),
    ('mark-poisoned', 'sys/kern/exec/appslot.c', 'a->as->appmem_poisoned || ', '', 'ineligible excluded'),
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
    (tmp / 'trim_slot_source.c').write_text(sources['sys/kern/exec/appslot.c'])
    (tmp / 'trim_exec_source.c').write_text('#include "exec_legacy.h"\n' + fn(sources['sys/kern/exec/exec.c'], 'i32 exec_trim_done('))
    (tmp / 'trim_gui_source.c').write_text(sources['sys/kern/gui.c'])
    stat = sources['sys/kapi/kapi_sysinfo.c']
    (tmp / 'mem_stat_source.c').write_text(base.mem_stat_source(stat))
    setters = ('appslot_trim_epoch', 'appslot_trim_request_count', 'appslot_trim_mark_count',
               'appslot_trim_done_count', 'appslot_trim_done_reject_count',
               'appslot_reclaim_count', 'appslot_last_reclaim_id')
    (tmp / 'trim_selftest_source.c').write_text(
        ''.join('static void kdi_set_' + name + '(' + ('int' if name.endswith('_id') else 'u32') +
                ' value) { ' + name + ' = value; }\n' for name in setters) +
        fn(sources['sys/kern/kdebug/kselftest/appmem.inc'], 'static void test_appmem('))
    (tmp / 'heap_source.c').write_text(sources['sys/kern/exec/exec_heap.c'])
    return cc


def execute(tmp, sources, runner):
    cc = build_fixture(tmp, sources)
    objects = []
    for i, name in enumerate(('sys/kern/exec/appmem.c', 'sys/kern/exec/appmem_map.c', 'sys/kern/exec/appmem_unmap.c',
                              'sys/kern/mm/paging_app.c', 'sys/kern/mm/physmem.c')):
        src, obj = tmp / f'part{i}.c', tmp / f'part{i}.o'
        src.write_text(sources[name])
        extra = ['-Dpgalloc_alloc_phys=map_alloc', '-Dpgalloc_free_n_owner=map_free'] if name == 'sys/kern/mm/paging_app.c' else []
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
    paths = ['sys/kern/exec/appslot.c', 'sys/kern/exec/exec.c', 'sys/kern/gui.c', 'sys/kern/kdebug/kselftest/appmem.inc',
             'sys/kapi/kapi_sysinfo.c', 'sys/kern/exec/exec_heap.c', 'sys/kern/exec/appmem.c', 'sys/kern/exec/appmem_map.c',
             'sys/kern/exec/appmem_unmap.c', 'sys/kern/mm/paging_app.c', 'sys/kern/mm/physmem.c']
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
