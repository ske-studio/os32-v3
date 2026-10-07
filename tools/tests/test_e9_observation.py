"""Execute the real tvdump and guest probes with hardware/syscall boundaries stubbed.

ILP32, explicit host32 runner. --baseline loads the old observers from HEAD
and requires their behavioral tests to fail; --mutate checks source-copy regressions.
Guest page permissions and cleanup still require PM acceptance.
"""
import argparse
from pathlib import Path
import subprocess
import tempfile
import host32

ROOT = Path(__file__).resolve().parents[2]
FLAGS = ['-m32', '-std=gnu11', '-O2', '-fno-pie', '-no-pie', '-fno-stack-protector',
         '-ffreestanding', '-nostdlib', '-static', '-Wall', '-Wextra', '-Werror']
PRE = r'''
typedef unsigned long u32;
typedef unsigned short u16;
typedef unsigned char u8;
static __attribute__((noreturn)) void finish(int rc) {
    __asm__ volatile("int $0x80" : : "a"(1), "b"(rc) : "memory");
    __builtin_unreachable();
}
#define CHECK(x) do { if (!(x)) finish(1); } while (0)
'''


def body(src, head):
    start = src.index(head)
    end = src.index('\n}', start) + 2
    return src[start:end]


def run(code, runner):
    with tempfile.TemporaryDirectory(prefix='e9-host-') as d:
        d = Path(d)
        p = d / 'probe.c'
        p.write_text(code)
        subprocess.run(['gcc', *FLAGS, '-I' + str(ROOT / 'include'),
                        '-I' + str(ROOT / 'sdk/include/os32'), '-I' + str(ROOT / 'userland/tests'), str(p), '-o', str(d / 'probe')],
                       check=True, capture_output=True, text=True)
        return host32.run([str(d / 'probe')], runner=runner, timeout=30,
                          capture_output=True).returncode


def tvdump(src):
    return PRE + r'''
#define ATTR_RED 1
#define ATTR_GREEN 2
#define SH_STATUS_ERROR 1
static u8 wire[4006];
static int pos, reads, ready = 1;
static int initialized(void) { return ready; }
static void put(int c) { CHECK(pos < 4006); wire[pos++] = c; }
static void print(int a, const char *fmt, const char *s) { (void)a; (void)fmt; (void)s; }
static void readcell(int x, int y, u16 *ch, u8 *attr) {
    CHECK(x == reads % 80 && y == reads / 80);
    *ch = (u16)(0x8100 + reads * 7); *attr = (u8)(reads * 13); reads++;
}
static struct {
    int (*serial_is_initialized)(void);
    void (*serial_putchar)(int);
    void (*kprintf)(int, const char *, const char *);
    void (*tvram_readchar_at)(int, int, u16 *, u8 *);
} api = {initialized, put, print, readcell}, *g_api = &api;
''' + body(src, 'static int cmd_tvdump(') + r'''
void _start(void) {
    int i;
    CHECK(cmd_tvdump(0, 0) == 0);
    CHECK(pos == 4006 && reads == 2000);
    CHECK(wire[0]=='T' && wire[1]=='V' && wire[2]=='D' && wire[3]=='M');
    CHECK(wire[4]==80 && wire[5]==25);
    for (i=0; i<2000; i++) {
        CHECK(wire[6+i*2] == (u8)(i*7));
        CHECK(wire[7+i*2] == (u8)(i*13));
    }
    pos = reads = 0; ready = 0;
    CHECK(cmd_tvdump(0, 0) == SH_STATUS_ERROR && pos == 0 && reads == 0);
    finish(0);
}
'''


