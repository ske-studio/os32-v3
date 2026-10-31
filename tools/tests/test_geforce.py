"""P5 HAL, real P3 start state machine, real ledger/selection and byte routing."""
CHECK = dict(name='geforce-host', tier=1,
    covers=['tools/tests/paging_source.py', 'drivers/video/geforce/backend_geforce.c', 'drivers/video/geforce/gf_present.c', 'drivers/video/pc98/pc98_route.c', 'drivers/video/pc98/pc98_route.h', 'drivers/video/geforce/geforce_module.c', 'drivers/video/geforce/geforce_module.h', 'drivers/video/geforce/geforce_body.c', 'lib/pc98_cell.h', 'gfx/gfx_core.c', 'include/gfx_hal.h', 'include/video_service.h', 'init/kernel.c', 'mm/pgalloc.h', 'hwdev/gf/service.c', 'hwdev/gf/display/modes.c', 'tools/tests/backend_geforce_host.c', 'tools/tests/gf_present_host.c', 'tools/tests/pc98_route_host.c', 'tools/tests/test_gfx_kernel_fb.py', 'tools/tests/gfx_kernel_fb_host.c', 'tools/tests/host32.py', 'tools/tests/kernel_layout.py', 'tools/tests/host_arch/arch_io.h', 'tools/tests/serial_diag_host.h', 'build/geforce-module.mk', 'build/geforce-module.ld', 'tools/prototypes/geforce_imports.def', 'tools/gen_sqlite_imports.py', 'tools/module_schema.py', 'build/core.yaml', 'build/kernel.mk', 'Makefile', 'tools/tests/geforce_module_host.c', 'tools/tests/module_runtime_host.c', 'tools/tests/test_module_runtime.py', 'tools/tests/test_module_image.py', 'tools/tests/module_image_host.c', 'tools/mkmod.py', 'build/out/geforce.mod', 'build/out/geforce/*', 'build/out/kernel.elf', 'build/out/vmkernel.lz4', 'include/gfx_format.h', 'exec/access_walk.c', 'exec/lease.c', 'exec/redir_access.c', 'exec/surface_query.c', 'drivers/video/cirrus/backend_cirrus.c', 'drivers/video/cirrus/cirrus_identify.c', 'drivers/video/pc98/backend_pc98.c', 'drivers/video/pc98/backend_pegc.c', 'gfx/palette.c', 'gfx/gfx_scroll.c', 'gfx/gfx_vram.c', 'mm/paging.c', 'platform/pc98/platform_memory_desc.h', 'include/platform_memory.h', 'mm/pgalloc.c', 'mm/physmem.c', 'mm/shlib.c', 'kernel/sys.c', 'tools/tests/mutpar.py', 'lib/serial_diag.h', 'gfx/gfx_internal.h', 'tools/tests/gfx_reinit_host.c', 'tools/tests/test_gfx_reinit.py', 'lib/utf8.c', 'tools/tests/gfx_attach_host.c', 'tools/tests/test_gfx_attach.py', 'userland/lib/gfx/**', 'tools/tests/test_gfx_reattach.py', 'tools/tests/gfx_reattach_host.c', 'tools/run_host_group.py', 'kernel/module/module_runtime.c', 'kernel/module/module_image.c', 'platform/pc98/boot_names.c', 'hwdev/gf/internal.h', 'hwdev/gfinit_mode.h', 'drivers/base/dev.c', 'fs/vfs.c', 'sdk/kapi.json', 'drivers/video/geforce/geforce_platform.c', 'kernel/gui.c', 'kernel/gui.h', 'kernel/console/console.c', 'platform/pc98/console.c', 'platform/pc98/display.c', 'tools/tests/dep_dir_d_console_grid_host.c', 'tools/tests/test_dep_dir_d_console.py', 'hwdev/gf/production.h', 'include/gf_production.h', 'mm/memory_boot.c', 'lib/kprintf_attr.c', 'platform/pc98/console_attr.c', 'tools/tests/dep_dir_d_console_host.c', 'tools/tests/fixtures/dep_dir_d/*', 'tools/tests/kernel_includes.py', 'tools/tests/kernel_boot_source.py', 'kernel/module/kdebug_imports.def', 'lib/sqlite3/os32_sqlite_vfs.c', 'tools/gen_kdebug_imports.py', 'tools/prototypes/sqlite_imports.def'],
    needs=['make:all'], budget=60.0)

