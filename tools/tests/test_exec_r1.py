"""T2a R1: 実 exec の移譲・両着地・回収関数 + 実 setjmp.asm を ILP32 実行。

AppSlot/CR3/IF と資源利用終了は記録する足場。全 loader/PCM 実機の代替ではない。
写しだけを変異し、コンパイル成功後の実行失敗だけを RED とする。
"""
import pathlib
import subprocess
import sys
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[2]


def function(s, name):
    import re
    m = re.search(r'^(?:static )?(?:void|int) (?:__cdecl )?' + name + r'\([^)]*\)\n\{', s, re.M)
    if not m:
        raise ValueError(name)
    return s[m.start():s.index('\n}', m.end()) + 2]


def landing(s, start, buf, name):
    a = s.index('    if (exec_setjmp(' + buf + ') != 0)', s.index(start))
    b = s.index('\n    }', a) + len('\n    }')
    return ('static int ' + name + '(void) {\n'
            '    AppSlot *a = &slots[2], *ctx = a; int gui = host_gui;\n'
            '    (void)ctx; (void)gui;\n' + s[a:b] + '\n'
            '    simulate(); return 99;\n}\n')


PRE = r'''
#include "appslot.h"
#include "exec.h"
#define CHECK(x) do { if (!(x)) die(__LINE__); } while (0)
static void die(int n) {
    (void)n;
    __asm__ volatile("int $0x80" : : "a"(1), "b"(1) : "memory");
    for (;;) {}
}
volatile u32 kctx_irq_depth, kctx_exc_depth;
static AppSlot slots[APP_SLOT_COUNT];
static AppSlot *g_cur_app;
static int cur = 2, owner = 2, cr3 = 2, host_if = 1, host_gui;
static int cleanup, teardown, notices, transfers, scheduled_kind, phase;
static int host_wm_kill;
volatile u32 ring3_transition_count;
volatile int exec_nest_level, ring3_wm_depth;
static int exec_exit_status, ring3_in_syscall;
static int fault_kill_count;
volatile u32 ring3_wm_fault_count;
static int g_last_kind, g_last_code;
static u32 g_exit_jmpbuf[KSETJMP_BUF_LEN];
int appslot_cur(void) { return cur; }
AppSlot *appslot_get(int id) { return &slots[id]; }
static void _stop(void) { die(__LINE__); }
static void _enable(void) { CHECK(!kctx_irq_depth && !kctx_exc_depth); host_if = 1; }
u32 paging_kernel_pd_phys(void) { return 1; }
void paging_load_cr3(u32 p) { cr3 = (int)p; }
int paging_set_page(u32 a, u32 b, u32 c) { (void)a; (void)b; (void)c; return 0; }
static void exec_heap_reset(void) {}
static void ring3_band_set(int n) { (void)n; }
static void res_owner_set(int id) { owner = id; }
static void exec_cpl0_release(void) {}
int appslot_return_target(int id) { CHECK(id == 2); return slots[id].parent; }
u32 appslot_reclaim(int id) { CHECK(teardown == 1); slots[id].state = APP_STATE_FREE; return 0; }
void appslot_switch_to(int id) { cur = owner = id; }
static void exec_restore_context(int id) { CHECK(id == 3); cr3 = 3; }
static void exec_teardown_app(AppSlot *a) {
    CHECK(a == &slots[2] && cleanup == 7 && cr3 == 1 && host_if);
    CHECK(!kctx_irq_depth && !kctx_exc_depth);
    CHECK(g_pending_id == 0 && g_longjmp_reason == EXEC_LJ_EXIT);
    teardown++;
}
/* db -> redirects -> FD -> pipe -> SHM -> sound -> PCM -> private AS ->
 * parent CR3/owner/heap -> WM notification. 他 owner を回収しない。 */
static void resource(int id, int step) {
    CHECK(id == 2 && cleanup == step && !teardown && host_if && cr3 == 1);
    CHECK(!kctx_irq_depth && !kctx_exc_depth);
    cleanup++;
}
static void db_cleanup_owned(int id) { resource(id, 0); }
static void fd_redirect_reset_owned(int id) { resource(id, 1); }
static void vfs_close_owned(int id) { resource(id, 2); }
static void pipe_free_owned(int id) { resource(id, 3); }
static void shm_free_owned(int id) { resource(id, 4); }
static void snd_owner_exit(int id) { resource(id, 5); }
static void pcm_reclaim(int id) { resource(id, 6); }
static void gui_owner_exit(int id) {
    CHECK(id == 2 && cleanup == 7 && teardown == 1);
    CHECK(cur == (host_wm_kill ? 1 : 3) && owner == cur && cr3 == cur);
    CHECK(host_if && !kctx_irq_depth && !kctx_exc_depth); notices++;
}
static void kbd_inject_owner_exit(int id) { CHECK(id == 2); }
static int con_sink_is_enabled(void) { return 0; }
static int con_sink_reader_get(void) { return 3; }
static void con_sink_push_exit(int id) { CHECK(id == 2); }
static int launch_child(int id) { (void)id; return 0; }
static void kbd_inject_discard(void) {}
static void launch_owner_exit(int id) { CHECK(id == 2); }
static void host_owner_exit(int id) { CHECK(id == 2); }
static void con_sink_owner_exit(int id) { CHECK(id == 2); }
void appslot_gfx_owner_exit(int id) { CHECK(id == 2); }
void asm_longjmp(u32 *buf);
void exec_longjmp(u32 *buf) {
    if (g_longjmp_reason == EXEC_LJ_PENDING) {
        CHECK(cleanup == 0 && teardown == 0 && notices == 0);
        CHECK(g_pending_id == 2 && g_pending_kind == scheduled_kind);
        CHECK(slots[2].state == (scheduled_kind == EXEC_KIND_ABORTED ?
              APP_STATE_ABORT_PENDING : APP_STATE_FAULT_PENDING));
        CHECK(buf == slots[2].jmpbuf && cr3 == 2 && owner == 2);
        transfers++;
    }
    asm_longjmp(buf);
}
'''

