"""Timeout diagnostics through the real F1/F2/F3 composition."""
CHECK = dict(name="gf-objwait-host", tier=1, needs=[], budget=6.0, covers=['tools/tests/recover_gf_dev_stub.c', 'tools/tests/gf_ownerless_fixture.py', 'include/**', 'kdebug/*.h', 'sys/kern/irq/irq.h', 'hwdev/*.h', 'mm/*.h', 'sys/dev/pci/pci.h', 'lib/serial_diag.h', 'hwdev/gf/**', 'sys/include/gfx_hal.h', 'sys/hal/gfx/gfx_internal.h', 'hwdev/gf/accel*', 'hwdev/gf/vram_view.*', 'sys/dev/video/geforce/command.h', 'sys/dev/video/geforce/foundation.h', 'drivers/video/geforce/*', 'tools/tests/gf_accel*', 'tools/tests/gf_command_runtime_host.h', 'tools/tests/test_gf_accel.py', 'tools/ops/gf_accel.py', 'sys/dev/video/geforce/gf_accept.c', 'sys/dev/video/geforce/gf_present.c', 'sys/dev/video/geforce/geforce_module.h', 'sys/dev/video/geforce/production.h', 'userland/gshell/src/**', 'userland/gshell/host/**', 'userland/gshell/build.rs', 'sdk/rust/os32api/**', 'tools/tests/os32api_host.py', 'userland/lib/font/**', 'sys/kern/exec/shell_service.h', 'sdk/include/os32/os32_gui_shared.h', 'sdk/include/os32/os32_kapi_shared.h', 'tools/tests/host32.py', 'tools/tests/kernel_includes.py', 'sys/dev/video/geforce/wait_diag.h', 'lib/serial_diag.h', 'tools/tests/gf_objwait_host.c', 'tools/tests/test_gf_objwait.py', 'tools/tests/mutpar.py', 'sys/arch/x86/video/gf_accept_cpu.c'],
    basis=['GF_FEAT2_DESIGN §6'],
)

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
            str(ROOT/'tools/tests/recover_gf_dev_stub.c'),
            str(ROOT/'sys/dev/video/geforce/accel_copy.c'),
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
    ('fifo-site', 'sys/dev/video/geforce/fifo.c', '"fifo_hash_busy"', '"wrong_fifo"'),
    ('push-site', 'sys/dev/video/geforce/push.c', '"push_reserve"', '"wrong_push"'),
    ('fence-site', 'sys/dev/video/geforce/fence.c', '"fence_reference"', '"wrong_fence"'),
    ('push-put', 'sys/dev/video/geforce/push.c', 'serial_diag_u32(ch->put);', 'serial_diag_u32(ch->put + 1);'),
    ('push-free', 'sys/dev/video/geforce/push.c', 'serial_diag_u32(ch->free);', 'serial_diag_u32(ch->free + 1);'),
    ('fence-seq', 'sys/dev/video/geforce/fence.c', 'serial_diag_u32(fence.seq);', 'serial_diag_u32(fence.seq + 1);'),
    ('last-value', 'sys/dev/video/geforce/wait_diag.h',
     'serial_puts_polled(" last="); serial_diag_hex(last, 8);',
     'serial_puts_polled(" last="); serial_diag_hex(last ^ 1U, 8);'),
]

if __name__ == '__main__':
    raise SystemExit(main())

def run(ctx):
    return subprocess.call(['python3', '-B', 'tools/tests/test_gf_objwait.py',
                            *(['--mutate'] if ctx.mutate else [])], cwd=ctx.root,
                           env=dict(os.environ, HOST32_RUNNERS=' '.join(ctx.runners)), close_fds=False)
