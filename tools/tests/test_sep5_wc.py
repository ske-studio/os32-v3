#!/usr/bin/env python3
"""H5: production CPU/PTE checks and single video WC lease with host primitives."""
CHECK = dict(name='sep5-wc-host', tier=1,
    covers=['arch/x86/video/gf_accept_cpu.c', 'arch/ppc/video/gf_accept_cpu.c', 'arch/x86/kernel/video_wc.c', 'arch/ppc/video_wc.c', 'include/video_wc.h',
            'drivers/video/geforce/gf_wc.c', 'tools/tests/test_sep5_wc.py',
            'tools/tests/gf_wc_host.c', 'tools/tests/recover_gf_dev_stub.c', 'tools/tests/kernel_includes.py',
            'tools/tests/gf_ownerless_fixture.py', 'tools/tests/gf_startlog_host.h',
            'tools/tests/legacy_guest_fake.h', 'include/legacy_guest.h',
            'drivers/video/geforce/geforce_body.c', 'drivers/video/geforce/geforce_module.c', 'drivers/video/geforce/backend_geforce.c',
            'hwdev/gf/accel.h', 'gfx/gfx_policy.h', 'gfx/gfx_osri_view.h',
            'arch/x86/kernel/mtrr_wc.c', 'arch/x86/kernel/mtrr_wc.h',
            'tools/tests/test_mtrr_wc.py', 'tools/tests/host32.py'],
    needs=[], budget=12.0)

import sys
from pathlib import Path
import subprocess
import tempfile
import host32
from kernel_includes import include_flags
from gf_ownerless_fixture import ownerless_body
ROOT = Path(__file__).resolve().parents[2]
FLAGS = ['gcc', '-m32', '-march=i386', '-std=gnu11', '-O2', '-Wall', '-Wextra',
         '-Werror', '-ffreestanding', '-fno-pie', '-fno-stack-protector',
         '-nostdlib', '-static', '-no-pie']
INCLUDES = include_flags(ROOT)

def wc_lifecycle(mutation=None):
    source=(ROOT/'arch/x86/kernel/video_wc.c').read_text()
    gpu=(ROOT/'drivers/video/geforce/gf_wc.c').read_text()
    body=(ROOT/'drivers/video/geforce/geforce_body.c').read_text()
    module=(ROOT/'drivers/video/geforce/geforce_module.c').read_text()
    backend=(ROOT/'drivers/video/geforce/backend_geforce.c').read_text()
    targets={
        'wc-load': ('module', '    if (context.platform && context.platform->wc_start) (void)context.platform->wc_start();', ''),
        'wc-stop': ('body', 'int rc = !identify() && p && p->wc_stop ? p->wc_stop() : 0;', 'int rc = 0; (void)p;'),
        'wc-stop-normal': ('body', 'int rc = !identify() && p && p->wc_stop ? p->wc_stop() : 0;', 'int rc = p && p->wc_stop ? p->wc_stop() : 0;'),
        'wc-repeat': ('source', 'if (lease.active) return wc_result;', 'if (0) return wc_result;'),
        'wc-start': ('backend', 'rc = start_wc ? start_wc() : 0;', 'rc = 0;'),
        'wc-flags': ('source', 'gf_wc_set_flags(flags);', '(void)flags;'),
        'wc-capability': ('source', 'if ((d & GF_WC_CPU_REQUIRED) != GF_WC_CPU_REQUIRED) return 0;', 'if (0) return 0;'),
        'wc-vendor': ('source', 'b != GF_WC_INTEL_B', '0'),
        'wc-alias': ('source', 'no_alias(fb, mapped, extent) && bar0_uc', '((void)extent, 1) && bar0_uc'),
    }
    if mutation:
        name,old,new=targets[mutation]
        values=dict(source=source,body=body,backend=backend,module=module)
        assert values[name].count(old)==1,(mutation,old)
        values[name]=values[name].replace(old,new,1)
        source,body,backend,module=(values[n] for n in ('source','body','backend','module'))
    a=source.index('/* Read-only CPU primitives begin.')
    b=source.index('/* Read-only CPU primitives end. */')
    source=source[:a]+source[b:]
    with tempfile.TemporaryDirectory(prefix='gf-wc-') as td:
        tmp=Path(td);exe=tmp/'host'
        (tmp/'gf_wc_source.inc').write_text(source+'\n'+gpu)
        # Preserve product relative includes in the staged fixture/body.
        (tmp/'geforce_body.inc').write_text(ownerless_body(body, bind_f3=False))
        (tmp/'backend.inc').write_text(backend.replace('"../../../gfx/', '"gfx/'))
        harness='#include "'+str(ROOT/'tools/tests/gf_startlog_host.h')+'"\n'+(ROOT/'tools/tests/gf_wc_host.c').read_text()
        start=module.index('    if (context.platform) gf_production_set_return(geforce_return, context.platform);')
        start=module.index('\n', start)+1
        end=module.index('    return 0;', start)
        call=module[start:end].replace('context.platform', 'ctx.platform')
        harness=harness.replace('static void load(void)', 'static void wc_registered_start(void)\n{\n'+call+'\n}\nstatic void load(void)')
        harness=harness.replace('"../../drivers/video/geforce/geforce_body.c"','"geforce_body.inc"')
        harness=harness.replace('"../../drivers/video/geforce/backend_geforce.c"','"backend.inc"')
        (tmp/'host.c').write_text(harness)
        (tmp/'legacy_guest_fake.h').write_text((ROOT/'tools/tests/legacy_guest_fake.h').read_text())
        host32.build(FLAGS+INCLUDES+['-I'+str(ROOT),'-I'+str(ROOT/'arch/x86/kernel'),'-I'+td,'-Wno-unused-function',str(tmp/'host.c'),str(ROOT/'tools/tests/recover_gf_dev_stub.c'),'-o',str(exe)],check=True)
        result=host32.run([str(exe)],capture_output=True,text=True,timeout=60)
        assert result.returncode==(1 if mutation else 0),(mutation,result.returncode,result.stdout,result.stderr)
        print(('RED '+mutation+' '+result.stdout.strip() if mutation else 'PASS WC lifetime: first/reentry/same init/reconfigure/abort/failures/double stop, owned/BIOS, unchanged PTE/ledger'),flush=True)


if __name__ == '__main__':
    wc_lifecycle()
    if '--mutate' in sys.argv:
        for mutation in ('wc-load', 'wc-stop', 'wc-stop-normal', 'wc-repeat', 'wc-alias', 'wc-flags', 'wc-capability', 'wc-vendor'):
            wc_lifecycle(mutation)


def run(ctx):
    import os
    import subprocess
    env = dict(os.environ, HOST32_RUNNERS=' '.join(ctx.runners))
    args = ['python3', '-B', 'tools/tests/test_sep5_wc.py']
    if ctx.mutate:
        args.append('--mutate')
    return subprocess.call(args, cwd=ctx.root, env=env, close_fds=False)