POST = r'''
static void simulate(void) {
    if (phase == 0) {
        /* park は資源を保持。別 setjmp の resume が次に取り直す。 */
        g_longjmp_reason = EXEC_LJ_PARK; g_longjmp_id = 2;
        exec_longjmp(slots[2].jmpbuf);
    }
    if (phase == 3) {
        ring3_in_syscall = 1; ring3_wm_depth = 1;
        kapi_sys_exit(17);
    } else if (phase == 4) {
        ring3_abort_kill(); /* syscall 入口の通常の安全点 */
    } else {
        host_if = 0;
        if (scheduled_kind == EXEC_KIND_ABORTED) {
            kctx_irq_depth = 1; ring3_abort_kill();
        } else {
            kctx_exc_depth = 1; ring3_in_syscall = 1;
            ring3_wm_depth = 1;
            if (phase == 2) exec_fault_recover();
            else ring3_fault_kill();
        }
    }
}
static void setup(int gui, int kind) {
    cur = owner = cr3 = 2; host_if = 1; host_gui = gui;
    cleanup = teardown = notices = transfers = 0;
    ring3_in_syscall = ring3_wm_depth = 0;
    fault_kill_count = ring3_wm_fault_count = 0;
    g_pending_id = 0; g_longjmp_reason = EXEC_LJ_EXIT;
    slots[2].state = APP_STATE_RUNNING; slots[2].parent = 3;
    slots[2].cpl3 = 1; slots[2].gui = gui; g_cur_app = &slots[2];
    scheduled_kind = kind;
}
'''

BROKER = r'''
#include "irq.h"
struct irq_line irq_lines[IRQ_DYN_COUNT];
u32 irq_ctx_violations;
static unsigned int irq_save(void) { unsigned int f = host_if; host_if = 0; return f; }
static void irq_restore(unsigned int f) { host_if = f; }
static void irq_recount_locked(unsigned int irq, struct irq_line *ln) {
    (void)irq; (void)ln; CHECK(!host_if);
}
static int host_handler(unsigned int irq, void *arg) { (void)irq; (void)arg; return IRQ_HANDLED; }
'''

