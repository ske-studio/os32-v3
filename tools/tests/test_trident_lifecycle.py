#!/usr/bin/env python3
"""Reference ScreenInit/EnterVT, ResetVideo, blank/viewport and DPMS traces."""
CHECK = dict(name='trident-lifecycle', tier=1, needs=[], budget=8.8,
    covers=['include/copy_units.h', 'arch/*/arch_copy.h', 'gfx/sync/sync_int.h', 'gfx/v2/gfx2_int.h', 'include/gfx_sync.h', 'include/gfx_hal2.h', 'sdk/include/os32/os32_gfx2.h', 'tools/tests/concrete_probe_broker.c', 'tools/tests/concrete_probe_broker.h', 'kernel/module/debug_probe.c', 'lib/kstring_c.c', 'lib/kstring.h', 'include/debug_probe.h', 'kernel/module/module_runtime.h', 'kernel/module/module_image.h', 'drivers/video/trident/probes.h', 'drivers/video/trident/accel.c', 'drivers/video/trident/cursor.c', 'include/sys.h', 'drivers/video/trident/screen.c', 'drivers/video/trident/video.c', 'drivers/video/trident/dpms.c', 'drivers/video/trident/lifecycle.c', 'drivers/video/trident/backend_trident.c', 'drivers/video/trident/trident.h', 'drivers/video/trident/trident_module.h', 'drivers/video/trident/regs.c', 'drivers/video/trident/probe.c', 'drivers/video/trident/state_save.c', 'drivers/video/trident/pc98_init.c', 'drivers/video/trident/mode_640.c', 'drivers/video/trident/route_ra266.c', 'drivers/video/trident/vram.c', 'drivers/video/trident/default_dac.c', 'drivers/video/trident/state_restore.c', 'drivers/video/trident/present.c', 'drivers/video/trident/palette.c', 'tools/tests/trident_lifecycle_host.c', 'tools/tests/trident_device_fake.c','tools/tests/trident_sync_fake.c', 'tools/tests/trident_device_fake.h', 'tools/tests/trident_pc98init_host.c', 'tools/tests/host32.py', 'tools/tests/kernel_includes.py', 'build/config.mk', 'include/types.h', 'include/memmap.h', 'include/io.h', 'include/pc98.h', 'include/gfx_hal.h', 'include/gfx_mode.h', 'include/gfx_format.h', 'include/pegc.h', 'gfx/gfx.h', 'gfx/palette.h', 'gfx/gfx_internal.h', 'mm/kmalloc.h', 'lib/serial_diag.h', 'userland/tests/hw/trident_mode.hwb', 'drivers/char/serial.h', 'sdk/include/os32/os32_kapi_shared.h', 'sdk/include/os32/os32_generations.h'])
