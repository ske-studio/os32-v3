"""f13: real AS/allocator/caller snapshot and both builds of cmd_mem.

The appmem_map/exec_heap fixture supplies only host MMU/IRQ and side effects.
Product functions are extracted verbatim; mutations must compile and fail a
named runtime assertion (compiler failure is never RED).
"""
import argparse
import pathlib
import re
import subprocess
import tempfile

import host32
import test_appmem_map as base

ROOT = pathlib.Path(__file__).resolve().parents[2]
TARGET_SRCS = base.TARGET_SRCS + [
    'kapi/kapi_sys.c', 'userland/shell/cmd_sys.c',
    'tools/tests/mem_stat_host.c', 'tools/tests/test_mem_stat.py',
]


def function(source, signature):
    begin = source.index(signature)
    brace = source.index('{', begin)
    depth, end = 1, brace + 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[begin:end]


def build_fixture(tmp):
    """Reuse the real f9/f10 fixture with a separate entry point for f13."""
    def read(p):
        return (ROOT / p).read_text()
    exe = read('exec/exec.c')
    slot = read('exec/appslot.c')
    gen = read('kapi/kapi_generated.c')
    fixture = read('tools/tests/appmem_map_host.c').replace('void _start(void)', 'void map_fixture_start(void)')
    # Use the product slot lookup, including FREE filtering, not its fixture double.
    fixture = fixture.replace(function(fixture, 'AppSlot *appslot_get('), function(slot, 'AppSlot *appslot_get('))
    fixture = fixture.replace('int appslot_cur(void) { return current_slot; }',
                              '#define g_cur current_slot\n' + function(slot, 'int appslot_cur(') + '\n#undef g_cur')
    (tmp / 'mem_fixture.c').write_text(fixture)
    heap = read('tools/tests/exec_heap_host.h')
    heap = heap.replace(function(heap, 'AppSlot *appslot_at('), function(slot, 'AppSlot *appslot_at('))
    heap = heap.replace(function(heap, 'int ring3_call_from_user('),
                        function(read('exec/ring3_str.c'), 'int ring3_guard_active(') + '\n' + function(exe, 'int ring3_call_from_user('))
    (tmp / 'exec_heap_host.h').write_text(heap)
    io = read('tools/tests/host_arch/arch_io.h').replace(
        'static unsigned int host_arch_if = 0x202U;', 'extern unsigned int host_arch_if;')
    (tmp / 'arch_io.h').write_text(io)
    for name in ('paging', 'pgalloc'):
        (tmp / (name + '_source.c')).write_text(read('kernel/' + name + '.c'))
    parts = [function(exe, sig) for sig in
             ('void exec_addrspace_abort(', 'void ring3_abort_check(', 'static void exec_teardown_app(')]
    resume = function(exe, 'i32 exec_resume(')
    tail = resume[resume.index('    appslot_resume_commit('):resume.index('    return 0;   /* 到達しない */')]
    parts.append('static void host_resume_tail(int app_id) { AppSlot *a = appslot_get(app_id);\n' + tail + '\n}')
    dispatch = function(exe, 'void __cdecl ring3_syscall_dispatch(')
    tail = dispatch[dispatch.index('syscall_complete:') + len('syscall_complete:'):dispatch.rfind('}')]
    parts.append('static void host_syscall_tail(u32 *frame) { int prev_caller=0, prev_in_syscall=0; u32 *prev_frame=0;\n' + tail + '\n}')
    (tmp / 'exec_source.c').write_text('\n'.join(parts))
    (tmp / 'abort_clear_source.c').write_text(function(slot, 'int appslot_abort_clear('))
    lease = read('exec/lease.c')
    (tmp / 'lease_revoke_source.c').write_text(
        'volatile u32 lease_revoke_fail_count, lease_revoke_all_fail_count;\n' +
        '\n'.join(function(lease, sig) for sig in
                  ('static int context(', 'int lease_release(', 'int lease_revoke_sid(', 'int lease_revoke_all(')))
    (tmp / 'public_wrap_source.c').write_text('\n'.join(function(gen, sig) + ';' for sig in
        ('const u16 kapi_argptr[KAPI_FUNC_COUNT] = ', 'void * __cdecl wrap_mem_map(', 'int __cdecl wrap_mem_unmap(')))
    (tmp / 'heap_wrap_source.c').write_text('\n'.join(function(gen, sig) for sig in
        ('void * __cdecl wrap_mem_alloc(', 'void __cdecl wrap_mem_free(')))
    (tmp / 'heap_source.c').write_text(read('exec/exec_heap.c'))
    context = function(exe, 'static void exec_restore_context(').replace('exec_restore_context', 'heap_restore_context')
    a = exe.index('        if (want_ring3) paging_load_cr3(ctx->as->pd_phys);')
    b = exe.index('        if (want_ring3) kselftest_run_audit', a)
    (tmp / 'heap_context_source.c').write_text(context +
        '\nstatic void heap_init_context(AppSlot *ctx, int want_ring3, u32 exec_heap_base, u32 exec_heap_size) {\n' + exe[a:b] + '}\n')
    (tmp / 'wm_source.c').write_text('volatile u32 ring3_wm_depth_underflow;\n' +
        '\n'.join(function(exe, sig) for sig in ('void ring3_wm_enter(', 'void ring3_wm_leave(')))
    return ['gcc', '-m32', '-march=i386', '-std=gnu11', '-ffreestanding', '-fno-builtin',
            '-fno-pie', '-fno-stack-protector', '-ffunction-sections', '-fdata-sections',
            '-Wall', '-Wextra', '-Werror', '-DPHYSMEM_HOST_TEST=1', '-I' + str(tmp),
            *['-I' + str(ROOT / p) for p in ('tools/tests/host_arch', '.', 'include',
                'arch/x86', 'platform/pc98', 'kernel', 'lib', 'exec', 'fs', 'sdk/include/os32', 'tools/tests')]]


