"""Timeout diagnostics through the real F1/F2/F3 composition."""
CHECK = dict(name="gf-objwait-host", tier=1, needs=[], budget=6.0, covers=['tools/tests/gf_ownerless_fixture.py', 'include/**', 'kdebug/*.h', 'kernel/irq/irq.h', 'hwdev/*.h', 'mm/*.h', 'drivers/pci/pci.h', 'lib/serial_diag.h', 'hwdev/gf/**', 'include/gfx_hal.h', 'gfx/gfx_internal.h', 'hwdev/gf/accel*', 'hwdev/gf/vram_view.*', 'hwdev/gf/command.h', 'hwdev/gf/foundation.h', 'drivers/video/geforce/*', 'tools/tests/gf_accel*', 'tools/tests/gf_command_runtime_host.h', 'tools/tests/test_gf_accel.py', 'tools/accept/gf_accel.py', 'drivers/video/geforce/gf_accept.c', 'drivers/video/geforce/gf_present.c', 'drivers/video/geforce/geforce_module.h', 'hwdev/gf/production.h', 'userland/gshell/src/**', 'userland/gshell/host/**', 'userland/gshell/build.rs', 'sdk/rust/os32api/**', 'tools/tests/os32api_host.py', 'userland/lib/font/**', 'exec/shell_service.h', 'sdk/include/os32/os32_gui_shared.h', 'sdk/include/os32/os32_kapi_shared.h', 'tools/tests/host32.py', 'tools/tests/kernel_includes.py', 'hwdev/gf/wait_diag.h', 'lib/serial_diag.h', 'tools/tests/gf_objwait_host.c', 'tools/tests/test_gf_objwait.py', 'tools/tests/mutpar.py'])

import argparse
import os
from pathlib import Path
import subprocess
import tempfile
import host32
import mutpar
from kernel_includes import include_flags
ROOT = Path(__file__).resolve().parents[2]

def command(exe):
    return ['gcc', '-m32', '-march=i386', '-std=gnu11', '-O2', '-Wall',
            '-Wextra', '-Werror', '-Werror=vla', '-ffreestanding', '-fno-pie',
            '-fno-stack-protector', '-ffunction-sections', '-fdata-sections',
            '-nostdlib', '-static', '-no-pie', '-Wl,--gc-sections',
            *include_flags(ROOT), '-I'+str(ROOT),
            str(ROOT/'tools/tests/gf_objwait_host.c'),
            str(ROOT/'hwdev/gf/accel_copy.c'),
            str(ROOT/'tools/tests/gf_accel_unbound_stubs.c'), '-o', str(exe)]

def execute(exe, green, label):
    for runner in os.environ.get('HOST32_RUNNERS', 'qemu').split():
        rc = host32.run([str(exe)], runner=runner, cwd=ROOT).returncode
        assert (rc == 0) == green, (label, runner, rc)
        print(f'{label}: runtime {"GREEN" if green else "RED"} runner={runner} rc={rc}', flush=True)

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--mutate', action='store_true')
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix='gf-objwait-') as tmp:
        directory = Path(tmp)
        exe = directory/'host'
        subprocess.run(command(exe), cwd=ROOT, check=True)
        execute(exe, True, 'control')
        if args.mutate:
            for name, rel, old, new in MUTATIONS:
                source = (ROOT/rel).read_text()
                assert source.count(old) == 1, name
                mutpar.build_in_tree(ROOT, directory/name,
                    {rel: source.replace(old, new, 1)}, [command(exe)])
                execute(exe, False, name)
            subprocess.run(command(exe), cwd=ROOT, check=True)
            execute(exe, True, 'restored')
    return 0

MUTATIONS = [
    ('fifo-site', 'hwdev/gf/fifo.c', '"fifo_hash_busy"', '"wrong_fifo"'),
    ('push-site', 'hwdev/gf/push.c', '"push_reserve"', '"wrong_push"'),
    ('fence-site', 'hwdev/gf/fence.c', '"fence_reference"', '"wrong_fence"'),
    ('push-put', 'hwdev/gf/push.c', 'serial_diag_u32(ch->put);', 'serial_diag_u32(ch->put + 1);'),
    ('push-free', 'hwdev/gf/push.c', 'serial_diag_u32(ch->free);', 'serial_diag_u32(ch->free + 1);'),
    ('fence-seq', 'hwdev/gf/fence.c', 'serial_diag_u32(fence.seq);', 'serial_diag_u32(fence.seq + 1);'),
    ('last-value', 'hwdev/gf/wait_diag.h',
     'serial_puts_polled(" last="); serial_diag_hex(last, 8);',
     'serial_puts_polled(" last="); serial_diag_hex(last ^ 1U, 8);'),
]

if __name__ == '__main__':
    raise SystemExit(main())

def run(ctx):
    return subprocess.call(['python3', '-B', 'tools/tests/test_gf_objwait.py',
                            *(['--mutate'] if ctx.mutate else [])], cwd=ctx.root,
                           env=dict(os.environ, HOST32_RUNNERS=' '.join(ctx.runners)), close_fds=False)
