"""g1: real appslot/map/heap/gui/mem_stat with fault injection and USER frames.

Normal control always precedes mutations; only named runtime failures are RED.
The shared f9/f13 fixture simulates MMU/IRQ and external resource side effects.
"""
import argparse
import pathlib
import subprocess
import tempfile

import host32
import test_mem_stat as base

ROOT = pathlib.Path(__file__).resolve().parents[2]
TARGET_SRCS = base.TARGET_SRCS + [
    'kernel/gui.c', 'kernel/gui.h', 'kernel/kselftest.c', 'fs/fd_redirect.h',
    'sdk/include/os32/os32_gui_shared.h', 'tools/tests/trim_kernel_host.c',
    'tools/tests/test_trim_kernel.py', 'tools/tests/test_mem_stat.py',
]

# Each substitution applies to exactly one product input, never the assertions.
MUTANTS = [
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
    (tmp / 'mem_stat_source.c').write_text(stat[stat.index('STATIC_ASSERT(sizeof(MemStat)'):stat.index('/* カーネルビルド時')])
    (tmp / 'trim_selftest_source.c').write_text(
        fn(sources['kernel/kselftest.c'], 'static void test_appmem('))
    (tmp / 'heap_source.c').write_text(sources['exec/exec_heap.c'])
    return cc


def execute(tmp, sources, runner):
    cc = build_fixture(tmp, sources)
    objects = []
    for i, name in enumerate(('exec/appmem.c', 'exec/appmem_map.c', 'exec/appmem_unmap.c',
                              'kernel/paging_app.c', 'kernel/physmem.c')):
        src, obj = tmp / f'part{i}.c', tmp / f'part{i}.o'
        src.write_text(sources[name])
        extra = ['-Dpgalloc_alloc_phys=map_alloc', '-Dpgalloc_free_n_owner=map_free'] if name == 'kernel/paging_app.c' else []
        host32.build(cc + extra + ['-c', str(src), '-o', str(obj)],
                     check=True, capture_output=True, text=True)
        objects.append(str(obj))
    out = tmp / 'trim'
    host32.build(cc + ['-nostdlib', '-static', '-no-pie', '-Wl,--gc-sections',
        str(ROOT / 'tools/tests/trim_kernel_host.c'), *objects, '-o', str(out)],
        check=True, capture_output=True, text=True)
    return host32.run([str(out)], runner=runner, capture_output=True, text=True,
                      timeout=host32.RUN_TIMEOUT)


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--mutate', action='store_true')
    p.add_argument('--runner', choices=('native', 'qemu'), default='native')
    args = p.parse_args()
    paths = ['exec/appslot.c', 'exec/exec.c', 'kernel/gui.c', 'kernel/kselftest.c',
             'kapi/kapi_sys.c', 'exec/exec_heap.c', 'exec/appmem.c', 'exec/appmem_map.c',
             'exec/appmem_unmap.c', 'kernel/paging_app.c', 'kernel/physmem.c']
    sources = {name: (ROOT / name).read_text() for name in paths}
    with tempfile.TemporaryDirectory(prefix='trim-kernel-') as directory:
        tmp = pathlib.Path(directory)
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
                print('RED:', name)
    return 0


if __name__ == '__main__':
    try:
        raise SystemExit(main())
    except subprocess.CalledProcessError as error:
        print((error.stdout or '') + (error.stderr or ''))
        raise
