"""R1 recovery probes: real lifecycle slices and real SDK/lease attachment."""
import re
import subprocess
from pathlib import Path
import host32

ROOT = Path(__file__).resolve().parents[2]
TARGET_SRCS = ['tools/tests/r1_recovery_host.c', 'tools/tests/r1_recovery.py',
               'exec/appslot.c', 'exec/appslot.h', 'exec/owner_diag.h',
               'kernel/paging.c', 'exec/lease.c']


def recovery(tmp, function, runner, mutation=None):
    def read(path):
        text = (ROOT / path).read_text()
        if mutation and path == mutation[0]:
            assert text.count(mutation[1]) == 1, mutation
            text = text.replace(mutation[1], mutation[2])
        return text
    def stage(name, text):
        # Only source-relative includes are normalized, never policy.
        text = text.replace('"../exec/appslot.h"', '"appslot.h"')
        text = text.replace('"../gfx/gfx.h"', '"gfx.h"')
        (tmp / name).write_text(text)
    stage('paging_source.c', read('kernel/paging.c'))
    stage('pgalloc_source.c', read('kernel/pgalloc.c'))
    exe = read('exec/exec.c')
    slot = read('exec/appslot.c')
    stage('slot_source.c', '\n'.join(function(slot, sig) for sig in (
        'AppSlot *appslot_at(', 'AppSlot *appslot_get(', 'void appslot_resume_commit(',
        'int appslot_return_target(', 'void appslot_switch_to(', 'void appslot_mark_scheduled(')))
    stage('exec_source.c', '\n'.join(function(exe, sig) for sig in (
        'void exec_addrspace_abort(', 'static void exec_stop_mark(',
        'static void exec_teardown_app(', 'static void exec_finish(',
        'void exec_exit(', 'static void exec_pending_transfer(',
        'static void exec_pending_finish(', 'static void ring3_kill_kind(int kind)\n',
        'void ring3_fault_kill(', 'void ring3_abort_kill(', 'void ring3_abort_check(')))
    resume = function(exe, 'i32 exec_resume(')
    marker = '\n    }\n\n'
    start = resume.index(marker, resume.index('if (exec_setjmp')) + len(marker)
    tail = resume[start:resume.index('    return 0;   /* 到達しない */')]
    dispatch = function(exe, 'void __cdecl ring3_syscall_dispatch(')
    end = dispatch[dispatch.index('syscall_complete:') + len('syscall_complete:'):dispatch.rfind('}')]
    stage('tails_source.c', 'static void host_resume_tail(int app_id) { AppSlot *a=appslot_get(app_id);\n'
          + tail + '}\nstatic void host_syscall_tail(u32 *frame) { int prev_caller=0, prev_in_syscall=0; u32 *prev_frame=0;\n' + end + '}\n')
    # Linux cannot map guest page zero. Only BIOS/CPU/device edges are substitutes.
    stage('v86_source.c', read('kernel/v86_mem.c').replace(
        '(volatile u32 *)(V86_REMAP_START + PAGE_SIZE)', '(volatile u32 *)P2V(backing_phys)'))
    entry = function(read('kernel/isr_handlers.c'), 'void exception_handler(')
    entry = entry[:entry.index('    /* 画面上部クリア */')] + '}\n'
    entry = entry.replace('    const char *name;\n    int row = 0;', '    (void)error_code;')
    stage('entry_source.c', entry)
    stage('fixture_source.c', read('kernel/r1_fixture.c').replace('__asm__ volatile("ud2")', 'host_raise_ud()'))
    flags = ['gcc', '-m32', '-march=i386', '-std=gnu11', '-Wall', '-Wextra', '-Werror',
             '-ffreestanding', '-fno-builtin', '-fno-pie', '-fno-stack-protector',
             '-ffunction-sections', '-fdata-sections', '-nostdlib', '-static', '-no-pie',
             '-Wl,--gc-sections', '-DOS32_R1_FIXTURE', '-DPHYSMEM_HOST_TEST=1', '-I'+str(tmp)]
    flags += ['-I'+str(ROOT/p) for p in ('tools/tests/host_arch','tools/tests','include',
             'arch/x86','platform/pc98','kernel','exec','gfx','drivers','lib','sdk/include/os32')]
    result = host32.build(flags + [str(ROOT/'tools/tests/r1_recovery_host.c'),
                         str(ROOT/'kernel/physmem.c'), '-o', str(tmp/'recovery')],
                         capture_output=True, text=True)
    assert result.returncode == 0, result.stderr
    return host32.run([str(tmp/'recovery')], runner=runner, capture_output=True, text=True, timeout=30)