def marker(src, header, name, sel='', alloc_fail=False):
    tags = {'nop': (0x21504f4e, 0x4b4f, 0), 'ring3_hello': (0x33474e52, 0x3352, 0),
            'ring3_fault': (0x3f544c46, 0x4652, 0x100000)}
    if name == 'ring3_guard':
        tag, target = {'': (0x3f445247, 0x8ffbf000), 'shlib': (0x3f424c53, 0x80000000),
                       'cirrus': (0x3f534956, 0x1000000), 'pegc': (0x3f474550, 0xf00000),
                       'bb': (0x3f3f4242, 0x6a000)}[sel]
        tags[name] = (tag, 0x4752, target)
    tag, label, target = tags[name]
    should_fault = target and sel != 'bb'
    # Only privileged syscall boundary and intentional faulting stores are replaced.
    # Real allocation/initialization/arming/entry control flow remains in the fixture.
    header = header.replace(body(header, 'static __inline__ __attribute__((always_inline)) unsigned long\nr3_call('),
                            '#define r3_call host_call')
    source = src.replace('#include "ring3_marker.h"', '').replace('#include "os32api.h"', '')
    source = source.replace('void _start(', 'void guest_entry(').replace('int main(', 'int guest_entry(')
    # The baseline nop has its own KernelAPI/types and void main.
    if name == 'nop' and 'void main(' in source:
        source = source.replace('void main(', 'void guest_entry(')
        source = source[source.index('void guest_entry('):]
        source = source.replace('(void)api;', '(void)api; (void)argc; (void)argv;')
    import re
    source = re.sub(r'\*(kern|guard|shtext|cirrus_vis|pegc_vis|pc98_bb) = (0x[0-9A-Fa-f]+UL);',
                    r'target_write((u32)\1, \2);', source)
    declarations = f'''
#include "os32_kapi_slots.h"
#define KernelAPI SDKKernelAPI
#include "os32_kapi_shared.h"
#undef KernelAPI
typedef struct {{ int unused; }} KernelAPI;
static volatile u32 storage[4096];
static int allocations, writes;
static void validate(int complete) {{
    CHECK(allocations == 1);
    CHECK(storage[0] == {tag}UL && storage[4] == {label}UL);
    CHECK(storage[5] == (u32)storage && storage[2] == {target}UL);
    CHECK(storage[3] == ({target}UL ? 0x444d5241UL : 0));
    CHECK(storage[1] == (complete ? ({target}UL ? 0x56525553UL : 0x454e4f44UL) : 0));
}}
static __attribute__((unused)) void target_write(u32 addr, u32 value) {{
    CHECK(addr == {target}UL && value == ({str(sel == 'bb').lower()} ? 0 : 0xdeadbeefUL));
    validate(0); writes++;
    if ({int(bool(should_fault))}) finish(0);
}}
static __attribute__((unused)) u32 host_call(u32 slot, u32 arg) {{
    if (slot == KAPI_SLOT_SYS_SHM_ALLOC) {{
        CHECK(arg == 1 && allocations++ == 0);
        return {0 if alloc_fail else '(u32)storage'};
    }}
    CHECK(slot == KAPI_SLOT_SYS_EXIT);
    if ({int(alloc_fail)}) {{ CHECK(arg == 2 && allocations == 1 && !writes); finish(0); }}
    CHECK(arg == 0 && !{int(bool(should_fault))}); validate(1); finish(0);
}}
'''.replace('true', '1').replace('false', '0')
    if name == 'nop':
        call = 'guest_entry(0, 0, 0); validate(1); finish(0);'
    elif name == 'ring3_guard':
        call = f'char *args[] = {{"ring3_guard", "{sel}"}}; guest_entry({2 if sel else 1}, args);'
    else:
        call = 'guest_entry();'
    return PRE + declarations + header + '\n' + source + '\nvoid _start(void) { ' + call + ' }\n'


