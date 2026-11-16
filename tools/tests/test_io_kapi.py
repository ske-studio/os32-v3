#!/usr/bin/env python3
"""C2/L1: actual public boundary, policy and extracted exec finish/kill bodies."""
CHECK = dict(name='io-kapi-host', tier=1, budget=30, needs=[], covers=[
    'sdk/kapi.json', 'sdk/kapi_rust_gen.py', 'kernel/io_kapi.c', 'kernel/io_kapi.h', 'kernel/io_policy.c', 'kernel/io_policy.h',
    'kernel/io_work.h', 'exec/exec_syscall.inc', 'exec/exec_finish.inc', 'exec/exec_schedule.inc',
    'exec/appslot.h', 'exec/shell_service.h', 'include/app_ids.h',
    'tools/tests/io_kapi_host.c', 'tools/tests/test_io_kapi.py', 'tools/tests/host32.py',
    'include/types.h', 'include/callconv.h', 'sdk/include/os32/os32_kapi_shared.h'],
    basis=['IO_TIER_DESIGN §4', '§8', 'N4', 'AC-IO-1', 'AC-IO-2'],
    env_limit='PPC BE runtime requires powerpc-elf-gcc and qemu-ppc user mode')
import os
import json
import contextlib
import io
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile
import host32

MUTANTS = [
    ('syscall entry', 'io_syscall_begin(io_owner);', '(void)io_owner;'),
    ('syscall maximum', 'io_syscall_max_count = elapsed; /* IO_SYSCALL_MAX */', '(void)elapsed; /* IO_SYSCALL_MAX */'),
    ('syscall active', 'syscall_active &= ~(1u << owner);', '(void)owner;'),
    ('syscall exit', 'io_syscall_end(id); /* IO_SYSCALL_EXIT */', '(void)id; /* IO_SYSCALL_EXIT */'),
    ('syscall park', 'io_syscall_end(id); /* IO_SYSCALL_PARK */', '(void)id; /* IO_SYSCALL_PARK */'),
    ('Rust unsigned int', '"unsigned int":    "u32"', '"unsigned int":    "u16"'),
    ('Rust unsigned int pointer', '"unsigned int *":  "*mut u32"', '"unsigned int *":  "*mut u8"'),
    ('irq context', ' && !kctx_irq_depth', ''),
    ('exception context', ' && !kctx_exc_depth', ''),
    ('exclusion context', 'return _irq_enabled() && !kctx_irq_depth', 'return !kctx_irq_depth'),
    ('set owner', '(u32)owner, gen); /* IO_KAPI_SET_OWNER */', '0, gen); /* IO_KAPI_SET_OWNER */'),
    ('set generation', '(u32)owner, gen); /* IO_KAPI_SET_OWNER */', '(u32)owner, gen + 1); /* IO_KAPI_SET_OWNER */'),
    ('reset owner', '(u32)owner, gen); /* IO_KAPI_RESET_OWNER */', '0, gen); /* IO_KAPI_RESET_OWNER */'),
    ('get FULL/INVAL', 'return io_policy_get(bytes, cap, needed);', 'io_policy_get(bytes, cap, needed); return 0;'),
    ('set BUSY', 'return io_policy_set(bytes, len, (u32)owner, gen);', 'io_policy_set(bytes, len, (u32)owner, gen); return 0;'),
    ('reset BUSY', 'return io_policy_reset((u32)owner, gen);', 'io_policy_reset((u32)owner, gen); return 0;'),
    ('pump budget cap', 'budget_us < IO_PASS_US ? budget_us : IO_PASS_US', 'budget_us'),
    ('pump zero', 'if (!budget_us) return OS32_ERR_INVAL;', 'if (0) return OS32_ERR_INVAL;'),
    ('pump nosys', 'if (rc < 0) return rc;\n    return io_dispatch', 'if (0 && rc < 0) return rc;\n    return io_dispatch'),
    ('reclaim', 'io_policy_owner_exit((u32)owner, gen); /* IO_KAPI_RECLAIM */', '(void)gen; /* IO_KAPI_RECLAIM */'),
    ('common reclaim', 'io_kapi_owner_exit(id); /* IO_KAPI_RECLAIM_COMMON */', '(void)id; /* IO_KAPI_RECLAIM_COMMON */'),
    ('normal/fault reclaim', 'exec_reclaim_resources(id);\n        exec_teardown_app(a);', 'exec_teardown_app(a);'),
    ('kill reclaim', 'exec_reclaim_resources(id);\n    exec_teardown_app(a);', 'exec_teardown_app(a);'),
    ('kill child order', 'for (i = n - 1; i >= 0; i--)', 'for (i = 0; i < n; i++)'),
]

HEADERS = {
    'io.h': 'unsigned int irq_save(void);\nvoid irq_restore(unsigned int);\nint _irq_enabled(void);\n',
    'pgalloc.h': '#include "types.h"\nextern volatile u32 kctx_irq_depth, kctx_exc_depth;\n',
    'fd_redirect.h': 'int res_owner_get(void);\n',
    'shell_service.h': '#include "types.h"\ntypedef struct { u32 generation; } ShellService;\nextern ShellService g_shell_service;\n',
    'appslot.h': '''#ifndef FIXTURE_APPSLOT_H
#define FIXTURE_APPSLOT_H
#include "types.h"
#include "app_ids.h"
struct addrspace { u32 generation; };
typedef struct { int state, cpl3, gui, parent, jmpbuf; struct addrspace *as; } AppSlot;
AppSlot *appslot_get(int);
#endif
''',
}

