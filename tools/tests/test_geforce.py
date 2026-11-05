"""P5 HAL, real P3 start state machine, real ledger/selection and byte routing."""
CHECK = dict(name='geforce-host', tier=1,
    covers=['gfx/palette.h', 'hwdev/gf/vram_layout.h', 'tools/tests/test_geforce.py', 'tools/tests/gf_startlog_host.h', 'tools/tests/gf_ownerless_fixture.py', 'tools/tests/gf_accel_no_bind.h', 'tools/tests/gf_accel_unbound_stubs.c', 'tools/tests/gf_accel_host.h', 'tools/tests/gf_command_runtime_host.h', 'hwdev/gf/accel.h', 'hwdev/gf/vram_view.h', 'hwdev/gf/accel_service.h', 'hwdev/gf/foundation.h', 'hwdev/gf/gf2_test.h', 'drivers/video/geforce/geforce_tick.h', 'tools/tests/gf_wc_recovery_host.inc', 'exec/exec.c', 'exec/exec_*.inc', 'arch/x86/kernel/setjmp.asm', 'arch/x86/kernel/cpu_flags.inc', 'tools/tests/exec_source.py', 'sdk/gen_kapi.py', 'drivers/video/geforce/gf_wc.c', 'arch/x86/kernel/mtrr_wc.h', 'tools/tests/gf_wc_host.c', 'tools/tests/geforce_core_cases.inc', 'tools/tests/paging_source.py', 'drivers/video/geforce/backend_geforce.c', 'drivers/video/geforce/gf_present.c', 'drivers/video/pc98/pc98_route.c', 'drivers/video/pc98/pc98_route.h', 'drivers/video/geforce/geforce_module.c', 'drivers/video/geforce/geforce_module.h', 'drivers/video/geforce/geforce_body.c', 'lib/pc98_cell.h', 'gfx/gfx_core.c', 'include/gfx_hal.h', 'include/video_service.h', 'init/kernel.c', 'mm/pgalloc.h', 'hwdev/gf/service.c', 'hwdev/gf/display/modes.c', 'tools/tests/backend_geforce_host.c', 'tools/tests/gf_present_host.c', 'tools/tests/pc98_route_host.c', 'tools/tests/test_gfx_kernel_fb.py', 'tools/tests/gfx_kernel_fb_host.c', 'tools/tests/host32.py', 'tools/tests/kernel_layout.py', 'tools/tests/host_arch/arch_io.h', 'tools/tests/serial_diag_host.h', 'build/geforce-module.mk', 'build/geforce-module.ld', 'tools/prototypes/geforce_imports.def', 'tools/gen_sqlite_imports.py', 'tools/module_schema.py', 'build/core.yaml', 'build/kernel.mk', 'Makefile', 'tools/tests/geforce_module_host.c', 'tools/tests/module_runtime_host.c', 'tools/tests/test_module_runtime.py', 'tools/tests/test_module_image.py', 'tools/tests/module_image_host.c', 'tools/mkmod.py', 'build/out/geforce.mod', 'build/out/geforce/*', 'build/out/kernel.elf', 'build/out/vmkernel.lz4', 'include/gfx_format.h', 'exec/access_walk.c', 'exec/lease.c', 'exec/redir_access.c', 'exec/surface_query.c', 'drivers/video/cirrus/backend_cirrus.c', 'drivers/video/cirrus/cirrus_identify.c', 'drivers/video/pc98/backend_pc98.c', 'drivers/video/pc98/backend_pegc.c', 'gfx/palette.c', 'gfx/gfx_scroll.c', 'gfx/gfx_vram.c', 'mm/paging.c', 'platform/pc98/platform_memory_desc.h', 'include/platform_memory.h', 'mm/pgalloc.c', 'mm/physmem.c', 'mm/shlib.c', 'kernel/sys.c', 'tools/tests/mutpar.py', 'lib/serial_diag.h', 'gfx/gfx_internal.h', 'tools/tests/gfx_reinit_host.c', 'tools/tests/test_gfx_reinit.py', 'lib/utf8.c', 'tools/tests/gfx_attach_host.c', 'tools/tests/test_gfx_attach.py', 'userland/lib/gfx/**', 'tools/tests/test_gfx_reattach.py', 'tools/tests/gfx_reattach_host.c', 'tools/run_host_group.py', 'kernel/module/module_runtime.c', 'kernel/module/module_image.c', 'platform/pc98/boot_names.c', 'hwdev/gf/internal.h', 'hwdev/gfinit_mode.h', 'drivers/base/dev.c', 'fs/vfs.c', 'sdk/kapi.json', 'drivers/video/geforce/geforce_platform.c', 'kernel/gui.c', 'kernel/gui.h', 'kernel/console/console.c', 'platform/pc98/console.c', 'platform/pc98/display.c', 'tools/tests/dep_dir_d_console_grid_host.c', 'tools/tests/test_dep_dir_d_console.py', 'hwdev/gf/production.h', 'include/gf_production.h', 'mm/memory_boot.c', 'lib/kprintf_attr.c', 'platform/pc98/console_attr.c', 'tools/tests/dep_dir_d_console_host.c', 'tools/tests/fixtures/dep_dir_d/*', 'tools/tests/kernel_includes.py', 'tools/tests/kernel_boot_source.py', 'kernel/module/kdebug_imports.def', 'lib/sqlite3/os32_sqlite_vfs.c', 'tools/gen_kdebug_imports.py', 'tools/prototypes/sqlite_imports.def', 'tools/tests/test_gf_display.py', 'tools/tests/gf4_platform_host.c', 'hwdev/gf/*.c', 'hwdev/gf/*.h', 'hwdev/gf/display/*.c', 'hwdev/gf/display/*.h', 'hwdev/gf/op/*.c', 'kdebug/gfinit_pll.c', 'kdebug/gfinit_decode.c', 'drivers/video/geforce/edid.c', 'drivers/video/geforce/tv_data.c', 'drivers/video/geforce/gf_display_data.c', 'hwdev/gf_load_state.c', 'kdebug/gfinit_decode.h', 'kdebug/gfinit_decode/*.inc', 'kdebug/gfinit_op/*.inc', 'kdebug/gfinit/*.inc', 'kdebug/gfinit_plan_c/*.inc', 'kdebug/gfinit_pll/*.inc', 'kdebug/gfinit_resources/*.inc', 'kdebug/recover.c', 'kdebug/recover_common/*.inc', 'kdebug/recover_plan/*.inc', 'kdebug/recover_gf_stage/*.inc', 'kdebug/recover.h', 'kdebug/recover_gf.c', 'kdebug/recover_gf_expand.h', 'kdebug/recover_gf.h', 'tools/tests/gfinit_host.c', 'tools/tests/gfinit_fixture.h', 'tools/tests/test_gfinit.py', 'include/types.h', 'tools/tests/recover_gf_host.c', 'tools/tests/recover_gf_inventory.h', 'kdebug/gfinit_pll.h', 'include/memmap.h', 'kdebug/gfinit_exec.c', 'kdebug/gfinit_exec.h', 'tools/tests/gfinit_exec_host.c', 'tools/tests/test_gfinit_exec.py', 'tools/tests/gfinit_pll_host.c', 'tools/tests/test_gfinit_pll.py', 'kdebug/gfinit_bus.c', 'kdebug/gfinit_bus.h', 'kdebug/kdebug_window.c', 'kdebug/kdebug_window.h', 'tools/tests/gfinit_bus_host.c', 'tools/tests/test_gfinit_bus.py', 'tools/tests/recover_hostshim/io.h', 'kdebug/gfinit_resources.c', 'kdebug/gfinit_resources.h', 'drivers/pci/pci.h', 'drivers/pci/pci_decode.h', 'sdk/include/os32/os32_kapi_shared.h', 'include/kprintf.h', 'tools/tests/gfinit_resources_ra266.h', 'hwdev/owner.h', 'kdebug/gfinit.c', 'kdebug/gfinit.h', 'kdebug/gfinit_plan.h', 'kdebug/kpeek.h', 'mm/paging.h', 'mm/physmem.h', 'lib/kstring.h', 'arch/x86/kernel/idt.h', 'kernel/module/module.h', 'drivers/char/serial.h', 'include/bootinfo.h', 'include/sys.h', 'kdebug/generated/kdebug_import_macros.h', 'kdebug/generated/kdebug_imports.h', 'tools/tests/gfinit_owner_host.c', 'tools/tests/test_gfinit_owner.py', 'include/hwdev_private.h', 'kdebug/gfinit_wire.h', 'userland/shell/cmd_gfinit.c', 'userland/shell/shell.h', 'userland/shell/cmd_sys.c', 'build/programs.mk', 'tools/tests/gfinit_output_host.h', 'lib/kprintf.c', 'include/console_attr.h', 'kernel/module/kdebug_call.c', 'hwdev/hwdev_imports.def', 'kdebug/gfinit_plan.c', 'tools/tests/gfinit_plan_host.c', 'tools/tests/test_gfinit_plan.py', 'kdebug/recover_profile.c', 'tools/tests/gfinit_romfile_host.c', 'tools/gfinit_rom_decode.py', 'include/port_service.h', 'include/console_output.h', 'kernel/port_service.c', 'kdebug/recover_cleanup.h', 'kdebug/recover_diag.h', 'include/pci_dev_contract.h', 'kernel/console/console_backend.h', 'kdebug/gfinit_mode.c', 'kdebug/gfinit_plan_c.c', 'kdebug/gfinit_plan_c.h', 'kdebug/recover_gf_c.h', 'tools/tests/test_gfinit_plan_c.py', 'tools/tests/gfinit_plan_c_host.c', 'tools/tests/test_gfinit_mode.py', 'tools/tests/gfinit_mode_host.c', 'tools/tests/test_gfinit_budget.py', 'tools/tests/gfinit_budget_host.c', 'tools/rshell_recover.py', 'tools/lan_bridge.py', 'tools/host_image.py', 'tools/lanfs_host.py', 'tools/rshell_peek.py', 'tools/rshell_serial.py', 'tools/serialfs_host.py', 'kdebug/gf_stage_state.c', 'kdebug/gf_stage_state.h', 'hwdev/gf_load_state.h', 'tools/tests/test_gf_runner.py', 'tools/tests/gf_runner_host.c', 'hwdev/gf/runner.c', 'hwdev/gf/save.c', 'hwdev/gf/pll.c', 'hwdev/gf/scripts.h', 'hwdev/gf/display/calc.h', 'hwdev/gf/display/calc.c', 'hwdev/gf/display/timing.c', 'hwdev/gf/display/format.c', 'hwdev/gf/display/pitch.c', 'hwdev/gf/display/start.c', 'hwdev/gf/display/dac_format.c', 'hwdev/gf/display/output.c', 'hwdev/gf/display/cursor.c', 'hwdev/gf/display/fifo.c', 'hwdev/gf/display/extended.c', 'hwdev/gf/display/pll_program.c', 'hwdev/gf/display/state.h', 'hwdev/gf/quiesce_rows.h', 'tools/tests/gf_display_host.c', 'tools/tests/gf_display_palette_host.c', 'tools/tests/gf_display_blank_host.c', 'tools/tests/gf_display_modes_host.c', 'tools/tests/gfinit_c_only_host.c', 'tools/tests/gfinit_init_host.c', 'tools/tests/gfinit_s0_host.c', 'tools/tests/gfinit_stages_host.c', 'tools/tests/test_gfinit_stages.py', 'tools/tests/gf4_*_host.c', 'tools/tests/gf4_display_fake.h', 'tools/tests/gf_accel*.[ch]', 'drivers/video/geforce/*.c', 'drivers/video/geforce/*.h', 'build/hwdev.mk', 'kdebug/gf_display_test.c', 'kdebug/gf_display_test.h', 'kdebug/gf_command_wire.h', 'kernel/module/module_schema.h', 'hwdev/gf/rom.inc', 'tools/rshell_hwb.py'],
    needs=['make:all'], budget=60.0)

