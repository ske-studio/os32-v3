"""D1: product x86 call/entry and common dispatcher, ILP32 without libc."""
CHECK = dict(name='kapi-call-host', tier=1,
    covers=['include/legacy_guest.h', 'tools/tests/legacy_guest_fake.h', 'kapi/kapi_generated.c', 'arch/x86/kernel/kapi_call.c', 'arch/x86/kernel/syscall_entry.c',
            'arch/x86/kernel/ring3_entry.asm', 'arch/x86/kernel/exec_context.c',
            'arch/x86/kernel/exec_entry.c', 'arch/x86/kernel/tss.h',
            'exec/ring3_ls.h', 'include/kapi_call.h', 'include/kapi_call_generated.h', 'exec/redir_access.c', 'lib/kstring_asm.asm',
            'include/exec_arch.h', 'include/redir_access.h', 'exec/exec_syscall.inc',
            'sdk/gen_kapi.py', 'sdk/kapi_abi_x86.py', 'sdk/target_profile.py', 'sdk/kapi.json',
            'tools/tests/test_kapi_call.py', 'tools/tests/host_exec_context.h',
            'tools/tests/host_exec_entry.h', 'tools/tests/host_arch/*.h',
            'tools/tests/kernel_includes.py', 'tools/tests/host32.py'],
    needs=[], budget=5)