from kernel_includes import include_flags
from pathlib import Path
from kernel_boot_source import kernel_source
import subprocess
import tempfile
import host32
from test_gfx_kernel_fb import run_case, replace_function
ROOT=Path(__file__).resolve().parents[2]
FLAGS=['gcc','-m32','-march=i386','-std=gnu11','-O2','-Wall','-Wextra','-Werror',
       '-ffreestanding','-fno-pie','-fno-stack-protector','-nostdlib','-static','-no-pie']
INCLUDES=include_flags(ROOT)

def component(name):
    with tempfile.TemporaryDirectory(prefix='gf-hal-') as td:
        exe=Path(td)/'host'
        subprocess.run(FLAGS+INCLUDES+[str(ROOT/'tools/tests'/f'{name}_host.c'),'-o',str(exe)],check=True)
        result=host32.run([str(exe)],capture_output=True,text=True,timeout=60)
        assert result.returncode==0,(name,result.returncode,result.stdout,result.stderr)
        print('PASS '+name,flush=True)

def core(init_fail=0, unsafe=0, missing=0, gui_first=0):
    fixture=(ROOT/'tools/tests/gfx_kernel_fb_host.c').read_text().replace('return id == 2 ? &slot : 0;', 'return id == 2 || id == APP_ID_SHELL ? &slot : 0;')
    fixture=fixture.replace('void gui_display_failed(void) {}', '')
    fixture=fixture.replace('static void run(void)\n{',
        '#include "geforce_core_cases.inc"\nstatic void run(void)\n{',1)
    fixture=replace_function(fixture,'static void run(void)',r'''
    CHECK(gfx_geforce_pref(1)==GFX_PREF_PC98);
    CHECK(gfx_geforce_pref(0)==GFX_PREF_GEFORCE);
    CHECK(gfx_geforce_pref(2)==GFX_PREF_GEFORCE && gfx_geforce_pref(-1)==GFX_PREF_GEFORCE);
    map_host(0x10000,0xff0000); map_host(GF_TEST_DISPLAY,MEM_GFX_BB8_SIZE);
    boot(); gfx_set_backend_pref(GFX_PREF_GEFORCE); gfx_boot_reserve();
    const struct ledger_surface *client=ledger_surface_find(LEDGER_SF_GEFORCE,LEDGER_ROLE_CLIENT);
    const struct ledger_surface *display=ledger_surface_find(LEDGER_SF_GEFORCE,LEDGER_ROLE_DISPLAY);
    CHECK(client && display && client->backing==LEDGER_SB_RAM && display->backing==LEDGER_SB_MMIO);
    CHECK(client->npages*PAGE_SIZE==307200 && display->npages*PAGE_SIZE==307200);
    CHECK(!ledger_surface_find(LEDGER_SF_PEGC,LEDGER_ROLE_CLIENT));
    CHECK(!ledger_surface_find(LEDGER_SF_PEGC,LEDGER_ROLE_DISPLAY));
    CHECK(!geforce_range_reserved(GF_TEST_BAR0,GF_PRODUCTION_BAR0_BYTES));
    CHECK(!geforce_range_reserved(GF_TEST_DISPLAY,GF_PRODUCTION_BAR1_BYTES));
    CHECK(!geforce_range_reserved(GF_TEST_BAR2,GF_PRODUCTION_BAR2_BYTES));
    CHECK((paging_pte_raw(GF_TEST_BAR0)&(PTE_PRESENT|PTE_USER|PTE_PCD))==(PTE_PRESENT|PTE_PCD));
    CHECK((paging_pte_raw(GF_TEST_DISPLAY)&(PTE_PRESENT|PTE_USER|PTE_PCD))==(PTE_PRESENT|PTE_PCD));
    CHECK(!(paging_pte_raw(GF_TEST_BAR2)&PTE_PRESENT));
    CHECK(!(paging_pte_raw(GF_TEST_DISPLAY+MEM_GFX_BB8_SIZE)&PTE_PRESENT));
    CHECK(!acquires && !starts);
    if (!MISSING) geforce_backend_bind(&device_gf,identify_gf,0,point_gf,0);
    gfx_prepare_backend(); CHECK(!acquires && !starts);
    decode_safe=!UNSAFE; host_gui=GUI_FIRST;
    gfx_init();
    if (INIT_FAIL || MISSING) {
        CHECK(acquires==(MISSING ? 0 : 1) && !starts);
        struct gfx_kernel_fb fb;
        if (UNSAFE) {
            struct surface_query_source source;
            CHECK(gfx_surface_source(LEDGER_ROLE_CLIENT,&source)==OS32_ERR_INVAL);
            CHECK(!host_planar_inits && !console_safe);
            gfx_init_200(); CHECK(!starts && acquires==1 && !host_planar_inits);
        } else {
            CHECK(g_backend==&gfx_backend_pc98);
            CHECK(!gfx_kernel_framebuffer(&fb) && fb.height==400);
        }
    } else {
        CHECK(acquires==1 && releases==1 && starts==1 && !validates);
        CHECK(gfx_sf_backend()==LEDGER_SF_GEFORCE && gfx_current_height==480);
        CHECK(bb[0]==P2V(client->first*PAGE_SIZE) && !bb[1]);
        CHECK(gfx_backend_geforce.bb_size==307200 && gfx_selected_selfcheck());
        CHECK(cell_checks==1 && route_gf==1);
        gfx_init_200();
        CHECK(gfx_sf_backend()==LEDGER_SF_GEFORCE && gfx_current_height==480);
        CHECK(acquires==1 && starts==1 && cell_checks==1 && route_gf==1);
        host_user=0; gfx_shutdown(); CHECK(stops==1 && route_98==1);
        gfx_init_200(); CHECK(acquires==1 && starts==1 && validates==1);
        CHECK(gfx_sf_backend()==LEDGER_SF_GEFORCE && gfx_current_height==480);
        CHECK(cell_checks==2 && route_gf==2);
        host_gui=1; slot.abort_req=0;
        fail_palette=1; u8 rgb[3]={1,2,3};
        gfx_backend_geforce.set_palette(16,1,rgb); CHECK(wm_pending && !slot.abort_req);
        struct surface_query_source source;
        CHECK(gfx_surface_source(LEDGER_ROLE_CLIENT,&source)==OS32_ERR_INVAL);
        CHECK(gfx_sf_backend()==LEDGER_SF_GEFORCE && !host_planar_inits);
        wm_consume(); CHECK(gfx_sf_backend()==LEDGER_SF_PC98);
        CHECK(bb[0] && bb[1] && !wm_pending);
    }
    SAY("PASS GF core lifecycle/reservation/fallback"); die(0);
''')
    fixture=f'#define GF_RESERVATION_TEST 1\n#define INIT_FAIL {init_fail}\n#define UNSAFE {unsafe}\n#define MISSING {missing}\n#define GUI_FIRST {gui_first}\nunsigned int host_arch_if=0x202U;\n'+fixture
    def stage(tmp,sources):
        gui=(ROOT/'kernel/gui.c').read_text()
        (tmp/'geforce_gui_notice.inc').write_text(gui[gui.index('void gui_display_failed(void)'):])
        source=(ROOT/'drivers/video/geforce/geforce_module.c').read_text()
        start=source.index('int geforce_boot_reserve(void)')
        end=source.index('static int geforce_return(',start)
        (tmp/'geforce_reserve.inc').write_text(source[start:end])
        for rel in ('drivers/video/geforce/backend_geforce.c','hwdev/gf/service.c','hwdev/gf/display/modes.c'):
            sources.append(ROOT/rel)
        # Transfer is tested separately; this fixture tests the HAL's boundary.
        transfer=tmp/'transfer.c'
        transfer.write_text('''#include "types.h"
int gf_present_rect(const u8 *c,u32 cp,volatile u8 *d,u32 dp,volatile u8 *b,int x,int y,int w,int h,int (*p)(void *),void *v)
{(void)c;(void)cp;(void)d;(void)dp;(void)b;(void)x;(void)y;(void)w;(void)h;(void)p;(void)v;return 0;}
''')
        sources.append(transfer)
    result=run_case(None,'qemu',fixture_body=fixture,stage=stage,extra_changes=[
        ('gfx/gfx_core.c','static void _gfx_common_init(int plane_sz)\n{',
         'static void _gfx_common_init(int plane_sz)\n{\n    extern int host_planar_inits; host_planar_inits++;')])
    assert result.returncode==0,(init_fail,unsafe,missing,result.returncode,result.stdout,result.stderr)
    print(result.stdout,end='',flush=True)

