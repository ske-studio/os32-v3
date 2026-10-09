#!/usr/bin/env python3
"""g3: real kernel/nano cooperative trim flow with independent app state.

Only the host MMU, KAPI transport and WM delivery are fixture seams. Private
newlib nano objects retain their cross-toolchain ABI and are namespaced per
application, just as separately linked USER images isolate allocator state.
"""
import argparse
import importlib.util
import json
import os
from pathlib import Path
import subprocess
import tempfile

import host32
import g3fix_wm_probe
import test_trim_fixtures
import test_trim_kernel as kernel

ROOT = Path(__file__).resolve().parents[2]
TARGET_SRCS = [
    'exec/appslot.c', 'exec/exec.c', 'kernel/gui.c', 'kernel/kselftest.c',
    'kapi/kapi_sys.c', 'exec/exec_heap.c', 'exec/appmem.c', 'exec/appmem_map.c',
    'exec/appmem_unmap.c', 'kernel/paging_app.c', 'kernel/physmem.c',
    'sdk/allocator/nano_adapter.c',
    'tools/tests/trim_flow_host.c', 'tools/tests/multiapp_model_host.c',
]
CASES = ('success', 'noanswer', 'raw', 'cui', 'realloc', 'remark', 'blocked-primary', 'input-exception')
MUTANTS = [
    ('poll-excludes-input', 7, 'tools/tests/multiapp_model_host.c',
     'return (a->state == MA_PARKED || a->state == MA_WAIT_POLL) && a->input_ready;',
     'return a->state == MA_PARKED && a->input_ready;',
     'input exception preserves pending before retry'),
    ('poll-excludes-ready', 7, 'tools/tests/multiapp_model_host.c',
     'if (a->state != MA_PARKED && a->state != MA_WAIT_POLL) return 0;',
     'if (a->state != MA_PARKED) return 0;',
     'input exception ready includes requester'),
    ('blocked-primary-drops-secondary-mark', 6, 'exec/appmem_map.c',
     'if (rc == APPMEM_ENOSPC) appslot_trim_request_as(as);',
     '/* omit the secondary arena pressure request */',
     'blocked primary marks before first yield'),
    ('map-mid-pump', 0, 'exec/appmem_map.c',
     'rc = paging_app_stage(&tx, as, plan.base, plan.end);',
     'rc = paging_app_stage(&tx, as, plan.base, plan.end);\n'
     '    extern void flow_pump_observe(void); flow_pump_observe();',
     'no WM inside map transaction'),
    ('retry-raw-map', 2, 'tools/tests/trim_flow_host.c',
     'if (FLOW_CASE == 2) p = map_at(3 * PAGE_SIZE, 0, 0);',
     'if (FLOW_CASE == 2) { p = map_at(3 * PAGE_SIZE, 0, 0); if (!p) flow_yield(); }',
     'raw map no yield'),
    ('cui-self-delivery', 3, 'exec/appslot.c',
     'if (requester < 0) return;',
     'if (requester < 0) return;\n'
     '    if (!g_slot[requester].gui) { g_slot[requester].trim_pending = 1; '
     'g_slot[requester].trim_epoch = appslot_trim_epoch; }',
     'requester not marked'),
    ('cui-drops-back-mark', 3, 'exec/appslot.c',
     'if (requester < 0) return;',
     'if (requester < 0 || !g_slot[requester].gui) return;',
     'mark target count'),
    ('retry-before-unlock', 0, 'sdk/allocator/nano_adapter.c',
     'p = allocate(r, 1, size, 0, NULL);\n    leave();',
     'p = allocate(r, 1, size, 0, NULL);\n'
     '    if (!p && gui_retry) kapi->sys_yield();\n    leave();',
     'retry after unlock'),
    ('front-no-yield', 0, 'sdk/allocator/nano_adapter.c',
     'kapi->sys_yield();', '(void)0;', 'one whole operation retry'),
]

BRIDGE = r'''
#include "flow_adapter.c"
extern void *flow_map(void *, size_t, unsigned);
extern int flow_unmap(void *, uintptr_t, size_t);
extern int flow_grow(void *, uintptr_t, size_t);
extern void flow_yield(void);
extern int flow_gui(unsigned, unsigned);
static struct _reent fixture_reent;
struct _reent *_impure_ptr = &fixture_reent;
static KernelAPI fixture_api;
KernelAPI *kapi = &fixture_api;
static struct os32_nano_arena fixture_primary;
static i32 fixture_yield(void) { flow_yield(); return 0; }
static int fixture_gui(u32 op, u32 arg) { return flow_gui(op, arg); }
static unsigned (*fixture_hook)(void);
static uint32_t fixture_call_hook(void) { return fixture_hook(); }
void fixture_init(unsigned base, unsigned bytes)
{
    fixture_api.sys_yield = fixture_yield;
    fixture_api.gui_call = fixture_gui;
    fixture_primary.initial = fixture_primary.brk = base;
    fixture_primary.mapped_end = base + bytes;
    fixture_primary.limit = UINT32_MAX;
    fixture_primary.grow_exact = flow_grow;
    if (!os32_nano_configure(&fixture_primary, flow_map, flow_unmap, NULL))
        __builtin_trap();
}
void fixture_enable(void) { os32_gui_retry_enable(); }
unsigned fixture_serve(unsigned epoch, unsigned (*hook)(void))
{
    fixture_hook = hook;
    return os32_gui_trim_serve(epoch, hook ? fixture_call_hook : NULL);
}
unsigned fixture_retries(void) { return os32_gui_retry_stats().retry_count; }
unsigned fixture_serves(void) { return os32_gui_retry_stats().trim_serve_count; }
unsigned fixture_trim(void) { return os32_nano_trim(); }
int fixture_busy(void) { return busy; }
int fixture_errno(void) { return fixture_reent._errno; }
int fixture_nomem(void) { return fixture_reent._errno == ENOMEM; }
'''


