#!/usr/bin/env python3
"""SoftFloat GCC ABI and SQLite REAL/datetime runtime regression, no libc."""
import argparse
import os
from pathlib import Path
import subprocess
import tempfile
import host32

ROOT=Path(__file__).resolve().parents[2]
DIRECTORY=ROOT/'build/out/sqlite-proto'
CC=Path(os.environ['CROSS_DIR'])/'bin/i386-elf-gcc'
FLAGS=['-m32','-march=i386','-std=gnu11','-Os','-ffreestanding','-fno-pie',
       '-fno-stack-protector','-msoft-float','-mno-fp-ret-in-387','-I'+str(ROOT),
       '-I'+str(ROOT/'include')]

def compile_obj(source, out, extra=()):
    subprocess.run([CC,*FLAGS,*extra,'-c',source,'-o',out],check=True)

def run(work, runner, mutation=None):
    objects=[]
    for name in ('softfloat_abi','softfloat_bits'):
        source=(ROOT/'lib'/f'{name}.c').read_text()
        if mutation and mutation[0]==name:
            old,new=mutation[1:]
            assert source.count(old)==1, old
            source=source.replace(old,new)
        path=work/f'{name}.c'; path.write_text(source)
        obj=work/f'{name}.o'
        compile_obj(path,obj,['-I'+str(ROOT/'lib')]); objects.append(obj)
    sqlite=DIRECTORY/'sqlite3.o'
    if mutation and mutation[0]=='datetime':
        sqlite=work/'sqlite3.o'
        compile_obj(ROOT/'lib/sqlite3/sqlite3.c',sqlite,
            ['-std=gnu89','-w','-DSQLITE_OMIT_DATETIME_FUNCS=1',
             '-include',str(ROOT/'lib/sqlite3/os32_sqlite_config.h')])
    fixture=work/'fixture.o';compile_obj(ROOT/'tools/tests/softfloat_sql_host.c',fixture)
    reference=work/'reference.o'
    compile_obj(ROOT/'tools/tests/softfloat_x87_reference.c',reference,['-mhard-float'])
    objects.append(reference)
    exe=work/'softfloat-sql'
    subprocess.run([CC,'-m32','-nostdlib','-static','-no-pie',
        str(ROOT/'sdk/link/generations.ld'),fixture,*objects,sqlite,
        *[DIRECTORY/(name+'.o') for name in ('kstring','kstring_c','kmath','divdi3')],
        *sorted((DIRECTORY/'softfloat').glob('*.o')),'-o',exe],check=True)
    return host32.run([str(exe)],runner=runner,capture_output=True)

def main():
    parser=argparse.ArgumentParser();parser.add_argument('--mutate',action='store_true')
    parser.add_argument('--runner',default='qemu',choices=['native','qemu'])
    args=parser.parse_args()
    with tempfile.TemporaryDirectory(prefix='softfloat-sql-') as directory:
        work=Path(directory)
        result=run(work,args.runner);assert result.returncode==0,result
        print('PASS soft GCC arithmetic/comparisons/conversions/NaN/inf/signed zero; SQL REAL/datetime; runner='+args.runner,flush=True)
        if args.mutate:
            mutations=[
                ('softfloat_abi','return pack(f64_add(unpack(a), unpack(b)));','return pack(f64_sub(unpack(a), unpack(b)));'),
                ('softfloat_abi','return compare(a, b, -1);','return compare(a, b, 1);'),
                ('softfloat_bits','result = f64_to_i32(value, softfloat_round_minMag, false);','result = f64_to_i32(value, softfloat_round_near_even, false);'),
                ('datetime',None,None)]
            # Two g*/unordered wrappers share the same body; mutate one complete
            # declaration to keep every source anchor unique.
            mutations[1]=('softfloat_abi',
                'int __gtdf2(double a, double b) { return compare(a, b, -1); }',
                'int __gtdf2(double a, double b) { return compare(a, b, 1); }')
            for mutation in mutations:
                result=run(work,args.runner,mutation)
                assert result.returncode==1,(mutation,result)
                print('MUTATE '+mutation[0]+': '+str(mutation[1])+' RED runtime rc=1',flush=True)
if __name__=='__main__': main()