def function(source, name):
    source = re.sub(r'/\*.*?\*/', '', source, flags=re.S)
    match = re.search(r'(?:static )?(?:void|i32) ' + name + r'\([^\n]*\)\s*\{', source)
    assert match, name
    depth, i = 1, match.end()
    while depth:
        depth += (source[i] == '{') - (source[i] == '}')
        i += 1
    return source[match.start():i] + '\n'


def exercise(root, work, sources, ppc):
    namespace = {'__name__': 'io_kapi_generator_test'}
    exec(compile(sources['kapi_rust_gen.py'], 'kapi_rust_gen.py', 'exec'), namespace)
    generated = work / 'io-types.rs'
    with contextlib.redirect_stdout(io.StringIO()):
        namespace['generate_rust'](json.loads((root/'sdk/kapi.json').read_text()), str(generated))
    rust = generated.read_text()
    if ('fn(bytes: *const u8, len: u32) -> i32' not in rust or
        'fn(bytes: *mut u8, cap: u32, needed: *mut u32) -> i32' not in rust):
        return 1
    for name, body in HEADERS.items(): (work / name).write_text(body)
    for name in ['io_kapi.c', 'io_policy.c']:
        (work / name).write_text(sources[name])
    (work / 'reclaim.inc').write_text(''.join([
        function(sources['exec_finish.inc'], 'exec_reclaim_resources'),
        function(sources['exec_finish.inc'], 'exec_reclaim_owned'),
        function(sources['exec_finish.inc'], 'exec_finish'),
        function(sources['exec_schedule.inc'], 'exec_kill_one'),
        function(sources['exec_schedule.inc'], 'exec_kill')]))
    park = function(sources['exec_syscall.inc'], 'exec_park_transfer')
    prefix = park[park.index('{') + 1:park.index('    arch_user_save(')]
    enter = sources['exec_syscall.inc']
    start = enter.index('    int io_owner = appslot_cur();')
    enter = enter[start:enter.index('    u32 result;', start)]
    (work/'park.inc').write_text('static void fixture_park(int id) {' + prefix + '}\n' +
                               'static void fixture_enter(void) {\n' + enter + '}\n')
    cc = '/home/hight/opt/cross-ppc/bin/powerpc-elf-gcc' if ppc else 'gcc'
    machine = ['-mcpu=7450', '-mbig-endian', '-msoft-float', '-mno-altivec',
               '-msdata=none', '-mcall-sysv', '-mno-eabi'] if ppc else ['-no-pie']
    exe = work / ('ppc' if ppc else 'x86')
    subprocess.run([cc, '-m32', '-std=gnu11', '-O2', '-Wall', '-Wextra', '-Werror',
        '-Wno-unused-function', '-Wno-unused-variable', '-ffreestanding', '-fno-pie',
        '-fno-stack-protector', '-nostdlib', '-static', *machine,
        '-I'+str(work), '-I'+str(root/'include'), '-I'+str(root/'kernel'),
        '-I'+str(root/'sdk/include/os32'), str(root/'tools/tests/io_kapi_host.c'),
        str(work/'io_kapi.c'), str(work/'io_policy.c'), '-o', str(exe)], check=True)
    return host32.run(['qemu-ppc', str(exe)] if ppc else [str(exe)], timeout=30).returncode


def run(ctx):
    sources = {name: (ctx.root / prefix / name).read_text() for prefix, name in [
        ('sdk','kapi_rust_gen.py'), ('kernel','io_kapi.c'), ('kernel','io_policy.c'),
        ('exec','exec_finish.inc'), ('exec','exec_schedule.inc'), ('exec','exec_syscall.inc')]}
    arches = [False]
    if Path('/home/hight/opt/cross-ppc/bin/powerpc-elf-gcc').is_file() and shutil.which('qemu-ppc'):
        arches.append(True)
    else: print('SKIP PPC runtime: toolchain/runner unavailable', flush=True)
    with tempfile.TemporaryDirectory(prefix='io-kapi-') as td:
        work = Path(td)
        for ppc in arches:
            assert exercise(ctx.root, work, sources, ppc) == 0, ('control', ppc)
            print('PASS public boundary/owner/finish/fault/kill/child ' + ('PPC BE' if ppc else 'x86 LE'), flush=True)
        if ctx.mutate:
            for name, old, new in MUTANTS:
                candidates = [key for key, value in sources.items() if old in value]
                assert len(candidates) == 1, (name, candidates)
                key = candidates[0]
                assert sources[key].count(old) == 1, name
                changed = dict(sources)
                changed[key] = sources[key].replace(old, new, 1)
                results = [exercise(ctx.root, work, changed, ppc) for ppc in arches]
                assert all(rc >= 0 for rc in results) and any(rc > 0 for rc in results), (name, results)
                print('RED runtime: ' + name + ' rc=' + str(results), flush=True)
    return 0

if __name__ == '__main__':
    from types import SimpleNamespace
    sys.exit(run(SimpleNamespace(root=Path(__file__).resolve().parents[2],
        mutate='--mutate' in sys.argv or os.environ.get('MUTATE') == '1')))
