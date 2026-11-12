#!/usr/bin/env python3
"""S-C: USER nesting and direct shell acceptance on mac99."""
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
                     'arch/ppc/kapi_call.c', 'platform/powermac/shell_boot.c',
                     'platform/powermac/input_boot.c', 'platform/powermac/input_decode.c',
                     'platform/powermac/console_stream.c', 'platform/powermac/kboot_console.c',
                     'userland/ppc/hello.c'])

EXPECTED = ('/bin/nest.bin\n' +
            'hello ok\nnest kind=1 code=7 parent=12345\n' * 2 +
            'exit=5\nos32> ')
BINS = 'KB_ROOT_BINS=' + ' '.join('build/out/ppc-user/'+n+'.bin'
                                for n in ('hello', 'fault', 'nest'))


def lan_accept(elf, cpu, out, shell=False):
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
                if phase == 0 and ready and (shell or 'os32> ' in aa) and host.boot:
                    validate(aa, bb, True)
                    q.call('eject', {'device': 'bootcd', 'force': True})
                    terminal.open(); phase = 1
                elif phase == 1 and terminal.connected and (not shell or b'sh> ' in display.getvalue()):
                    if shell:
                        require('OS32 External Shell Started' in display.getvalue().decode() and
                                b'sh> ' in display.getvalue(), 'delayed shell startup')
                        terminal.send_bytes(COMMANDS.rsplit(b'exit 3\n', 1)[0])
                    else: terminal.send_bytes(b'/bin/nest.bin\n')
                    phase = 2
                elif shell and phase == 2 and b'pipe-ok\n' in display.getvalue().replace(b'\r', b'') and display.getvalue().endswith(b'sh> '):
                    terminal.send_bytes(b'echo discarded'); phase = 3
                elif shell and phase == 3 and display.getvalue().endswith(b'echo discarded'):
                    terminal.open(); phase = 4
                elif shell and phase == 4 and terminal.connected:
                    terminal.send_bytes(b'\nexit 3\n'); phase = 5
                elif shell and phase == 5 and display.getvalue().count(b'OS32 External Shell Started') == 2 and display.getvalue().endswith(b'sh> '):
                    actual = display.getvalue().decode()
                    check_shell(actual)
                    require('\ndiscarded\n' not in actual.replace('\r', ''), 'reconnect failed to cancel line')
                    require('KBOOT READY' not in actual, 'kernel log leaked to terminal')
                    require(not any('gap' in x or 'timeout' in x for x in lines), 'terminal loss')
                    (out/'terminal.log').write_text(actual)
                    q.call('quit'); p.wait(timeout=2); require(p.returncode == 0, 'QEMU exit')
                    print(cpu+' LAN S-C PASS', flush=True)
                    return False
                elif not shell and phase == 2 and display.getvalue().replace(b'\r', b'').endswith(b'exit=5\nos32> '):
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


def nest_main(args):
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


# The same commands run against the real USER image on both transports.
COMMANDS = (b'help\necho $PATH\necho $HOME\necho $SHELL\npwd\nls /bin\necho sc-ok > /tmp/sc\ncat /tmp/sc\n'
            b'cat /nofile > /tmp/e\ncat /tmp/e\n/bin/hello.bin ok\necho $?\n'
            b'cd /tmp\nset SC_PARENT=kept\n/bin/hello.bin ok\necho $?\npwd\necho $SC_PARENT\n'
            b'echo again\n\x1b[A\necho echo script-ok > /tmp/test.sh\n'
            b'source /tmp/test.sh\necho pipe-ok | cat\n'
            b'echo cancel-me\x03\necho edit-x\x7fok\nexit 3\n')