def unsafe_console(old=False):
    from test_dep_dir_d_console import grid_instrument
    body=(ROOT/'kernel/console/console.c').read_text()
    if old:
        body=subprocess.check_output(['git','show','HEAD:kernel/console/console.c'],cwd=ROOT,text=True)
    harness=(ROOT/'tools/tests/dep_dir_d_console_grid_host.c').read_text()
    harness='int safe=1; static int serial_count;\n'+harness
    harness=harness.replace('    if (address < TVRAM_CHAR_BASE',
                            '    if (!safe) finish(71);\n    if (address < TVRAM_CHAR_BASE')
    harness=harness.replace('event(2, p, v);', 'if (!safe) finish(72); event(2, p, v);')
    harness=harness.replace('event(9,(u8)c,0);', 'serial_count++; event(9,(u8)c,0);')
    harness=replace_function(harness,'void _start(void)',r'''
        (void)snapshot; seed(); active=gui=0; rshell_active=1;
        console_write("A",1,0xe1); /* positive control: normal console writes */
        safe=0; serial_count=0;
        console_write("X",1,0xe1); shell_print("Y",0xe1);
        shell_print_utf8("Z",0xe1); shell_putchar('!',0xe1);
        tvram_clear(); tvram_putchar_at(0,0,'X',0xe1);
        tvram_putkanji_at(0,0,0x2422,0xe1); tvram_scroll();
        u16 code=0; u8 attr=0; tvram_readchar_at(0,0,&code,&attr);
        (void)tvram_reverse_cell(0,0);
        console_set_cursor(0,0); console_hw_cursor_enable();
        console_text_gdc_stop(); gui=1; console_text_gdc_start();
        if (serial_count!=3) finish(73);
        safe=1; console_write("R",1,0xe1); /* safe return resumes output */
        finish(0);
    ''')
    with tempfile.TemporaryDirectory(prefix='gf-console-') as td:
        tmp=Path(td); exe=tmp/'host'
        (tmp/'console_source.inc').write_text(grid_instrument(body))
        (tmp/'platform/pc98').mkdir(parents=True)
        (tmp/'platform/pc98/console.c').write_text(grid_instrument((ROOT/'platform/pc98/console.c').read_text()))
        (tmp/'host.c').write_text(harness)
        (tmp/'safe.c').write_text('extern int safe; int pc98_console_safe(void) { return safe; }')
        subprocess.run(FLAGS+['-I'+td,*INCLUDES,'-Wno-sign-compare',str(tmp/'host.c'),str(tmp/'safe.c'),'-o',str(exe)],check=True)
        result=host32.run([str(exe)],capture_output=True,text=True,timeout=60)
        if old:
            assert result.returncode==71,result
            print('REPRO unsafe console writes TVRAM (rc=71)',flush=True)
        else:
            assert result.returncode==0,result
            print('PASS unsafe console: ordinary output/clear/cursor/GUI return suppress MMIO, serial retained',flush=True)


