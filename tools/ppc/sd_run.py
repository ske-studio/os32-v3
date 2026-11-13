#!/usr/bin/env python3
"""S-D: persistent /host, bounded absence and guest-only restart on mac99."""
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

from kboot_accept import build
from kboot_run import ROM, QMP, command, require, serial, validate, lan

CHECK = dict(name='ppc-sd', tier=2, budget=900, needs=[],
             covers=['tools/ppc/sd_run.py', 'platform/powermac/hostfs*',
                     'tools/ppc_lan_host.py', 'tools/ppc_hostfs.py'],
             inputs=['tools/ppc/sc_run.py', 'tools/ppc/kboot_accept.py',
                     'tools/ppc/kboot_run.py', 'tools/ppc/run.py',
                     'build/ppc-kboot.mk', 'build/ppc-user.mk',
                     'fs/serialfs/*.[ch]', 'tools/lanfs_host.py',
                     'tools/serialfs_host.py'])


def result(text, cmd):
    """Require a complete prompt interval and remove exactly its input echo."""
    text = text.replace('\r', '')
    require(text.endswith('sh> '), 'command did not return to sh>: '+repr(text))
    echo, newline, output = text[:-4].partition('\n')
    require(newline and echo == cmd, 'command echo mismatch: '+repr(text))
    return output


def exact(text, cmd, expected):
    actual = result(text, cmd)
    require(actual == expected, cmd+': expected '+repr(expected)+', got '+repr(actual))


