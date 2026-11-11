#!/usr/bin/env python3
"""Trident actual facade/loader loans, rejection/pin/abort and paired product.
Includes real device initialization and core drawing dispatch over fake hardware.
"""
CHECK = dict(name='trident-module', tier=1, needs=['make:all'], budget=8.9,
    covers=['include/copy_units.h', 'arch/*/arch_copy.h', 'gfx/sync/sync_int.h', 'gfx/v2/gfx2_int.h', 'include/gfx_sync.h', 'include/gfx_hal2.h', 'sdk/include/os32/os32_gfx2.h', 'init/sysconfig.h', 'kernel/time/cpu_calibrate.h', 'drivers/video/trident/*.c', 'drivers/video/trident/*.h',
            'tools/tests/trident_module_host.c', 'tools/tests/test_trident_module.py',
            'tools/tests/trident_device_fake.c','tools/tests/trident_sync_fake.c', 'tools/tests/trident_device_fake.h', 'gfx/gfx_core_api.inc',
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
            'init/kernel.c', 'init/kernel_gfx_config.inc', 'platform/pc98/boot_names.c', 'userland/shell/cmd_sys.c',
            'sdk/kapi.json', 'sdk/include/os32/*.h',
            'build/out/trident/*', 'build/out/trident.mod',
            'build/out/vmkernel.lz4', 'build/out/kernel.elf', 'include/exec_arch.h', 'arch/x86/arch_exec_storage.h', 'tools/tests/host_arch/arch_exec_storage.h', 'tools/tests/host_exec_context.h', 'arch/x86/kernel/exec_context.c', 'arch/x86/kernel/exec_entry.c', 'tools/tests/host_exec_entry.h', 'arch/x86/kernel/tss.h'])
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
 ('M_PROBE_REGISTER', 'trident_module.c', 'debug_probe_register(handle, probe_table, probe_count)', '0', 'register before prepare'),
 ('M_PROBE_UNREGISTER', 'trident_module.c', 'if (debug_probe_unregister(handle)) return 0;', '(void)0;', 'unregister before rollback'),
 ('M_PUBLIC_CONTEXT', 'trident_module.c', 'context.backend = &gfx_backend_trident;', 'context.backend = &candidate;', 'runtime publication reaches core'),
 ('M_CONTEXT', 'trident_body.c', '!context->display_owner)', 'false)', 'NULL display owner rejection'),
 ('M_PIN_RELEASE', 'trident_module.c', 'if (armed) return TRI_E_BUSY;', 'if (false) return TRI_E_BUSY;', 'arm retains module on abort'),
 ('M_OPTIONAL_TEXT', 'trident_module.c', 'if (optional[i] && !module_text_contains(r, optional[i]))', 'if (false)', 'out-of-text optional hooks rejected'),
 ('M_OPTIONAL_ACCEPT', 'trident_module.c', 'if (optional[i] && !module_text_contains(r, optional[i]))', 'if (optional[i])', 'in-text optional hooks accepted'),
 ('M_REQUIRED', 'trident_module.c', 'if (!module_text_contains(r, required[i]))', 'if (required[i] && !module_text_contains(r, required[i]))', 'missing required prepare rejected'),
 ('M_PIN_FAIL', 'trident_module.c', 'return MODULE_INIT_FAILED;\n    pinned = true;', 'pinned = false;\n    pinned = true;', 'ref acquisition failure prevents prepare'),
 ('M_PREPARE_RC', 'trident_module.c', 'return rc;\n}\nstatic int detach', 'return 0;\n}\nstatic int detach', 'prepare failure prevents publication'),
 ('M_MODE_COUNT', 'trident_module.c', '!candidate.mode_count || candidate.mode_count > TRI_MODE_COUNT ||', 'candidate.mode_count || candidate.mode_count > TRI_MODE_COUNT ||', 'nonempty mode array accepted'),
 ('M_MODE_LIMIT', 'trident_module.c', 'candidate.mode_count > TRI_MODE_COUNT', 'candidate.mode_count > 2', 'three-mode normal control accepted'),
 ('M_DEFAULT', 'trident_module.c', 'candidate.default_mode >= candidate.mode_count', 'candidate.default_mode > candidate.mode_count', 'default at end rejected'),
 ('M_MODE_RANGE', 'trident_module.c', 'a - r->base <= size - bytes', 'a - r->base > size - bytes', 'mode and device data must be resident'),
 ('M_SELECTION_END', 'trident_module.c', '(selected - first) / sizeof(GfxMode) >= candidate.mode_count', '(selected - first) / sizeof(GfxMode) > candidate.mode_count', 'one-past selection rejected'),
 ('M_SELECTION_ALIGN', 'trident_module.c', '(selected - first) % sizeof(GfxMode)', '0', 'unaligned selection rejected'),
 ('M_DAC', 'trident_module.c', 'm->dac_bits != TRI_MODULE_DAC_BITS', 'false', 'six-bit DAC required'),
 ('M_RESERVED', 'trident_module.c', '|| m->reserved ||', '|| false ||', 'reserved mode byte rejected'),
 ('M_MIN_PITCH', 'trident_module.c', 'm->pitch < (u32)m->width * cpp', 'false', 'depth-scaled minimum pitch'),
 ('M_PITCH_ALIGN', 'trident_module.c', 'm->pitch % cpp', '0', 'pitch aligned to pixel size'),
 ('M_MAP_CAPACITY', 'trident_module.c', 'm->pitch > res->mapped_bytes / m->height', 'false', 'mapped capacity enforced'),
 ('M_DECODE_CAPACITY', 'trident_module.c', 'm->pitch > res->decode_bytes / m->height', 'false', 'decode capacity enforced'),
 ('M_BB_PITCH', 'trident_module.c', 'candidate.bb_pitch != selected->pitch', 'false', 'selected pitch matches BB'),
 ('M_BB_FORMAT', 'trident_module.c', 'candidate.bb_format != GFX_BB_PACKED8', 'false', 'legacy BB marker retained'),
 ('M_BB_BASE', 'trident_module.c', 'candidate.bb_base == res->client', 'true', 'BB uses client base'),
 ('M_BB_SIZE', 'trident_module.c', 'candidate.bb_size == res->client_bytes', 'true', 'BB size matches descriptor'),
 ('M_CLIENT_CAPACITY', 'trident_module.c', 'selected->pitch <= candidate.bb_size / selected->height', 'true', 'selected image fits client capacity'),
 ('M_CLIENT_PITCH', 'trident_module.c', 'res->client_surface.pitch == selected->pitch', 'true', 'surface pitch matches selection'),

)