def product():
    import json
    import sys
    sys.path.insert(0,str(ROOT/'tools'))
    import mkmod
    from test_module_image import build_validator, validate, fnv
    boot=kernel_source(ROOT)
    assert 'gfx_geforce_pref(sysconfig_get_int(SYS_SYSTEM_CFG, \"GUI\", 0))' in boot
    spec=json.loads((ROOT/'sdk/kapi.json').read_text())
    meta=json.loads((ROOT/'build/out/geforce/geforce_imports.json').read_text())
    kernel=(ROOT/'build/out/vmkernel.lz4').read_bytes()
    body=(ROOT/'build/out/geforce/geforce.elf').read_bytes()
    data=(ROOT/'build/out/geforce.mod').read_bytes()
    rebuilt,stats=mkmod.build(body,meta,kernel,spec['version'],spec['generations']['memory_layout'],module_id=12)
    assert rebuilt==data
    symbols=mkmod.Elf(body).symbols
    for symbol in ('gf_video_ops','gf_init_run','gf_display_modeset','geforce_backend_ops','gf_production_prepare',
                   'gf_production_identify','gf_production_quiesce','gf_production_decode_safe'):
        assert symbol in symbols,symbol
    for symbol in ('recover_run','gfinit_decode_rom','gfinit_plan_c','gfinit_begin'):
        assert symbol not in symbols,symbol
    with tempfile.TemporaryDirectory(prefix='gf-image-') as td:
        exe=build_validator(Path(td))
        result=validate(exe,data,runner='qemu',relocate_base=0x1200000,
            kapi=spec['version'],layout=spec['generations']['memory_layout'],
            abi=meta['module_abi_gen'],pair=mkmod.pair_crc(kernel),ops=1,img_valid=1,module_id=12,
            imports=[(fnv(e['name']),e['type_ver'],e['kind']) for e in meta['imports']],
            exports=[(1,fnv('initialize'),1)])
        assert result.returncode==0,result
    print('PASS paired GF product: P2/P3 symbols, independent of KDEBUG, relocation',flush=True)