from pathlib import Path
import json
import os
import subprocess
import sys
import tempfile
import host32
from kernel_includes import include_flags
ROOT = Path(__file__).resolve().parents[2]
HOST = r'''
#include "types.h"
#include "os32_kapi_shared.h"
#include "config.h"
static u32 host_cr3;
#include "exec.h"
#include "ring3_str.h"
#include "appslot.h"
#include "kapi_call.h"
#include "exec_legacy.h"
#include "exec_gfx_poll.h"
#include "kapi_print.h"
#include "kapi_strings.h"
#include "ring3_ls.h"
#include "legacy_guest.h"
#include "host_exec_context.h"
#include "host_exec_entry.h"
static void finish(int rc) __attribute__((noreturn));
static void finish(int rc) { __asm__ volatile("int $0x80"::"a"(1),"b"(rc):"memory"); __builtin_unreachable(); }
static void fail(unsigned line) {
    char text[16]; unsigned n=0;
    do { text[n++]='0'+line%10; line/=10; } while(line);
    __asm__ volatile("int $0x80"::"a"(4),"b"(2),"c"(text),"d"(n):"memory");
    finish(1);
}
#define CHECK(x) do { if (!(x)) fail(__LINE__); } while (0)
struct tss_entry kernel_tss;
u32 tss_get_esp0(void) { return 0; }
void tss_set_esp0(u32 n) { (void)n; }
void ring3_resume(const u32 *f, u32 pd, void *t) { (void)f; (void)pd; (void)t; CHECK(0); }
static u32 table[KAPI_FUNC_CAPACITY+2];
#undef KAPI_ADDR
#define KAPI_ADDR table
/* TABLES */
static u8 argument_pages[8192] __attribute__((aligned(4096)));
static u32 *words=(u32 *)(argument_pages+4096-8), frame[13];
static AppSlot caller_slot;
static struct addrspace caller_as;
static u32 expected[KAPI_FIXED_MAX];
static unsigned calls, copies, captures, kills, parks, aborts, copied_bytes;
static int identity=1, copy_ok=1, vm, legacy_result, strings_result, print_ok=1, abort_now;
static void *abort_target[5];
static u32 mapped_bytes;
static struct arch_syscall *g_cur_frame;
static AppSlot *g_cur_app;
static u32 tick_count;
static int ring3_in_syscall;
volatile int ring3_wm_depth;
static unsigned ring3_wm_shell_gen;
volatile u32 ring3_caller_reject_count;
/* Real caller identity/copy; only the host MMU translation is observed. */
int appslot_cur(void) { return 2; }
int res_owner_get(void) { return 2; }
AppSlot *appslot_get(int id) { return identity && id==2 ? &caller_slot : 0; }
u32 paging_kernel_pd_phys(void) { return 1; }
u32 paging_current_cr3(void) { return 2; }
int ring3_call_from_user(void) { return 1; }
int as_access_page(const struct addrspace *as, u32 va, int write, u32 *pa) {
    (void)write; CHECK(as==&caller_as);
    u32 first=(u32)argument_pages, end=(u32)words+mapped_bytes;
    if (!copy_ok || va<first || va>=end) return 0;
    *pa=va; return 1;
}
#define caller_access_get_user observed_caller_get_user
#define copy_caller_bytes observed_copy_caller_bytes
/* CALLER_PRODUCT */
#undef caller_access_get_user
#undef copy_caller_bytes
int caller_access_get_user(struct caller_access *c) {
    captures++; return observed_caller_get_user(c);
}
int copy_caller_bytes(const struct caller_access *c, const void *p, void *out, u32 n) {
    copies++; copied_bytes=n; return observed_copy_caller_bytes(c,p,out,n);
}
void ring3_fault_kill(void) { kills++; } /* N3: deliberately returns. */
static void exec_cui_display_abort(void) {}
static void ring3_abort_check(void) { aborts++; if (abort_now) __builtin_longjmp(abort_target,1); }
static void ring3_gui_pump(void) {}
int ring3_ptr_ok(u32 p) { return p==0 || p>=0x1000000; }
static void exec_park_stop(struct arch_syscall *c) { CHECK(c->native==frame); parks++; }
bool geforce_fg_due(u32 now) { (void)now; return 0; }
void geforce_foreground_poll(void) { CHECK(0); }
int legacy_trap(void *native) { (void)native; return vm; }
static int legacy_observe(struct kapi_arg_source *src, u32 *result) {
    CHECK(!copies && !captures && !src->next && !src->caller_valid);
    return legacy_result ? (*result=81, legacy_result) : 0;
}
#define LEGACY_FAKE_DISPATCH_ENABLE
#define LEGACY_FAKE_DISPATCH legacy_observe(src, result)
#include "legacy_guest_fake.h"
int kapi_strings_from_source(u32 s, void *f, struct kapi_arg_source *a, unsigned n, u16 m, u32 *r) {
    (void)s; (void)f; (void)n; (void)m;
    CHECK(a->next==0 && copies==0 && legacy_fake.count==1); legacy_fake_note(15);
    if (strings_result) { *r=82; return strings_result; } return 0;
}
int kapi_print_call(struct kapi_arg_source *a) { (void)a; CHECK(!copies && legacy_fake.count==1); legacy_fake_note(14); return print_ok; }
int ring3_ls_dispatch(const struct kapi_values *values) {
    CHECK(ring3_in_syscall && copies==1 && captures==1 && copied_bytes==3*sizeof(u32));
    for (unsigned k=0;k<3;k++) CHECK(values->value[k]==expected[k]);
    calls++; return 83;
}
static i32 typed(u8 a, i16 b, i32 c, const char *d) {
    CHECK(a==255 && b==-2 && c==-12345 && (u32)d==0x12345678);
    return -7;
}
/* WRAPPERS */
/* PRODUCT */
static void reset(unsigned count) {
    calls=copies=captures=kills=parks=aborts=0;
    legacy_fake.count=0;
    identity=copy_ok=print_ok=1; vm=legacy_result=strings_result=abort_now=0;
    g_cur_frame=0; ring3_in_syscall=0;
    mapped_bytes=(KAPI_FIXED_MAX+2)*sizeof(u32);
    caller_slot.cpl3=1; caller_slot.state=APP_STATE_RUNNING; caller_slot.as=&caller_as;
    caller_as.pd_phys=2; caller_as.owner=2; caller_as.generation=1;
    CallerAccessFrame previous;
    caller_access_invalidate(); CHECK(caller_access_enter(&previous,CALLER_USER));
    for (unsigned k=0;k<KAPI_FIXED_MAX;k++) words[k+1]=expected[k]=0x1000020+k*13;
    frame[11]=(u32)words; frame[7]=0;
    (void)count;
}
static void dispatch(u32 slot) { frame[7]=slot; ring3_syscall_dispatch(frame); }
void _start(void) {
    struct arch_syscall call={frame};
    struct kapi_arg_source src;
    struct kapi_values out;
    for (unsigned count=0;count<=KAPI_FIXED_MAX;count++) {
        reset(count); arch_kapi_source(&src,&call);
        CHECK(src.next==0 && !src.caller_valid && captures==0 && copies==0);
        CHECK(arch_kapi_read_block(&src,0,count,&out));
        CHECK(copies==1 && captures==1 && src.next==count);
        for (unsigned k=0;k<count;k++) CHECK(out.value[k]==expected[k]);
        CHECK(arch_kapi_invoke(wrappers[count],&out,count)==0x87654321);
        CHECK(calls==1);
    }
    reset(4); out.value[0]=255; out.value[1]=(u32)-2;
    out.value[2]=(u32)-12345; out.value[3]=0x12345678;
    CHECK(arch_kapi_invoke(typed,&out,4)==(u32)-7);
    reset(2); arch_kapi_source(&src,&call);
    CHECK(arch_kapi_read_block(&src,1,2,&out));
    CHECK(out.value[0]==expected[1] && out.value[1]==expected[2] && src.next==3);
    CHECK(arch_kapi_read_block(&src,0,1,&out) && captures==1 && copies==2);
    unsigned next=src.next;
    CHECK(!arch_kapi_read_block(&src,0,KAPI_FIXED_MAX+1,&out));
    CHECK(!arch_kapi_read_block(&src,~0u,1,&out));
    CHECK(!arch_kapi_read_block(&src,0x40000000u,1,&out));
    frame[11]=U32_MAX-3; CHECK(!arch_kapi_read_block(&src,0,1,&out));
    frame[11]=U32_MAX-7; CHECK(!arch_kapi_read_block(&src,1,1,&out));
    CHECK(src.next==next);
    reset(2); mapped_bytes=8; arch_kapi_source(&src,&call);
    CHECK(!arch_kapi_read_block(&src,0,2,&out) && src.next==0);
    copy_ok=0; CHECK(!arch_kapi_read_block(&src,0,1,&out) && src.next==0);
    identity=0; arch_kapi_source(&src,&call);
    CHECK(!arch_kapi_read_block(&src,0,0,&out) && !src.caller_valid);
    /* DISPATCH_CASES */
    reset(0); vm=1; dispatch(KAPI_FUNC_CAPACITY+1);
    CHECK(!kills && !copies && !captures && !aborts && !parks);
    reset(0); dispatch(RING3_LS_CALL); CHECK(frame[7]==83 && !kills && copies==1 && calls==1);
    reset(0); mapped_bytes=8; dispatch(RING3_LS_CALL);
    CHECK(kills==1 && !calls && frame[7]==(u32)OS32_ERR_INVAL && parks==1 && !g_cur_frame && !ring3_in_syscall);
    reset(0); frame[11]=U32_MAX; dispatch(RING3_LS_CALL);
    CHECK(kills==1 && !calls && !copies && frame[7]==(u32)OS32_ERR_INVAL);
    reset(0); legacy_result=1; dispatch(0); CHECK(frame[7]==81 && !copies);
    reset(0); legacy_result=-1; dispatch(0); CHECK(kills==1 && frame[7]==(u32)OS32_ERR_INVAL && !calls);
    reset(0); strings_result=1; dispatch(0); CHECK(frame[7]==82 && !copies);
    reset(0); print_ok=0; dispatch(KAPI_SLOT_KPRINTF); CHECK(kills==1 && !calls);
    reset(0); abort_now=1;
    if (!__builtin_setjmp(abort_target)) { dispatch(KAPI_FUNC_CAPACITY+1); CHECK(0); }
    CHECK(!kills && !calls && !copies && !captures && aborts==1);
    CHECK(frame[7]==KAPI_FUNC_CAPACITY+1);
    for (unsigned slot=KAPI_FUNC_COUNT;slot<KAPI_FUNC_CAPACITY+1;slot++) {
        reset(0); dispatch(slot); CHECK(kills==1 && !calls && !copies && frame[7]==(u32)OS32_ERR_INVAL);
    }
    finish(0);
}
'''
MUTATIONS = (
    ('argument-order', 'call', 'out->value, bytes)', 'out->value, bytes) /* order */'),
    ('word-multiplication', 'call', 'u32 offset = first * sizeof(u32);', 'u32 offset = first;'),
    ('invoke-size', 'call', 'count * sizeof(u32));', 'count);'),
    ('copy-failure', 'call', 'out->value, bytes)) return 0;', 'out->value, bytes)) { for (unsigned k=0;k<count;k++) out->value[k]=0; }'),
    ('pointer-gate', 'dispatch', '(ptrmask & (u16)(1u << k))', '(0 & (u16)(1u << k))'),
    ('consumer-order', 'dispatch', 'if (!handled && slot == KAPI_SLOT_KPRINTF) {', 'if (!handled) handled = kapi_strings_from_source(slot, wrapper, src, count, kapi_argptr[slot], result);\n    if (!handled && slot == KAPI_SLOT_KPRINTF) {'),
    ('entry-legacy-first', 'entry', 'if (legacy_trap(native)) return;', 'if (0 && legacy_trap(native)) return;'),
    ('abort-before-range', 'dispatch', '    ring3_abort_check();        /* 要求があれば longjmp して戻らない */', '    /* first abort omitted */'),
    ('private-before-range', 'dispatch', '/* 範囲外 slot', '/* 範囲外 slot'),
    ('private-count', 'dispatch', 'arch_kapi_read_block(&src, 0, 3, &values)', 'arch_kapi_read_block(&src, 0, 4, &values)'),
    ('private-copy-failure', 'dispatch', '!arch_kapi_read_block(&src, 0, 3, &values)', '(!arch_kapi_read_block(&src, 0, 3, &values) && 0)'),
)