class Session:
    def __init__(self, elf, cpu, out, mutation):
        self.out, self.cpu, self.mutation = out, cpu, mutation
        out.mkdir()
        self.root = out/'saved'; self.root.mkdir()
        (self.root/'big').write_bytes(bytes(range(256))*256)
        self.reservation = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.reservation.bind(('127.0.0.1', 0))
        self.port = self.reservation.getsockname()[1]
        self.transport = lan.UDP(f'127.0.0.1:0,127.0.0.1:{self.port}')
        self.lines = []
        self.previous = b''
        self.host = lan.Host(self.transport, bytes.fromhex('525400123456'),
                             out/'capture', output=self.lines.append,
                             root=self.root, allow_write=['/'])
        self.display = io.BytesIO()
        self.terminal = lan.Terminal(self.host, display=self.display, diagnostic=self.lines.append)
        self.drop = False
        self.saved = None
        self.p = self.q = None
        cd = out/'cd'; cd.mkdir(); shutil.copyfile(elf, cd/'PPCKERN.ELF')
        self.iso = out/'boot.iso'
        command(['genisoimage', '-quiet', '-hfs', '-r', '-part', '-no-desktop', '-o', self.iso, cd])

    def pump(self):
        require(self.p.poll() is None, 'QEMU exited before shell result: '+str(self.p.returncode))
        validate(serial(self.a), serial(self.b))
        while select.select([self.transport], [], [], 0)[0]:
            frame = self.transport.receive()
            try: message = lan.decode(frame[14:])
            except ValueError: continue
            if message['kind'] == lan.FS_REQ:
                if self.saved is None: self.saved = frame
                if self.drop: continue
            self.host.receive(frame)
            self.terminal.receive(frame)
        self.host.expire()
        time.sleep(0.01)

    def wait(self, predicate, seconds, label):
        deadline = time.monotonic()+seconds
        while time.monotonic() < deadline:
            self.pump()
            if predicate(): return
        raise RuntimeError(label+' timeout; terminal='+repr(self.display.getvalue()))

    def start(self, number):
        self.display.seek(0); self.display.truncate()
        self.terminal.connected = False
        self.a, self.b = [self.out/f'boot{number}-serial-{x}.log' for x in ('a', 'b')]
        args = ['qemu-system-ppc', *(['-g', '800x600x8'] if os.environ.get('PPC_FB') == '1' else []), '-M', 'mac99', '-cpu', self.cpu, '-m', '128M', '-smp', '1',
                '-accel', 'tcg', '-bios', str(ROM), '-drive', f'file={self.iso},media=cdrom,id=bootcd',
                '-boot', 'd', '-prom-env', 'boot-device=cd:,\\PPCKERN.ELF',
                '-prom-env', 'auto-boot?=true', *(['-display', 'none'] if os.environ.get('PPC_FB') == '1' else ['-nographic']), '-serial', 'file:'+str(self.a),
                '-serial', 'file:'+str(self.b), '-monitor', 'none', '-qmp', 'stdio', '-no-reboot',
                '-nic', 'none', '-device', 'sungem,netdev=n0,mac=52:54:00:12:34:56',
                '-netdev', f'socket,id=n0,udp=127.0.0.1:{self.transport.sock.getsockname()[1]},localaddr=127.0.0.1:{self.port}']
        self.reservation.close()
        self.err = (self.out/f'boot{number}-qemu.stderr').open('w')
        self.p = subprocess.Popen(args, stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=self.err, bufsize=0)
        self.q = QMP(self.p, time.monotonic()+60)
        before = self.host.boot  # the host keeps boot1 until boot2's BEACON
        self.wait(lambda: validate(serial(self.a), serial(self.b)) and
                  self.host.boot and self.host.boot != before,
                  60, f'boot{number} KBOOT READY/beacon')
        validate(serial(self.a), serial(self.b), True)
        self.q.call('eject', {'device': 'bootcd', 'force': True})
        # Like S-C, open the terminal only after READY and CD eject.
        offset = len(self.display.getvalue())
        self.terminal.open()
        self.wait(lambda: self.terminal.connected and len(self.display.getvalue()) > offset and
                  self.display.getvalue().endswith(b'sh> '),
                  15, f'boot{number} TERM_STATE/sh>')
        require(b'OS32 External Shell Started' in self.display.getvalue(), 'missing USER shell startup')

    def cmd(self, cmd, seconds=10):
        offset = len(self.display.getvalue())
        self.terminal.send_bytes(cmd.encode()+b'\n')
        self.wait(lambda: len(self.display.getvalue()) > offset and
                  self.display.getvalue()[offset:].endswith(b'sh> '), seconds, cmd)
        text = self.display.getvalue()[offset:].decode()
        result(text, cmd)
        return text

    def check(self, cmd, output=''):
        exact(self.cmd(cmd), cmd, output)

    def stop(self):
        self.q.call('quit'); self.p.wait(timeout=2)
        require(self.p.returncode == 0, 'QEMU quit rc='+str(self.p.returncode))
        self.q.close(); self.q = None
        self.err.close()

    def close(self):
        (self.out/'terminal.log').write_bytes(self.previous+self.display.getvalue())
        (self.out/'host.log').write_text('\n'.join(self.lines))
        if self.q: self.q.close()
        if self.p and self.p.poll() is None:
            self.p.terminate()
            try: self.p.wait(timeout=2)
            except subprocess.TimeoutExpired: self.p.kill(); self.p.wait()
        self.transport.sock.close(); self.reservation.close()


