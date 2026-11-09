"""KDEBUG=0 production startup, resource gate, cleanup and reboot order."""
CHECK = dict(name='gf-production-host', tier=1,
    covers=['include/video_wc.h', 'arch/x86/kernel/video_wc.c', 'arch/ppc/video_wc.c', 'gfx/gfx_policy.h', 'gfx/gfx_osri_view.h', 'drivers/video/geforce/geforce_platform.c', 'tools/tests/test_gf_production.py', 'tools/tests/gf_ownerless_fixture.py', 'tools/tests/gf_accel_no_bind.h', 'tools/tests/gf_accel_unbound_stubs.c', 'include/io.h', 'arch/x86/arch_io.h', 'tools/tests/host_arch/arch_io.h', 'tools/tests/gf_production_hal_host.inc', 'lib/serial_diag.h', 'include/kprintf.h', 'hwdev/gf/bus.c', 'hwdev/gf/devinit.c', 'hwdev/gf/native.c', 'hwdev/gf/pll.c', 'hwdev/gf/resources.c', 'hwdev/gf/runner.c', 'hwdev/gf/save.c', 'hwdev/gf/service.c', 'hwdev/gf/accel_surface.c', 'hwdev/gf_load_state.c', 'kdebug/gfinit_decode.c', 'kdebug/gfinit_pll.c', 'kdebug/gfinit_resources.c', 'kdebug/recover.c', 'kdebug/recover_common/*.inc', 'kdebug/recover_plan/*.inc', 'kdebug/recover_gf_stage/*.inc', 'kdebug/recover_gf.c', 'kernel/sys.c', 'init/sysconfig.c', 'tools/tests/gf_production_host.c', 'tools/tests/gf_foundation_width_host.h', 'tools/tests/gfinit_host.c', 'tools/tests/host32.py', 'tools/tests/recover_gf_host.c', 'hwdev/gf/*.h', 'hwdev/gf/display/*.c', 'hwdev/gf/display/*.h', 'hwdev/gf/op/*.c', 'include/gf_production.h', 'include/gfx_hal.h', 'gfx/gfx_internal.h', 'drivers/video/geforce/geforce_body.c', 'drivers/video/geforce/geforce_module.c', 'drivers/video/geforce/geforce_module.h', 'drivers/video/geforce/backend_geforce.c', 'drivers/video/geforce/gf_present.c', 'gfx/palette.h', 'drivers/video/pc98/pc98_route.h', 'tools/tests/kernel_includes.py'],
    needs=[], budget=23.6)

from kernel_includes import include_flags
from gf_ownerless_fixture import ownerless_body
from pathlib import Path
import os
import subprocess
import tempfile
import sys
import host32

ROOT = Path(__file__).resolve().parents[2]