def execute(folder, sources, host):
    (folder/'host.c').write_text(host.replace('/* PRODUCT */',
        sources['call']+'\n'+sources['entry']+'\n'+sources['dispatch']).replace('/* CALLER_PRODUCT */', (ROOT/'exec/redir_access.c').read_text()))
    asm=(ROOT/'arch/x86/kernel/ring3_entry.asm').read_text()
    (folder/'invoke.asm').write_text('BITS 32\nsection .text\n'+asm[asm.index('global kapi_invoke'):])
    subprocess.run(['nasm','-f','elf32',str(folder/'invoke.asm'),'-o',str(folder/'invoke.o')],check=True)
    subprocess.run(['nasm','-f','elf32',str(ROOT/'lib/kstring_asm.asm'),'-o',str(folder/'kstring.o')],check=True)
    subprocess.run(['gcc','-m32','-std=gnu11','-O2','-Wall','-Wextra','-Werror',
        '-ffreestanding','-fno-pie','-fno-stack-protector','-nostdlib','-static','-no-pie',
        '-ffunction-sections','-Wl,--gc-sections','-I'+str(ROOT/'tools/tests'),
        '-I'+str(ROOT/'tools/tests/host_arch'),*include_flags(ROOT),
        str(folder/'host.c'),str(folder/'invoke.o'),str(folder/'kstring.o'),'-o',str(folder/'host')],check=True,capture_output=True,text=True)
    result = host32.run([str(folder/'host')],timeout=30).returncode
    return result


