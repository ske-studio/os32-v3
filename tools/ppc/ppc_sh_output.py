#!/usr/bin/env python3
"""Check USER fd1 routing against the independent LAN terminal and log."""
import argparse
import io
import os
from pathlib import Path
import select
import shutil
import socket
import subprocess
import tempfile
import time
from kboot_accept import build, mutant_settings
from sb_run import INPUT, serial_accept
from kboot_run import ROOT, ROM, QMP, command, require, serial, validate, lan

CHECK = dict(name='ppc-sh-output', tier=2, budget=900, needs=[],
             covers=['tools/ppc/ppc_sh_output.py', 'userland/ppc/outprobe.c'],
             inputs=['build/ppc-kboot.mk', 'build/ppc-user.mk',
                     'platform/powermac/kboot_console.c', 'platform/powermac/console_stream.c',
                     'fs/vfs_fd.c', 'tools/ppc/kboot_accept.py', 'tools/ppc/kboot_run.py',
                     'tools/ppc/sc_run.py', 'tools/ppc_lan_host.py'])

def lan_accept(elf, cpu, out, mutant=False):
    """Observe terminal and diagnostic log on separate transports."""
    out.mkdir()
    try:
        reservation = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    except PermissionError:
        return True
    transport = None
    p = q = None
    try:
        reservation.bind(('127.0.0.1', 0))
        port = reservation.getsockname()[1]
        transport = lan.UDP(f'127.0.0.1:0,127.0.0.1:{port}')
        lines = []
        host = lan.Host(transport, bytes.fromhex('525400123456'), out/'capture', output=lines.append)
        display = io.BytesIO()
        terminal = lan.Terminal(host, display=display, diagnostic=lines.append)
        cd = out/'cd'; cd.mkdir()
        shutil.copyfile(elf, cd/'PPCKERN.ELF')
        iso = out/'boot.iso'
        command(['genisoimage', '-quiet', '-hfs', '-r', '-part', '-no-desktop', '-o', iso, cd])
        a, b = out/'serial-a.log', out/'serial-b.log'
        args = ['qemu-system-ppc', '-M', 'mac99', '-cpu', cpu, '-m', '128M', '-smp', '1',
                '-accel', 'tcg', '-bios', str(ROM), '-drive', f'file={iso},media=cdrom,id=bootcd',
                '-boot', 'd', '-prom-env', 'boot-device=cd:,\\PPCKERN.ELF',
                '-prom-env', 'auto-boot?=true', '-nographic', '-serial', 'file:'+str(a),
                '-serial', 'file:'+str(b), '-monitor', 'none', '-qmp', 'stdio', '-no-reboot',
                '-nic', 'none', '-device', 'sungem,netdev=n0,mac=52:54:00:12:34:56',
                '-netdev', f'socket,id=n0,udp=127.0.0.1:{transport.sock.getsockname()[1]},localaddr=127.0.0.1:{port}']
        reservation.close()
        deadline = time.monotonic()+60
        with (out/'qemu.stderr').open('w') as err:
            p = subprocess.Popen(args, stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=err, bufsize=0)
            q = QMP(p, deadline)
            phase = 0
            while time.monotonic() < deadline:
                aa, bb = serial(a), serial(b)
                ready = validate(aa, bb)
                while select.select([transport], [], [], 0)[0]:
                    frame = transport.receive(); host.receive(frame); terminal.receive(frame)
                host.expire()
                if phase == 0 and ready and host.boot:
                    validate(aa, bb, True)
                    q.call('eject', {'device': 'bootcd', 'force': True})
                    terminal.open(); phase = 1
                elif phase == 1 and terminal.connected:
                    terminal.send_bytes(b'/bin/outprobe.bin\n'); phase = 2
                elif phase == 2 and display.getvalue().replace(b'\r', b'').endswith(b'exit=0\nos32> '):
                    actual = display.getvalue().replace(b'\r', b'').decode()
                    # The terminal OPEN redraws the prompt first; compare from there.
                    require('hello ok\nH\n' in actual, 'USER hello/shell_putchar missing')
                    require(('FD1 PROBE\n' in actual) != mutant, 'fd1 stream routing: '+repr(actual))
                    require(('FD1 PROBE\n' in aa) == mutant, 'fd1 log routing')
                    require('KBOOT READY stage=pre-user' in aa and 'KBOOT READY stage=pre-user' not in actual, 'kernel log routing')
                    require(not any('gap' in x or 'timeout' in x for x in lines), 'terminal loss')
                    q.call('quit'); p.wait(timeout=2)
                    require(p.returncode == 0, 'QEMU exit')
                    print(cpu+' LAN: '+repr(actual), flush=True)
                    return False
                time.sleep(0.01)
            raise RuntimeError('N01 LAN timeout: '+repr(display.getvalue()))
    finally:
        reservation.close()
        if q: q.close()
        if p and p.poll() is None:
            p.terminate()
            try: p.wait(timeout=2)
            except subprocess.TimeoutExpired: p.kill(); p.wait()
        if transport: transport.sock.close()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--mutate', action='store_true')
    parser.add_argument('--serial', action='store_true')
    args = parser.parse_args()
    base = Path(os.environ['TMPDIR']) / 'sh-output'
    base.mkdir(exist_ok=True)
    work = Path(tempfile.mkdtemp(prefix='run-', dir=base))
    # Serial is normally a mirror of the terminal; remove only that mirror so
    # serial observes the log while LAN observes the actual terminal stream.
    src = 'platform/powermac/console_stream.c'
    text = (ROOT / src).read_text()
    before = '    (void)pm_escc_write(&serial, bytes, len);'
    require(text.count(before) == 1, 'stream mirror anchor')
    injected = work / 'console_stream.c'
    if args.serial:
        # Mark only the probe's stream write; ordinary CLI output stays intact.
        replacement = ('    if (len == 1 && ((const char *)bytes)[0] == 70)\n'
                       '        (void)pm_escc_write(&serial, "STREAM ", 7);\n' + before)
        injected.write_text(text.replace(before, replacement))
        cli = 'platform/powermac/cli_boot.c'
        cli_text = (ROOT / cli).read_text()
        cli_file = work / 'cli_boot.c'
        commands = INPUT.replace('/bin/hello.bin ok\\n/bin/hello.bin ok\\n/bin/fault.bin\\n/bin/hello.bin ok\\n', '/bin/outprobe.bin\\n')
        cli_file.write_text(cli_text.replace('enum { CLI_CAT_BYTES', commands+'\nenum { CLI_CAT_BYTES')
                            .replace('.getkey = pm_terminal_getkey', '.getkey = sb_getkey'))
    else:
        injected.write_text(text.replace(before, '    (void)serial;'))
    bins = 'KB_ROOT_BINS=build/out/ppc-user/hello.bin build/out/ppc-user/fault.bin build/out/ppc-user/outprobe.bin'
    settings = ['KBOOT_SHELL=1', 'KB_FD1_STREAM=1', bins,
                'KB_MUT_EXTRA_SRC='+src, 'KB_MUT_EXTRA_FILE='+str(injected)]
    if args.serial:
        settings += ['KB_MUT_THIRD_SRC='+cli, 'KB_MUT_THIRD_FILE='+str(cli_file)]
    elf = build(work / 'normal', *settings)
    expected = '/bin/outprobe.bin\nhello ok\nH\nSTREAM FD1 PROBE\nexit=0\nos32> '
    skipped = False
    if args.serial:
        serial_accept(elf, '7450', work / 'normal-guest', expected)
    else:
        skipped = lan_accept(elf, '7450', work / 'normal-guest')
    if args.mutate:
        mutation = mutant_settings(work, 'fd1-log', 'platform/powermac/console_stream.c',
                                   '    console_write(buf, size, color);',
                                   '    (void)color; pm_log_write(buf, size);')
        elf = build(work / 'fd1-log', *settings, *mutation)
        if args.serial:
            serial_accept(elf, '7450', work / 'fd1-log-guest', expected.replace('STREAM FD1 PROBE', 'FD1 PROBE'))
        else:
            skipped |= lan_accept(elf, '7450', work / 'fd1-log-guest', True)
        if not skipped:
            print('RED fd1-log: terminal missing marker, log contains marker')
    print(('SKIP LAN socket rc=3' if skipped else 'PASS fd1 routing')+' evidence='+str(work))
    return 3 if skipped else 0

if __name__ == '__main__':
    raise SystemExit(main())