def main(override=None):
    flags = ['-m32', '-march=i386', '-std=gnu11', '-O2', '-Wall', '-Wextra',
             '-Werror', '-Werror=vla', '-ffreestanding', '-fno-pie',
             '-fno-stack-protector', '-ffunction-sections', '-fdata-sections',
             '-DKDEBUG=0', '-DGF_PRODUCTION', '-DGF_WIDTH_TRACE',
             *include_flags(ROOT)]
    with tempfile.TemporaryDirectory(prefix='gf-production-') as tmp:
        (Path(tmp)/'gf_ownerless_body.inc').write_text(ownerless_body(
            (ROOT/'drivers/video/geforce/geforce_body.c').read_text(), bind_f3=False))
        exe = Path(tmp) / 'host'
        native = Path(tmp) / 'native.o'
        # P5 owns module/build registration. Compile the new production TUs
        # with the actual cross compiler too, while that parallel wiring lands.
        cross = Path(os.environ['CROSS_DIR']) / 'bin/i386-elf-gcc'
        for name in ('devinit', 'bus', 'native', 'resources'):
            subprocess.run([str(cross), *flags, '-D__KERNEL_BUILD__', '-c',
                            str(ROOT / f'hwdev/gf/{name}.c'),
                            '-o', str(Path(tmp) / f'{name}-cross.o')], check=True)
        flags = ['-I'+str(ROOT/'tools/tests/host_arch'), *flags]
        subprocess.run(['gcc', *flags, '-Dgf_production_mmio=gf_production_native_mmio',
                        '-Dgf_production_clock=gf_production_native_clock',
                        '-Dgf_production_wait=gf_production_native_wait',
                        '-c', str(override[1] if override and override[0]=='hwdev/gf/native.c' else ROOT / 'hwdev/gf/native.c'), '-o', str(native)], check=True)
        devinit = Path(tmp) / 'devinit.o'
        devinit_source = override[1] if override and override[0]=='hwdev/gf/devinit.c' else ROOT/'hwdev/gf/devinit.c'
        subprocess.run(['gcc', *flags, '-fsanitize=kernel-address', '-c',
                        str(devinit_source), '-o', str(devinit)], check=True)
        mode = Path(tmp) / 'mode.o'
        gamma = Path(tmp) / 'gamma.o'
        for name, obj in (('modeset', mode), ('gamma', gamma)):
            source = ROOT / f'hwdev/gf/display/{name}.c'
            if override and str(source.relative_to(ROOT)) == override[0]:
                source = override[1]
            subprocess.run(['gcc', *flags, '-DGF_PRODUCTION_MODULE',
                            '-Dgf_display_modeset=gf_unused_legacy_modeset',
                            '-c', str(source), '-o', str(obj)], check=True)
        module=(ROOT/'drivers/video/geforce/geforce_module.c').read_text()
        a=module.index('static int geforce_return(')
        (Path(tmp)/'geforce_return.inc').write_text(module[a:module.index('static int geforce_start(',a)])
        backend=Path(tmp)/'backend.o'
        subprocess.run(['gcc',*flags,'-Igfx','-DOS32_GEFORCE_MODULE','-c',
                        str(ROOT/'drivers/video/geforce/backend_geforce.c'),'-o',str(backend)],check=True)
        sources = ['tools/tests/gf_accel_unbound_stubs.c', 'tools/tests/gf_production_host.c', 'kernel/sys.c',
                   'hwdev/gf/bus.c', 'hwdev/gf/resources.c',
                   'hwdev/gf/runner.c', 'hwdev/gf/save.c', 'hwdev/gf/pll.c',
                   'hwdev/gf/service.c', 'hwdev/gf/accel_surface.c', 'hwdev/gf_load_state.c', 'kdebug/gfinit_resources.c']
        # Ownerless legacy display composition: F4-only lifecycle TUs must
        # not override the trap for an unexpected gf4_service_start call.
        f4_only = {'dcb', 'ddc', 'dpms', 'flip', 'gamma', 'tmds', 'tv',
                   'heads', 'transaction', 'service', 'save', 'recover', 'scan', 'links'}
        sources += [str(p.relative_to(ROOT)) for p in sorted(ROOT.glob('hwdev/gf/op/*.c'))]
        sources += [str(p.relative_to(ROOT)) for p in sorted(ROOT.glob('hwdev/gf/display/*.c'))
                    if p.stem not in f4_only]
        subprocess.run(['gcc', *flags, '-nostdlib', '-static', '-no-pie', '-Wl,--gc-sections',
                        *[str(override[1] if override and p==override[0] else ROOT / p) for p in sources], str(native), str(devinit), str(mode), str(gamma), '-o', str(exe)], check=True)
        symbols = subprocess.check_output(['nm', '--defined-only', str(exe)], text=True)
        assert not any(name in symbols for name in (' recover_', ' kpeek_', ' trial_'))
        rc=host32.run([str(exe)], cwd=ROOT, timeout=180).returncode
        if rc: return rc
        print('PASS production prepare -> foundation MMIO: byte/word/dword width/address/count, adjacent bytes and CR index/data', flush=True)
        subprocess.run(['gcc',*flags,'-Igfx','-I'+tmp,'-DGF_HAL_TEST','-nostdlib','-static','-no-pie',
                        '-Wl,--gc-sections',*[str(override[1] if override and p==override[0] else ROOT / p) for p in sources],str(native),str(devinit),str(mode),str(gamma),str(backend),
                        '-o',str(exe)],check=True)
        return host32.run([str(exe)],cwd=ROOT,timeout=180).returncode


