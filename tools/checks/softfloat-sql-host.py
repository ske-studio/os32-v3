#!/usr/bin/env python3
CHECK = dict(name='softfloat-sql-host', tier=1,
    covers=['build/out/sqlite-proto/*.o', 'build/out/sqlite-proto/softfloat/*.o', 'tools/tests/test_softfloat_sql.py', 'tools/tests/softfloat_sql_host.c',
            'tools/tests/host32.py', 'tools/tests/softfloat_x87_reference.c', 'lib/softfloat/**', 'lib/softfloat_abi.c',
            'lib/softfloat_bits.c', 'lib/softfloat_bits.h', 'build/softfloat.mk',
            'build/sqlite-proto.mk', 'build/config.mk', 'lib/sqlite3/**',
            'lib/kstring.c', 'lib/kstring_c.c', 'lib/kmath.c', 'lib/divdi3.c',
            'include/types.h', 'lib/kstring.h', 'sdk/link/generations.ld'],
    needs=['make:sqlite-module-proto'], budget=15,
    basis=['[L1]', '[ABI]'])

def run(ctx):
    import subprocess
    for i, runner in enumerate(ctx.runners):
        rc=subprocess.call(['python3','-B','tools/tests/test_softfloat_sql.py',
            '--runner',runner]+(['--mutate'] if ctx.mutate and i==0 else []),cwd=ctx.root)
        if rc: return rc
    return 0 if ctx.runners else 1
