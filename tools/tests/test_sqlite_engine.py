"""T4b-2: actual core guard, typed thunks and NASM trampoline on ILP32."""
import argparse
import pathlib
import re
import tempfile
import subprocess
import host32
ROOT = pathlib.Path(__file__).resolve().parents[2]
TARGET_SRC = ['kernel/sqlite_engine.c', 'kernel/sqlite_outer.c', 'exec/exec.c',
              'kernel/isr_handlers.c', 'lib/sqlite3/sqlite_stack.asm']

def run(runner, mutant=None):
    with tempfile.TemporaryDirectory(prefix='sqlite-engine-') as directory:
        tmp = pathlib.Path(directory)
        engine = (ROOT / TARGET_SRC[0]).read_text()
        if mutant == 'guard bypass':
            engine = engine.replace('sqlite_engine_active || kctx_irq_depth', '0 || kctx_irq_depth')
        if mutant == 'early STOP':
            engine = engine.replace('return sqlite_engine_active != 0;', 'return 0;')
        (tmp / 'engine.c').write_text(engine)
        (tmp / 'io.h').write_text('extern unsigned long host_flags;\nvoid host_stop(void);\n'
            'static inline unsigned int irq_save(void) { unsigned int f=host_flags; host_flags &= ~512u; return f; }\n'
            'static inline void irq_restore(unsigned int f) { host_flags=f; }\n'
            'static inline void _disable(void) { host_flags &= ~512u; }\n'
            'static inline void _stop(void) { host_stop(); }\n')
        (tmp / 'pgalloc.h').write_text('extern volatile u32 kctx_irq_depth, kctx_exc_depth;\n')
        (tmp / 'memmap.h').write_text('#define MEM_SQLITE_STACK_TOP 0u\n')
        src = (ROOT / 'exec/exec.c').read_text()
        a = src.index('void ring3_abort_check(void)\n{')
        (tmp / 'stop_source.c').write_text(src[a:src.index('\n}', a)+2])
        src = (ROOT / 'kernel/isr_handlers.c').read_text()
        gates = []
        for original, name in [('exception_handler', 'test_exception'), ('page_fault_handler','test_page_fault')]:
            a = src.index('void '+original+'(')
            a = src.index('    _disable();', a)
            b = src.index('    /* ---- CPL=3', a)
            gate = src[a:b]
            if mutant == 'fault app kill':
                gate = gate.replace('if (sqlite_engine_active) sqlite_engine_fault();', '')
            gates.append('static void '+name+'(void) {\n'+gate+' kills++; }\n')
        (tmp / 'fault_source.c').write_text('#include "io.h"\n'+'\n'.join(gates))
        hdr = (ROOT / 'lib/sqlite3/os32_sqlite_outer.h').read_text()
        mocks, calls = [], []
        for ret, name, args in re.findall(r'^(.+?) os32_sqlite_(\w+)\(([^;]*)\);', hdr, re.M):
            if name in ('start', 'outer_db_fd', 'outer_group_open', 'snprintf'): continue
            value = '(void *)0x1234' if '*' in ret else '73'
            mocks.append(f'{ret} sqlite3_{name}({args}) {{ probe(); return {value}; }}')
            argv = ', '.join('0' for _ in args.split(',')) if args != 'void' else ''
            calls.append(f'{{ int before=calls; CHECK(os32_sqlite_{name}({argv}) == ({ret})({value})); CHECK(calls > before); }}')
        mocks += ['int os32_sqlite_group_for_db(void *db, Os32SqliteGroupInfo *out) { if (db != (void *)7) return SQLITE_MISUSE; out->owner=1; out->cookie.group_index=2; out->cookie.generation=9; return SQLITE_OK; }', 'int os32_sqlite_group_snapshot(const Os32SqliteGroup *g, Os32SqliteGroupInfo *out) { (void)g; (void)out; return SQLITE_MISUSE; }', 'sqlite3 *sqlite3_db_handle(sqlite3_stmt *s) { (void)s; probe(); return 0; }', 'int os32_sqlite_group_open(const Os32SqliteGroup *g, const char *p, int f, sqlite3 **o) { (void)g; (void)p; (void)f; (void)o; probe(); return 0; }', 'int os32_sqlite_init(void) { probe(); return 0; }',
                  'int os32_sqlite_db_fd(void *db) { (void)db; probe(); return 12; }',
                  'char *sqlite3_vsnprintf(int n, char *out, const char *fmt, __builtin_va_list ap) { (void)n; (void)fmt; (void)ap; probe(); out[0]=0; return out; }']
        calls += ['CHECK(os32_sqlite_outer_group_open(0,0,0,0) == 0);', 'CHECK(os32_sqlite_start() == 0);', 'CHECK(os32_sqlite_outer_db_fd(0) == 12);',
                  '{ char out[8]; CHECK(os32_sqlite_snprintf(8,out,"%d",1) == out && !out[0]); }']
        (tmp / 'sqlite_mocks.c').write_text('\n'.join(mocks))
        (tmp / 'outer_calls.c').write_text('\n'.join(calls))
        objects = []
        for rel in ['lib/sqlite3/sqlite_stack.asm','tools/tests/sqlite_engine_regs.asm']:
            obj = tmp / (pathlib.Path(rel).stem+'.o')
            subprocess.run(['nasm','-f','elf32', str(ROOT/rel), '-o', str(obj)],check=True)
            objects.append(str(obj))
        exe = tmp/'test'
        cmd = ['gcc','-m32','-march=i386','-std=gnu11','-Os','-ffreestanding','-fno-pie',
               '-fno-stack-protector','-ffunction-sections','-fdata-sections','-nostdlib',
               '-static','-no-pie','-Wl,--gc-sections','-I'+str(tmp)]
        cmd += ['-I'+str(ROOT/p) for p in ['include','kernel','fs','lib/sqlite3','sdk/include/os32']]
        cmd += [str(ROOT/'tools/tests/sqlite_engine_host.c'),str(tmp/'engine.c'),
                str(ROOT/'kernel/sqlite_outer.c'), *objects, '-o',str(exe)]
        host32.build(cmd, check=True, capture_output=True, text=True)
        return host32.run([str(exe)], runner=runner, capture_output=True, text=True, timeout=host32.RUN_TIMEOUT)

@host32.control_session
def main():
    parser=argparse.ArgumentParser()
    parser.add_argument('--runner',choices=['native','qemu'],default='qemu')
    parser.add_argument('--mutate',action='store_true')
    args=parser.parse_args()
    host32.begin_control(args.mutate,args.runner,ROOT)
    with host32.control(args.mutate,args.runner,ROOT) as normal:
        if normal:
            r=run(args.runner)
            print(r.stdout+r.stderr,end='')
            assert r.returncode == 0, r.returncode
    if args.mutate:
        for name in ['guard bypass','early STOP','fault app kill']:
            r=run(args.runner,name)
            assert r.returncode != 0 and 'FAIL:' in r.stdout, (name,r.returncode,r.stdout,r.stderr)
            print('RED (runtime): '+name)
if __name__ == '__main__':
    try: main()
    except subprocess.CalledProcessError as e:
        print(e.stdout or '',e.stderr or '')
        raise
