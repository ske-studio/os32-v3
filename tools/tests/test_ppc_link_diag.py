"""Reserved NOTIFY decoding, diagnostic execution and asynchronous result delivery."""
CHECK = dict(name='ppc-link-diag-host', tier=2, budget=30, needs=[], covers=[
    'platform/powermac/link_diag.c', 'platform/powermac/lan_monitor.c',
    'tools/tests/test_ppc_link_diag.py', 'tools/tests/ppc_link_diag_host.c',
    'platform/powermac/lan_boot.h', 'include/boot_token.h', 'include/remote_control.h',
    'include/types.h', 'include/callconv.h', 'include/net_eth.h', 
    'arch/ppc/kernel_probe.h', 'arch/ppc/ppc_trap.h', 'arch/ppc/ppc_oea.h',
    'lib/kstring.h', 'net/host_mailbox.h', 'net/link.h',
    'sdk/include/os32/os32_host_notice.h', 'sdk/include/os32/os32_kapi_shared.h',
    'tools/tests/host32.py'],
    basis=['LAN_OPERATOR_DESIGN §5'],
)
from pathlib import Path
import subprocess
import tempfile
import host32


def run(ctx):
    adapter = (ctx.root / 'platform/powermac/link_diag.c').read_text()
    monitor = (ctx.root / 'platform/powermac/lan_monitor.c').read_text()
    cases = [('baseline', adapter, monitor)]
    if ctx.mutate:
        for name, source, old, new in [
            ('range', 'monitor', 'if (!range) goto bad_range;',
             'if (!range) range = &d->peek_ok[0];'),
            ('duplicate', 'adapter', 'if (have_id && id <= last_id)',
             'if (have_id && id < last_id)'),
            ('permanent-refusal', 'adapter', '(status >= 400 && status < 500)',
             '(status >= 500 && status < 500)'),
            ('declaration', 'adapter', 'p = number(p, result.payload_bytes);',
             'p = number(p, result.payload_bytes + 1);')]:
            text = adapter if source == 'adapter' else monitor
            assert text.count(old) == 1, old
            changed = text.replace(old, new)
            cases.append((name, changed if source == 'adapter' else adapter,
                          changed if source == 'monitor' else monitor))
    with tempfile.TemporaryDirectory() as td:
        work = Path(td)
        for name, a, m in cases:
            (work / 'link_diag.c').write_text(a)
            (work / 'monitor.c').write_text(m)
            exe = work / 'probe'
            cmd = ['gcc', '-m32', '-DKBOOT_LINK=1', '-std=gnu11', '-Wall', '-Wextra',
                   '-Werror', '-fno-pie', '-fno-builtin', '-fno-stack-protector',
                   '-ffunction-sections', '-fdata-sections', '-nostdlib', '-static',
                   '-no-pie', '-Wl,--gc-sections', '-I'+td]
            cmd += ['-I'+str(ctx.root / p) for p in
                    ['.', 'include', 'platform/powermac', 'arch/ppc', 'sdk/include']]
            subprocess.run(cmd + [str(ctx.root / 'tools/tests/ppc_link_diag_host.c'),
                                  str(work / 'monitor.c'), '-o', str(exe)], check=True)
            rc = host32.run([str(exe)], runner='qemu').returncode
            assert (rc == 0) == (name == 'baseline'), (name, rc)
            print(('GREEN ' if not rc else 'RED runtime ') + name + ' rc='+str(rc))
    return 0