def check_shell(text, fd1=True, restart=True):
    """Check results between prompts, never accepting command echo as output."""
    text = text.replace('\r', '')
    require('OS32 External Shell Started' in text and 'sh> ' in text, 'sh startup')
    # Split at prompts, then discard the echoed input line in each interval.
    results = {}
    for interval in text.split('sh> ')[1:]:
        echo, newline, output = interval.partition('\n')
        if newline:
            results.setdefault(echo, []).append(output)

    def outputs(cmd, expected):
        require(results.get(cmd) == expected,
                'shell output '+repr(cmd)+': '+repr(results.get(cmd)))

    help_output = results.get('help', [''])[0]
    names = {line.strip() for line in help_output.splitlines()}
    registered = set('help ? clear cls tick uptime time exit cp mv rm cat cat2 '
                     'echo ls cd pwd mkdir rmdir env set export unset source . '
                     'ask if goto return'.split())
    require(registered <= names, 'help registered commands')
    require(not names.intersection('ver uname date beep np2 mnt sys pci hdprep filer rshell'.split()),
            'help excluded commands')
    outputs('echo $PATH', ['/bin\n'])
    outputs('echo $HOME', ['/tmp\n'])
    outputs('echo $SHELL', ['/bin/sh.bin\n'])
    outputs('pwd', ['/\n', '/tmp/\n'])
    listing = results.get('ls /bin', [''])[0].upper().split()
    require('SH.BIN' in listing and 'HELLO.BIN' in listing, 'bin listing')
    outputs('echo sc-ok > /tmp/sc', [''])
    outputs('cat /tmp/sc', ['sc-ok\n'] if fd1 else [''])
    diagnostic = results.get('cat /nofile > /tmp/e', [''])[0]
    require('cat: /nofile: No such file or directory\n' == diagnostic,
            'diagnostic missing from terminal')
    outputs('cat /tmp/e', [''])
    outputs('/bin/hello.bin ok', ['hello ok\n', 'hello ok\n'])
    outputs('echo $?', ['7\n', '7\n'])
    outputs('echo $SC_PARENT', ['kept\n'])
    outputs('echo again', ['again\n', 'again\n'])
    outputs('source /tmp/test.sh', ['script-ok\n'])
    outputs('echo pipe-ok | cat', ['pipe-ok\n'])
    require('edit-ok\n' in text and 'sh exited=3\n' in text, 'edit/exit result')
    require(text.count('OS32 External Shell Started') == (2 if restart else 1), 'sh restart')
    require(('sc-ok\n' in text) == fd1, 'fd1 redirect/cat routing')
    require('\ncancel-me\n' not in text, 'cancelled command executed')