def module_registration():
    import struct
    from test_module_runtime import FLAGS as runtime_flags, INCLUDES as runtime_includes, image
    with tempfile.TemporaryDirectory(prefix='gf-register-') as td:
        exe=Path(td)/'host'
        subprocess.run(['gcc',*runtime_flags,'-Wno-unused-parameter',*runtime_includes,'-Igfx','-Ibuild/out/geforce',
            str(ROOT/'tools/tests/geforce_module_host.c'),
            *[str(ROOT/f) for f in ('kernel/module/module_runtime.c','kernel/module/module_image.c','fs/vfs.c','drivers/base/dev.c')],'-nostdlib','-static','-no-pie',
            '-Wl,--gc-sections','-o',str(exe)],cwd=ROOT,check=True)
        for mode in range(3):
            result=host32.run([str(exe)],input=struct.pack('<II',0,mode)+image(),
                              capture_output=True,timeout=60)
            assert result.returncode==0,(mode,result.returncode,result.stdout,result.stderr)
    print('PASS GF registration: missing stub, NULL acceleration, prepare required, lifetime pin',flush=True)

def main():
    unsafe_console()
    product()
    module_registration()
    for name in ('gf_present','backend_geforce','pc98_route'): component(name)
    for args in ((0,0,0,0),(0,0,0,1),(1,0,0,0),(1,1,0,0),(0,0,1,0)): core(*args)
    return 0
if __name__=='__main__': raise SystemExit(main())


def run(ctx) -> int:
    import os
    import shlex
    import subprocess
    base_env = dict(os.environ, HOST32_RUNNERS=' '.join(ctx.runners))
    env = dict(base_env)
    argv = ['python3', '-B', 'tools/tests/test_geforce.py']
    rc = subprocess.call(argv, cwd=ctx.root, env=env, close_fds=False)
    if rc:
        return rc
    return 0