def main():
    subprocess.run([sys.executable,'-B','sdk/gen_kapi.py'],cwd=ROOT,check=True,stdout=subprocess.DEVNULL)
    data=json.loads((ROOT/'sdk/kapi.json').read_text())['api']
    maximum=max(len([a for a in x.get('args',[]) if a!='...']) for x in data)
    wrappers=[]
    for n in range(maximum+1):
        params=', '.join(('void *' if k%2 else 'u32 ')+f'a{k}' for k in range(n)) or 'void'
        checks=''.join(f'CHECK((u32)a{k}==expected[{k}]);' for k in range(n))
        wrappers.append(f'static u32 f{n}({params}) {{ {checks} calls++; return 0x87654321; }}')
    wrappers.append('static void *wrappers[]={'+','.join(f'f{n}' for n in range(maximum+1))+'};')
    generated=(ROOT/'kapi/kapi_generated.c').read_text()
    tables=''
    for name in ('kapi_argsize','kapi_argptr'):
        start=generated.index('const u16 '+name)
        tables+=generated[start:generated.index('};',start)+2]+'\n'
    cases=[]
    for n in range(maximum+1):
        slot=next(i for i,a in enumerate(data) if len(a.get('args',[]))==n and '...' not in a.get('args',[]) and i!=4)
        cases.append(f'reset({n}); table[KAPI_FN_TABLE_FIRST+{slot}]=(u32)wrappers[{n}]; dispatch({slot}); CHECK(!kills && calls==1 && copies==1 && frame[7]==0x87654321 && parks==1 && !g_cur_frame && !ring3_in_syscall);')
    # A non-string explicit pointer slot, and a non-string fixed call.
    slot=next(i for i,a in enumerate(data) if a['name']=='sys_ls')
    cases.append(f'reset(1); words[1]=1; words[2]=1; words[3]=1; table[KAPI_FN_TABLE_FIRST+{slot}]=(u32)wrappers[1]; dispatch({slot}); CHECK(kills==1 && !calls);')
    cases.append(f'reset(1); copy_ok=0; table[KAPI_FN_TABLE_FIRST+{slot}]=(u32)wrappers[1]; dispatch({slot}); CHECK(kills==1 && !calls && frame[7]==(u32)OS32_ERR_INVAL);')
    host=HOST.replace('/* TABLES */',tables).replace('/* WRAPPERS */','\n'.join(wrappers)).replace('/* DISPATCH_CASES */','\n'.join(cases))
    dispatch=(ROOT/'exec/exec_syscall.inc').read_text(); start=dispatch.index('/* Keep the dispatcher')
    sources=dict(call=(ROOT/'arch/x86/kernel/kapi_call.c').read_text(),entry=(ROOT/'arch/x86/kernel/syscall_entry.c').read_text(),dispatch=dispatch[start:])
    with tempfile.TemporaryDirectory(prefix='kapi-call-') as d:
        folder=Path(d)
        try:
            assert execute(folder,sources,host)==0
            print('GREEN product call/entry/dispatcher + cdecl ASM: 0..max, order, kinds, result, range, copy, gates')
            if '--mutate' in sys.argv:
                for name,key,old,new in MUTATIONS:
                    changed=dict(sources)
                    if name=='private-before-range':
                        old='    /* 範囲外 slot'
                        start=changed[key].index('    if (slot == RING3_LS_CALL) {')
                        end=changed[key].index(old,start)
                        private=changed[key][start:end]
                        changed[key]=changed[key][:start]+changed[key][end:]
                        old='    /* 本物の表から'
                        new=private+old
                    if name=='argument-order':
                        old='    src->next = first + count;'
                        new='    if (count > 1) { u32 v=out->value[0]; out->value[0]=out->value[1]; out->value[1]=v; }\n'+old
                    assert changed[key].count(old)==1,(name,changed[key].count(old))
                    changed[key]=changed[key].replace(old,new)
                    assert execute(folder,changed,host)==1,name
                    print('runtime RED product: '+name)
        except subprocess.CalledProcessError as error:
            print(error.stderr)
            raise


def run(ctx):
    return subprocess.call([sys.executable,'-B','tools/tests/test_kapi_call.py',*(['--mutate'] if ctx.mutate else [])],cwd=ctx.root,env=dict(os.environ,HOST32_RUNNERS=' '.join(ctx.runners)))


if __name__=='__main__':
    main()