def mutations():
    rows = [
        ('hwdev/gf/native.c',
         'u32 part=us>GF_INIT_WAIT_CHUNK_US ? GF_INIT_WAIT_CHUNK_US : us;',
         'u32 part=us;', 'native wait passed unsplit'),
        ('hwdev/gf/native.c', '    while (us) {',
         '    if (us>GF_INIT_WAIT_CHUNK_US) return -1;\n    while (us) {',
         'native wait long request rejected'),
        ('hwdev/gf/native.c', '        us-=part;',
         '        us=0;', 'native wait total truncated'),
        ('hwdev/gf/native.c', 'return sys_time_now(value,&hi);',
         'return sys_time_now(value,&hi) ? -1 : 0;', 'native clock raw rc'),
        ('hwdev/gf/native.c', 'int rc=gf_production_clock(opaque,&start);',
         'int rc=gf_production_clock(opaque,&start) ? -1 : 0;', 'native wait start raw rc'),
        ('hwdev/gf/native.c', 'rc=gf_production_clock(opaque,&now);',
         'rc=gf_production_clock(opaque,&now) ? -1 : 0;', 'native wait poll raw rc'),
        ('hwdev/gf/display/modeset.c',
         'u32 part=us>GF_INIT_WAIT_CHUNK_US ? GF_INIT_WAIT_CHUNK_US : us;',
         'u32 part=us;', 'mode_load outer deadline interval'),
        ('hwdev/gf/devinit.c', 'p->foundation.expected_pmc=prepared_pmc;', '(void)prepared_pmc;', 'prepare PMC publication omitted'),
        ('hwdev/gf/devinit.c', '.timing={foundation_clock,foundation_wait}', '.timing={0 ? foundation_clock : 0,0 ? foundation_wait : 0}', 'prepare timing omitted'),
        ('hwdev/gf/devinit.c', 'u16 stopped=command&~PCI_CMD_BUS_MASTER;', 'u16 stopped=command;', 'prepare BM clear omitted'),
        ('hwdev/gf/devinit.c', 'u16 stopped=command&~PCI_CMD_BUS_MASTER;', 'u16 stopped=RECOVER_COMMAND_STOP;', 'prepare other Command bits lost'),
        ('hwdev/gf/runner.c', '    /* Production point reads the clock again and updates elapsed_us. */\n    if (!ctx->opcode_timing && ctx->elapsed_us >= GF_INIT_EXEC_US)', '    /* Production point reads the clock again and updates elapsed_us. */\n    if (0)', 'script point budget reported as EIO'),
        ('hwdev/gf/devinit.c', 'if (p->foundation.revoked) c->engaged=false;', '(void)c;', 'revoked cleanup exception omitted'),
        ('hwdev/gf/devinit.c', 'else *value=*(volatile u32 *)(f->bar0+off);', 'else *value=0;', 'resident MMIO read omitted'),
        ('hwdev/gf/devinit.c', 'else *(volatile u32 *)(f->bar0+off)=value;', 'else (void)value;', 'resident MMIO write omitted'),
        ('hwdev/gf/devinit.c', '    if (f->revoked) return -1;\n    if (width==1) *value=', '    if (width==1) *value=', 'revoked resident read allowed'),
        ('hwdev/gf/devinit.c', '    if (f->revoked) return -1;\n    if (width==1) *(volatile', '    if (width==1) *(volatile', 'revoked resident write allowed'),
        ('hwdev/gf/bus.c', '    gf_foundation_revoke(&p->foundation);', '    (void)p;', 'revoke publication omitted'),
        ('hwdev/gf/devinit.c', 'gf_master_enable(&p->foundation)', '0', 'N1 BM enable omitted'),
        ('hwdev/gf/devinit.c', '    u32 amount;\n    if (gf_production_decode_safe(p)', '    u32 amount,pmc;\n    if (gf_production_read(p,0,RECOVER_PMC_ENABLE,4,&pmc) || pmc!=p->foundation.expected_pmc || gf_production_decode_safe(p)', 'unverified PMC readback comparison restored'),
        ('hwdev/gf/bus.c', '!=p->foundation.expected_command)', '!=RECOVER_COMMAND_STOP)', 'RUN transport rejected'),
        ('hwdev/gf/devinit.c', 'if (width==1) *value=*(volatile u8 *)(f->bar0+off);', 'if (width==1) *value=*(volatile u32 *)(f->bar0+off);', 'resident u8 read widened'),
        ('hwdev/gf/devinit.c', 'if (width==1) *value=*(volatile u8 *)(f->bar0+off);', 'if (width==1) *value=(u8)*(volatile u32 *)(f->bar0+off);', 'resident masked overread'),
        ('hwdev/gf/devinit.c', 'if (width==1) *(volatile u8 *)(f->bar0+off)=(u8)value;', 'if (width==1) *(volatile u32 *)(f->bar0+off)=(u32)value;', 'resident u8 write widened'),
        ('hwdev/gf/devinit.c', 'if (width==2) *value=*(volatile u16 *)(f->bar0+off);', 'if (width==2) *value=*(volatile u32 *)(f->bar0+off);', 'resident u16 read widened'),
        ('hwdev/gf/devinit.c', 'if (width==2) *(volatile u16 *)(f->bar0+off)=(u16)value;', 'if (width==2) *(volatile u32 *)(f->bar0+off)=(u32)value;', 'resident u16 write widened'),
    ]
    with tempfile.TemporaryDirectory(prefix='gf-cw-production-mut-') as td:
        for rel, old, new, label in rows:
            source = ROOT / rel
            original = source.read_text()
            assert original.count(old) == 1, label
            import re
            text = re.sub(r'#include "([^"\n]+)"',
                          lambda m: '#include "' + str((source.parent / m[1]).resolve()) + '"',
                          original.replace(old, new, 1))
            mutant = Path(td) / source.name
            mutant.write_text(text)
            assert main((rel, mutant)) != 0, label
            print('MUTATE runtime RED: ' + label, flush=True)


if __name__ == '__main__':
    rc = main()
    if not rc and '--mutate' in sys.argv:
        mutations()
    raise SystemExit(rc)


def run(ctx) -> int:
    import os
    import shlex
    import subprocess
    base_env = dict(os.environ, HOST32_RUNNERS=' '.join(ctx.runners))
    env = dict(base_env)
    argv = ['python3', '-B', 'tools/tests/test_gf_production.py']
    if ctx.mutate:
        argv.append('--mutate')
    rc = subprocess.call(argv, cwd=ctx.root, env=env, close_fds=False)
    if rc:
        return rc
    return 0
