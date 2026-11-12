#!/usr/bin/env python3
"""S-C N01: real USER -> USER exec twice, with the parent's stack intact."""
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
from kboot_run import ROOT, ROM, QMP, command, require, serial, validate, lan
from sb_run import INPUT, serial_accept

CHECK = dict(name='ppc-sc', tier=2, budget=900, needs=[],
             covers=['tools/ppc/sc_run.py', 'userland/ppc/nest.c'],
             inputs=['build/ppc-user.mk', 'build/ppc-kboot.mk',
                     'tools/ppc/sb_run.py', 'tools/ppc/kboot_accept.py',
                     'tools/ppc/kboot_run.py', 'tools/ppc/run.py',
                     'tools/ppc_lan_host.py', 'tools/mkroot_image.py',
                     'platform/powermac/cli_boot.c', 'arch/ppc/exec_context.c',
                     'arch/ppc/kapi_call.c',
                     'userland/ppc/hello.c'])

EXPECTED = ('/bin/nest.bin\n' +
            'hello ok\nnest kind=1 code=7 parent=12345\n' * 2 +
            'exit=5\nos32> ')
BINS = 'KB_ROOT_BINS=' + ' '.join('build/out/ppc-user/'+n+'.bin'
                                for n in ('hello', 'fault', 'nest'))


def lan_accept(elf, cpu, out):
    """Same mac99/UDP terminal transport as S-B; exact N01 transcript."""
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
                if phase == 0 and ready and 'os32> ' in aa and host.boot:
                    validate(aa, bb, True)
                    q.call('eject', {'device': 'bootcd', 'force': True})
                    terminal.open(); phase = 1
                elif phase == 1 and terminal.connected:
                    terminal.send_bytes(b'/bin/nest.bin\n'); phase = 2
                elif phase == 2 and display.getvalue().replace(b'\r', b'').endswith(b'exit=5\nos32> '):
                    actual = display.getvalue().replace(b'\r', b'').decode()
                    # The terminal OPEN redraws the prompt first; compare from there.
                    require(actual.partition('os32> ')[2] == EXPECTED, 'N01 LAN transcript: '+repr(actual))
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
    parser.add_argument('--cpu', choices=['7450', '7455', 'both'], default='both')
    parser.add_argument('--serial', action='store_true')
    parser.add_argument('--mutate', action='store_true')
    args = parser.parse_args()
    require(not args.mutate or args.serial, '--mutate requires --serial')
    cpus = ['7450', '7455'] if args.cpu == 'both' else [args.cpu]
    base = Path(os.environ['TMPDIR'])/'sc-run'; base.mkdir(exist_ok=True)
    work = Path(tempfile.mkdtemp(prefix='run-', dir=base))
    settings = ['KBOOT_SHELL=1', BINS]
    if args.serial:
        source = 'platform/powermac/cli_boot.c'
        text = (ROOT/source).read_text()
        injected = work/'cli_boot.c'
        input_text = INPUT.replace('/bin/hello.bin ok\\n/bin/hello.bin ok\\n/bin/fault.bin\\n/bin/hello.bin ok\\n', '/bin/nest.bin\\n')
        injected.write_text(text.replace('enum { CLI_CAT_BYTES', input_text+'\nenum { CLI_CAT_BYTES')
                            .replace('.getkey = pm_terminal_getkey', '.getkey = sb_getkey'))
        settings += ['KB_MUT_SRC='+source, 'KB_MUT_FILE='+str(injected)]
    elf = build(work/'normal', *settings)
    skipped = False
    for cpu in cpus:
        if args.serial:
            serial_accept(elf, cpu, work/('normal-'+cpu), EXPECTED)
        else:
            skipped |= lan_accept(elf, cpu, work/('lan-'+cpu))
    if args.mutate:
        # Lose only the second result read. First child and final CLI return
        # must still be observed, so unrelated faults/timeouts cannot be RED.
        extra = mutant_settings(work, 'parent-result', 'arch/ppc/kapi_call.c',
                                'return ppc_kapi_invoke(&api, current_slot, values);',
                                'static unsigned reads;\n'
                                '    if (current_slot == (__builtin_offsetof(KernelAPI, exec_last_result) - 8) / 4 && ++reads == 2) return (u32)-1;\n'
                                '    return ppc_kapi_invoke(&api, current_slot, values);')
        extra = [s.replace('KB_MUT_', 'KB_MUT_EXTRA_') for s in extra]
        mutant = build(work/'parent-result', *settings, *extra)
        serial_accept(mutant, cpus[0], work/'parent-result-guest',
                      EXPECTED.replace('nest kind=1 code=7 parent=12345\nexit=5',
                                       'nest kind=0 code=0 parent=12345\nexit=5'))
        print('RED parent-result: exact transcript, sb_stop, QEMU rc=0', flush=True)
    print(('SKIP LAN socket rc=3' if skipped else 'PASS N01')+' evidence='+str(work))
    return 3 if skipped else 0


if __name__ == '__main__':
    raise SystemExit(main())
