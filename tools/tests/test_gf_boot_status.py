"""Unprepared production GF1 status must not call a missing MMIO callback."""
CHECK = dict(name='gf-boot-status-host', tier=1, needs=[], budget=10.0,
    covers=['tools/tests/test_gf_boot_status.py', 'tools/tests/gf_boot_status_host.c',
            'tools/tests/host32.py', 'tools/tests/kernel_includes.py',
            'kdebug/gf_foundation_test.c', 'kdebug/gf_foundation_test.h',
            'kdebug/gfinit_bus.h', 'kdebug/gf_stage_state.h', 'kdebug/recover*.h',
            'hwdev/gf/mc.c', 'hwdev/gf/*.h', 'hwdev/gf/display/*.h', 'hwdev/gfinit_mode.h',
            'drivers/video/geforce/geforce_module.h', 'drivers/pci/pci.h',
            'lib/serial_diag.h', 'lib/crc32_core.inc', 'include/*.h', 'mm/*.h'])
from pathlib import Path
import os
import re
import subprocess
import sys
import tempfile
import host32
from kernel_includes import include_flags
ROOT = Path(__file__).resolve().parents[2]


def main(mutate=False):
    with tempfile.TemporaryDirectory(prefix='gf-boot-status-') as td:
        tmp = Path(td)
        source = ROOT/'kdebug/gf_foundation_test.c'
        original = source.read_text()
        mc_source = ROOT/'hwdev/gf/mc.c'
        mc_original = mc_source.read_text()
        variants = [('control', original, mc_original)]
        if mutate:
            old = 'if (!owner->prepared || !owner->display.io.read) return GF1_EINVAL;'
            assert original.count(old) == 1
            variants.append(('missing readiness guard', original.replace(old, 'if (0) return GF1_EINVAL;'), mc_original))
            old = 'f->expected_pmc = enabled;'
            assert mc_original.count(old) == 1
            variants.append(('MC write value retained', original, mc_original.replace(old, 'f->expected_pmc = GF_MC_ENABLE_ALL;')))
        for name, body, mc_body in variants:
            mc_body = re.sub(r'#include "([^"\n]+)"',
                lambda m: '#include "'+str((mc_source.parent/m[1]).resolve())+'"', mc_body)
            (tmp/'mc.c').write_text(mc_body)
            body = re.sub(r'#include "([^"\n]+)"',
                          lambda m: '#include "'+str((source.parent/m[1]).resolve())+'"', body)
            (tmp/'status.c').write_text(body)
            host = (ROOT/'tools/tests/gf_boot_status_host.c').read_text().replace(
                '../../kdebug/gf_foundation_test.c', str(tmp/'status.c'))
            (tmp/'host.c').write_text(host)
            subprocess.run(['gcc', '-m32', '-std=gnu11', '-O2', '-Wall', '-Wextra', '-Werror',
                            '-ffreestanding', '-fno-pie', '-fno-stack-protector',
                            '-ffunction-sections', '-fdata-sections', '-nostdlib', '-static', '-no-pie',
                            '-Wl,--gc-sections', *include_flags(ROOT), str(tmp/'host.c'), str(tmp/'mc.c'),
                            '-o', str(tmp/'host')], check=True)
            rc = host32.run([str(tmp/'host')], timeout=30).returncode
            assert (rc == 0 if name == 'control' else rc != 0), (name, rc)
            print(('PASS ' if name == 'control' else 'MUTATE runtime RED: ')+name, flush=True)
    return 0


if __name__ == '__main__':
    raise SystemExit(main('--mutate' in sys.argv))


def run(ctx):
    return subprocess.call(['python3', '-B', 'tools/tests/test_gf_boot_status.py',
                            *(['--mutate'] if ctx.mutate else [])], cwd=ctx.root,
                           env=dict(os.environ, HOST32_RUNNERS=' '.join(ctx.runners)))
