#!/usr/bin/env python3
"""Check USER fd1 routing against the independent LAN terminal and log."""
import argparse
import io
import json
import json
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
from kboot_run import ROOT, ROM, QMP, command, require, serial, validate_root as validate, lan

CHECK = dict(name='ppc-sh-output', tier=2, budget=900, needs=[],
             covers=['tools/ppc/ppc_sh_output.py', 'userland/ppc/outprobe.c'],
             inputs=['build/targets/ppc-powermac.mk', 'build/ppc-user.mk',
                     'platform/powermac/kboot_console.c', 'platform/powermac/console_stream.c',
                     'fs/vfs_fd.c', 'tools/ppc/kboot_accept.py', 'tools/ppc/kboot_run.py',
                     'tools/ppc/sc_run.py', 'tools/ppc_lan_host.py',
                     'tools/ppc/link_sh_run.py', 'tools/ppc/link_run.py',
                     'tools/host_agent.py',
                     'tools/host_operator_socket.py', 'tools/host_rshell.py',
                     'tools/ppc/usb_run.py', 'userland/ppc/hello.c'])

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


def rsh_command(session, line):
    """Use only the product operator result, within one agent generation."""
    op = session.agent.operator
    generation = (session.agent.host_generation, session.agent.sess, session.agent.epoch)
    start = len(session.events)
    ident = op.submit('cui', line, 'accept', timeout_ms=20000)
    session.wait(lambda: ident in op.results or (op.pending is not None and
                 op.pending['state'] in ('done', 'truncated')), 'RSH result '+line, 25)
    result = op.results.get(ident) or op.take_result()
    require(generation == (session.agent.host_generation, session.agent.sess,
                           session.agent.epoch), 'RSH generation changed')
    events = [e for e in session.events[start:]
              if e['parts'][:2] == ['RSH', 'OUT'] and e['parts'][3] == ident]
    require(len(events) == 1, 'one RSH OUT')
    body = bytes.fromhex(events[0]['body'])
    require(result['state'] == 'done' and result['drop'] == 0 and
            result['result'] == body, 'complete RSH OUT')
    prefix = b'> '+line.encode()+b'\n'
    require(body.startswith(prefix) and body.endswith(b'\x04'), 'RSH framing')
    return body[len(prefix):-1]


def link_accept(elf, cpu, out, mutant=False):
    import link_sh_run as link

    class OutputSession(link.Session):
        def new_agent(self):
            super().new_agent()
            self.logs = []
            # Install the in-process LOG observer before QEMU starts. Retain the
            # original logger and operator publisher, including raw payloads.
            publish = self.operator.publish_log
            def observed(record):
                self.logs.append(dict(record))
                publish(record)
            self.operator.publish_log = observed

        def start(self):
            cd = self.out/'cd'; cd.mkdir()
            shutil.copyfile(self.elf, cd/'PPCKERN.ELF')
            iso = self.out/'boot.iso'
            command(['genisoimage', '-quiet', '-hfs', '-r', '-part', '-no-desktop', '-o', iso, cd])
            with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as reservation:
                reservation.bind(('127.0.0.1', 0)); port = reservation.getsockname()[1]
                self.io = lan.UDP(f'127.0.0.1:0,127.0.0.1:{port}')
            args = ['qemu-system-ppc', '-M', 'mac99', '-cpu', self.cpu, '-m', '128M',
                '-smp', '1', '-accel', 'tcg', '-bios', str(ROM), '-cdrom', str(iso),
                '-boot', 'd', '-prom-env', 'boot-device=cd:,\\PPCKERN.ELF',
                '-prom-env', 'auto-boot?=true', '-nographic',
                '-serial', 'file:'+str(self.out/'serial-a.log'),
                '-serial', 'file:'+str(self.out/'serial-b.log'), '-monitor', 'none',
                '-qmp', 'stdio', '-no-reboot', '-nic', 'none',
                '-device', 'sungem,netdev=n0,mac=52:54:00:12:34:56', '-netdev',
                f'socket,id=n0,udp=127.0.0.1:{self.io.sock.getsockname()[1]},localaddr=127.0.0.1:{port}']
            (self.out/'command.json').write_text(json.dumps(args, indent=2))
            self.err = (self.out/'qemu.stderr').open('w')
            self.frames = (self.out/'frames.log').open('w')
            self.p = subprocess.Popen(args, stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                                      stderr=self.err, bufsize=0)
            self.q = link.PumpQMP(self.p, self.deadline, self.pump)

    s = OutputSession(elf, cpu, out, 120)
    try:
        s.start(); s.wait(s.ready, 'RSH READY', 60)
        actual = rsh_command(s, '/bin/outprobe.bin')
        # sh prints one '\n' after an external program exits with success
        # (userland/shell/sh_exec.inc run_cmd_report), as on x86.
        expected = b'hello ok\nH\n'+(b'' if mutant else b'FD1 PROBE\n')+b'\n'
        require(actual == expected, 'fd1 OUT routing: '+repr(actual))
        alive = rsh_command(s, 'echo alive')
        if mutant:
            # The fd1-log mutant also routes the built-in echo to LOG.
            require(alive == b'', 'subsequent command (mutant): '+repr(alive))
            s.wait(lambda: b'alive\n' in b''.join(r['text'] for r in s.logs), 'alive LOG')
        else:
            require(alive == b'alive\n', 'subsequent command: '+repr(alive))
        s.wait(lambda: b'KBOOT READY stage=pre-user' in
               b''.join(r['text'] for r in s.logs), 'kernel LOG READY')
        if mutant:
            s.wait(lambda: b'FD1 PROBE\n' in b''.join(r['text'] for r in s.logs), 'probe LOG')
        s.settle(1)
        logs = b''.join(r['text'] for r in s.logs)
        require([r['seq'] for r in s.logs] == list(range(len(s.logs))), 'LOG sequence')
        require(len({r['boot'] for r in s.logs}) == 1 and
                all(r['drop_records'] == r['drop_bytes'] == 0 for r in s.logs), 'LOG loss/generation')
        require((b'FD1 PROBE\n' in logs) == mutant, 'fd1 LOG routing')
        require(b'KBOOT READY' not in actual, 'kernel READY in OUT')
        s.wait(lambda: validate(serial(out/'serial-a.log'), serial(out/'serial-b.log')),
               'serial ALIVE', 10)
        s.q.call('quit'); s.p.wait(timeout=5)
        require(s.p.returncode == 0, 'QEMU exit')
    finally:
        (out/'log.json').write_text(json.dumps([
            dict(r, text=r['text'].hex()) for r in s.logs], indent=2))
        s.close()


