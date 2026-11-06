#!/usr/bin/env python3
"""Reference ScreenInit/EnterVT, ResetVideo, blank/viewport and DPMS traces."""
CHECK = dict(name='trident-lifecycle', tier=1, needs=[], budget=600,
    covers=['drivers/video/trident/screen.c', 'drivers/video/trident/video.c', 'drivers/video/trident/dpms.c', 'drivers/video/trident/lifecycle.c', 'drivers/video/trident/backend_trident.c', 'drivers/video/trident/trident.h', 'drivers/video/trident/trident_module.h', 'drivers/video/trident/regs.c', 'drivers/video/trident/probe.c', 'drivers/video/trident/state_save.c', 'drivers/video/trident/pc98_init.c', 'drivers/video/trident/mode_640.c', 'drivers/video/trident/route_ra266.c', 'drivers/video/trident/vram.c', 'drivers/video/trident/default_dac.c', 'drivers/video/trident/state_restore.c', 'drivers/video/trident/present.c', 'drivers/video/trident/palette.c', 'tools/tests/trident_lifecycle_host.c', 'tools/tests/trident_device_fake.c', 'tools/tests/trident_device_fake.h', 'tools/tests/trident_pc98init_host.c', 'tools/tests/host32.py', 'tools/tests/kernel_includes.py', 'build/config.mk', 'include/types.h', 'include/memmap.h', 'include/io.h', 'include/gfx_hal.h', 'include/gfx_mode.h', 'include/gfx_format.h', 'include/pegc.h', 'gfx/gfx.h', 'gfx/palette.h', 'gfx/gfx_internal.h', 'mm/kmalloc.h', 'lib/serial_diag.h', 'userland/tests/hw/trident_mode.hwb', 'drivers/char/serial.h', 'sdk/include/os32/os32_kapi_shared.h', 'sdk/include/os32/os32_generations.h'])
import argparse
import os
from pathlib import Path
import subprocess
import tempfile
import host32
from kernel_includes import include_flags
ROOT=Path(__file__).resolve().parents[2]
DIR='drivers/video/trident/'
SOURCES=('lifecycle','regs','probe','state_save','pc98_init','mode_640','route_ra266','vram','default_dac','state_restore','present','palette','screen','video','dpms')
WRAPS=('identify','capacity_plan','state_save','pc98_init','mode_640','route_enable','adjust_frame','video_reset','screen_blank','state_restore','route_disable','regs_bind')
MUTATIONS=[
 ('prepare-retry','backend_trident','    device.failed_step = 0;\n    device.last_rc = 0;','    if (device.last_rc) return device.last_rc;'),
 ('protect-inactive','regs','if (!d->active) return 0;', 'if (d->active) return 0;'),
 ('wait-twice','video','frame < 2','frame < 1'),
 ('overlay-stop','video','0x8E, 0x84','0x8E, 0'),
 ('overlay-dac','video','0x57, 0xC0','0x57, 0'),
 ('overlay-control','video','0xBE, 0x26','0xBE, 0'),
 *[(f'key-{i:02x}','video',f'video_out(d, 0x{i:02X}, {v})',f'video_out(d, 0x{i:02X}, 1)') for i,v in [(0x50,'0xFF'),(0x51,'0'),(0x52,'0'),(0x54,'0xFF'),(0x55,'0'),(0x56,'0')]],
 ('blank','screen','blank ? scrn | 0x20','blank ? scrn | 0x00'),
 ('viewport-low','screen','base, 0x0C, 0','base, 0x0C, 1'),
 ('viewport-high','screen','temp & 0xDF','temp & 0xFF'),
 ('viewport-upper','screen','temp & 0xF8','temp & 0xFF'),
 ('dpms-on','dpms','pm |= 3','pm |= 1'),
 ('dpms-standby','dpms','dpms |= 1','dpms |= 0'),
 ('dpms-suspend','dpms','dpms |= 2','dpms |= 0'),
 ('dpms-off','dpms','dpms |= 3','dpms |= 0'),
 ('screen-video','lifecycle','STAGE(TRI_STEP_CLIENT, trident_video_reset(d));','/* removed video */'),
 ('screen-blank','lifecycle','STAGE(TRI_STEP_MODE, trident_screen_blank(d, true));','/* removed blank */'),
 ('blank-after-video','lifecycle',
  '    STAGE(TRI_STEP_MODE, trident_screen_blank(d, true));\n    STAGE(TRI_STEP_MODE, trident_adjust_frame(d));\n    rc = client_init(d);\n    if (rc) return failed(d, TRI_STEP_CLIENT, rc);\n    d->step = TRI_STEP_CLIENT;\n    STAGE(TRI_STEP_CLIENT, trident_video_reset(d));',
  '    STAGE(TRI_STEP_MODE, trident_adjust_frame(d));\n    rc = client_init(d);\n    if (rc) return failed(d, TRI_STEP_CLIENT, rc);\n    d->step = TRI_STEP_CLIENT;\n    STAGE(TRI_STEP_CLIENT, trident_video_reset(d));\n    STAGE(TRI_STEP_MODE, trident_screen_blank(d, true));'),
 ('screen-frame','lifecycle','STAGE(TRI_STEP_MODE, trident_adjust_frame(d));','/* removed viewport */'),
 ('screen-retry','lifecycle','    d->last_rc = d->failed_step = d->restore_rc = d->disable_rc = 0;','    if (d->last_rc) return d->last_rc;'),
 ('active-enter','lifecycle','    if (!d->screen_ready) return TRI_E_STATE;','    if (d->active) return 0;\n    if (!d->screen_ready) return TRI_E_STATE;'),
]
def main():
    parser=argparse.ArgumentParser();parser.add_argument('--mutate',action='store_true');args=parser.parse_args()
    with tempfile.TemporaryDirectory(prefix='trident-life-') as tmp:
      work=Path(tmp)
      for name,file,old,new in [('normal',None,None,None)]+(MUTATIONS if args.mutate else []):
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
        subprocess.run(['gcc','-m32','-march=i386','-std=gnu11','-O2','-Wall','-Wextra','-Werror','-ffreestanding','-fno-pie','-fno-stack-protector','-fno-builtin','-nostdlib','-static','-no-pie','-I'+str(work),'-I'+str(ROOT),'-I'+str(ROOT/DIR),*include_flags(ROOT),str(ROOT/'tools/tests/trident_lifecycle_host.c'),str(ROOT/'tools/tests/trident_device_fake.c'),*sources,*('-Wl,--wrap=trident_'+n for n in WRAPS),'-o',str(exe)],check=True)
        for runner in os.environ.get('HOST32_RUNNERS','qemu').split():
          result=host32.run([str(exe)],runner=runner,capture_output=True)
          assert result.returncode==(0 if file is None else 1),(name,result)
          if file is not None:assert result.stdout.startswith(b'RED lifecycle '),result
        print(('GREEN ' if file is None else 'MUTATE RED ')+name,flush=True)
    return 0

def run(ctx):
    return subprocess.call(['python3','-B',__file__,*(['--mutate'] if ctx.mutate else [])],cwd=ctx.root,env=dict(os.environ,HOST32_RUNNERS=' '.join(ctx.runners)))
if __name__=='__main__':raise SystemExit(main())