MUTANTS = [
    ('size ignored', 'snap.size = size < sizeof(snap) ? size : sizeof(snap);', 'snap.size = sizeof(snap);', 'size prefix'),
    ('invalid USER trusted', 'if (!caller_access_get_user(&caller)) goto done;',
     'if (!caller_access_get_user(&caller)) { caller.app_id = app_id; if (app_id == -1) app_id = 0; }', 'caller matrix'),
    ('USER restriction removed', 'if (app_id != 0 && app_id != caller.app_id) goto done;', '', 'caller matrix'),
    ('FREE accepted', 'slot = appslot_get(app_id);', 'slot = appslot_at(app_id);', 'caller matrix'),
    ('other USER NOTFOUND', 'if (app_id != 0 && app_id != caller.app_id) goto done;',
     'if (app_id != 0 && app_id != caller.app_id) { result = OS32_ERR_NOTFOUND; goto done; }', 'caller matrix'),
    ('ANON counted as ARENA', 'snap.extents[kind - 1]++;', 'snap.extents[(kind == APPMEM_ANON ? APPMEM_EXEC_ARENA : kind) - 1]++;', 'extent kinds'),
    ('used from size', 'snap.exec_heap_used = as->exec_heap_used;', 'snap.exec_heap_used = slot->exec_heap_size;', 'AS used'),
    ('used from slot', 'snap.exec_heap_used = as->exec_heap_used;', 'snap.exec_heap_used = slot->exec_heap_used;', 'AS used'),
    ('initial end substituted', 'snap.exec_heap_cur_end = as->appmem_layout.exec_heap_cur_end;',
     'snap.exec_heap_cur_end = slot->exec_heap_base + slot->exec_heap_size;', 'current end'),
    ('free from arenas', 'snap.extents_free = APPMEM_EXTENT_MAX - snap.extents_total;',
     'snap.extents_free = APPMEM_EXTENT_MAX - snap.extents[APPMEM_ANON - 1] - snap.extents[APPMEM_EXEC_ARENA - 1];', 'extent free'),
]