import argparse
import os
from pathlib import Path
import subprocess
import tempfile
import host32
from kernel_includes import include_flags
ROOT=Path(__file__).resolve().parents[2]
DIR='drivers/video/trident/'
SOURCES=('lifecycle','regs','probe','state_save','pc98_init','mode_640','route_ra266','vram','default_dac','state_restore','present','palette','screen','video','dpms','accel','cursor')
WRAPS=('identify','capacity_plan','state_save','pc98_init','route_enable','adjust_frame','video_reset','screen_blank','state_restore','route_disable','regs_bind')
MUTATIONS=[
 ('init-bind-failure','backend_trident','if (rc) { finish(TRI_STEP_CLIENT, rc, true); return; }','if (rc) { display_rc = 0; return; }'),
 ('shutdown-unregister','backend_trident','    gfx2_adapter_unregister(trident_context->backend);','    (void)trident_context;'),
 ('probe-hit','lifecycle','    PROBE_HIT(screeninit_post);','    (void)screeninit_post;'),
 ('probe-table','lifecycle','"tri.screeninit.post"','"tri.screeninit.wrong"'),
 ('close-before-init','backend_trident','if (device.active) {','if (false) {'),
 ('prepare-retry','backend_trident','    device.failed_step = 0;\n    device.last_rc = 0;','    if (device.last_rc) return device.last_rc;'),
 ('protect-inactive','regs','if (!d->active) return 0;', 'if (d->active) return 0;'),
 ('wait-twice','video','frame < 2','frame < 1'),
 ('overlay-stop','video','0x8E, 0x84','0x8E, 0'),
 ('overlay-dac','video','0x57, 0xC0','0x57, 0'),
 ('overlay-control','video','0xBE, 0x26','0xBE, 0'),
 *[(f'key-{i:02x}','video',old,new) for i,old,new in [
   (0x50, 'video_out(d, VIDEO_KEY_BASE, (u8)d->video.color_key)', 'video_out(d, VIDEO_KEY_BASE, 1)'),
   (0x51, 'video_out(d, VIDEO_KEY_BASE + 1, depth == 8 ? 0 : (u8)(d->video.color_key >> 8))', 'video_out(d, VIDEO_KEY_BASE + 1, 1)'),
   (0x52, 'video_out(d, VIDEO_KEY_BASE + 2, depth == 24 ? (u8)(d->video.color_key >> 16) : 0)', 'video_out(d, VIDEO_KEY_BASE + 2, 1)'),
   (0x54, 'video_out(d, VIDEO_KEY_BASE + 4, 0xFF)', 'video_out(d, VIDEO_KEY_BASE + 4, 1)'),
   (0x55, 'video_out(d, VIDEO_KEY_BASE + 5, depth == 8 ? 0 : 0xFF)', 'video_out(d, VIDEO_KEY_BASE + 5, 1)'),
   (0x56, 'video_out(d, VIDEO_KEY_BASE + 6, depth == 24 ? 0xFF : 0)', 'video_out(d, VIDEO_KEY_BASE + 6, 1)'),
 ]],
 ('blank','screen','blank ? scrn | 0x20','blank ? scrn | 0x00'),
 ('viewport-low','screen','base, 0x0C, 0','base, 0x0C, 1'),
 ('viewport-high','screen','temp & 0xDF','temp & 0xFF'),
 ('viewport-upper','screen','temp & 0xF8','temp & 0xFF'),
 ('dpms-on','dpms','pm |= 3','pm |= 1'),
 ('dpms-standby','dpms','dpms |= 1','dpms |= 0'),
 ('dpms-suspend','dpms','dpms |= 2','dpms |= 0'),
 ('dpms-off','dpms','dpms |= 3','dpms |= 0'),
 ('screen-video','lifecycle','    rc = trident_video_reset(d);','    rc = 0;'),
 ('screen-blank','lifecycle','STAGE(TRI_STEP_MODE, trident_screen_blank(d, true));','/* removed blank */'),
 ('screen-accel','lifecycle','    accel_init(d);\n    rc = tri_cursor_init(d);','    rc = tri_cursor_init(d);'),
 ('screen-cursor','lifecycle','    rc = tri_cursor_init(d);','    rc = 0;'),
 ('screen-palette','lifecycle','STAGE(TRI_STEP_CLIENT, tri_palette_load_default(d));','/* removed palette */'),
 ('enter-palette','lifecycle','STAGE(TRI_STEP_CLIENT, tri_palette_reload(d));','/* removed reload */'),
 ('enter-save','lifecycle','    u32 features = d->mode.features;','    trident_state_save(d);\n    u32 features = d->mode.features;'),
 ('close-sync-order','lifecycle','    int first = tri_sync(d);','    int first = trident_state_restore(d);\n    if (!first) first = tri_sync(d);'),
 ('configure-size','lifecycle','configure(&d->mode.selected, d->vram.first_off, true)','configure(&d->mode.selected, TRI_DISPLAY_BYTES, true)'),
 ('optional-accel','lifecycle','        d->mode.no_accel = true;','        d->screen_ready = false; return;'),
 ('optional-cursor','lifecycle','        d->cursor.available = false;','        return rc;'),
 ('optional-video','lifecycle','        d->video.polling = false;','        return rc;'),
 ('screen-frame','lifecycle','STAGE(TRI_STEP_MODE, trident_adjust_frame(d));','/* removed viewport */'),
 ('screen-retry','lifecycle','    d->last_rc = d->failed_step = d->restore_rc = d->disable_rc = 0;','    if (d->last_rc) return d->last_rc;'),
 ('active-enter','lifecycle','    if (!d->screen_ready) return TRI_E_STATE;','    if (d->active) return 0;\n    if (!d->screen_ready) return TRI_E_STATE;'),
]
MUTATIONS += [
 ('fallback-noaccel','mode_640','d->settings.no_accel || d->settings.ram_fallback || m.bpp == 24',
  'd->settings.no_accel || m.bpp == 24'),
 ('initial-byte-present','lifecycle','return trident_present(d, 0, 0, d->mode.client.width, d->mode.client.height);',
  'return TRI_OK;'),
 ('enter-byte-present','lifecycle','STAGE(TRI_STEP_CLIENT, trident_present(d, 0, 0, d->mode.client.width, d->mode.client.height));',
  'STAGE(TRI_STEP_CLIENT, TRI_OK);'),
 ('mode-byte-present','backend_trident','rc = trident_present(&device, 0, 0, device.mode.client.width, device.mode.client.height);',
  'rc = TRI_OK;'),
 ('cursor-byte-upload','cursor','if (d->res->ram_fallback) {','if (false) {'),
 ('overlay-byte-upload','video','((u32)v[x / 2] << 24);\n                if (d->res->ram_fallback)',
  '((u32)v[x / 2] << 24);\n                if (false)'),
 ('overlay-packed-byte-upload','video','((u32)y[x + 3] << 24);\n                if (d->res->ram_fallback)',
  '((u32)y[x + 3] << 24);\n                if (false)'),
]