def client_marker(src, header, revoke=False, bad_fb=False):
    """Real F control flow; only int80 and intentional lease stores are mocked."""
    header = header.replace(body(header, 'static __inline__ __attribute__((always_inline)) unsigned long\nr3_call('),
                            '#define r3_call host_call')
    source = src.replace('#include "ring3_marker.h"', '').replace('#include "os32api.h"', '')
    source = source.replace('void _start(', 'void guest_entry(')
    source = source.replace('*client = 0;', 'client_write((u32)client);')
    declarations = r'''
#include "os32_kapi_shared.h"
#include "os32_kapi_slots.h"
#include "memmap.h"
static volatile u32 storage[4096];
static int initialized, queried, stopped, writes;
static void client_write(u32 addr) {
    CHECK(!BAD_FB && initialized && queried && addr==MEM_LEASE_BASE);
    CHECK(storage[2]==addr && storage[3]==0x444d5241UL);
    CHECK(storage[4]==0x4752UL && storage[5]==(u32)storage);
    CHECK(storage[1]==0);
    if (writes++) {
        CHECK(REVOKE && stopped && writes==2 && storage[0]==0x3f564552UL);
        finish(0); /* mocked PF at the revoked lease */
    }
    CHECK(!stopped && storage[0]==0x3f53454cUL);
}
static u32 host_call(u32 slot, u32 arg) {
    if (slot==KAPI_SLOT_SYS_SHM_ALLOC) { CHECK(arg==1);return (u32)storage; }
    if (slot==KAPI_SLOT_GFX_INIT) { CHECK(!initialized++);return 0; }
    if (slot==KAPI_SLOT_GFX_GET_FRAMEBUFFER) {
        CHECK(initialized && !queried++);
        ((GFX_Framebuffer *)arg)->planes[0]=BAD_FB ? 0 : (u8 *)MEM_LEASE_BASE;
        return 0;
    }
    if (slot==KAPI_SLOT_GFX_SHUTDOWN) {
        CHECK(writes==1 && storage[1]==0x56525553UL && !stopped++);
        return 0;
    }
    CHECK(slot==KAPI_SLOT_SYS_EXIT);
    if (BAD_FB) { CHECK(arg==1 && !writes);finish(0); }
    CHECK(!REVOKE && arg==0 && writes==1 && stopped==1 && storage[1]==0x56525553UL);
    finish(0);
}
'''
    tail = '\nvoid _start(void) { char *args[]={"ring3_guard", "'+('free' if revoke else 'lease')+'"};guest_entry(2,args); }\n'
    return PRE + f'#define REVOKE {int(revoke)}\n#define BAD_FB {int(bad_fb)}\n' + declarations + header + source + tail


def reuse(src):
    source = src.replace('#include "os32api.h"', '').replace('#include <stdlib.h>', '').replace('#include <string.h>', '').replace('int main(', 'static int child_main(')
    return PRE + r'''
#define KernelAPI SDKKernelAPI
#include "os32_kapi_shared.h"
#undef KernelAPI
static volatile u32 block[4096], report[4];
static int locks, frees, fail_alloc, fail_lock, fail_free;
static void *allocate(int n) { CHECK(n == 1); return fail_alloc ? 0 : (void *)block; }
static int lock(void *p) { CHECK(p == (void *)block); locks++; return fail_lock ? -1 : 0; }
static int release(void *p) { CHECK(p == (void *)block); frees++; return fail_free ? -1 : 0; }
static unsigned long strtoul(const char *s, char **e, int b) { (void)s; (void)e; CHECK(b==16); return (u32)report; }
static int strcmp(const char *a, const char *b) { while (*a && *a==*b) {a++;b++;} return *a-*b; }
typedef struct { void *(*sys_shm_alloc)(int); int (*sys_shm_lock)(void *); int (*sys_shm_free)(void *); } KernelAPI;
''' + source + r'''
void _start(void) {
    KernelAPI api = {allocate, lock, release};
    char *args[] = {"child", "free", "report"};
    u32 i;
    CHECK(child_main(3,args,&api)==0 && locks==1 && frees==1);
    CHECK(report[0]==(u32)block && report[1]==2);
    args[1]="exit"; report[1]=0;
    CHECK(child_main(3,args,&api)==0 && locks==2 && frees==1 && report[1]==1);
    args[1]="write";
    CHECK(child_main(3,args,&api)==0 && report[2]==report[0] && report[3]==0x39574853UL);
    for (i=0;i<4096;i+=1024) CHECK(block[i]==0x39574853UL+i);
    report[0]++; report[3]=0;
    CHECK(child_main(3,args,&api)==1 && report[3]==0);
    fail_alloc=1; CHECK(child_main(3,args,&api)==1); fail_alloc=0;
    args[1]="free"; fail_lock=1; CHECK(child_main(3,args,&api)==1); fail_lock=0;
    fail_free=1; CHECK(child_main(3,args,&api)==1);
    CHECK(child_main(2,args,&api)==2);
    finish(0);
}
'''