def build(work, changed=None):
    from mutpar import build_in_tree
    exe=work/'module'
    sources=[DIR+n+'.c' for n in ('backend_trident','probe','regs','default_dac',
        'state_save','state_restore','pc98_init','vram','accel','cursor','ddc1','mode_640','route_ra266','present','palette','screen','video','dpms','lifecycle')]
    cmd=['gcc',*FLAGS,'-I.','-I'+DIR,'-Ibuild/out/trident',
         *include_flags(ROOT),'-nostdlib','-static','-no-pie',
         '-Wl,--gc-sections','tools/tests/trident_module_host.c',*sources,'lib/kstring.c','tools/tests/trident_device_fake.c','tools/tests/trident_sync_fake.c','-Wl,--wrap=trident_regs_bind','-o',str(exe)]
    changes={}
    if changed:
        path,old,new=changed; original=(ROOT/path).read_text()
        assert original.count(old)==1,(path,old)
        changes[path]=original.replace(old,new)
    tree=build_in_tree(ROOT,work/'tree',changes,[cmd],capture_output=True)
    return exe,tree

def execute(exe,runners,red=False):
    rejected=False
    for mode in range(68):
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
        abi=meta['module_abi_gen'],pair=mkmod.pair_crc(kernel),ops=5,img_valid=1,module_id=13,
        imports=[(fnv(e['name']),e['type_ver'],e['kind']) for e in meta['imports']],
        exports=[(i,fnv(n),1) for i,n in enumerate(('initialize','prepare','restore','get_probe_decls'),1)])
    exe=build_validator(work)
    for runner in runners:
        good=validate(exe,data,relocate_base=0x1200000,runner=runner,**contract)
        assert good.returncode==0,good
        for field,value in ((31,contract['pair']^1),(6,contract['abi']^1),(8,1),(33,1)):
            bad=bytearray(data); word(bad,field,value)
            result=validate(exe,crc(bad),relocate_base=0x1200000,runner=runner,**contract)
            assert result.returncode!=0,result
    core=mkmod.Elf((ROOT/'build/out/kernel.elf').read_bytes())
    for name in ('trident_screen_init','trident_state_save','trident_module_init','trident_identify'):
        assert name not in core.symbols,name
    for name in ('gfx_backend_trident','trident_module_load','trident_boot_reserve'):
        assert name in core.symbols,name
    print('GREEN paired product, four exports, relocation, ABI/PAIR rejection, core/body split',flush=True)