END = r'''
static int test(void) {
    int gui, kind, resumed;
    CHECK(irq_register(5, host_handler, 0, 0) == 0);
    kctx_irq_depth = 1; /* IRQ1/0 等固定スタブの上も同じ真実 */
    CHECK(irq_register(6, host_handler, 0, 0) == IRQ_ERR_CTX);
    CHECK(irq_unregister(5, host_handler, 0) == IRQ_ERR_CTX);
    CHECK(irq_ctx_violations == 2);
    kctx_irq_depth = 0; host_if = 0; /* 通常文脈の短い IF=0 は許す */
    CHECK(irq_unregister(5, host_handler, 0) == 0 && !host_if);
    host_if = 1;
    for (gui = 0; gui < 2; gui++)
    for (kind = EXEC_KIND_FAULT; kind <= EXEC_KIND_ABORTED; kind++)
    for (resumed = 0; resumed < 2; resumed++) {
        setup(gui, kind);
        if (resumed) {
            phase = 0;
            CHECK(launch_landing() == 2);
            CHECK(cleanup == 0 && teardown == 0 && transfers == 0);
        }
        phase = 1;
        CHECK((resumed ? resume_landing() : launch_landing()) ==
              (resumed || gui ? 0 : EXEC_ERR_FAULT));
        CHECK(cleanup == 7 && teardown == 1 && notices == 1 && transfers == 1);
        CHECK(!g_pending_id && !kctx_irq_depth && !kctx_exc_depth && host_if);
        CHECK(!ring3_in_syscall && !ring3_wm_depth && fault_kill_count == 1);
        CHECK(ring3_wm_fault_count == (kind == EXEC_KIND_FAULT));
        if (!gui) CHECK(g_last_kind == kind && g_last_code == EXEC_ERR_FAULT);
        exec_pending_finish(); /* 一度消費した pending を二重回収しない */
        CHECK(cleanup == 7 && teardown == 1 && notices == 1);
    }
    setup(0, EXEC_KIND_EXITED); phase = 3;
    CHECK(launch_landing() == 17);
    CHECK(cleanup == 7 && teardown == 1 && notices == 1 && !transfers);
    CHECK(!ring3_in_syscall && !ring3_wm_depth);
    setup(0, EXEC_KIND_ABORTED); phase = 4;
    CHECK(launch_landing() == EXEC_ERR_FAULT);
    CHECK(cleanup == 7 && notices == 1 && !transfers);
    setup(0, EXEC_KIND_FAULT); phase = 2;
    CHECK(launch_landing() == EXEC_ERR_FAULT);
    CHECK(cleanup == 7 && notices == 1 && transfers == 1);
    setup(1, EXEC_KIND_ABORTED);
    cur = owner = cr3 = 1; g_cur_app = 0; host_wm_kill = 1;
    slots[2].state = APP_STATE_PARKED;
    exec_kill_one(2);
    CHECK(cleanup == 7 && teardown == 1 && notices == 1 && !transfers);
    CHECK(cur == 1 && owner == 1 && cr3 == 1 && ring3_transition_count == 1);
    return 0;
}
void _start(void) {
    int r = test();
    __asm__ volatile("int $0x80" : : "a"(1), "b"(r) : "memory");
    for (;;) {}
}
'''