def lockwrite(src, header, sel, survive=False, failed=False):
    # Only the OS32 int80 boundary and the intentional CPL3 store are replaced.
    # The real marker, mode parsing, lock/arm/store order and failure return run.
    header = header.replace(body(header, 'static __inline__ __attribute__((always_inline)) unsigned long\nr3_call('),
                            '#define r3_call host_call')
    source = src.replace('#include "os32api.h"', '').replace('#include "ring3_marker.h"', '')
    source = source.replace('#include <stdlib.h>', '').replace('#include <string.h>', '')
    source = source.replace('int main(', 'static int child_main(')
    source = source.replace('*target = WRITE_PATTERN;', 'target_write((u32)target, WRITE_PATTERN);')
    declarations = r'''
#include "os32_kapi_slots.h"
#define KernelAPI SDKKernelAPI
#include "os32_kapi_shared.h"
#undef KernelAPI
static volatile u32 storage[4096], block[4096];
static int locks, writes;
static u32 host_call(u32 slot, u32 arg) {
    CHECK(slot == KAPI_SLOT_SYS_SHM_ALLOC && arg == 1);
    return (u32)storage;
}
static void *allocate(int n) { CHECK(n == 1); return (void *)block; }
static int lock(void *p) { CHECK(p == (void *)block); locks++; return FAIL_LOCK ? -1 : 0; }
static int release(void *p) { (void)p; finish(1); }
static unsigned long strtoul(const char *s, char **e, int b) { (void)s; (void)e; (void)b; finish(1); }
static int strcmp(const char *a, const char *b) { while (*a && *a==*b) {a++;b++;} return *a-*b; }
typedef struct { void *(*sys_shm_alloc)(int); int (*sys_shm_lock)(void *); int (*sys_shm_free)(void *); } KernelAPI;
static __attribute__((unused)) void target_write(u32 addr, u32 value) {
    CHECK(!FAIL_LOCK && locks == 1 && value == 0x39574853UL);
    CHECK(addr == (u32)block + OFFSET);
    CHECK(storage[0] == 0x3f4b4c53UL && storage[1] == 0);
    CHECK(storage[2] == addr && storage[3] == 0x444d5241UL);
    CHECK(storage[4] == 0x4c53UL && storage[5] == (u32)storage);
    writes++;
    if (!SURVIVE) finish(0);
}
'''
    tail = f'''
void _start(void) {{
    KernelAPI api = {{allocate, lock, release}};
    char *args[] = {{"child", "lockwrite", "{sel}"}};
    CHECK(child_main(3, args, &api) == 1);
    CHECK(locks == 1 && writes == {0 if failed else 1});
    CHECK(storage[1] == {0 if failed else '0x56525553UL'});
    CHECK({int(failed or survive)});
    finish(0);
}}
'''
    flags = f'#define FAIL_LOCK {int(failed)}\n#define SURVIVE {int(survive)}\n#define OFFSET {0 if sel == "first" else 0x3000}UL\n'
    return PRE + flags + declarations + header + source + tail


