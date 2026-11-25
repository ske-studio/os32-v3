"""A11 real API/deadline hooks; no libc, explicit ILP32 runner."""
CHECK = dict(name='hostsvc-link-api', tier=1, needs=[], budget=3.6,
    covers=['sys/hal/time/tick_clock.h', 'sys/kern/irq/irq.h', 'sys/kern/irq/irq_math.h',
            'sys/include/platform_irq.h', 'sys/platform/pc98/platform_irq_limits.h', 'tools/tests/link_platform_fake/platform_link_inline.h', 'sys/net/nic.h', 'sys/hal/net/link_platform.h', 'sys/platform/pc98/platform_link_inline.h', 'sys/platform/pc98/link_platform.c', 'sys/net/link.c', 'sys/net/link.h', 'sys/net/link_notify.h', 'sys/net/link_host_api.inc',
            'sys/net/link_rx.inc', 'sys/net/link_selftest.inc', 'lib/le_internal.h',
            'sys/include/types.h', 'sys/include/net_eth.h', 'sys/include/kprintf.h',
            'sys/kern/kstring.h', 'sys/dev/net/ne2000/ne2000.h',
            'sys/kern/irq/tick.h', 'sys/platform/pc98/tick_order.h',
            'sdk/include/os32/os32_kapi_shared.h', 'build/config.mk',
            'tools/tests/kernel_includes.py', 'tools/tests/host32.py',
            'tools/tests/hostsvc_link_api_host.c', 'sys/include/remote_control.h', 'sys/net/host_mailbox.h', 'sdk/include/os32/os32_host_notice.h'],
    env=[],
    basis=['LAN_OPERATOR_DESIGN §5', 'docs/design/net/link_host_api.md 誤りの意味', 'docs/design/net/README.md 誤りの番号', 'docs/design/net/link_host_api.md 容量'],
)
import argparse
from pathlib import Path
import shutil
import subprocess
import tempfile
import host32
from kernel_includes import include_flags
ROOT = Path(__file__).resolve().parents[2]
ANCHOR = 'if ((u32)(link_platform_now() - d->progress_tick) < LINK_HOST_SILENCE_TICKS) continue;'

MUTATIONS = [
    ('    link_tx_kick();\n    LINK_IRQ_RESTORE(f);\n    return hh;',
     '    LINK_IRQ_RESTORE(f);\n    return hh;', 'open immediate TX disabled'),
    ('    e->wsent_total += n;\n    link_tx_kick();',
     '    e->wsent_total += n;', 'write immediate TX disabled'),
    ('tok_eq(req, a0, b0, "SFS")', '0', 'SFS CALL disabled'),
    ('length <= REMOTE_SFS_CALL_MAX', '1', 'SFS CALL limit disabled'),
    (ANCHOR, "if (1) continue;", "deadline disabled"),
    ('tok_eq(req, a0, b0, "os32.clip")', "0", "named clip body disabled"),
    ('tok_eq(req, a0, b0, "os32.print")', "0", "named print body disabled")]

def build(folder, mutation=None):
    folder.mkdir()
    shutil.copytree(ROOT / 'net', folder / 'net')
    api = folder / 'sys/net/link_host_api.inc'
    text = api.read_text()
    if mutation:
        old, new, _ = mutation
        assert text.count(old) == 1
        api.write_text(text.replace(old, new))
    exe = folder / 'probe'
    subprocess.run(['gcc', '-std=gnu11', '-m32', '-O2', '-Wall', '-Wextra', '-Werror',
        '-ffreestanding', '-nostdinc', '-nostdlib', '-static', '-fno-pie', '-no-pie',
        '-fno-stack-protector', '-ffunction-sections', '-fdata-sections',
        '-Wl,--gc-sections', '-I' + str(folder), '-I' + str(ROOT / 'tools/tests/link_platform_fake'), *include_flags(ROOT),
        str(ROOT / 'tools/tests/hostsvc_link_api_host.c'), '-o', str(exe)], check=True)
    return exe

def main():
    p = argparse.ArgumentParser(); p.add_argument('--mutate', action='store_true')
    args = p.parse_args()
    with tempfile.TemporaryDirectory(prefix='hostsvc-api-') as tmp:
        folder = Path(tmp)
        host32.run([str(build(folder / 'control'))], check=True)
        print('GREEN: eight slots, isolated expiry, progress, wrap, close/reuse, stale, IF')
        if args.mutate:
            for i, mutation in enumerate(MUTATIONS):
                rc = host32.run([str(build(folder / f'mutant{i}', mutation))]).returncode
                assert rc == 1, (mutation[2], rc)
                print('RED runtime:', mutation[2], '(compiled successfully)')
if __name__ == '__main__': main()
def run(ctx):
    return subprocess.call(['python3', '-B', __file__, *(['--mutate'] if ctx.mutate else [])], cwd=ctx.root)