from kernel_includes import include_flags
from gf_ownerless_fixture import ownerless_body
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

def component(name, flags=(), mutation=None):
    with tempfile.TemporaryDirectory(prefix='gf-hal-') as td:
        exe=Path(td)/'host'
        sources = [ROOT/'tools/tests'/f'{name}_host.c']
        if name == 'backend_geforce' and mutation:
            original = (ROOT/'drivers/video/geforce/backend_geforce.c').read_text()
            old, new = mutation
            assert original.count(old) == 1
            original = original.replace(old, new, 1)
            backend = Path(td)/'backend.c'
            import re
            original = re.sub(r'^#include "([^"]+)"', lambda m: '#include "'+str((ROOT/'drivers/video/geforce'/m[1]).resolve())+'"' if m[1].startswith('../') else m[0], original, flags=re.M)
            backend.write_text(original)
            fixture = (ROOT/'tools/tests/backend_geforce_host.c').read_text().replace('../../drivers/video/geforce/backend_geforce.c', str(backend))
            fixture = fixture.replace('#include "gf_startlog_host.h"', '#include "'+str(ROOT/'tools/tests/gf_startlog_host.h')+'"')
            host = Path(td)/'fixture.c'
            host.write_text(fixture)
            sources = [host]
        if name == 'backend_geforce':
            sources.append(ROOT/'tools/tests/gf_accel_unbound_stubs.c')
        subprocess.run(FLAGS+INCLUDES+list(flags)+[*map(str, sources),'-o',str(exe)],check=True)
        result=host32.run([str(exe)],capture_output=True,text=True,timeout=60)
        assert result.returncode==(20 if mutation else 0),(name,result.returncode,result.stdout,result.stderr)
        if mutation: print('RED backend startlog runtime rc=20', flush=True)
        print('PASS '+name,flush=True)

