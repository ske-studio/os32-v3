"""KDEBUG=0 production startup, resource gate, cleanup and reboot order."""
CHECK = dict(name='gf-production-host', tier=1,
    covers=['include/io.h', 'arch/x86/arch_io.h', 'tools/tests/host_arch/arch_io.h', 'tools/tests/gf_production_hal_host.inc', 'lib/serial_diag.h', 'hwdev/gf/bus.c', 'hwdev/gf/devinit.c', 'hwdev/gf/native.c', 'hwdev/gf/pll.c', 'hwdev/gf/resources.c', 'hwdev/gf/runner.c', 'hwdev/gf/save.c', 'hwdev/gf/service.c', 'hwdev/gf_load_state.c', 'kdebug/gfinit_decode.c', 'kdebug/gfinit_pll.c', 'kdebug/gfinit_resources.c', 'kdebug/recover.c', 'kdebug/recover_common/*.inc', 'kdebug/recover_plan/*.inc', 'kdebug/recover_gf_stage/*.inc', 'kdebug/recover_gf.c', 'kernel/sys.c', 'init/sysconfig.c', 'tools/tests/gf_production_host.c', 'tools/tests/gfinit_host.c', 'tools/tests/host32.py', 'tools/tests/recover_gf_host.c', 'hwdev/gf/*.h', 'hwdev/gf/display/*.c', 'hwdev/gf/display/*.h', 'hwdev/gf/op/*.c', 'include/gf_production.h', 'include/gfx_hal.h', 'gfx/gfx_internal.h', 'drivers/video/geforce/geforce_body.c', 'drivers/video/geforce/geforce_module.c', 'drivers/video/geforce/geforce_module.h', 'drivers/video/geforce/backend_geforce.c', 'drivers/video/geforce/gf_present.c', 'gfx/palette.h', 'drivers/video/pc98/pc98_route.h', 'tools/tests/kernel_includes.py'],
    needs=[], budget=60.0)

from kernel_includes import include_flags
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
             '-DKDEBUG=0', '-DGF_PRODUCTION',
             *include_flags(ROOT)]
    with tempfile.TemporaryDirectory(prefix='gf-production-') as tmp:
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
                        '-c', str(ROOT / 'hwdev/gf/native.c'), '-o', str(native)], check=True)
        module=(ROOT/'drivers/video/geforce/geforce_module.c').read_text()
        a=module.index('static int geforce_return(')
        (Path(tmp)/'geforce_return.inc').write_text(module[a:module.index('static int geforce_start(',a)])
        backend=Path(tmp)/'backend.o'
        subprocess.run(['gcc',*flags,'-Igfx','-DOS32_GEFORCE_MODULE','-c',
                        str(ROOT/'drivers/video/geforce/backend_geforce.c'),'-o',str(backend)],check=True)
        sources = ['tools/tests/gf_production_host.c', 'kernel/sys.c',
                   'hwdev/gf/devinit.c', 'hwdev/gf/bus.c', 'hwdev/gf/resources.c',
                   'hwdev/gf/runner.c', 'hwdev/gf/save.c', 'hwdev/gf/pll.c',
                   'hwdev/gf/service.c', 'hwdev/gf_load_state.c', 'kdebug/gfinit_resources.c']
        sources += [str(p.relative_to(ROOT)) for pattern in ('hwdev/gf/op/*.c', 'hwdev/gf/display/*.c')
                    for p in sorted(ROOT.glob(pattern))]
        subprocess.run(['gcc', *flags, '-nostdlib', '-static', '-no-pie', '-Wl,--gc-sections',
                        *[str(override[1] if override and p==override[0] else ROOT / p) for p in sources], str(native), '-o', str(exe)], check=True)
        symbols = subprocess.check_output(['nm', '--defined-only', str(exe)], text=True)
        assert not any(name in symbols for name in (' recover_', ' kpeek_', ' trial_'))
        rc=host32.run([str(exe)], cwd=ROOT, timeout=180).returncode
        if rc: return rc
        subprocess.run(['gcc',*flags,'-Igfx','-I'+tmp,'-DGF_HAL_TEST','-nostdlib','-static','-no-pie',
                        '-Wl,--gc-sections',*[str(override[1] if override and p==override[0] else ROOT / p) for p in sources],str(native),str(backend),
                        '-o',str(exe)],check=True)
        return host32.run([str(exe)],cwd=ROOT,timeout=180).returncode


def mutations():
    rows = [
        ('hwdev/gf/devinit.c', 'if (p->foundation.revoked) c->engaged=false;', '(void)c;', 'revoked cleanup exception omitted'),
        ('hwdev/gf/devinit.c', '*value=*(volatile u32 *)(f->bar0+off);', '(void)f;(void)off;*value=0;', 'resident MMIO read omitted'),
        ('hwdev/gf/devinit.c', '*(volatile u32 *)(f->bar0+off)=value;', '(void)f;(void)off;(void)value;', 'resident MMIO write omitted'),
        ('hwdev/gf/devinit.c', '    if (f->revoked) return -1;\n    *value=', '    *value=', 'revoked resident read allowed'),
        ('hwdev/gf/devinit.c', '    if (f->revoked) return -1;\n    *(volatile', '    *(volatile', 'revoked resident write allowed'),
        ('hwdev/gf/bus.c', '    gf_foundation_revoke(&p->foundation);', '    (void)p;', 'revoke publication omitted'),
        ('hwdev/gf/devinit.c', 'gf_master_enable(&p->foundation)', '0', 'N1 BM enable omitted'),
        ('hwdev/gf/devinit.c', 'pmc!=p->foundation.expected_pmc', 'pmc!=c->baseline_pmc', 'saved PMC comparison restored'),
        ('hwdev/gf/bus.c', '!=p->foundation.expected_command)', '!=RECOVER_COMMAND_STOP)', 'RUN transport rejected'),
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
