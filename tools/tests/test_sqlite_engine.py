"""T4b-2: actual core guard, typed thunks and NASM trampoline on ILP32."""
import argparse
import json
import pathlib
import re
import tempfile
import subprocess
import host32
ROOT = pathlib.Path(__file__).resolve().parents[2]
TARGET_SRC = ['kernel/sqlite_engine.c', 'kernel/sqlite_outer.c', 'exec/exec.c',
              'arch/x86/kernel/isr_handlers.c', 'lib/sqlite3/sqlite_stack.asm']

def run(runner, mutant=None):
    with tempfile.TemporaryDirectory(prefix='sqlite-engine-') as directory:
        tmp = pathlib.Path(directory)
        engine = (ROOT / TARGET_SRC[0]).read_text().split('/* Boot-only adapter:')[0]
        engine = engine.replace('#include "../lib/sqlite3/generated/', '#include "'+str(ROOT / 'lib/sqlite3/generated')+'/')
        if mutant == 'guard bypass':
            engine = engine.replace('sqlite_engine_active || kctx_irq_depth', '0 || kctx_irq_depth')
        if mutant == 'early STOP':
            old = '    sqlite_call_on_alt_stack(call, sqlite_engine_stack_top, frame);'
            assert engine.count(old) == 1
            engine = engine.replace(old, old + '\n    extern void ring3_abort_check(void);\n    ring3_abort_check();')
        (tmp / 'engine.c').write_text(engine)
        (tmp / 'io.h').write_text('extern unsigned long host_flags;\nvoid host_stop(void);\n'
            'static inline int _irq_enabled(void) { return (host_flags & 512u) != 0; }\n'
            'static inline unsigned int irq_save(void) { unsigned int f=host_flags; host_flags &= ~512u; return f ^ 512u; }\n'
            'static inline void irq_restore(unsigned int f) { host_flags=f ^ 512u; }\n'
            'static inline void _disable(void) { host_flags &= ~512u; }\n'
            'static inline void _stop(void) { host_stop(); }\n')
        (tmp / 'pgalloc.h').write_text('extern volatile u32 kctx_irq_depth, kctx_exc_depth;\n')
        (tmp / 'memmap.h').write_text('')
        (tmp / 'paging.h').write_text('')
        src = (ROOT / 'exec/exec.c').read_text()
        a = src.index('void ring3_abort_check(void)\n{')
        (tmp / 'stop_source.c').write_text(src[a:src.index('\n}', a)+2])
        src = (ROOT / 'arch/x86/kernel/isr_handlers.c').read_text()
        a = src.index('void sqlite_engine_fault(')
        fault = src[a:src.index('\n}', a)+2]
        gates = [fault]
        for original, name in [('exception_handler', 'test_exception'), ('page_fault_handler','test_page_fault')]:
            a = src.index('void '+original+'(')
            a = src.index('    _disable();', a)
            b = src.index('    /* ---- CPL=3', a)
            gate = src[a:b]
            if mutant == 'fault app kill':
                gate = re.sub(r'    if \(sqlite_engine_active\) \{.*?\n    \}', '', gate, flags=re.S)
                gate = re.sub(r'    if \(sqlite_engine_active\) sqlite_engine_fault\([^;]+;', '', gate)
            gate = gate.replace('__asm__ volatile("mov %%cr2, %0" : "=r"(cr2));', 'cr2 = test_cr2;')
            gates.append('static void '+name+'(void) {\nu32 vector=13, fault_eip=test_eip, fault_addr=test_cr2;\n'+gate+' kills++; }\n')
        (tmp / 'fault_source.c').write_text('#include "io.h"\n#include "pc98.h"\n#include "idt.h"\n'+'\n'.join(gates))
        rows = json.loads((ROOT / 'tools/prototypes/sqlite_imports.def').read_text())['facade']
        mocks, calls, stub_calls, bindings, refused_calls, io_refused_calls = [], [], [], [], [], []
        for row in rows:
            ret, name, args, target, wrapper = [row[k] for k in ('result','name','args','target','wrapper')]
            value = '(void *)0x1234' if '*' in ret else '73'
            result = 'return;' if ret == 'void' else f'return {value};'
            mocks.append(f'{ret} {target}({args}) {{ CHECK(0); {result} }}')
            mocks.append(f'static {ret} exported_{name}({args}) {{ probe(); {result} }}')
            bindings.append(f'.{name} = exported_{name},')
            argv = ', '.join('0' for _ in args.split(',')) if args != 'void' else ''
            call = f'{wrapper}({argv})'
            check = call+';' if ret == 'void' else f'CHECK({call} == ({ret})({value}));'
            calls.append(f'{{ int before=calls; {check} CHECK(calls > before); }}')
            stub_calls.append(f'CHECK(sqlite_stub_api.{name} != 0);')
            if ret == 'void': stub_calls.append(call+';')
            elif '*' in ret and row['stub'] != '0':
                stub_calls.append(f'CHECK({call} != 0);')
            else: stub_calls.append(f'CHECK({call} == ({ret})({row["stub"]}));')
            refused = ('SQLITE_MISUSE' if row['stub'] == 'OS32_ERR_NOSYS' else row['stub'])
            check = (call+';' if ret == 'void' else
                     f'CHECK({call} != 0);' if '*' in ret and refused != '0' else
                     f'CHECK({call} == ({ret})({refused}));')
            refused_calls.append(check)
            if row['io']: io_refused_calls.append(check)
        mocks += ['static const SqliteModuleApi exports = {'+' '.join(bindings)+'};',
                  'const SqliteModuleApi *sqlite_module_api = &sqlite_stub_api;',
                  'int sqlite_module_ready(void) { return sqlite_module_api != &sqlite_stub_api; }']
        (tmp / 'sqlite_mocks.c').write_text('\n'.join(mocks))
        (tmp / 'outer_calls.c').write_text('\n'.join(calls))
        (tmp / 'stub_calls.c').write_text('\n'.join(stub_calls))
        (tmp / 'refused_calls.c').write_text('\n'.join(refused_calls))
        (tmp / 'io_refused_calls.c').write_text('\n'.join(io_refused_calls))
        outer = (ROOT / 'kernel/sqlite_outer.c').read_text()
        outer = outer.replace('../lib/sqlite3/generated/', str(ROOT / 'lib/sqlite3/generated')+'/')
        generated = (ROOT / 'lib/sqlite3/generated/sqlite_outer_calls.inc').read_text()
        if mutant == 'direct call':
            generated = generated.replace('sqlite_module_api->close(f->db)', 'sqlite3_close(f->db)')
        if mutant == 'NULL stub':
            generated = generated.replace('.close = stub_close,', '.close = 0,')
        (tmp / 'outer_calls.inc').write_text(generated)
        outer = outer.replace(str(ROOT / 'lib/sqlite3/generated/sqlite_outer_calls.inc'), str(tmp / 'outer_calls.inc'))
        (tmp / 'outer.c').write_text(outer)
        objects = []
        for rel in ['lib/sqlite3/sqlite_stack.asm','tools/tests/sqlite_engine_regs.asm']:
            obj = tmp / (pathlib.Path(rel).stem+'.o')
            subprocess.run(['nasm','-f','elf32', str(ROOT/rel), '-o', str(obj)],check=True)
            objects.append(str(obj))
        exe = tmp/'test'
        cmd = ['gcc','-m32','-march=i386','-std=gnu11','-Os','-ffreestanding','-fno-pie',
               '-fno-stack-protector','-ffunction-sections','-fdata-sections','-nostdlib',
               '-static','-no-pie','-Wl,--gc-sections','-I'+str(tmp), '-I'+str(ROOT)]
        cmd += ['-I'+str(ROOT/p) for p in ['include','kernel','fs','lib','lib/sqlite3','sdk/include/os32']]
        cmd += [str(ROOT/'tools/tests/sqlite_engine_host.c'),str(tmp/'engine.c'),
                str(tmp/'outer.c'), *objects, '-o',str(exe)]
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
        for name in ['guard bypass','fault app kill','direct call','NULL stub']:
            r=run(args.runner,name)
            assert r.returncode != 0 and 'FAIL:' in r.stdout, (name,r.returncode,r.stdout,r.stderr)
            print('RED (runtime): '+name)
if __name__ == '__main__':
    try: main()
    except subprocess.CalledProcessError as e:
        print(e.stdout or '',e.stderr or '')
        raise