def link_main(work, mutate):
    bins = 'KB_ROOT_BINS=build/out/ppc-user/link-sh/hello.bin build/out/ppc-user/link-sh/fault.bin build/out/ppc-user/link-sh/outprobe.bin'
    settings = ['KBOOT_SH=1', 'KBOOT_LINK=1', bins]
    elf = build(work/'link-normal', *settings)
    link_accept(elf, '7450', work/'link-normal-guest')
    if mutate:
        mutation = mutant_settings(work, 'link-fd1-log', 'platform/powermac/console_stream.c',
            '    console_write(buf, size, color);', '    (void)color; pm_log_write(buf, size);')
        elf = build(work/'link-fd1-log', *settings, *mutation)
        link_accept(elf, '7450', work/'link-fd1-log-guest', True)
        print('RED fd1-log: OUT missing probe, LOG contains probe; subsequent echo alive')
    print('PASS link fd1 routing evidence='+str(work))
    return 0


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--mutate', action='store_true')
    parser.add_argument('--serial', action='store_true')
    parser.add_argument('--link', action='store_true')
    args = parser.parse_args()
    require(not (args.link and args.serial), '--link and --serial are exclusive')
    import link_sh_run as link
    required = [link.cross(t) for t in ('gcc', 'nm', 'objdump', 'ld', 'objcopy')]
    required += ['qemu-system-ppc', 'genisoimage']
    missing = [p for p in required if not shutil.which(p)]
    if not ROM.is_file(): missing.append(str(ROM))
    if missing:
        print(('FAIL' if os.environ.get('OS32_REQUIRE_PPC') == '1' else 'SKIP')+
              ' ppc-sh-output: unavailable '+', '.join(missing))
        return int(os.environ.get('OS32_REQUIRE_PPC') == '1')
    if args.link:
        try:
            for kind in (socket.SOCK_DGRAM, socket.SOCK_STREAM):
                with socket.socket(socket.AF_INET, kind) as probe:
                    probe.bind(('127.0.0.1', 0))
        except OSError as exc:
            print(('FAIL' if os.environ.get('OS32_REQUIRE_PPC') == '1' else 'SKIP')+
                  ' ppc-sh-output: socket unavailable '+str(exc))
            return int(os.environ.get('OS32_REQUIRE_PPC') == '1')
    base = Path(os.environ['TMPDIR']) / 'sh-output'
    base.mkdir(exist_ok=True)
    work = Path(tempfile.mkdtemp(prefix='run-', dir=base))
    if args.link:
        return link_main(work, args.mutate)
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
    settings = ['KBOOT_SHELL=1', 'KBOOT_LINK=0', bins,
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
        elf = build(work / 'fd1-log', 'KBOOT_LINK=0', *settings, *mutation)
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
