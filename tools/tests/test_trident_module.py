#!/usr/bin/env python3
"""Trident actual facade/loader loans, rejection/pin/abort and paired product.
Device and real regs integration oracle lives in test_trident_lifecycle.
"""
CHECK = dict(name='trident-module', tier=1, needs=['make:all'], budget=120,
    covers=['drivers/video/trident/*.c', 'drivers/video/trident/*.h',
            'tools/tests/trident_module_host.c', 'tools/tests/test_trident_module.py',
            'tools/tests/test_module_runtime.py', 'tools/tests/test_module_image.py',
            'tools/tests/module_image_host.c', 'tools/tests/kernel_includes.py',
            'tools/tests/host32.py', 'tools/tests/mutpar.py',
            'tools/module_schema.py', 'tools/gen_module_schema.py',
            'tools/gen_sqlite_imports.py', 'tools/gen_kdebug_imports.py',
            'tools/prototypes/trident_imports.def', 'tools/mkmod.py',
            'kernel/module/*.h', 'kernel/module/*.c',
            'drivers/base/dev.c', 'fs/vfs.c', 'kernel/module/kdebug_imports.def',
            'lib/sqlite3/os32_sqlite_vfs.c', 'tools/prototypes/sqlite_imports.def',
            'tools/tests/module_runtime_host.c', 'lib/kstring.c', 'lib/kstring.h',
            'lib/serial_diag.h', 'mm/kmalloc.h', 'exec/appslot.h',
            'include/*.h', 'gfx/gfx_internal.h', 'gfx/gfx_emergency.h', 'gfx/palette.h',
            'build/trident-module.mk', 'build/trident-module.ld', 'build/kernel.mk',
            'build/config.mk', 'build/core.yaml', 'build/packages.yaml', 'Makefile',
            'init/kernel.c', 'init/kernel_gfx_config.inc', 'userland/shell/cmd_sys.c',
            'sdk/kapi.json', 'sdk/include/os32/*.h',
            'build/out/trident/*', 'build/out/trident.mod',
            'build/out/vmkernel.lz4', 'build/out/kernel.elf'])
import os
from pathlib import Path
import json
import struct
import subprocess
import sys
import tempfile
import host32
from kernel_includes import include_flags
from test_module_runtime import FLAGS
ROOT = Path(__file__).resolve().parents[2]
DIR = 'drivers/video/trident/'
# Unique anchors; all mutants compile and fail a normal runtime assertion.
MUTATIONS = (
 ('M_CONTEXT', 'trident_body.c', '!context->display_owner)', 'false)', 'NULL display owner rejection'),
 ('M_PIN_RELEASE', 'trident_module.c', 'if (armed) return TRI_E_BUSY;', 'if (false) return TRI_E_BUSY;', 'arm retains module on abort'),
 ('M_OPTIONAL_NULL', 'trident_module.c', 'if (candidate.fill_rect ||', 'if (!candidate.fill_rect ||', 'production optional NULL slots accepted'),
 ('M_REQUIRED', 'trident_module.c', 'if (!module_text_contains(r, required[i]))', 'if (required[i] && !module_text_contains(r, required[i]))', 'missing required prepare rejected'),
 ('M_PIN_FAIL', 'trident_module.c', 'return MODULE_INIT_FAILED;\n    pinned = true;', 'pinned = false;\n    pinned = true;', 'ref acquisition failure prevents prepare'),
 ('M_PREPARE_RC', 'trident_module.c', 'return rc;\n}\nstatic int detach', 'return 0;\n}\nstatic int detach', 'prepare failure prevents publication'),
)