def run_body(runner, mutate):
    source = (ROOT / 'kapi/kapi_sys.c').read_text()
    source = source[source.index('STATIC_ASSERT(sizeof(MemStat)'):source.index('/* カーネルビルド時')]
    with tempfile.TemporaryDirectory(prefix='memstat-') as directory:
        tmp = pathlib.Path(directory)
        cc = build_fixture(tmp)
        objs = []
        for i, path in enumerate(['exec/appmem.c', 'exec/appmem_map.c', 'exec/appmem_unmap.c',
                                  'kernel/paging_app.c', 'kernel/physmem.c']):
            obj = tmp / f'part{i}.o'
            host32.build(cc + ['-c', str(ROOT / path), '-o', str(obj)], check=True, capture_output=True, text=True)
            objs.append(str(obj))
        def execute(body):
            (tmp / 'mem_stat_source.c').write_text(body)
            out = tmp / 'test'
            host32.build(cc + ['-nostdlib', '-static', '-no-pie', '-Wl,--gc-sections',
                str(ROOT / 'tools/tests/mem_stat_host.c'), *objs, '-o', str(out)],
                check=True, capture_output=True, text=True)
            return host32.run([str(out)], runner=runner, capture_output=True,
                              text=True, timeout=host32.RUN_TIMEOUT)

        control = execute(source)
        assert control.returncode == 0, (control.returncode, control.stdout, control.stderr)
        print(control.stdout, end='')
        if mutate:
            for name, old, new, label in MUTANTS:
                assert source.count(old) == 1, name
                result = execute(source.replace(old, new))
                assert result.returncode == 1 and 'FAIL: ' + label in result.stdout, (
                    name, result.returncode, result.stdout, result.stderr)
                print('RED:', name)