def reuse_parent(src):
    source = src.replace('#include "os32api.h"', '').replace('#include "rt/testresult.h"', '').replace('#include <stdio.h>', '').replace('int main(', 'static int parent_main(')
    return PRE + r'''
#define KernelAPI SDKKernelAPI
#include "os32_kapi_shared.h"
#undef KernelAPI
static volatile u32 report[4];
static int scenario, bad_call, calls, summary_passed;
static void *allocate(int n) { CHECK(n == 1); return scenario == 1 ? 0 : (void *)report; }
static int strcmp(const char *a, const char *b) { while (*a && *a==*b) {a++;b++;} return *a-*b; }
static int snprintf(char *out, unsigned size, const char *fmt, const char *path,
                    const char *mode, u32 addr) {
    CHECK(size == 128 && strcmp(fmt, "%s %s %lx") == 0);
    CHECK(strcmp(path, "/usr/bin/shm_reuse_child.bin") == 0 && addr == (u32)report);
    CHECK(strcmp(mode, (calls & 1) ? "write" : (calls ? "exit" : "free")) == 0);
    out[0] = mode[0]; out[1] = 0; return 1;
}
static int execute(const char *cmd) {
    int n = calls++;
    CHECK(n < 4 && cmd[0] == ((n & 1) ? 'w' : (n ? 'e' : 'f')));
    if (!(n & 1)) {
        CHECK(report[0] == 0 && report[1] == 0 && report[2] == 0 && report[3] == 0);
        /* Failed exit-mode child writes nothing: stale free-mode data must not pass. */
        if (scenario == 10 && n == 2) return 0;
        report[0] = scenario == 2 && n == bad_call ? 0 : 0x12340000UL;
        report[1] = scenario == 3 && n == bad_call ? 9 : (n ? 1 : 2);
    } else {
        report[2] = scenario == 4 && n == bad_call ? 0x12341000UL : 0x12340000UL;
        report[3] = scenario == 5 && n == bad_call ? 0 : 0x39574853UL;
    }
    return scenario == 6 && n == bad_call ? -1 : 0;
}
static int last_result(int *kind, int *code) {
    int bad = calls - 1 == bad_call;
    *kind = scenario == 8 && bad ? EXEC_KIND_NONE : EXEC_KIND_EXITED;
    *code = scenario == 9 && bad ? 1 : 0;
    return scenario == 7 && bad ? -1 : 0;
}
static void print(int attr, const char *fmt, const char *mode, u32 addr,
                  u32 first, u32 second, u32 locked, u32 written) {
    CHECK(attr == ATTR_WHITE && addr == (u32)report);
    (void)fmt; (void)mode; (void)first; (void)second; (void)locked; (void)written;
}
typedef struct {
    void *(*sys_shm_alloc)(int);
    int (*exec_run)(const char *);
    int (*exec_last_result)(int *, int *);
    void (*kprintf)(int, const char *, const char *, u32, u32, u32, u32, u32);
} KernelAPI;
static int os32_test_summary(KernelAPI *api, const char *name, int passed, int total) {
    (void)api; CHECK(strcmp(name, "shm_reuse_test") == 0 && total == 4);
    summary_passed = passed; return passed == total ? 0 : 1;
}
''' + source + r'''
void _start(void) {
    KernelAPI api = {allocate, execute, last_result, print};
    for (scenario = 0; scenario <= 10; scenario++) {
        for (bad_call = 0; bad_call < 4; bad_call++) {
            int expected = 4, rc;
            calls = 0;
            report[0] = report[1] = report[2] = report[3] = 99;
            if (scenario == 1) expected = 0;
            if (scenario == 2 && !(bad_call & 1)) expected = 2;
            if (scenario == 3 && !(bad_call & 1)) expected = 3;
            if ((scenario == 4 || scenario == 5) && (bad_call & 1)) expected = 3;
            if (scenario >= 6 && scenario <= 9) expected = (bad_call & 1) ? 3 : 2;
            if (scenario == 10) expected = 2;
            rc = parent_main(0, 0, &api);
            CHECK(summary_passed == expected && rc == (expected == 4 ? 0 : 1));
            CHECK(calls == (scenario == 1 ? 0 : 4));
        }
    }
    finish(0);
}
'''


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--mutate', action='store_true')
    parser.add_argument('--baseline', action='store_true')
    args = parser.parse_args()
    def read(path):
        if args.baseline:
            return subprocess.check_output(['git', 'show', 'HEAD:' + path], cwd=ROOT, text=True)
        return (ROOT / path).read_text()
    shell = read('userland/shell/rshell.c')
    header = (ROOT / 'userland/tests/ring3_marker.h').read_text()
    runner = host32.selected_runner()
    cases = [('tvdump wire/copy', tvdump(shell))]
    sources = {}
    for name in ['nop', 'ring3_hello', 'ring3_fault', 'ring3_guard']:
        src = sources[name] = read('userland/tests/' + name + '.c')
        for sel in (['', 'shlib', 'cirrus', 'pegc', 'bb'] if name == 'ring3_guard' else ['']):
            cases.append((name + ' ' + sel, marker(src, header, name, sel)))
        if not args.baseline:
            cases.append((name + ' alloc failure', marker(src, header, name, alloc_fail=True)))
    if not args.baseline:
        for revoke, bad in ((False, False), (True, False), (False, True)):
            cases.append((f'CLIENT F revoke={revoke} bad_fb={bad}', client_marker(sources['ring3_guard'], header, revoke, bad)))
        child = (ROOT / 'userland/tests/shm_reuse_child.c').read_text()
        cases.append(('SHM reuse child phases/errors', reuse(child)))
        for sel in ('first', 'last'):
            for survive, failed in ((False, False), (True, False), (False, True)):
                cases.append((f'SHM lockwrite {sel} survive={survive} lock_fail={failed}',
                              lockwrite(child, header, sel, survive, failed)))
        parent = (ROOT / 'userland/tests/shm_reuse_test.c').read_text()
        cases.append(('SHM reuse parent verdicts/errors/reset', reuse_parent(parent)))
    for name, code in cases:
        rc = run(code, runner)
        assert (rc != 0) if args.baseline else (rc == 0), (name, rc)
        print(('RED baseline: ' if args.baseline else 'GREEN: ') + name, flush=True)
    if args.mutate:
        mutations = [
            ('CLIENT lease cleanup omitted', client_marker(sources['ring3_guard'].replace('(void)r3_call(KAPI_SLOT_GFX_SHUTDOWN, 0);', ''), header)),
            ('CLIENT cleanup before SURV', client_marker(sources['ring3_guard'].replace('        mark[1] = R3_SURV;\n        (void)r3_call(KAPI_SLOT_GFX_SHUTDOWN, 0);', '        (void)r3_call(KAPI_SLOT_GFX_SHUTDOWN, 0);\n        mark[1] = R3_SURV;'), header)),
            ('CLIENT revoke omitted', client_marker(sources['ring3_guard'].replace('(void)r3_call(KAPI_SLOT_GFX_SHUTDOWN, 0);', ''), header, True)),
            ('CLIENT first write omitted', client_marker(sources['ring3_guard'].replace('*client = 0;', '(void)client;', 1), header, True)),
            ('CLIENT completion stale', client_marker(sources['ring3_guard'].replace('            mark[1] = 0;', '            mark[1] = R3_SURV;'), header, True)),
            ('wire attr', tvdump(shell.replace('serial_putchar(at)', 'serial_putchar(0)'))),
            ('wire high byte', tvdump(shell.replace('ch_val & 0xFF', 'ch_val >> 8'))),
            ('missing armed', marker(sources['ring3_fault'], header.replace('mark[3] = R3_ARMED;', 'mark[3] = 0;'), 'ring3_fault')),
            ('wrong target', marker(sources['ring3_fault'], header.replace('mark[2] = target;', 'mark[2] = target + 4;'), 'ring3_fault')),
            ('stale completion', marker(sources['ring3_fault'], header.replace('mark[1] = 0;', 'mark[1] = R3_SURV;'), 'ring3_fault')),
            ('parent passed condition', reuse_parent(parent.replace('report[0] == report[2]', 'report[0] != report[2]'))),
            ('parent child exit code', reuse_parent(parent.replace('code == 0', 'code >= 0'))),
            ('parent mode reset', reuse_parent(parent.replace('report[0] = report[1] = report[2] = report[3] = 0;', 'if (!mode) report[0] = report[1] = report[2] = report[3] = 0;'))),
            ('lockwrite not observed', lockwrite(child.replace('*target = WRITE_PATTERN;', '(void)target;'), header, 'first')),
            ('lockwrite last is first', lockwrite(child.replace('BLOCK_BYTES - PAGE_BYTES', '0'), header, 'last')),
            ('lockwrite unlocked', lockwrite(child.replace('api->sys_shm_lock((void *)block)', '0'), header, 'first')),
            ('no lock', reuse(child.replace('api->sys_shm_lock((void *)block)', '0'))),
            ('no free', reuse(child.replace('api->sys_shm_free((void *)block)', '0'))),
            ('wrong block allowed', reuse(child.replace('if ((u32)block != report[0]) return 1;', ''))),
            ('only first page', reuse(child.replace('i < BLOCK_BYTES / sizeof(u32)', 'i < 1'))),
        ]
        for name, code in mutations:
            assert run(code, runner) != 0, name
            print('RED mutation: ' + name, flush=True)
    print('PASS e9 observation runner=' + runner)

if __name__ == '__main__':
    try:
        main()
    except subprocess.CalledProcessError as e:
        print(e.stdout or '', e.stderr or '')
        raise