def main():
    parser=argparse.ArgumentParser();parser.add_argument('--mutate',action='store_true');args=parser.parse_args()
    with tempfile.TemporaryDirectory(prefix='trident-life-') as tmp:
      work=Path(tmp)
      for name,file,old,new in [('normal',None,None,None)]+(MUTATIONS+[('restored',None,None,None)] if args.mutate else []):
        sources=[]
        for stem in SOURCES+('backend_trident',):
          text=(ROOT/DIR/(stem+'.c')).read_text()
          if stem==file:
            assert text.count(old)==1,(name,text.count(old));text=text.replace(old,new)
          if stem=='lifecycle':
            # Keep the private client implementation intact, redirect only its
            # call through the host WRAP seam (same-TU calls bypass ld --wrap).
            old_decl='static int client_init(struct trident_device *d)'
            assert text.count(old_decl)==1 and text.count('rc = client_init(d);')==1
            text=text.replace(old_decl, 'int __wrap_trident_client_init(struct trident_device *);\nint __real_trident_client_init(struct trident_device *d)')
            text=text.replace('rc = client_init(d);','rc = __wrap_trident_client_init(d);')
          path=work/(stem+'.c');path.write_text(text)
          if stem!='backend_trident':sources.append(str(path))
          else:(work/'backend_under_test.c').write_text(text)
        exe=work/'life'
        subprocess.run(['gcc','-m32','-march=i386','-std=gnu11','-O2','-Wall','-Wextra','-Werror','-ffreestanding','-fno-pie','-fno-stack-protector','-fno-builtin','-nostdlib','-static','-no-pie','-I'+str(work),'-I'+str(ROOT),'-I'+str(ROOT/DIR),*include_flags(ROOT),str(ROOT/'tools/tests/trident_lifecycle_host.c'),str(ROOT/'tools/tests/trident_device_fake.c'),str(ROOT/'tools/tests/trident_sync_fake.c'),str(ROOT/'tools/tests/concrete_probe_broker.c'),str(ROOT/'kernel/module/debug_probe.c'),str(ROOT/'lib/kstring_c.c'),*sources,*('-Wl,--wrap=trident_'+n for n in WRAPS), *('-Wl,--wrap=tri_'+n for n in ('mode_set','accel_init','cursor_init','palette_load_default','palette_reload','sync','video_stop')),'-o',str(exe)],check=True)
        for runner in os.environ.get('HOST32_RUNNERS','qemu').split():
          result=host32.run([str(exe)],runner=runner,capture_output=True)
          assert result.returncode==(0 if file is None else 1),(name,result)
          if file is not None:assert result.stdout.startswith(b'RED lifecycle '),result
        print(('GREEN ' if file is None else 'MUTATE RED ')+name,flush=True)
    return 0

def run(ctx):
    return subprocess.call(['python3','-B',__file__,*(['--mutate'] if ctx.mutate else [])],cwd=ctx.root,env=dict(os.environ,HOST32_RUNNERS=' '.join(ctx.runners)))
if __name__=='__main__':raise SystemExit(main())
