#!/usr/bin/env python3
"""Runtime X11 arbitration, GUI acceptance and resident SFS guards."""
import argparse
import subprocess
import sys
import tempfile
from pathlib import Path
import host32

ROOT = Path(__file__).resolve().parents[2]
CHECK = dict(name='remote-x11', tier=1, needs=[], budget=15,
    covers=[
        'userland/shell/rshell_lan.c',
        'userland/shell/shell.h',
        'include/remote_control.h',
        'include/con_sink.h',
        'net/host_mailbox.h',
        'net/link.h',
        'include/net_eth.h',
        'include/types.h',
        'userland/lib/host/libos32host.h',
        'sdk/include/os32/os32api.h',
        'sdk/include/os32/ls.h',
        'sdk/include/os32/os32_kapi_shared.h',
        'sdk/include/os32/os32_kapi_generated.h',
        'sdk/include/os32/os32_host_notice.h',
        'tools/tests/remote_x10_host.c',
        'tools/tests/test_remote_x10.py',
        'tools/tests/host32.py',
        'userland/shell/rshell.c',
        'userland/shell/serial_watchdog.c',
        'userland/shell/serial_watchdog.h',
        'include/config.h',
        'userland/lib/save/libos32save.h',
        'tools/tests/remote_x11_host.c',
        'tools/tests/test_remote_x11.py',
        'tools/host_agent.py',
        'tools/host_agent_config.py',
        'tools/host_linux.py',
        'tools/host_services.py',
        'tools/host_notify.py',
        'tools/host_rshell.py',
        'tools/host_operator_socket.py',
        'net/link_host_api.inc',
    ])


def guest_test(folder, mutation=None):
    rshell = (ROOT / 'userland/shell/rshell.c').read_text()
    lan = (ROOT / 'userland/shell/rshell_lan.c').read_text()
    if mutation:
        target, old, new = mutation
        text = rshell if target == 'rshell' else lan
        assert text.count(old) == 1, old
        text = text.replace(old, new)
        if target == 'rshell': rshell = text
        else: lan = text
    rshell = rshell.replace('"save/libos32save.h"', '"' + str(ROOT / 'userland/lib/save/libos32save.h') + '"')
    lan = lan.replace('"../../include/', '"' + str(ROOT / 'include') + '/')
    lan = lan.replace('"../lib/host/', '"' + str(ROOT / 'userland/lib/host') + '/')
    (folder / 'rshell.c').write_text(rshell)
    (folder / 'lan.c').write_text(lan)
    fixture = (ROOT / 'tools/tests/remote_x11_host.c').read_text()
    fixture = fixture.replace('"../../userland/shell/rshell.c"', '"rshell.c"')
    fixture = fixture.replace('"remote_x10_host.c"', '"' + str(ROOT / 'tools/tests/remote_x10_host.c') + '"')
    (folder / 'host.c').write_text(fixture)
    exe = folder / 'test'
    subprocess.run(['cc', '-std=gnu11', '-Wall', '-Wextra', '-Werror', '-D__cdecl=',
        '-ffunction-sections', '-fdata-sections', '-Wl,--gc-sections',
        '-I' + str(ROOT / 'sdk/include/os32'), '-I' + str(ROOT / 'userland/shell'),
        '-I' + str(ROOT / 'include'), str(folder / 'host.c'), str(folder / 'lan.c'),
        str(ROOT / 'userland/shell/serial_watchdog.c'), '-o', str(exe)], check=True)
    result = host32.run([str(exe)], capture_output=True, text=True)
    if mutation:
        assert result.returncode == 1 and 'FAIL line' in result.stderr, result.stdout + result.stderr
    else:
        assert result.returncode == 0, result.stdout + result.stderr
        print(result.stdout, end='')