def run(source=None, irq=None):
    source = source or (ROOT / 'exec/exec.c').read_text()
    irq = irq or (ROOT / 'kernel/irq.c').read_text()
    constants = '\n'.join(l for l in source.splitlines() if
                          l.startswith(('#define EXEC_LJ_', 'static volatile int g_pending_',
                                        'static volatile int g_longjmp_')))
    names = ('exec_reclaim_resources', 'exec_notify_owned', 'exec_reclaim_owned',
             'exec_finish', 'exec_exit', 'kapi_sys_exit', 'exec_pending_transfer', 'exec_pending_finish',
             'exec_kill_one', 'exec_fault_recover', 'ring3_kill_kind', 'ring3_fault_kill', 'ring3_abort_kill')
    code = ('#include "types.h"\n' + constants + '\n' + PRE + '\n' +
            '\n'.join(function(source, n) for n in names) + '\n' + POST +
            landing(source, 'static int exec_launch(', 'ctx->jmpbuf', 'launch_landing') +
            landing(source, 'i32 exec_resume(', 'a->jmpbuf', 'resume_landing') + BROKER +
            function(irq, 'irq_register') + '\n' + function(irq, 'irq_unregister') + END)
    with tempfile.TemporaryDirectory(prefix='os32-exec-r1-') as tmp:
        tmp = pathlib.Path(tmp)
        (tmp / 't.c').write_text(code)
        asm = (ROOT / 'kernel/setjmp.asm').read_text().replace('exec_longjmp', 'asm_longjmp')
        (tmp / 's.asm').write_text(asm)
        r = subprocess.run(['nasm', '-f', 'elf32', str(tmp / 's.asm'), '-o', str(tmp / 's.o')],
                           capture_output=True, text=True)
        if r.returncode:
            return 'compile', r.stderr
        inc = ['-I' + str(ROOT / p) for p in ('include', 'kernel', 'exec', 'sdk/include/os32')]
        r = subprocess.run(['gcc', '-m32', '-std=gnu11', '-O0', '-Wall', '-Werror',
                            '-Wno-unused-function', '-ffreestanding', '-fno-pie',
                            '-fno-stack-protector', '-nostdlib', '-static', '-no-pie',
                            *inc, str(tmp / 't.c'), str(tmp / 's.o'), str(ROOT / 'kernel/irq_math.c'), '-o', str(tmp / 't')],
                           capture_output=True, text=True)
        if r.returncode:
            return 'compile', r.stderr
        r = subprocess.run([str(tmp / 't')], capture_output=True, text=True, timeout=30)
        return ('pass' if r.returncode == 0 else 'runtime'), "rc=%d " % r.returncode + r.stdout + r.stderr


class ExecR1(unittest.TestCase):
    def test_transfer_land_cleanup(self):
        status, log = run()
        self.assertEqual(status, 'pass', log)


MUTATIONS = (
    ('wm-notify-before-as',
     '    exec_reclaim_resources(id);\n    if (!a->cpl3) exec_cpl0_release();\n    exec_teardown_app(a);\n    appslot_reclaim(id);\n    exec_notify_owned(id);',
     '    exec_reclaim_owned(id);\n    if (!a->cpl3) exec_cpl0_release();\n    exec_teardown_app(a);\n    appslot_reclaim(id);'),
    ('irq-teardown', '    g_pending_id = id;', '    exec_teardown_app(a);\n    g_pending_id = id;'),
    ('landing-if-missing', '    _enable();\n    exec_finish(id,', '    exec_finish(id,'),
    ('pending-consumed-twice', '    g_pending_id = 0;             /* callback',
     '    g_pending_id = id;            /* callback'),
    ('resume-landing-missing', '        exec_pending_finish();\n        _enable();\n        if (g_longjmp_reason',
     '        _enable();\n        if (g_longjmp_reason'),
    ('launch-landing-missing', '        exec_pending_finish();\n        _enable();\n        /* 畳み',
     '        _enable();\n        /* 畳み'),
)

if __name__ == '__main__':
    suite = unittest.main(argv=[sys.argv[0]], exit=False)
    if not suite.result.wasSuccessful():
        sys.exit(1)
    if '--mutate' in sys.argv:
        source = (ROOT / 'exec/exec.c').read_text()
        for name, old, new in MUTATIONS:
            if old not in source:
                raise SystemExit('missing mutation: ' + name)
            status, log = run(source.replace(old, new, 1))
            print(name + ': ' + status)
            if status != 'runtime':
                raise SystemExit(log or 'mutation must fail at runtime')

        irq = (ROOT / 'kernel/irq.c').read_text()
        status, log = run(irq=irq.replace('flags, kctx_irq_depth)', 'flags, 0)').replace('arg, kctx_irq_depth)', 'arg, 0)'))
        print('broker-old-depth: ' + status)
        if status != 'runtime':
            raise SystemExit(log or 'mutation must fail at runtime')