def core(init_fail=0, unsafe=0, missing=0, gui_first=0, bar2_bytes=0x1000000, mutation=None, diagnostic=False, vram_bytes=0x8000000, vs_mask=0x700, no_vram_pt=False, fail_boot=False):
    fixture=(ROOT/'tools/tests/gfx_kernel_fb_host.c').read_text().replace('return id == 2 ? &slot : 0;', 'return id == 2 || id == APP_ID_SHELL ? &slot : 0;')
    fixture=fixture.replace('void gui_display_failed(void) {}', '')
    fixture=fixture.replace('void __cdecl kprintf(u8 a,const char *f,...) { (void)a;(void)f; host_kprintf_count++; }', r'''static int log_is(const char *a,const char *b)
{ while (*a && *a==*b) { a++;b++; } return *a==*b; }
static unsigned fallback_logs, reserve_fail_logs, reserve_stage_logs;
void __cdecl kprintf(u8 a,const char *f,...)
{
    (void)a; host_kprintf_count++;
    if (log_is(f,"[gfx] geforce boot_reserve failed\n")) reserve_fail_logs++;
    if (log_is(f,"[geforce] boot_reserve fail stage=%s\n")) {
        __builtin_va_list stage_args; __builtin_va_start(stage_args,f);
        const char *stage = __builtin_va_arg(stage_args,const char *);
        if (log_is(stage,"identify")) reserve_stage_logs++;
        __builtin_va_end(stage_args);
    }
    const char *expected = "[gfx] backend=%s init failed -> pc98\n";
    unsigned i = 0;
    while (f[i] && f[i] == expected[i]) i++;
    if (f[i] != expected[i]) return;
    __builtin_va_list args; __builtin_va_start(args,f);
    const char *name = __builtin_va_arg(args,const char *);
    const char *wanted = "geforce";
    i = 0; while (name[i] && name[i] == wanted[i]) i++;
    if (name[i] == wanted[i]) fallback_logs++;
    __builtin_va_end(args);
}''')

    fixture=fixture.replace('static void run(void)\n{',
        '#include "geforce_core_cases.inc"\nstatic void run(void)\n{',1)
    fixture=replace_function(fixture,'static void run(void)',r'''
    CHECK(gfx_geforce_pref(1)==GFX_PREF_PC98);
    CHECK(gfx_geforce_pref(0)==GFX_PREF_GEFORCE);
    CHECK(gfx_geforce_pref(2)==GFX_PREF_GEFORCE && gfx_geforce_pref(-1)==GFX_PREF_GEFORCE);
    map_host(0x10000,0xff0000); map_host(GF_TEST_DISPLAY,MEM_GFX_BB8_SIZE);
    map_host(GF_TEST_BAR0,GF_PRODUCTION_BAR0_BYTES);
    *(volatile u32 *)(GF_TEST_BAR0+GF_VRAM_SIZE_REG)=0;
    *(volatile u32 *)(GF_TEST_BAR0+GF_VS_REG)=0;
    boot();
    gfx_set_backend_pref(GFX_PREF_GEFORCE); gfx_boot_reserve();
    const struct ledger_surface *client=ledger_surface_find(LEDGER_SF_GEFORCE,LEDGER_ROLE_CLIENT);
    const struct ledger_surface *display=ledger_surface_find(LEDGER_SF_GEFORCE,LEDGER_ROLE_DISPLAY);
    CHECK(client && display && client->backing==LEDGER_SB_RAM && display->backing==LEDGER_SB_MMIO);
    CHECK(client->npages*PAGE_SIZE==307200 && display->npages*PAGE_SIZE==307200);
    CHECK(!ledger_surface_find(LEDGER_SF_PEGC,LEDGER_ROLE_CLIENT));
    CHECK(!ledger_surface_find(LEDGER_SF_PEGC,LEDGER_ROLE_DISPLAY));
    CHECK(!geforce_range_reserved(GF_TEST_BAR0,GF_PRODUCTION_BAR0_BYTES));
    CHECK(!geforce_range_reserved(GF_TEST_DISPLAY,GF_PRODUCTION_BAR1_BYTES));
    CHECK(!geforce_range_reserved(GF_TEST_BAR2,BAR2_BYTES));
    CHECK((paging_pte_raw(GF_TEST_BAR0)&(PTE_PRESENT|PTE_USER|PTE_PCD))==(PTE_PRESENT|PTE_PCD));
    CHECK((paging_pte_raw(GF_TEST_DISPLAY)&(PTE_PRESENT|PTE_USER|PTE_PCD))==(PTE_PRESENT|PTE_PCD));
    CHECK(!(paging_pte_raw(GF_TEST_BAR2)&PTE_PRESENT));
    CHECK(!(paging_pte_raw(GF_TEST_DISPLAY+MEM_GFX_BB8_SIZE)&PTE_PRESENT));
    /* Boot builds both instmem PTs, but leaves every aperture PTE closed. */
    const u32 instmem_bytes = BAR2_BYTES < 0x618000U ? BAR2_BYTES : 0x618000U;
    CHECK(GF_PRODUCTION_PT_PAGES==70);
    for (u32 off=0; off<instmem_bytes; off+=PAGE_SIZE) {
        CHECK(paging_registered_pt(GF_TEST_BAR2+off));
        CHECK(paging_pte_raw(GF_TEST_BAR2+off)==((GF_TEST_BAR2+off)|PTE_RW|PTE_PCD));
    }
    CHECK(!paging_pte_raw(GF_TEST_BAR2+instmem_bytes));
    /* A later live AS inherits the PTs and can reopen/close them without allocation. */
    u32 instmem_owner;
    CHECK(ledger_owner_new(LEDGER_KIND_AS,2,"gf-pt",&instmem_owner));
    CHECK(!paging_addrspace_create_lease(&space,instmem_owner));
    for (u32 off=0; off<instmem_bytes; off+=MEM_DEVICE_APERTURE_PDE_SIZE) {
        u32 va=GF_TEST_BAR2+off;
        u32 pde=((u32 *)P2V(space.pd_phys))[va/MEM_DEVICE_APERTURE_PDE_SIZE];
        CHECK((pde&PTE_FRAME_MASK)==paging_registered_pt(va) && (pde&PTE_PRESENT));
    }
    CHECK(!paging_map_phys(GF_TEST_BAR2,GF_TEST_BAR2,instmem_bytes/PAGE_SIZE,PAGE_RW|PTE_PCD));
    CHECK(!paging_set_not_present(GF_TEST_BAR2,GF_TEST_BAR2+instmem_bytes-1));
    CHECK(!geforce_range_reserved(GF_TEST_BAR2,BAR2_BYTES));
    paging_addrspace_destroy(&space);
    CHECK(ledger_owner_retire(instmem_owner));
    prepared_vram_test();
    CHECK(!acquires && !starts);
    if (!MISSING) geforce_backend_bind(&device_gf,identify_gf,0,point_gf,0);
    gfx_prepare_backend(); CHECK(!acquires && !starts);
    decode_safe=!UNSAFE; host_gui=GUI_FIRST;
    gfx_init();
    CHECK(fallback_logs == (unsigned)(INIT_FAIL && !UNSAFE));
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
    if fail_boot:
        fixture=replace_function(fixture,'static void run(void)',r"""
        map_host(0x10000,0xff0000);
        boot(); host_gf_identify_fail=1;
        gfx_set_backend_pref(GFX_PREF_GEFORCE); gfx_boot_reserve();
        CHECK(reserve_fail_logs==1 && reserve_stage_logs==1);
        CHECK(!geforce_platform_get()->resources);
        CHECK(!geforce_boot_reserve());
        CHECK(reserve_fail_logs==1 && reserve_stage_logs==2);
        SAY("PASS reserve failure rc and stage/caller logs"); die(0);
        """)
    if diagnostic:
        fixture=replace_function(fixture,'static void run(void)',r"""
        map_host(0x10000,0xff0000); map_host(GF_TEST_DISPLAY,MEM_GFX_BB8_SIZE);
        map_host(GF_TEST_BAR0,GF_PRODUCTION_BAR0_BYTES);
        *(volatile u32 *)(GF_TEST_BAR0+GF_VRAM_SIZE_REG)=0;
        *(volatile u32 *)(GF_TEST_BAR0+GF_VS_REG)=0;
        boot(); foundation_selection_test(); die(0);
        """)
    fixture=f'#define EXPECT_VRAM_PT {0 if no_vram_pt else 1}\n#define TEST_VRAM_BYTES {vram_bytes}U\n#define TEST_VS_MASK {vs_mask}U\n#define BAR2_BYTES {bar2_bytes}U\n#define GF_RESERVATION_TEST 1\n#define INIT_FAIL {init_fail}\n#define UNSAFE {unsafe}\n#define MISSING {missing}\n#define GUI_FIRST {gui_first}\nunsigned int host_arch_if=0x202U;\n'+fixture
    def stage(tmp,sources):
        cases=(ROOT/'tools/tests/geforce_core_cases.inc').read_text()
        # Match production BAR1/BAR2 below the private AS bands.
        cases=cases.replace('#define GF_TEST_DISPLAY 0xd0000000U', '#define GF_TEST_DISPLAY 0x30000000U')
        cases=cases.replace('#define GF_TEST_BAR2 0xe1000000U', '#define GF_TEST_BAR2 0x25000000U')
        cases=cases.replace('GF_TEST_BAR2,GF_TEST_BAR2,GF_PRODUCTION_BAR2_BYTES,1',
                            'GF_TEST_BAR2,GF_TEST_BAR2,BAR2_BYTES,1')
        platform=(ROOT/'drivers/video/geforce/geforce_platform.c').read_text()
        if no_vram_pt:
            start=platform.index("    u32 view_phys =")
            end=platform.index("    platform.bar0_phys =", start)
            platform=platform[:start]+platform[end:]
        if mutation and mutation != "reserve-drop":
            old,new={
                'cold-pfb': ('u32 display_end = PAGE_ALIGN_UP(GF_MODE_VRAM_BYTES);',
                    'u32 v=gf_vram_bytes(*(volatile u32 *)P2V_IO(boot_resources.bars[0].base+GF_VRAM_SIZE_REG));\n'
                    '    if (!v) return reserve_fail("pfb");\n'
                    '    u32 display_end = PAGE_ALIGN_UP(GF_MODE_VRAM_BYTES);'),
                'push-no-pt': ('view_bytes / PAGE_SIZE, PAGE_RW | PTE_PCD)', '(view_bytes - GF2_PUSH_BYTES) / PAGE_SIZE, PAGE_RW | PTE_PCD)'),
                'view-no-pt': ('view_bytes / PAGE_SIZE, PAGE_RW | PTE_PCD)', '0, PAGE_RW | PTE_PCD)'),
                'gui-boot-reserve': ('if (!gf_production_selected()) return reserve_fail("selection");', 'if (0) return reserve_fail("selection");'),
                'instmem-no-pt': ('instmem->base, instmem->base, bytes / PAGE_SIZE, PAGE_RW | PTE_PCD)', 'instmem->base, instmem->base, 0, PAGE_RW | PTE_PCD)'),
                'instmem-open': ('paging_set_not_present(instmem->base, instmem->base + bytes - 1)', '0'),
                'instmem-short': ('instmem->base, instmem->base, bytes / PAGE_SIZE, PAGE_RW | PTE_PCD)', 'instmem->base, instmem->base, bytes / PAGE_SIZE - 1, PAGE_RW | PTE_PCD)'),
                'instmem-overrun': ('instmem->length : GF_BOOT_INSTMEM_MAX_BYTES;', 'GF_BOOT_INSTMEM_MAX_BYTES : GF_BOOT_INSTMEM_MAX_BYTES;'),
                'instmem-wb': ('instmem->base, instmem->base, bytes / PAGE_SIZE, PAGE_RW | PTE_PCD)', 'instmem->base, instmem->base, bytes / PAGE_SIZE, PAGE_RW)'),
            }[mutation]
            assert platform.count(old)==1,(mutation,old)
            platform=platform.replace(old,new,1)
        # Preserve source-relative header lookup when staging the product code.
        platform=platform.replace('"../../../', '"'+str(ROOT)+'/')
        (tmp/'geforce_platform.inc').write_text(platform)
        cases=cases.replace('"../../drivers/video/geforce/geforce_platform.c"', '"geforce_platform.inc"')
        (tmp/'geforce_core_cases.inc').write_text(cases)
        gui=(ROOT/'kernel/gui.c').read_text()
        (tmp/'geforce_gui_notice.inc').write_text(gui[gui.index('void gui_display_failed(void)'):])
        source=(ROOT/'drivers/video/geforce/geforce_module.c').read_text()
        start=source.index('int geforce_boot_reserve(void)')
        end=source.index('static int geforce_return(',start)
        (tmp/'geforce_reserve.inc').write_text(source[start:end])
        for rel in ('drivers/video/geforce/backend_geforce.c','tools/tests/gf_accel_unbound_stubs.c','hwdev/gf/service.c','hwdev/gf/accel_surface.c','hwdev/gf/display/modes.c','hwdev/gf/instmem_map.c','hwdev/gf/command_platform.c','hwdev/gf/vram_view.c'):
            sources.append(ROOT/rel)
        # Transfer is tested separately; this fixture tests the HAL's boundary.
        transfer=tmp/'transfer.c'
        transfer.write_text('''#include "types.h"
int gf_present_rect(const u8 *c,u32 cp,volatile u8 *d,u32 dp,volatile u8 *b,int x,int y,int w,int h,int (*p)(void *),void *v)
{(void)c;(void)cp;(void)d;(void)dp;(void)b;(void)x;(void)y;(void)w;(void)h;(void)p;(void)v;return 0;}
''')
        sources.append(transfer)
    caller_mutation = ('reserve-drop',
        'if (!geforce_boot_reserve())\n            kprintf(KLOG_ATTR, "[gfx] geforce boot_reserve failed\\n");',
        '(void)geforce_boot_reserve();', '') if mutation == 'reserve-drop' else None
    result=run_case(caller_mutation,'qemu',fixture_body=fixture,stage=stage,extra_changes=[
        ('gfx/gfx_core.c','static void _gfx_common_init(int plane_sz)\n{',
         'static void _gfx_common_init(int plane_sz)\n{\n    extern int host_planar_inits; host_planar_inits++;')])
    assert result.returncode==(1 if mutation else 0),(init_fail,unsafe,missing,mutation,result.returncode,result.stdout,result.stderr)
    if mutation:
        assert 'FAIL ' in result.stdout,result.stdout
        print('RED '+mutation+' '+result.stdout.strip(),flush=True)
    else:
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
    from module_schema import POLICIES
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
    # The generator input owns the import inventory.
    declared=json.loads((ROOT/'tools/prototypes/geforce_imports.def').read_text())['imports']
    assert [e['name'] for e in meta['imports']]==[e['symbol'] for e in declared]
    resident=subprocess.check_output(['nm','--defined-only',str(ROOT/'build/out/kernel.elf')],text=True)
    assert ' geforce_tick_bind' in resident and ' t geforce_tick\n' in resident
    assert ' t geforce_tick\n' not in subprocess.check_output(
        ['nm','--defined-only',str(ROOT/'build/out/geforce/geforce.elf')],text=True)
    assert 'geforce_tick' not in {e['name'] for e in meta['imports']}
    assert 'tick_register' in {e['name'] for e in meta['imports']}
    symbols=mkmod.Elf(body).symbols
    assert 'gf_accept_transfer' not in symbols and 'samples' not in symbols
    for symbol in ('gf_video_ops','gf_init_run','gf_display_modeset','geforce_backend_ops','gf_production_prepare',
                   'gf_production_identify','gf_production_quiesce','gf_production_decode_safe'):
        assert symbol in symbols,symbol
    for symbol in ('recover_run','gfinit_decode_rom','gfinit_plan_c','gfinit_begin'):
        assert symbol not in symbols,symbol
    with tempfile.TemporaryDirectory(prefix='gf-image-') as td:
        exe=build_validator(Path(td))
        result=validate(exe,data,runner='qemu',relocate_base=0x1200000,
            kapi=spec['version'],layout=spec['generations']['memory_layout'],
            abi=meta['module_abi_gen'],pair=mkmod.pair_crc(kernel),ops=POLICIES[12]['ops_version'],img_valid=1,module_id=12,
            imports=[(fnv(e['name']),e['type_ver'],e['kind']) for e in meta['imports']],
            exports=[(1,fnv('initialize'),1)])
        assert result.returncode==0,result
        old=validate(exe,data,runner='qemu',relocate_base=0x1200000,
            kapi=spec['version'],layout=spec['generations']['memory_layout'],
            abi=meta['module_abi_gen'],pair=mkmod.pair_crc(kernel),ops=POLICIES[12]['ops_version']-1,img_valid=1,module_id=12,
            imports=[(fnv(e['name']),e['type_ver'],e['kind']) for e in meta['imports']],
            exports=[(1,fnv('initialize'),1)])
        assert old.returncode!=0,old
    print('PASS paired GF product: P2/P3 symbols, independent of KDEBUG, relocation',flush=True)

def module_registration(mutation=None):
    import struct
    from test_module_runtime import FLAGS as runtime_flags, INCLUDES as runtime_includes, image
    with tempfile.TemporaryDirectory(prefix='gf-register-') as td:
        exe=Path(td)/'host'
        fixture=ROOT/'tools/tests/geforce_module_host.c'
        if mutation:
            source=ROOT/'drivers/video/geforce/geforce_module.c'
            text=source.read_text()
            old,new={
                'getter-registration': ('return registered ? accel_slot : 0;', 'return accel_slot;'),
                'start-slot-clear': ('failed:\n    accel_slot=0;', 'failed:\n    (void)accel_slot;'),
                'detach-slot-clear': ('/* Close admission even when the module must retain its resources. */\n    accel_slot=0;', '/* Close admission even when the module must retain its resources. */\n    (void)accel_slot;'),
                'abort-slot-clear': ('int geforce_module_abort(void)\n{\n    accel_slot=0;', 'int geforce_module_abort(void)\n{\n    (void)accel_slot;'),
                'fill-text-check': ('candidate.fill_rect && !module_text_contains', 'false && !module_text_contains'),
                'blit-text-check': ('candidate.blit && !module_text_contains', 'false && !module_text_contains'),
                'detach-irq': ('owner->foundation.registered ||', 'false ||'),
                'detach-tick': ('if (geforce_tick_bind(0,0)) {', 'if (false) {'),
                'abort-slot': ('if (registered) return MODULE_ISOLATED;', 'foundation_slot=0;accel_slot=0;'),
            }[mutation]
            assert text.count(old)==1, mutation
            import re
            # Preserve normal include search for generated imports and shared headers.
            text=re.sub(r'#include "([^"\n]+)"',
                lambda m: '#include "'+str((source.parent/m[1]).resolve())+'"'
                if (source.parent/m[1]).exists() else m[0],text.replace(old,new,1))
            (Path(td)/'module.c').write_text(text)
            fixture=Path(td)/'fixture.c'
            fixture.write_text((ROOT/'tools/tests/geforce_module_host.c').read_text().replace(
                '../../drivers/video/geforce/geforce_module.c',str(Path(td)/'module.c')).replace(
                '../../drivers/video/geforce/geforce_body.c',str(ROOT/'drivers/video/geforce/geforce_body.c')))
        from test_gf_display import gf4_sources
        flags = ['gcc', *runtime_flags, '-Wno-unused-parameter', *runtime_includes,
                 '-I'+str(ROOT), '-Igfx', '-Ibuild/out/geforce',
                 '-I'+str(ROOT/'tools/tests'), '-DGF_PRODUCTION_MODULE']
        real_backend=Path(td)/'real_backend.o'
        aliases=('geforce_backend_ops','geforce_backend_bind',
                 'geforce_backend_wc_bind','geforce_backend_stop_bind')
        subprocess.run([*flags, '-DOS32_GEFORCE_MODULE',
            *['-D'+name+'=loader_real_'+name for name in aliases],
            '-c', str(ROOT/'drivers/video/geforce/backend_geforce.c'),
            '-o', str(real_backend)], cwd=ROOT, check=True)
        sources=gf4_sources(ROOT, 'c3', ROOT/'tools/tests/geforce_module_host.c')
        subprocess.run([*flags, str(fixture), *[str(ROOT/f) for f in sources],
            str(real_backend), '-nostdlib','-static','-no-pie',
            '-Wl,--gc-sections','-o',str(exe)],cwd=ROOT,check=True)
        for mode in range(3):
            result=host32.run([str(exe)],input=struct.pack('<II',0,mode)+image(),
                              capture_output=True,timeout=60)
            if mutation:
                assert result.returncode and b'FAIL' in result.stdout, (mutation,mode,result)
                print('MUTATE runtime RED: '+mutation,flush=True)
                return
            assert result.returncode==0,(mode,result.returncode,result.stdout,result.stderr)
    print('PASS GF registration: missing stub, layer 3 hook validation, prepare required, lifetime pin',flush=True)

def present_trace(mutation=None, byte=False):
    source=(ROOT/'drivers/video/geforce/gf_present.c').read_text()
    mutations={
        'bounds': ('while (n >= 4)', 'while (n > 0)'),
        'point': ('rc = point(opaque);', 'rc = row == 0 ? point(opaque) : 0;'),
    }
    if mutation:
        old,new=mutations[mutation]
        assert source.count(old)==1
        source=source.replace(old,new,1)
    drain='if (!rc) (void)*(volatile u32 *)(bar0 + GF_PRESENT_PFB_AMOUNT);'
    assert source.count(drain)==1
    source=source.replace(drain, 'if (!rc) { trace_drain(); (void)*(volatile u32 *)(bar0 + GF_PRESENT_PFB_AMOUNT); }')
    harness=r'''
#include "gfx_hal.h"
#include "hwdev/gfinit_mode.h"
GfxCounters gfx_counters;
static void trace_drain(void);
#include "present.c"
#define SIZE 4096
static u8 src[SIZE], dst[SIZE], expected[SIZE];
static volatile u8 *start;
static int pitch, left, top, width, height, errors, calls, fail, drains;
static u32 regs[(GF_PRESENT_PFB_AMOUNT / 4) + 1];
static void trace_drain(void) { if (calls != height || drains) errors++; drains++; }
static int point(void *p) { (void)p; calls++; return calls == fail ? -911 : 0; }
static void run(int sa, int da, int w, int clip, int failrow) {
    int sp = 653, dp = 657, x = clip ? -2 : (w == 640 ? 0 : 1), y = 1, h = 3;
    int marker = 0;
    for (int i=0; i<SIZE; i++) { src[i]=(u8)(i*17+3); dst[i]=expected[i]=0xa5; }
    start=dst+da; pitch=dp; left=clip ? 0 : x; top=y;
    width=clip ? w-2 : w; height=failrow ? failrow-1 : h;
    if (width < 0) width=0;
    calls=0; drains=0; fail=failrow; gfx_counters=(GfxCounters){0};
    for (int r=0; r<height; r++) for (int c=0; c<width; c++)
        expected[da+(y+r)*dp+left+c]=src[sa+(y+r)*sp+left+c];
    int rc=gf_present_rect(src+sa,sp,dst+da,dp,(volatile u8 *)regs,x,y,w,h,point,&marker);
    if (rc != (failrow ? -911 : 0) || calls != (failrow ? failrow : h) ||
        gfx_counters.present_bytes != (u32)(width*height) || gfx_counters.commits != (u32)!failrow || drains != !failrow) errors++;
    for (int i=0; i<SIZE; i++) if (dst[i]!=expected[i]) errors++;
}
void _start(void) {
    int widths[]={1,2,3,4,5,6,7,8,9,639,640};
    for (int a=0; a<4; a++) for (int b=0; b<4; b++)
        for (int i=0; i<11; i++) run(a,b,widths[i],0,0);
    for (int i=3; i<11; i++) run(i%4,(i+1)%4,widths[i],1,0);
    for (int i=1; i<=3; i++) run(1,2,9,0,i);
    __asm__ volatile("int $0x80" : : "a"(1), "b"(errors ? 1 : 0) : "memory");
    for (;;) {}
}
'''
    with tempfile.TemporaryDirectory(prefix='gf-trace-') as td:
        tmp=Path(td)
        (tmp/'present.c').write_text(source.replace('#include "../../../gfx/gfx_internal.h"', '#include "gfx/gfx_internal.h"'))
        (tmp/'host.c').write_text(harness)
        exe=tmp/'host'
        subprocess.run(FLAGS+(['-DGF_PRESENT_BYTE'] if byte else [])+['-I'+td]+INCLUDES+[str(tmp/'host.c'),'-o',str(exe)],check=True)
        result=host32.run([str(exe)],capture_output=True,text=True,timeout=60)
        assert result.returncode == (1 if mutation else 0), (mutation,result.returncode,result.stderr)
        print(('RED '+mutation if mutation else 'PASS present byte equivalence and bounds'),flush=True)

def present_store_width(source, mutation=None, byte=False):
    """Count executed stores into the display via QEMU instruction/register traces."""
    import re
    harness = r'''
#include "gfx_hal.h"
GfxCounters gfx_counters;
static u8 source[1400], display[1400] __attribute__((aligned(4)));
static const int widths[]={1,3,4,5,9,17,639,640};
static u32 registers[0x10020c / 4 + 1];
int gf_present_rect(const u8 *, u32, volatile u8 *, u32, volatile u8 *,
                    int, int, int, int, int (*)(void *), void *);
static int point(void *p) { (void)p; return 0; }
void _start(void) {
    int rc=0;
    for (int a=0; a<4; a++) for (int i=0; i<8; i++)
        rc |= gf_present_rect(source,649,display+8+a,645,(volatile u8 *)registers,
                              0,0,widths[i],2,point,0);
    __asm__ volatile("int $0x80" : : "a"(1), "b"(rc) : "memory");
    for (;;) {}
}
'''
    with tempfile.TemporaryDirectory(prefix='gf-store-') as td:
        tmp=Path(td); exe=tmp/'host'; log=tmp/'qemu.log'
        (tmp/'host.c').write_text(harness)
        # Include lookup preserves the product file byte-for-byte, including relative includes.
        if mutation:
            body=source.read_text()
            old,new={
                'alignment': ('while (n && ((uptr)d & 3)) { *d++ = *s++; --n; }', ''),
                'tail': ('while (n) { *d++ = *s++; --n; }', ''),
                'byte': ('#ifdef GF_PRESENT_BYTE', '#if 0'),
            }[mutation]
            assert body.count(old)==1, mutation
            modified=tmp/'present.c'
            modified.write_text(body.replace(old, new, 1))
            source=modified
        subprocess.run(FLAGS+(['-DGF_PRESENT_BYTE'] if byte else [])+INCLUDES+['-I'+str(ROOT/'drivers/video/geforce'),
            str(tmp/'host.c'),str(source),'-o',str(exe)],check=True)
        symbols=subprocess.check_output(['nm','-n',str(exe)],text=True)
        display=int(re.search(r'^([0-9a-f]+) [bB] display$',symbols,re.M).group(1),16)
        asm=subprocess.check_output(['objdump','-d','-Mintel','--no-show-raw-insn',str(exe)],text=True)
        instructions={}
        for line in asm.splitlines():
            m=re.match(r'^\s*([0-9a-f]+):\s+([a-z][a-z0-9]*)(?:\s+(.*))?$',line)
            if m: instructions[int(m[1],16)]=(m[2],m[3] or '')
        result=subprocess.run(['qemu-i386','-one-insn-per-tb','-d','exec,cpu,nochain',
            '-D',str(log),str(exe)],capture_output=True,text=True,timeout=60)
        assert result.returncode==0,('fixture execution',result.returncode,result.stderr)
        stores=[]
        pattern=r'Trace \d+: [^\n]*\nEAX=([0-9a-f]+) EBX=([0-9a-f]+) ECX=([0-9a-f]+) EDX=([0-9a-f]+)\nESI=([0-9a-f]+) EDI=([0-9a-f]+) EBP=([0-9a-f]+) ESP=([0-9a-f]+)\nEIP=([0-9a-f]+)'
        for m in re.finditer(pattern,log.read_text()):
            regs=dict(zip(('eax','ebx','ecx','edx','esi','edi','ebp','esp'),
                          (int(v,16) for v in m.groups()[:8])))
            op,args=instructions[int(m[9],16)]
            dest=args.split(',',1)[0].strip()
            if op in ('cmp','test','lea','push','call','jmp'): continue
            mem=re.fullmatch(r'(BYTE|WORD|DWORD) PTR (?:\[([^]]+)\]|ds:(0x[0-9a-f]+))',dest)
            if not mem:
                assert 'PTR' not in dest, (op,dest)
                continue
            # Objdump's i386 effective addresses consist only of registers,
            # integer constants and +, - or *; reject anything else.
            expr=mem[2] or mem[3]
            for reg,value in regs.items(): expr=re.sub(r'\b'+reg+r'\b',str(value),expr)
            assert re.fullmatch(r'[0-9xa-f+* -]+',expr),expr
            address=eval(expr,{'__builtins__':{}},{}) & 0xffffffff
            size={'BYTE':1,'WORD':2,'DWORD':4}[mem[1]]
            if address < display+1400 and address+size > display:
                stores.append((address-display,size))
        expected=[]
        for alignment in range(4):
            for width in (1,3,4,5,9,17,639,640):
                for row in range(2):
                    offset=8+alignment+row*645
                    remaining=width
                    while remaining and (byte or (display+offset)%4):
                        expected.append((offset,1)); offset+=1; remaining-=1
                    while remaining>=4:
                        expected.append((offset,4)); offset+=4; remaining-=4
                    while remaining:
                        expected.append((offset,1)); offset+=1; remaining-=1
        if stores != expected:
            mismatch=next((i for i,(a,b) in enumerate(zip(stores,expected)) if a!=b),
                          min(len(stores),len(expected)))
            detail=(mismatch, stores[mismatch:mismatch+4], expected[mismatch:mismatch+4],
                    len(stores),len(expected))
            print(f"present store mismatch: {detail}",flush=True)
            assert mutation, ('runtime store width/address',detail)
            print('RED '+mutation+' (executed store trace)',flush=True)
        else:
            assert not mutation, ('mutation survived',mutation)
            print(f'PASS present stores: {"byte" if byte else "dword"}, 32 rectangles, two rows, {len(stores)} stores',flush=True)


def wc_lifecycle(mutation=None):
    source=(ROOT/'drivers/video/geforce/gf_wc.c').read_text()
    body=(ROOT/'drivers/video/geforce/geforce_body.c').read_text()
    backend=(ROOT/'drivers/video/geforce/backend_geforce.c').read_text()
    targets={
        'wc-stop': ('body', 'int rc = p && p->wc_stop ? p->wc_stop() : 0;', 'int rc = 0; (void)p;'),
        'wc-repeat': ('source', 'if (lease.active) return wc_result;', 'if (0) return wc_result;'),
        'wc-start': ('backend', 'rc = start_wc ? start_wc() : 0;', 'rc = 0;'),
        'wc-alias': ('source', 'no_alias(base) && bar0_uc', '(1) && bar0_uc'),
    }
    if mutation:
        name,old,new=targets[mutation]
        values=dict(source=source,body=body,backend=backend)
        assert values[name].count(old)==1,(mutation,old)
        values[name]=values[name].replace(old,new,1)
        source,body,backend=(values[n] for n in ('source','body','backend'))
    a=source.index('/* Read-only CPU primitives begin.')
    b=source.index('/* Read-only CPU primitives end. */')
    source=source[:a]+source[b:]
    with tempfile.TemporaryDirectory(prefix='gf-wc-') as td:
        tmp=Path(td);exe=tmp/'host'
        (tmp/'gf_wc_source.inc').write_text(source)
        # Preserve product relative includes in the staged fixture/body.
        (tmp/'geforce_body.inc').write_text(ownerless_body(body, bind_f3=False))
        (tmp/'backend.inc').write_text(backend.replace('"../../../gfx/', '"gfx/'))
        harness='#include "'+str(ROOT/'tools/tests/gf_startlog_host.h')+'"\n'+(ROOT/'tools/tests/gf_wc_host.c').read_text()
        harness=harness.replace('"../../drivers/video/geforce/geforce_body.c"','"geforce_body.inc"')
        harness=harness.replace('"../../drivers/video/geforce/backend_geforce.c"','"backend.inc"')
        (tmp/'host.c').write_text(harness)
        subprocess.run(FLAGS+INCLUDES+['-I'+str(ROOT),'-I'+td,'-Wno-unused-function',str(tmp/'host.c'),'-o',str(exe)],check=True)
        result=host32.run([str(exe)],capture_output=True,text=True,timeout=60)
        assert result.returncode==(1 if mutation else 0),(mutation,result.returncode,result.stdout,result.stderr)
        print(('RED '+mutation+' '+result.stdout.strip() if mutation else 'PASS WC lifetime: first/reentry/same init/reconfigure/abort/failures/double stop, owned/BIOS, unchanged PTE/ledger'),flush=True)

def wc_recovery(path, bios=0, old=False, mutation=None):
    """Drive real gfx init and exec_fault_recover, rather than plat.stop()."""
    import re
    from exec_source import exec_source
    def function(source,name):
        start=re.search(r'^(?:static )?(?:void|int) '+name+r'\([^;{}]*\)\n\{',source,re.M)
        assert start,name
        return source[start.start():source.index('\n}',start.end())+2]
    fixture=(ROOT/'tools/tests/gfx_kernel_fb_host.c').read_text()
    fixture=fixture.replace('int appslot_cur(void) { return 2; }',
        'static int host_cur=2,host_owner=2; int appslot_cur(void) { return host_cur; }')
    fixture=fixture.replace('int res_owner_get(void) { return 2; }',
        'int res_owner_get(void) { return host_owner; }')
    fixture=fixture.replace('return id == 2 ? &slot : 0;',
        'return id > 0 && id < APP_SLOT_COUNT ? &host_slots[id] : 0;')
    fixture=fixture.replace('void gui_display_failed(void) {}', '')
    fixture=fixture.replace('static void run(void)\n{',
        '#include "geforce_recovery_cases.inc"\nstatic void run(void)\n{',1)
    fixture=replace_function(fixture,'static void run(void)','    wc_recovery_run();')
    fixture=f'#define GF_RESERVATION_TEST 1\n#define INIT_FAIL 0\n#define BIOS_WC {bios}\n#define RECOVERY_PATH {path}\nunsigned int host_arch_if=0x202U;\n'+fixture
    def read(rel):
        if old and rel in ('drivers/video/geforce/backend_geforce.c','kernel/gui.c'):
            return subprocess.check_output(['git','show','cead3085:'+rel],cwd=ROOT,text=True)
        value=(ROOT/rel).read_text()
        if mutation and rel==mutation[0]:
            _,anchor,replacement=mutation
            assert value.count(anchor)==1,(rel,anchor,value.count(anchor))
            value=value.replace(anchor,replacement,1)
        return value
    def stage(tmp,sources):
        gui=read('kernel/gui.c')
        (tmp/'geforce_gui_notice.inc').write_text(gui[gui.index('void gui_display_failed(void)'):])
        source=(ROOT/'drivers/video/geforce/geforce_module.c').read_text()
        start=source.index('int geforce_boot_reserve(void)')
        (tmp/'geforce_reserve.inc').write_text(source[start:source.index('static int geforce_return(',start)])
        cases=(ROOT/'tools/tests/geforce_core_cases.inc').read_text()
        cases=cases.replace('int geforce_wc_start(void) { return 0; }\nint geforce_wc_stop(void) { return 0; }\nvoid geforce_wc_report(const char *s) { (void)s; }','')
        cases += '\n#include "gf_wc_recovery_host.inc"\n'
        (tmp/'geforce_recovery_cases.inc').write_text(cases)
        body=(ROOT/'drivers/video/geforce/geforce_body.c').read_text()
        (tmp/'geforce_body.inc').write_text(ownerless_body(body, bind_f3=False))
        wc=(ROOT/'drivers/video/geforce/gf_wc.c').read_text()
        (tmp/'gf_wc_stop.inc').write_text(function(wc,'geforce_wc_stop'))
        exc=exec_source(ROOT)
        constants='\n'.join(l for l in exc.splitlines() if l.startswith(
            ('#define EXEC_LJ_', 'static volatile int g_pending_', 'static volatile int g_longjmp_')))
        names=('exec_notify_owned','exec_reclaim_owned','exec_finish','exec_pending_transfer',
               'exec_pending_finish','exec_fault_recover')
        (tmp/'gf_wc_fault_path.inc').write_text(function(gui,'gui_owner_exit')+'\n'+constants+'\n'+
            '\n'.join(function(exc,n) for n in names))
        backend=tmp/'backend.c'
        backend.write_text(read('drivers/video/geforce/backend_geforce.c').replace('"../../../gfx/', '"gfx/'))
        sources.extend([backend, ROOT/'tools/tests/gf_accel_unbound_stubs.c', ROOT/'hwdev/gf/service.c', ROOT/'hwdev/gf/accel_surface.c', ROOT/'hwdev/gf/display/modes.c'])
        asm=tmp/'setjmp.o'
        subprocess.run(['nasm','-f','elf32',str(ROOT/'arch/x86/kernel/setjmp.asm'),'-o',str(asm)],cwd=ROOT,check=True)
        sources.append(asm)
        transfer=tmp/'transfer.c'
        transfer.write_text('int gf_present_rect(void *a, unsigned b, void *c, unsigned d, void *e, int x, int y, int w, int h, void *p, void *o) { (void)a;(void)b;(void)c;(void)d;(void)e;(void)x;(void)y;(void)w;(void)h;(void)p;(void)o;return 0;}')
        sources.append(transfer)
    result=run_case(None,'qemu',fixture_body=fixture,stage=stage)
    expected=1 if old or mutation else 0
    assert result.returncode==expected,(path,bios,old,mutation,result.returncode,result.stdout,result.stderr)
    if expected:
        assert 'FAIL !lease.active && wc_effective==bios_wc' in result.stdout,(path,result.stdout)
        print(f'RED WC recovery path={path} BIOS={bios} '+result.stdout.strip(),flush=True)
    else: print(result.stdout,end='',flush=True)


def main():
    import sys
    if '--present-source' in sys.argv:
        present_store_width(Path(sys.argv[sys.argv.index('--present-source')+1]))
        return 0
    for path in range(3):
        for bios in range(2): wc_recovery(path,bios)
    wc_lifecycle()
    present_store_width(ROOT/'drivers/video/geforce/gf_present.c')
    present_trace()
    present_store_width(ROOT/'drivers/video/geforce/gf_present.c', byte=True)
    present_trace(byte=True)
    if '--mutate' in sys.argv:
        component('backend_geforce', ('-DOS32_GEFORCE_MODULE',), ('rc, stage, last, state, epoch, seq, ch_rc);', 'rc, stage, last, state, epoch, seq, 0); (void)ch_rc;'))
        for mutation in ('detach-irq', 'detach-tick', 'abort-slot', 'getter-registration', 'start-slot-clear', 'detach-slot-clear', 'abort-slot-clear', 'fill-text-check', 'blit-text-check'): module_registration(mutation)
        for mutation in ('instmem-no-pt','instmem-open','instmem-short','instmem-wb','push-no-pt','view-no-pt'):
            core(mutation=mutation)
        core(mutation='cold-pfb')
        core(fail_boot=True,mutation='reserve-drop')
        core(bar2_bytes=0x400000,mutation='instmem-overrun')
        core(diagnostic=True,mutation='gui-boot-reserve')
        wc_recovery(0,mutation=('drivers/video/geforce/backend_geforce.c', '    if (!ok && active) geforce_fail();', '    (void)ok;'))
        wc_recovery(2,mutation=('kernel/gui.c', '        if (kind != EXEC_KIND_EXITED) gfx_shutdown();', '        (void)kind;'))
        for mutation in ('wc-stop','wc-repeat','wc-start','wc-alias'): wc_lifecycle(mutation)
        for mutation in ('bounds', 'point'): present_trace(mutation)
        for mutation in ('alignment', 'tail'):
            present_store_width(ROOT/'drivers/video/geforce/gf_present.c', mutation)
        present_store_width(ROOT/'drivers/video/geforce/gf_present.c', 'byte', byte=True)
    unsafe_console()
    product()
    module_registration()
    for name in ('gf_present','backend_geforce','pc98_route'): component(name)
    component('backend_geforce', ('-DOS32_GEFORCE_MODULE',))
    component('backend_geforce', ('-DGF_ACCEPT_MODE=0',))
    for args in ((0,0,0,0),(0,0,0,1),(1,0,0,0),(1,1,0,0),(0,0,1,0)): core(*args)
    core(fail_boot=True)
    core(no_vram_pt=True)
    core(bar2_bytes=0x400000)
    core(vram_bytes=0x10000000, vs_mask=0)
    core(vram_bytes=0x10000000, vs_mask=0xff00)
    core(diagnostic=True)
    return 0
if __name__=='__main__': raise SystemExit(main())


def run(ctx) -> int:
    import os
    import shlex
    import subprocess
    base_env = dict(os.environ, HOST32_RUNNERS=' '.join(ctx.runners))
    env = dict(base_env)
    argv = ['python3', '-B', 'tools/tests/test_geforce.py']
    if ctx.mutate: argv.append('--mutate')
    rc = subprocess.call(argv, cwd=ctx.root, env=env, close_fds=False)
    if rc:
        return rc
    return 0