def declaration_test(folder):
    source = (ROOT / 'net/link_host_api.inc').read_text()
    start = source.index('static int tok(')
    end = source.index('/* ======================================================================== */', start)
    code = '#include "remote_control.h"\n' + source[start:end] + r"""
int main(void) {
    const char *good = "RSH ACCEPT boot 00000001 42";
    const char *short_req = "RSH ACCEPT boot 42";
    if (link_decl_len(good, sizeof("RSH ACCEPT boot 00000001 42") - 1) != 42) return 1;
    if (link_decl_len(short_req, sizeof("RSH ACCEPT boot 42") - 1) != 0) return 1;
    return 0;
}
"""
    src, exe = folder / 'decl.c', folder / 'decl'
    src.write_text(code)
    subprocess.run(['cc', '-std=gnu11', '-Wall', '-Wextra', '-Werror',
        '-I' + str(ROOT / 'include'), str(src), '-o', str(exe)], check=True)
    host32.run([str(exe)], check=True)
    print('GREEN X11 guest declaration: ACCEPT WDATA length and short request')


def agent_test():
    sys.path.insert(0, str(ROOT / 'tools'))
    import host_agent
    a = host_agent.HostAgent(b'\x02\0\0\0\0\1', quiet=True)
    out = host_agent.ListSink()
    a._switch_to(dict(sess=1, epoch=1, mac=b'\x02\0\0\0\0\2'))
    boot = 'a' * 32
    a._on_request(1, 0, ('RSH READY %s cui 1 none idle' % boot).encode(), out)
    a._on_release(1, out)
    ident = a.operator.submit('cui', 'gui', None)
    body = b'> gui\nSwitching to GUI shell...\n\x04'
    req = 'RSH ACCEPT %s %s %d' % (boot, ident, len(body))
    a._on_request(2, 0, req.encode(), out)
    assert a.ledger[2]['decl'] == len(body) and not a.operator.pending['result']
    a._on_wdata(2, 1, body, out)
    assert a.ledger[2]['resp'][0] == 200
    assert a.operator.pending['state'] == 'running' and a.operator.take_result() is None
    assert a.operator.pending['result'] == body
    a._on_release(2, out)
    # Wrong echo/id/length cannot authorize the GUI handoff.
    a._on_request(3, 0, ('RSH ACCEPT %s ffffffff %d' % (boot, len(body))).encode(), out)
    a._on_wdata(3, 1, body, out)
    assert a.ledger[3]['resp'][0] == 400
    a._on_release(3, out)
    failure = b'> gui\nos32gui: launch failed (rc=1)\n\x04'
    a._on_request(4, 0, ('RSH OUT %s %s 0 %d' % (boot, ident, len(failure))).encode(), out)
    a._on_wdata(4, 1, failure, out)
    assert a.ledger[4]['resp'][0] == 200
    assert a.operator.take_result()['result'] == failure
    print('GREEN X11 host: ACCEPT WDATA/echo/id checks, acceptance remains pending')


def main():
    p = argparse.ArgumentParser(); p.add_argument('--mutate', action='store_true'); args = p.parse_args()
    with tempfile.TemporaryDirectory(prefix='remote-x11-') as td:
        folder = Path(td); guest_test(folder); declaration_test(folder); agent_test()
        if args.mutate:
            for name, mutation in [
                ('serial read during LAN execution', ('lan', 'accepting = rsh_lan_execute(line);',
                    '(void)g_api->serial_trygetchar(); accepting = rsh_lan_execute(line);')),
                ('GUI starts without acceptance', ('lan', 'if (handoff) {', 'if (handoff || accepting) {')),
                ('UART SFS allowed', ('rshell', 'if (!g_api->sys_is_mounted(RSHELL_HOST_PREFIX)) {', 'if (0) {'))]:
                guest_test(folder, mutation); print('RED runtime:', name)


def run(ctx):
    return subprocess.call([sys.executable, '-B', __file__] + (['--mutate'] if ctx.mutate else []), cwd=ctx.root)


if __name__ == '__main__': main()