def build(work, changed=None):
    from mutpar import build_in_tree
    exe=work/'module'
    sources=[DIR+n+'.c' for n in ('backend_trident','probe','regs','default_dac',
        'state_save','state_restore','pc98_init','vram','mode_640','route_ra266','present','palette','lifecycle')]
    cmd=['gcc',*FLAGS,'-I.','-I'+DIR,'-Ibuild/out/trident',
         *include_flags(ROOT),'-nostdlib','-static','-no-pie',
         '-Wl,--gc-sections','tools/tests/trident_module_host.c',*sources,'lib/kstring.c','-o',str(exe)]
    changes={}
    if changed:
        path,old,new=changed; original=(ROOT/path).read_text()
        assert original.count(old)==1,(path,old)
        changes[path]=original.replace(old,new)
    tree=build_in_tree(ROOT,work/'tree',changes,[cmd],capture_output=True)
    return exe,tree

def execute(exe,runners,red=False):
    rejected=False
    for mode in range(37):
        for runner in runners:
            result=host32.run([str(exe)],input=struct.pack('<I',mode),runner=runner,capture_output=True)
            if red:
                assert result.returncode in (0,1),result
                rejected |= result.returncode==1 and result.stdout.startswith(b'RED module ')
            else:
                assert result.returncode==0,(mode,runner,result)
    if red: assert rejected,'mutant survived'

def product(work,runners):
    sys.path.insert(0,str(ROOT/'tools'))
    import mkmod
    from test_module_image import build_validator, validate, fnv, crc, word
    meta=json.loads((ROOT/'build/out/trident/trident_imports.json').read_text())
    spec=json.loads((ROOT/'sdk/kapi.json').read_text())
    kernel=(ROOT/'build/out/vmkernel.lz4').read_bytes()
    data=(ROOT/'build/out/trident.mod').read_bytes()
    rebuilt,stats=mkmod.build((ROOT/'build/out/trident/trident.elf').read_bytes(),meta,kernel,
        spec['version'],spec['generations']['memory_layout'],module_id=13)
    assert rebuilt==data
    contract=dict(kapi=spec['version'],layout=spec['generations']['memory_layout'],
        abi=meta['module_abi_gen'],pair=mkmod.pair_crc(kernel),ops=1,img_valid=1,module_id=13,
        imports=[(fnv(e['name']),e['type_ver'],e['kind']) for e in meta['imports']],
        exports=[(i,fnv(n),1) for i,n in enumerate(('initialize','prepare','restore'),1)])
    exe=build_validator(work)
    for runner in runners:
        good=validate(exe,data,relocate_base=0x1200000,runner=runner,**contract)
        assert good.returncode==0,good
        for field,value in ((31,contract['pair']^1),(6,contract['abi']^1),(8,1),(33,2)):
            bad=bytearray(data); word(bad,field,value)
            result=validate(exe,crc(bad),relocate_base=0x1200000,runner=runner,**contract)
            assert result.returncode!=0,result
    core=mkmod.Elf((ROOT/'build/out/kernel.elf').read_bytes())
    for name in ('trident_screen_init','trident_state_save','trident_module_init','trident_identify'):
        assert name not in core.symbols,name
    for name in ('gfx_backend_trident','trident_module_load','trident_boot_reserve'):
        assert name in core.symbols,name
    print('GREEN paired product, three exports, relocation, ABI/PAIR rejection, core/body split',flush=True)

def main():
    runners=os.environ.get('HOST32_RUNNERS','qemu').split(); assert runners
    with tempfile.TemporaryDirectory(prefix='trident-module-') as td:
        work=Path(td)
        product(work,runners)
        exe,tree=build(work/'normal'); execute(exe,runners)
        print('GREEN context/load/facade/pin/prepare/abort controls',flush=True)
        if '--mutate' in sys.argv:
            for name,path,old,new,reason in MUTATIONS:
                exe,tree=build(work/name,(DIR+path,old,new)); execute(exe,runners,red=True)
                print(f'MUTATE RED {name}: {reason} (compiled runtime)',flush=True)
    return 0

def run(ctx):
    return subprocess.call(['python3','-B','tools/tests/test_trident_module.py',
        *(['--mutate'] if ctx.mutate else [])],cwd=ctx.root,
        env=dict(os.environ,HOST32_RUNNERS=' '.join(ctx.runners)))
if __name__=='__main__': raise SystemExit(main())