def lease_recovery(function, runner, mutation=None):
    from test_gfx_attach import TARGET_SRCS as GFX_SRCS, SDK_SRCS, HEADERS, CORE
    from test_gfx_kernel_fb import run_case
    texts = {p: (ROOT/p).read_text() for p in GFX_SRCS}
    fixture = (ROOT/'kernel/r1_fixture.c').read_text()
    if mutation:
        path, old, new = mutation
        if path == 'kernel/r1_fixture.c':
            assert fixture.count(old) == 1
            fixture = fixture.replace(old, new)
        else:
            assert texts[path].count(old) == 1
            texts[path] = texts[path].replace(old, new)
    texts['exec/lease.c'] = '#define OS32_R1_FIXTURE\n' + texts['exec/lease.c']
    names = set(re.findall(r'\b(gfx_\w+)\s*\(', '\n'.join(texts[p] for p in SDK_SRCS+HEADERS)))
    names.discard('gfx_cpl')
    pattern = re.compile(r'(?<!->)(?<!\.)\b('+'|'.join(sorted(names))+r')\b')
    def stage(tmp, sources):
        for path in SDK_SRCS+HEADERS:
            body = texts[path]
            if path == CORE:
                body = body.replace('return cs & 3U;', 'extern unsigned int host_sdk_cpl; return host_sdk_cpl;')
            if path.endswith('gfx_dump.c'):
                body = '#pragma GCC diagnostic ignored "-Wsign-compare"\n'+body
            dest = tmp/Path(path).name
            dest.write_text(pattern.sub(lambda m: 'sdk_'+m[0], body))
            if path in SDK_SRCS: sources.append(dest)
        (tmp/'fixture_source.c').write_text(fixture.replace('"../exec/appslot.h"', '"appslot.h"')
                                          .replace('__asm__ volatile("ud2")', 'host_raise_ud()'))
        prefix = (ROOT/'tools/tests/gfx_attach_host.c').read_text().split('static void run(void) __attribute__((used));')[0]
        (tmp/'fixture.c').write_text(prefix + (ROOT/'tools/tests/r1_lease_host.c').read_text())
    return run_case(None, runner, source_texts=texts, stage=stage)


RECOVERY_MUTANTS = [
    ('K1-hook', 'kernel/v86_mem.c', '    r1_fixture_v86_ready();', '    /* omitted */', 'FAIL 0'),
    ('parent-hook', 'exec/exec.c', '        r1_fixture_parent(id);', '        /* omitted */', 'FAIL g_slot[2].as->appmem_poisoned'),
    ('parent-too-late', 'exec/exec.c',
     '#ifdef OS32_R1_FIXTURE\n        r1_fixture_parent(id);\n#endif\n        parent = appslot_return_target(id);',
     '        parent = appslot_return_target(id);\n#ifdef OS32_R1_FIXTURE\n        r1_fixture_parent(id);\n#endif', 'FAIL g_slot[2].as->appmem_poisoned'),
    ('parked-hook', 'exec/exec.c', '    r1_fixture_resume((int)app_id);', '    /* omitted */', 'FAIL g_slot[id].as->appmem_poisoned'),
    ('parked-too-late', 'exec/exec.c',
     '#ifdef OS32_R1_FIXTURE\n    r1_fixture_resume((int)app_id);\n#endif\n    appslot_resume_commit((int)app_id);',
     '    appslot_resume_commit((int)app_id);\n#ifdef OS32_R1_FIXTURE\n    r1_fixture_resume((int)app_id);\n#endif', 'FAIL g_slot[id].as->appmem_poisoned'),
    ('target-id', 'kernel/r1_fixture.c', 'r1_fixture_id[hook] != (u32)id ||', '0 ||', 'FAIL !spaces[2].appmem_poisoned'),
    ('target-generation', 'kernel/r1_fixture.c', 'r1_fixture_generation[hook] != a->as->generation', '0', 'FAIL !spaces[2].appmem_poisoned'),
    ('parked-mark', 'kernel/r1_fixture.c', 'a->state == APP_STATE_PARKED && a->parked_from_wait', 'a->state == APP_STATE_PARKED', 'FAIL !spaces[2].appmem_poisoned'),
    ('parent-state', 'kernel/r1_fixture.c', 'parent->state == APP_STATE_RUNNING', '1', 'FAIL !spaces[2].appmem_poisoned'),
    ('commit-loses-abort', 'exec/appslot.c', '    a->parked_from_wait = 0;      /* 印は 1 回きり (3 つの park 点すべてで) */',
     '    a->abort_req = 0;\n    a->parked_from_wait = 0;', 'FAIL !forbid_app_pd'),
    ('resume-abort', 'exec/exec.c', '    g_cur_app = a;\n    ring3_abort_check();', '    g_cur_app = a;', 'FAIL !forbid_app_pd'),
    ('syscall-abort', 'exec/exec.c', '    ring3_abort_check();\n    exec_park_stop(frame);', '    exec_park_stop(frame);', 'FAIL 0'),
    ('quarantine-account', 'exec/exec.c', '    exec_as_leftover_pages += ledger_owner_pages(a->as->owner);', '    /* omitted */', 'FAIL ledger_owner_pages(owner)==pages'),
    ('quarantine-pages', 'exec/exec.c', '    exec_as_leftover_pages += ledger_owner_pages(a->as->owner);',
     '    u32 lost; ledger_reclaim_owner(a->as->owner, &lost);', 'FAIL ledger_owner_pages(owner)==pages'),
    ('normal-context-release', 'kernel/v86_mem.c',
     'if (!v86_session.release_pending || kctx_irq_depth || kctx_exc_depth) goto done;',
     'if (!v86_session.release_pending) goto done;', 'FAIL !kctx_exc_depth'),
]
LEASE_MUTANTS = [
    ('lease-hook', 'exec/lease.c', '    if (r1_fixture_lease(as, auth->role)) return LEASE_FULL;',
     '    /* omitted */', 'FAIL libos32gfx_attach_checked()==OS32_ERR_FULL'),
    ('lease-one-shot', 'kernel/r1_fixture.c', '    r1_fixture_arm[slot] = 0;',
     '    /* leave armed */', 'FAIL !r1_fixture_arm[R1_FIXTURE_LEASE_FAIL]'),
    ('lease-role', 'kernel/r1_fixture.c', 'role == LEDGER_ROLE_CLIENT',
     '(role || 1)', 'FAIL !r1_fixture_lease(&space,LEDGER_ROLE_UNICODE)'),
    ('wait-no-retry', 'userland/lib/gfx/libos32gfx_core.c',
     '    gfx_api->sys_halt();\n    (void)libos32gfx_check();', '    gfx_api->sys_halt();',
     'FAIL waits==1'),
]