def nano_builder():
    spec = importlib.util.spec_from_file_location(
        'trim_flow_nano_builder', ROOT / 'sdk/allocator/build_nano.py')
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def build_nano(tmp, source):
    builder = nano_builder()
    prefix = Path(os.environ.get('CROSS_DIR', '/usr/local/cross'))
    (tmp / 'flow_adapter.c').write_text(source)
    bridge = tmp / 'nano_bridge.c'
    bridge.write_text(BRIDGE)
    directory = tmp / 'nano'
    builder.build(prefix, directory, adapter=bridge)
    members = json.loads(builder.inputs.LEDGER.read_text())['sdk_build']['members']
    members = [directory / member for member in members] + [directory / 'nano_adapter.o']
    symbols = set()
    for member in members:
        listing = builder.inputs.command(prefix, 'nm', '-g', '--defined-only', '-P', member)
        symbols.update(line.split()[0] for line in listing.decode().splitlines() if line.split())
    objects = []
    for app in ('A', 'B', 'C'):
        renames = tmp / (app + '_symbols.txt')
        renames.write_text(''.join(
            symbol + ' ' + app + '_' + symbol.removeprefix('fixture_') + '\n'
            for symbol in sorted(symbols)))
        for index, member in enumerate(members):
            obj = tmp / f'{app}_nano_{index}.o'
            builder.inputs.command(prefix, 'objcopy', '--redefine-syms=' + str(renames), member, obj)
            objects.append(str(obj))
    return objects


def build_fixture(tmp, sources):
    cc = kernel.build_fixture(tmp, sources)
    fixture = tmp / 'mem_fixture.c'
    source = fixture.read_text()
    anchor = '#include "public_wrap_source.c"'
    assert source.count(anchor) == 1
    # The shared f9 negative-pointer fixture binds this public wrapper to a
    # synthetic caller. End that binding: the flow uses real active AS state.
    fixture.write_text(source.replace(anchor, '#undef caller_access_get\n' + anchor))
    (tmp / 'trim_flow_source.c').write_text(sources['tools/tests/trim_flow_host.c'])
    (tmp / 'multiapp_model_host.c').write_text(sources['tools/tests/multiapp_model_host.c'])
    objects = []
    for index, name in enumerate(('exec/appmem.c', 'exec/appmem_map.c',
                                  'exec/appmem_unmap.c', 'kernel/paging_app.c',
                                  'kernel/physmem.c')):
        src, obj = tmp / f'part{index}.c', tmp / f'part{index}.o'
        src.write_text(sources[name])
        extra = []
        if name == 'exec/appmem_map.c':
            extra = ['-Dpaging_app_stage=flow_stage', '-Dappmem_publish=flow_publish']
        if name == 'kernel/paging_app.c':
            extra = ['-Dpgalloc_alloc_phys=map_alloc', '-Dpgalloc_free_n_owner=map_free']
        host32.build(cc + extra + ['-c', str(src), '-o', str(obj)],
                     check=True, capture_output=True, text=True)
        objects.append(str(obj))
    return cc, objects + build_nano(tmp, sources['sdk/allocator/nano_adapter.c'])


def execute(tmp, cc, objects, case, runner, flags=()):
    output = tmp / ('flow-' + CASES[case])
    host32.build(cc + list(flags) + ['-DFLOW_CASE=' + str(case),
        '-nostdlib', '-static', '-no-pie', '-Wl,--gc-sections',
        str(tmp / 'trim_flow_source.c'), *objects, '-o', str(output)],
        check=True, capture_output=True, text=True)
    return host32.run([str(output)], runner=runner, capture_output=True, text=True,
                      timeout=host32.RUN_TIMEOUT)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--runner', choices=('native', 'qemu'), default=host32.selected_runner())
    parser.add_argument('--mutate', action='store_true')
    args = parser.parse_args()
    sources = {name: (ROOT / name).read_text() for name in TARGET_SRCS}
    with tempfile.TemporaryDirectory(prefix='trim-flow-') as directory:
        tmp = Path(directory)
        cc, objects = build_fixture(tmp, sources)
        for case, name in enumerate(CASES):
            result = execute(tmp, cc, objects, case, args.runner)
            print(result.stdout, end='')
            assert result.returncode == 0, (name, result.returncode, result.stdout, result.stderr)
            print('PASS case=' + name + ' runner=' + args.runner)
        if args.mutate:
            for name, case, path, old, new, label in MUTANTS:
                assert sources[path].count(old) == 1, (name, sources[path].count(old))
                changed = dict(sources)
                changed[path] = sources[path].replace(old, new)
                cc, objects = build_fixture(tmp, changed)
                result = execute(tmp, cc, objects, case, args.runner)
                assert result.returncode == 1 and 'FAIL: ' + label in result.stdout.splitlines(), (
                    name, result.returncode, result.stdout, result.stderr)
                print('RED: ' + name + ' rc=1 -> FAIL: ' + label)
            print(f'trim flow: {len(MUTANTS)} runtime RED / 0 survived / 0 ERROR')
    test_trim_fixtures.run(args.mutate)
    g3fix_wm_probe.run()
    return 0


if __name__ == '__main__':
    try:
        raise SystemExit(main())
    except subprocess.CalledProcessError as error:
        for output in (error.stdout, error.stderr):
            if output:
                print(output.decode() if isinstance(output, bytes) else output)
        raise