def boot_order(work, runners):
    # Execute the real boot call sequence with observers for module publication.
    source=(ROOT/'init/kernel.c').read_text()
    start=source.index('    if (kdebug.ops.crt_diag_bootlog)')
    end=source.index('    kaudit_post_probe();', start)
    fixture=r'''#include "gfx_hal.h"
static int pref, loaded, prepared, bad;
int gfx_get_backend_pref(void) { return pref; }
static void trident_module_load(void) { ++loaded; }
static void geforce_module_load(void) { }
static void cirrus_module_load(void) { }
void gfx_prepare_backend(void) {
    ++prepared;
    if (loaded != (pref == GFX_PREF_TRIDENT)) bad=1;
}
static struct { struct { void (*crt_diag_bootlog)(void); } ops; } kdebug;
static void boot(void) {
'''+source[start:end]+r'''}
void _start(void) {
    for (pref=GFX_PREF_AUTO; pref<=GFX_PREF_TRIDENT; ++pref) {
        loaded=prepared=0; boot();
        if (prepared != 1 || loaded != (pref == GFX_PREF_TRIDENT)) bad=1;
    }
    __asm__ volatile("int $0x80" : : "a"(1), "b"(bad) : "memory");
    __builtin_unreachable();
}
'''
    path=work/'boot.c'; path.write_text(fixture); exe=work/'boot'
    subprocess.run(['gcc',*FLAGS,*include_flags(ROOT),'-nostdlib','-static','-no-pie',
                    str(path),'-o',str(exe)],check=True)
    for runner in runners:
        result=host32.run([str(exe)],runner=runner,capture_output=True)
        assert result.returncode==0,result
    print('GREEN boot loads Trident before backend preparation',flush=True)


def main():
    runners=os.environ.get('HOST32_RUNNERS','qemu').split(); assert runners
    with tempfile.TemporaryDirectory(prefix='trident-module-') as td:
        work=Path(td)
        boot_order(work,runners)
        product(work,runners)
        exe,tree=build(work/'normal'); execute(exe,runners)
        print('GREEN context/load/facade/pin/prepare/abort controls',flush=True)
        if '--mutate' in sys.argv:
            for name,path,old,new,reason in MUTATIONS:
                exe,tree=build(work/name,(DIR+path,old,new)); execute(exe,runners,red=True)
                print(f'MUTATE RED {name}: {reason} (compiled runtime)',flush=True)
            execute(build(work/'restored')[0],runners)
            print('GREEN restored module controls',flush=True)
    return 0

def run(ctx):
    return subprocess.call(['python3','-B','tools/tests/test_trident_module.py',
        *(['--mutate'] if ctx.mutate else [])],cwd=ctx.root,
        env=dict(os.environ,HOST32_RUNNERS=' '.join(ctx.runners)))
if __name__=='__main__': raise SystemExit(main())