SHELL_FIXTURE = r'''
#include <stdarg.h>
#include "os32api.h"
#include "memmap.h"
static KernelAPI api, *g_api = &api;
static int scenario, calls[8], count;
static u32 pmem(void) { return 17408; }
static u32 ram(void) { return 16384; }
static u32 ht(void) { return 180224; }
static u32 hu(void) { return 8192; }
static u32 hf(void) { return 172032; }
static int enabled(void) { return 1; }
static void emit(char c) {
    __asm__ volatile("int $0x80" : : "a"(4), "b"(1), "c"(&c), "d"(1) : "memory");
}
static void puts(const char *s) { while (*s) emit(*s++); emit('\n'); }
static void print(u8 attr, const char *fmt, ...) {
    (void)attr; va_list ap; va_start(ap, fmt);
    while (*fmt) {
        if (*fmt++ != '%') { emit(fmt[-1]); continue; }
        unsigned width = 0;
        while (*fmt >= '0' && *fmt <= '9') width = width*10 + *fmt++ - '0';
        char spec = *fmt++;
        if (spec == 's') { const char *s = va_arg(ap, const char *); while (*s) emit(*s++); }
        else {
            u32 n = va_arg(ap, u32), radix = spec == 'X' ? 16 : 10;
            if (spec == 'd' && (i32)n < 0) { emit('-'); n = 0U-n; }
            char buf[16]; unsigned count = 0;
            do { buf[count++] = "0123456789ABCDEF"[n%radix]; n /= radix; } while (n);
            while (width > count) { emit('0'); width--; }
            while (count) emit(buf[--count]);
        }
    }
    va_end(ap);
}
static i32 stat(i32 id, void *out, u32 size) {
    calls[count++] = id;
    if (scenario == 2) return OS32_ERR_INVAL;
    if (id && (scenario == 1 || id == 3 || id == 5)) return OS32_ERR_NOTFOUND;
    MemStat s = {0}; s.size = sizeof(s); s.app_id = id == -1 ? 2 : id;
    s.phys_total_pages = 4096; s.phys_free_pages = 3000;
    s.resident_heap_total = MEM_SHELL_HEAP_SIZE; s.resident_heap_used = 48;
    if (id) {
        s.flags = MEMSTAT_HAS_AS; s.state = 2;
        s.extents_total = 6; s.extents_free = 26; s.arenas = 3;
        s.extents[0] = s.extents[1] = s.extents[3] = s.extents[4] = 1; s.extents[2] = 2;
        s.exec_heap_used = 136; s.img_end = MEM_EXEC_LOAD_ADDR + 123;
        s.primary_mapped_end = MEM_EXEC_LOAD_ADDR + MEM_PAGE_SIZE;
        s.sbrk_heap_limit = MEM_EXEC_LOAD_ADDR + 2*MEM_PAGE_SIZE;
        s.exec_heap_base = MEM_EXEC_HEAP_BASE; s.exec_heap_size = MEM_EXEC_HEAP_MIN;
        s.exec_heap_cur_end = MEM_EXEC_HEAP_BASE + 2*MEM_EXEC_HEAP_MIN;
        s.stack_top = MEM_APP_STACK_TOP; s.stack_size = MEM_EXEC_STACK_SIZE;
        s.guard_b = s.stack_top - s.stack_size - MEM_GUARD_SIZE;
    }
    if (size != sizeof(s)) return OS32_ERR_INVAL;
    for (u32 i=0; i<sizeof(s); i++) ((u8 *)out)[i] = ((u8 *)&s)[i];
    return sizeof(s);
}
CMD_SOURCE
int main(void) {
    api.sys_get_mem_kb=pmem; api.sys_ram_kb=ram; api.paging_enabled=enabled;
    api.kmalloc_total=ht; api.kmalloc_used=hu; api.kmalloc_free=hf;
    api.kprintf=print; api.mem_stat=stat;
#ifdef SHELL_AS_APP
    api.sbrk_heap_limit=MEM_EXEC_LOAD_ADDR + 2*MEM_PAGE_SIZE;
#else
    api.sbrk_heap_limit=MEM_SHELL_GUARD;
#endif
    for (scenario=0; scenario<3; scenario++) {
        print(0, "SCENARIO %d\n", scenario); count=0;
        if (cmd_mem(0, 0)) return 1;
#ifdef SHELL_AS_APP
        if (count != 2 || calls[0] != 0 || calls[1] != -1) { puts("FAIL: caller queries"); return 1; }
#else
        if (count != 5 || calls[0] != 0) { puts("FAIL: caller queries"); return 1; }
        for (int i=1; i<5; i++) if (calls[i] != i+1) { puts("FAIL: caller queries"); return 1; }
#endif
    }
    return 0;
}
void _start(void) {
    int rc = main();
    __asm__ volatile("int $0x80" : : "a"(1), "b"(rc));
    for (;;) {}
}
'''


def shell_output(output, app):
    def check(pattern, text, label):
        assert re.search(pattern, text), 'FAIL: ' + label
    screens = output.split('SCENARIO ')[1:]
    assert len(screens) == 3, 'FAIL: scenarios'
    first = screens[0]
    check(r'Memory Info:\n  Physical : 17408 KB \(17 MB\)\n  RAM      : 16384 KB \(16 MB\).*\n  Paging   : ENABLED\n  Heap Tot : 180224 B, Used: 8192 B, Free: 172032 B', first, 'existing lines')
    for pattern in [r'実測: phys pages total=4096 free=3000; resident heap total=462848 used=48 B',
                    r'定数: 00000000-00001000 NULL guard; 00001000-000A0000 V86 窓',
                    r'定数: 000A0000-000F0000 VRAM; 00100000-00200000 Kernel band',
                    r'定数: 00200000-00300000 SQLite band; DMA pool 002E8000-002F8000',
                    r'定数: 002FB000-002FC000 kstack guard; 002FC000-00300000 kstack',
                    r'定数: Shell band load=00300000 sbrk上限=00375000 stack=00376000-00380000',
                    r'定数: Shell exec_heap 予約=00380000-003F1000 \(452 KiB\)',
                    r'定数: shlib=80000000-80100000 image起点=80100000',
                    r'定数: exec_heap 予約起点=88000000; stack top=90000000 既定=262144 B; lease=F0000000-FE000000',
                    r'実測: app=2 state=2 flags=1 extent=6 \[1,1,2,1,1\] free=26 arena=3 heap_used=136 B',
                    r'image=80100000-8010007B primary_end=80101000 libc 初期末尾=80102000',
                    r'flags=0窓=80101000-88000000 exec_heap現在端=88020000 TOPDOWN=88020000-8FFBF000',
                    r'stack=8FFC0000-90000000 \(262144 B\); 初期値: exec_heap=88000000\+65536 B']:
        check(pattern, first, 'map output')
    assert first.count('実測: app=') == (1 if app else 2), 'FAIL: live count'
    if app:
        assert '実測: Shell band' not in output, 'FAIL: CPL3 resident limit'
    else:
        check(r'実測: Shell band sbrk上限=00375000', first, 'resident limit')
        check(r'実測: app=4 ', first, 'all live IDs')
        for id in (3, 5):
            check(rf'mem_stat app={id}: error=-2', first, 'NOTFOUND output')
    check(r'\(app なし\)', screens[1], 'empty output')
    check(r'mem_stat app=.*: error=-2', screens[1], 'NOTFOUND output')
    check(r'mem_stat system: error=-9', screens[2], 'INVAL output')
    check(r'mem_stat app=.*: error=-9', screens[2], 'INVAL output')