def accept(elf, cpu, out, mutation='normal'):
    s = Session(elf, cpu, out, mutation)
    try:
        s.start(1)
        s.check('echo x > '+('/tmp/f' if mutation == 'tmp-target' else '/host/f'))
        # The boundary mutant must reach the second boot; all other A9 checks still run.
        if mutation != 'tmp-target':
            s.check('cat /host/f', 'x\n')
            require((s.root/'f').read_bytes() == b'x\n', 'first host f bytes')
        s.check('echo t > /tmp/t'); s.check('cat /tmp/t', 't\n')
        s.check('echo z > /f', 'redirect: cannot open /f\n')
        s.check('cat /host/big > /host/big2')
        require((s.root/'big2').read_bytes() == (s.root/'big').read_bytes(), '64KiB copy')
        if mutation != 'tmp-target':
            s.check('echo y >> /host/f')
            require((s.root/'f').read_bytes() == b'x\ny\n', 'append host bytes')
        s.check('echo s > /host/g'); s.check('echo ss > /host/g')
        require((s.root/'g').read_bytes() == b'ss\n', 'long overwrite host bytes')
        s.check('echo s > /host/g')
        require((s.root/'g').read_bytes() == b's\n', 'short overwrite truncation')
        listing = result(s.cmd('ls /host'), 'ls /host').split()
        require(set(['big', 'big2', 'g'] + ([] if mutation == 'tmp-target' else ['f'])) <= set(listing), 'host listing')
        if mutation != 'tmp-target':
            s.drop = True
            started = time.monotonic()
            failure = result(s.cmd('cat /host/f', 5), 'cat /host/f')
            elapsed = time.monotonic()-started
            require(failure.startswith('cat: /host/f: ') and failure != 'x\ny\n', 'absence diagnostic: '+repr(failure))
            require(elapsed <= 5, 'FS absence exceeded five RPC deadlines')
            s.check('echo a > /tmp/a'); s.check('cat /tmp/a', 'a\n')
            s.drop = False
            s.check('cat /host/f', 'x\ny\n')
        else: elapsed = 0
        old_boot = s.host.boot
        require(s.saved is not None, 'no first-boot FS_REQ captured')
        (out/'boot1-terminal.log').write_bytes(s.display.getvalue())
        s.previous = s.display.getvalue()
        s.stop()
        if mutation == 'no-boot-follow':
            # The host still adopts boot2 (TERM stays usable) but carries the
            # boot1 FS Server/adapter state and FS nonce across the switch.
            s.host.fs.reset = lambda: None
            fs_nonce = s.host.fs_nonce
            reset_boot = s.host.reset_boot
            def keep_fs():
                reset_boot(); s.host.fs_nonce = fs_nonce
            s.host.reset_boot = keep_fs
        s.start(2)
        readback = result(s.cmd('cat /host/f'), 'cat /host/f')
        if mutation != 'normal':
            require(readback.startswith('cat: /host/f: '), 'mutant survived: '+repr(readback))
            s.check('echo alive', 'alive\n')
            s.stop()
            print('RED '+mutation+': boot2 USER shell, cat diagnostic, alive, QEMU rc=0', flush=True)
            return
        require(readback == 'x\ny\n', 'boot2 persisted cat: '+repr(readback))
        tmp = result(s.cmd('ls /tmp'), 'ls /tmp')
        require(not tmp.startswith('ls:') and 't' not in tmp.split(), 'tmp survived restart: '+repr(tmp))
        require(s.host.boot != old_boot and old_boot in s.host.retired, 'host did not retire old boot')
        # Observe the real execution boundary: a rejected old request must never reach adapter.
        calls = []
        handler = s.host.fs.handle_complete_sfs
        def observed(*args):
            calls.append(args)
            return handler(*args)
        s.host.fs.handle_complete_sfs = observed
        s.host.receive(s.saved)
        require(not calls and s.host.boot != old_boot, 'retired FS_REQ executed')
        s.host.fs.handle_complete_sfs = handler
        s.check('cat /host/f', 'x\ny\n')
        require(not any('output gap' in line or 'terminal timeout' in line for line in s.lines), 'terminal loss')
        s.stop()
        print(f'{cpu} LAN S-D PASS absence={elapsed:.3f}s', flush=True)
    finally:
        s.close()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--cpu', choices=['7450', '7455', 'both'], default='both')
    parser.add_argument('--mutate', action='store_true')
    args = parser.parse_args()
    try:
        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as probe:
            probe.bind(('127.0.0.1', 0))
    except PermissionError:
        print('SKIP LAN S-D: AF_INET unavailable (normal and mutations); rc=0')
        return 0
    base = Path(os.environ['TMPDIR'])/'sd-run'; base.mkdir(parents=True, exist_ok=True)
    work = Path(tempfile.mkdtemp(prefix='run-', dir=base))
    print('evidence='+str(work), flush=True)
    elf = build(work/'product', 'KBOOT_SH=1')
    cpus = ['7450', '7455'] if args.cpu == 'both' else [args.cpu]
    for cpu in cpus: accept(elf, cpu, work/cpu)
    if args.mutate:
        for mutation in ('no-boot-follow', 'tmp-target'):
            accept(elf, cpus[0], work/mutation, mutation)
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