def shell_serial(work, cpus, mutate):
    from kboot_run import PREFIX
    import re
    # Substitute the external connection/input boundary; decoder, InputOps,
    # USER syscalls, parser, exec, pipes, redirects and restart are unchanged.
    stop = INPUT[INPUT.index('#include "kprintf.h"'):INPUT.index('static int sb_getkey')]
    boot_src = 'platform/powermac/shell_boot.c'
    boot = (ROOT/boot_src).read_text()
    helper = stop + r'''
static int sc_connected(void)
{
    u32 start = tick_count;
    kprintf(7, "SB READY\n");
    while ((u32)(tick_count - start) < 200) pm_terminal_poll();
    return 1;
}
'''
    boot = boot.replace('static void report', helper+'\nstatic void report')
    boot = boot.replace('pm_term_connected()', 'sc_connected()')
    boot = boot.replace('for (;;) pm_terminal_poll();', 'kprintf(7, "SB DONE\\n"); sb_done();')
    input_src = 'platform/powermac/input_boot.c'
    inp = (ROOT/input_src).read_text()
    literal = ''.join('\\x%02x' % c for c in COMMANDS)
    feeder = stop.replace('void sb_done', 'static void sc_input_done').replace('sb_stop', 'sc_input_stop')
    feeder += '\nstatic int sc_byte(void)\n{\n    static const unsigned char commands[] = "'+literal+'";\n'
    feeder += '''    static u32 pos;
    if (!commands[pos]) { kprintf(7, "SB DONE\\n"); sc_input_done(); }
    return commands[pos++];
}
'''
    inp = inp.replace('static PmInputDecoder', feeder+'\nstatic PmInputDecoder')
    inp = inp.replace('pm_terminal_trygetkey()', 'sc_byte()')
    stream_src = 'platform/powermac/console_stream.c'
    stream = (ROOT/stream_src).read_text()
    # Tag the stream mirror separately from log writes, even on serial.
    stream = stream.replace('(void)pm_escc_write(&serial, bytes, len);',
                            '(void)pm_escc_write(&serial, "~", 1);\n'
                            '    (void)pm_escc_write(&serial, bytes, len);')
    for name in ['normal'] + (['fd1-log', 'no-restart'] if mutate else []):
        settings = ['KBOOT_SH=1']
        for slot, src, text in [('', boot_src, boot), ('EXTRA_', input_src, inp),
                                ('THIRD_', stream_src, stream)]:
            if name == 'no-restart' and src == boot_src:
                anchor = 'if (kind != EXEC_KIND_EXITED) break;'
                require(text.count(anchor) == 1, 'restart anchor')
                text = text.replace(anchor, 'break;')
            file = work/(name+'-'+Path(src).name); file.write_text(text)
            settings += ['KB_MUT_'+slot+'SRC='+src, 'KB_MUT_'+slot+'FILE='+str(file)]
        if name == 'fd1-log':
            settings += ['KB_FD1_STREAM=0']
        elf = build(work/name, *settings)
        symbols = command([PREFIX+'nm', '-n', elf])
        require('resident_cli_run' not in symbols and 'pm_cli_boot' not in symbols, 'CLI in sh image')
        for cpu in cpus if name == 'normal' else cpus[:1]:
            out = work/(name+'-'+cpu); out.mkdir()
            cd = out/'cd'; cd.mkdir(); shutil.copyfile(elf, cd/'PPCKERN.ELF')
            iso = out/'boot.iso'
            command(['genisoimage', '-quiet', '-hfs', '-r', '-part', '-no-desktop', '-o', iso, cd])
            a, b = out/'serial-a.log', out/'serial-b.log'
            args = ['qemu-system-ppc', '-M', 'mac99', '-cpu', cpu, '-m', '128M', '-smp', '1',
                    '-accel', 'tcg', '-bios', str(ROM), '-drive', f'file={iso},media=cdrom,id=bootcd',
                    '-boot', 'd', '-prom-env', 'boot-device=cd:,\\PPCKERN.ELF',
                    '-prom-env', 'auto-boot?=true', '-nographic', '-serial', 'file:'+str(a),
                    '-serial', 'file:'+str(b), '-monitor', 'none', '-qmp', 'stdio', '-no-reboot', '-nic', 'none']
            deadline = time.monotonic()+60
            q = None
            with (out/'qemu.stderr').open('w') as err:
                p = subprocess.Popen(args, stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=err, bufsize=0)
                try:
                    q = QMP(p, deadline); ejected = False
                    while time.monotonic() < deadline:
                        aa, bb = serial(a).replace('\r', ''), serial(b)
                        validate(aa, bb)
                        if not ejected and 'SB READY\n' in aa:
                            require('External Shell Started' not in aa, 'sh started before connection')
                            q.call('eject', {'device': 'bootcd', 'force': True}); ejected = True
                        if 'SB DONE\n' in aa:
                            q.call('stop')
                            regs = q.call('human-monitor-command', {'command-line': 'info registers'})
                            (out/'registers.txt').write_text(regs)
                            pc = re.search(r'NIP\s+([0-9a-fA-F]+)', regs)
                            label = 'sb_stop' if name == 'no-restart' else 'sc_input_stop'
                            address = next(int(l.split()[0], 16) for l in symbols.splitlines() if l.endswith(' '+label))
                            require(pc and int(pc[1], 16) == address, 'unexpected SC stop PC')
                            validate(aa, bb, True); require(ejected, 'CD not ejected')
                            actual = aa.partition('SB READY\n')[2].partition('SB DONE\n')[0]
                            # Every stream chunk begins with ~; fd1 log mutation
                            # loses that marker for the cat result only.
                            require(('~sc-ok' in actual) == (name != 'fd1-log'), 'fd1 stream marker')
                            check_shell(actual.replace('~', ''), restart=name != 'no-restart')
                            q.call('quit'); p.wait(timeout=2); require(p.returncode == 0, 'QEMU exit')
                            print(('GREEN ' if name == 'normal' else 'RED ')+name+' '+cpu+
                                  ': exact stop PC, results, QEMU rc=0', flush=True)
                            break
                        time.sleep(0.01)
                    else: raise RuntimeError('SC serial timeout: '+repr(serial(a)))
                finally:
                    if q: q.close()
                    if p.poll() is None:
                        p.terminate()
                        try: p.wait(timeout=2)
                        except subprocess.TimeoutExpired: p.kill(); p.wait()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--cpu', choices=['7450', '7455', 'both'], default='both')
    parser.add_argument('--serial', action='store_true')
    parser.add_argument('--mutate', action='store_true')
    args = parser.parse_args()
    if args.mutate and not args.serial:
        parser.error('--mutate requires --serial')
    rc = nest_main(args)
    cpus = ['7450', '7455'] if args.cpu == 'both' else [args.cpu]
    work = Path(tempfile.mkdtemp(prefix='shell-', dir=Path(os.environ['TMPDIR'])/'sc-run'))
    shell_serial(work, cpus, args.mutate)
    print('PASS S-C serial evidence='+str(work), flush=True)
    if not args.serial:
        elf = build(work/'product', 'KBOOT_SH=1')
        symbols = command(['/home/hight/opt/cross-ppc/bin/powerpc-elf-nm', elf])
        require('resident_cli_run' not in symbols and 'pm_cli_boot' not in symbols, 'CLI in product')
        for cpu in cpus:
            if lan_accept(elf, cpu, work/('product-'+cpu), shell=True): rc = 3
    return rc


if __name__ == '__main__':
    raise SystemExit(main())