def shell_run(body, runner, app):
    assert not re.search(r'0x[0-9A-Fa-f]{5,}', body), 'FAIL: literal map'
    with tempfile.TemporaryDirectory(prefix='memcmd-') as directory:
        tmp = pathlib.Path(directory)
        c, out = tmp / 'cmd.c', tmp / 'cmd'
        c.write_text(SHELL_FIXTURE.replace('CMD_SOURCE', body))
        cmd = ['gcc', '-m32', '-std=gnu11', '-Wall', '-Wextra', '-Werror', '-static',
               '-nostdlib', '-fno-builtin', '-fno-pie', '-no-pie', '-fno-stack-protector',
               '-I' + str(ROOT / 'sdk/include/os32'), '-I' + str(ROOT / 'include'),
               *(['-DSHELL_AS_APP'] if app else []), str(c), '-o', str(out)]
        host32.build(cmd, check=True, capture_output=True, text=True)
        result = host32.run([str(out)], runner=runner, capture_output=True,
                            text=True, timeout=host32.RUN_TIMEOUT)
        assert result.returncode == 0, result.stdout + result.stderr
        shell_output(result.stdout, app)
        return result.stdout


def run_shell(runner, mutate):
    source = function((ROOT / 'userland/shell/cmd_sys.c').read_text(), 'static int cmd_mem(')
    for app in (False, True):
        output = shell_run(source, runner, app)
        print('CPL3' if app else 'CPL0')
        print(output.split('SCENARIO 1')[0], end='')
    if mutate:
        variants = [
            ('literal map', '(u32)MEM_EXEC_HEAP_BASE', '0x88000000U', 'literal map'),
            ('reservation line', 'exec_heap 予約起点=', 'exec_heap 起点=', 'map output'),
            ('CPL0 self only', 'int first = 2, last = 5;', 'int first = -1, last = -1;', 'caller queries'),
            ('CPL3 resident limit', '#ifndef SHELL_AS_APP', '#if 1', 'CPL3 resident limit'),
        ]
        for name, old, new, label in variants:
            assert old in source
            failed = False
            for app in (False, True):
                try:
                    shell_run(source.replace(old, new), runner, app)
                except AssertionError as error:
                    assert 'FAIL: ' + label in str(error), str(error)
                    failed = True
                    break
            assert failed, 'survived: ' + name
            print('RED:', name)
    print('PASS cmd_mem CPL0/CPL3 normal/empty/errors')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--runner', choices=('native', 'qemu'), default='qemu')
    parser.add_argument('--mutate', action='store_true')
    args = parser.parse_args()
    try:
        run_body(args.runner, args.mutate)
        # cmd_mem controls are below; both paths run even with --mutate.
        run_shell(args.runner, args.mutate)
    except subprocess.CalledProcessError as error:
        print(error.stdout or '', error.stderr or '')
        raise
