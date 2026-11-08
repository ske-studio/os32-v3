"""F0s backend stop, probe latch and explicit temperature gate."""
CHECK = dict(name='geforce-heat-backend', tier=1, needs=[], budget=3.4,
             covers=['gfx/gfx_policy.h', 'gfx/gfx_osri_view.h', 'tools/tests/test_geforce_heat_backend.py',
                     'tools/tests/geforce_heat_backend_host.c',
                     'tools/tests/backend_geforce_host.c',
                     'tools/tests/gf_startlog_host.h',
                     'tools/tests/gf_accel_unbound_stubs.c',
                     'tools/tests/host32.py', 'tools/tests/kernel_includes.py',
                     'drivers/video/geforce/**', 'hwdev/gf/**', 'include/**',
                     'gfx/gfx_internal.h', 'gfx/palette.h',
                     'drivers/video/pc98/pc98_route.h', 'kernel/module/*.h',
                     'lib/kstring.h', 'build/config.mk'])
import argparse
import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile
import host32
from kernel_includes import include_flags
ROOT = Path(__file__).resolve().parents[2]
MUTATIONS = [
    ('probe-latch', 'if (geforce_context->heat && geforce_context->heat->stop_state.state!=GF5_STOP_NORMAL) ok=0;', 'if (0) ok=0;'),
    ('stop-route', 'int route_rc = (was_routed || forced_stop) ? pc98_route_to_98() : 0;', 'int route_rc = ((void)was_routed, (void)forced_stop, (void)pc98_route_to_98, 0);'),
    ('stop-active', 'active = routed = 0;', 'active = routed = 1;'),
    ('cool-gate', 'sample.milli_c>cool', 'sample.milli_c>=cool'),
    ('force-stop', 'rc=gf_foundation_stop(feature5->ch->f);', 'rc=0;'),
]
def case(folder, label, mutation=None):
    path=ROOT/'drivers/video/geforce/backend_geforce.c'
    source=path.read_text()
    if mutation:
        old,new=mutation
        expected=2 if label=='stop-active' else 1
        assert source.count(old)==expected, label
        source=source.replace(old,new)
    source=re.sub(r'#include "([^"\n]+)"',
                  lambda m: '#include "'+str((path.parent/m[1]).resolve())+'"'
                  if (path.parent/m[1]).exists() else m[0],source)
    product=folder/'backend.c'; product.write_text(source)
    fixture=(ROOT/'tools/tests/backend_geforce_host.c').read_text().replace(
        '../../drivers/video/geforce/backend_geforce.c',str(product))
    (folder/'backend_geforce_host.c').write_text(fixture)
    fixture=(ROOT/'tools/tests/geforce_heat_backend_host.c').read_text()
    host=folder/'host.c'; host.write_text(fixture)
    exe=folder/'host'
    subprocess.run(['gcc','-m32','-march=i386','-std=gnu11','-O2','-Wall','-Wextra',
                    '-Werror','-ffreestanding','-fno-pie','-fno-stack-protector',
                    '-ffunction-sections','-fdata-sections','-D__KERNEL_BUILD__',
                    '-DOS32_GEFORCE_MODULE',*include_flags(ROOT),
                    '-I'+str(ROOT/'tools/tests'),str(host),
                    str(ROOT/'tools/tests/gf_accel_unbound_stubs.c'),
                    '-nostdlib','-static','-no-pie','-Wl,--gc-sections',
                    '-o',str(exe)],check=True)
    result=host32.run([str(exe)],capture_output=True,timeout=10)
    host32.report_signal(result.returncode)
    assert result.returncode==(1 if mutation else 0),(label,result.returncode,result.stderr)
    print(('MUTATE runtime RED: ' if mutation else 'GREEN: ')+label,flush=True)
def main():
    p=argparse.ArgumentParser(); p.add_argument('--mutate',action='store_true'); args=p.parse_args()
    with tempfile.TemporaryDirectory(prefix='gf-heat-') as td:
        folder=Path(td); case(folder,'backend')
        if args.mutate:
            for name,old,new in MUTATIONS:
                case(folder,name,(old,new)); case(folder,name+'-restored')
def run(ctx):
    args=[sys.executable,'-B','tools/tests/test_geforce_heat_backend.py']
    if ctx.mutate: args.append('--mutate')
    return subprocess.call(args,cwd=ctx.root,env=dict(os.environ,HOST32_RUNNERS=' '.join(ctx.runners)),close_fds=False)
if __name__=='__main__': main()
