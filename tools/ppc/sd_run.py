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

from kboot_accept import build, mutant_settings
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


# Link acceptance reuses the existing QMP/RSHELL/HostAgent mechanism. PLN1
# remains the default until the operator transport switch is accepted.
LINK_NONCE = ('fs/serialfs/serialfs_persistent.c',
              'nonce = sfs_crc32(token, sizeof(token));', 'nonce = 0;')


def link_settings(work, mutation=False):
    import link_sh_run as link
    opts = link.settings(work)
    user = next(Path(v.split('=', 1)[1]) for v in opts if v.startswith('PPC_SH_MUT_FILE='))
    text = user.read_text()
    text = link.replace_once(text, '    if (!strcmp(line, "p3-fault"))',
                             '    static int p5_fd = -1;\n    if (!strcmp(line, "p3-fault"))')
    text = link.replace_once(text, '    } else (void)execute_command(line);', r'''    } else if (!strcmp(line, "p5-hold")) {
        p5_fd = g_api->sys_open("/host/rw/f", KAPI_O_RDONLY);
        g_api->kprintf(7, "%s\n", p5_fd >= 0 ? "held" : "open failed");
    } else if (!strcmp(line, "p5-stale")) {
        char c;
        int rc = g_api->sys_read(p5_fd, &c, 1);
        g_api->kprintf(7, "%s\n", rc == OS32_ERR_STALE ? "stale" : "not stale");
        g_api->sys_close(p5_fd);
    } else if (!strcmp(line, "p5-copy")) {
        char buf[4096];
        int src = g_api->sys_open("/host/big", KAPI_O_RDONLY);
        int dst = g_api->sys_open("/host/rw/big2", KAPI_O_WRONLY | KAPI_O_CREAT | KAPI_O_TRUNC);
        int n = -1, ok = src >= 0 && dst >= 0;
        /* Exercise multiple SFS CALLs within one large syscall. */
        while (ok && (n = g_api->sys_read(src, buf, sizeof(buf))) > 0) {
            if (g_api->sys_write(dst, buf, n) != n) ok = 0;
        }
        if (src >= 0) g_api->sys_close(src);
        if (dst >= 0) g_api->sys_close(dst);
        g_api->kprintf(7, "%s\n", ok && n == 0 ? "copied" : "copy failed");
    } else (void)execute_command(line);''')
    user.write_text(text)
    if mutation:
        path, before, after = LINK_NONCE
        copy = work/'nonce.c'
        copy.write_text(link.replace_once((link.ROOT/path).read_text(), before, after))
        opts += ['KB_MUT_THIRD_SRC='+path, 'KB_MUT_THIRD_FILE='+str(copy)]
    return opts


def link_observation(data, expected, label):
    from run import require as criterion
    criterion(data == expected, label)



def link_absence(body, ticks):
    from run import require as criterion
    criterion(b'cat: /host/rw/f: ' in body and 0 < ticks <= 5000, 'bounded absence')


def link_large(calls):
    import serialfs_host as sfs
    from run import require as criterion
    criterion(any(len(req) >= 1030 and sfs.try_frame(req, 0, sfs.LAN_MAX_PAYLOAD)[2].type == sfs.T_WRITE
                  for req, _ in calls), 'large WRITE frame')
    criterion(any(len(reply) == 1044 for _, reply in calls), '1044B READ frame')

def link_accept(elf, cpu, out, mutation='normal'):
    import link_sh_run as link
    from host_agent import HostAgent, hdr_unpack, OP_HELLO
    import serialfs_host as sfs

    class LinkSession(link.Session):
        # icount is only for USB typing; HostFS acceptance uses host-paced time.
        qemu_clock = ()

        def new_agent(self):
            if self.operator: self.operator.close()
            directory = self.out/('agent-'+str(len(self.events))); directory.mkdir()
            self.agent = HostAgent(bytes.fromhex('02005e000001'), state_dir=str(directory),
                quiet=True, allow_net=False, clip='none', file_root=self.root,
                allow_write=['/rw'], peer=bytes.fromhex('525400123456'))
            self.operator = link.OperatorSocket(('127.0.0.1', 0), self.agent.operator, lambda: {})
            self.agent.operator_socket = self.operator
            service = self.agent._service_rshell
            def observed(rid, ent, parts, sink):
                self.events.append(dict(rid=rid, parts=list(parts), body=bytes(ent['got']).hex(), at=time.monotonic()))
                return service(rid, ent, parts, sink)
            self.agent._service_rshell = observed
            handle = self.agent.handle
            def capture(frame, sink):
                if len(frame) >= 42 and hdr_unpack(frame[14:])[0] != OP_HELLO:
                    self.old_frames.append(bytes(frame))
                return handle(frame, sink)
            self.agent.handle = capture
            service_sfs = self.agent._service_sfs
            def gated(rid, ent, sink):
                if not self.drop: return service_sfs(rid, ent, sink)
            self.agent._service_sfs = gated
            backend = self.agent._sfs_backend()
            call = backend.call
            def counted(frame, peer):
                reply = call(frame, peer)
                self.calls.append((bytes(frame), reply))
                return reply
            backend.call = counted

        def stop_guest(self):
            self.q.call('quit'); self.p.wait(timeout=5)
            link.require(self.p.returncode == 0, 'QEMU rc')
            self.q.close(); self.io.sock.close(); self.err.close(); self.frames.close()
            self.q = self.io = self.err = self.frames = None

    out.mkdir()
    root = out/'saved'; root.mkdir(); (root/'rw').mkdir()
    (root/'big').write_bytes(bytes(range(256))*256)
    (root/'ro').write_bytes(b'read only\n')
    s = LinkSession.__new__(LinkSession)
    s.root, s.drop, s.calls, s.old_frames = root, False, [], []
    LinkSession.__init__(s, elf, cpu, out/'boot1', 300)
    try:
        s.start(); s.wait(s.ready, 'boot1 READY', 60)
        target = '/tmp/f' if mutation == 'tmp-target' else '/host/rw/f'
        s.cmd('echo x > '+target, b'')
        if mutation == 'normal': s.cmd('cat /host/rw/f', b'x\n')
        if mutation != 'tmp-target':
            link_observation((root/'rw/f').read_bytes(), b'x\n', 'host bytes')
        if mutation not in ('no-boot-follow', 'first-session'):
            s.cmd('echo t > /tmp/t', b'')
            s.cmd('echo z > /host/ro', b'redirect: cannot open /host/ro\n')
            link_observation((root/'ro').read_bytes(), b'read only\n', 'RO bytes')
            s.cmd('p5-copy', b'copied\n', seconds=90)
            link_observation((root/'rw/big2').read_bytes(), (root/'big').read_bytes(), 'large copy')
            # Frames include CRC, not only payload. Require actual near-limit traffic
            # in both directions; a small-buffer copy cannot satisfy this meter.
            link_large(s.calls)
            if mutation != 'tmp-target':
                s.drop = True; first = s.word('tick_count')
                failed = s.cmd('cat /host/rw/f', seconds=20)['result']
                ticks = (s.word('tick_count')-first) & 0xffffffff
                link_absence(failed, ticks)
                s.drop = False
                s.cmd('cat /host/rw/f', b'x\n')
                # A live guest retains an FD while the host generation changes.
                s.cmd('p5-hold', b'held\n'); generation = s.agent.host_generation
                s.new_agent(); s.wait(s.ready, 'host generation READY', 60)
                link.require(s.agent.host_generation != generation, 'host generation')
                s.cmd('p5-stale', b'stale\n')
                s.cmd('cat /host/rw/f', b'x\n')
        # Retain this exact agent/backend across guest boots. Stop/start matches
        # PLN1 and avoids system_reset terminating -no-reboot QEMU.
        agent, backend = s.agent, s.agent._sfs_backend()
        session = (agent.sess, agent.epoch)
        old = list(s.old_frames)
        old_sfs = [req for req, _ in s.calls if sfs.try_frame(req, 0, sfs.LAN_MAX_PAYLOAD)[2].sid]
        link.require(old and old_sfs, 'captured old requests')
        s.stop_guest(); s.out = out/'boot2'; s.out.mkdir()
        s.deadline = time.monotonic()+180
        s.start()
        s.wait(lambda: s.ready() and (agent.sess, agent.epoch) != session, 'boot2 READY', 60)
        if mutation == 'normal': s.cmd('cat /host/rw/f', b'x\n')
        if mutation == 'tmp-target':
            observed = s.cmd('cat /host/rw/f')['result']
            link_observation(observed, b'> cat /host/rw/f\nx\n\x04', 'restart persistence')
        s.cmd('echo boot2 > /host/rw/f')
        link_observation((root/'rw/f').read_bytes(), b'boot2\n', 'restart write')
        s.cmd('cat /host/rw/f', b'boot2\n')
        s.cmd('cat /tmp/t', b'cat: /tmp/t: No such file or directory\n')
        link.require(s.agent is agent and agent._sfs_backend() is backend, 'retained host')
        before = len(s.calls)
        for frame in old: agent.handle(frame, s)
        link.require(len(s.calls) == before, 'old link request reached backend')
        for req in old_sfs:
            response = backend.call(req, agent.peer)
            if response:
                parsed = sfs.try_frame(response, 0, sfs.LAN_MAX_PAYLOAD)[2]
                link.require(parsed.type == sfs.T_ERR and link.struct.unpack('<i', parsed.payload)[0] == sfs.ERR_STALE,
                             'old SFS request accepted')
        link_observation((root/'rw/f').read_bytes(), b'boot2\n', 'old request write')
        s.cmd('echo alive', b'alive\n'); s.stop_guest()
        print(cpu+' link S-D PASS', flush=True)
    finally:
        s.close()


def link_main(args):
    import link_sh_run as link
    missing = [x for x in (link.cross('gcc'), link.cross('nm'), 'qemu-system-ppc', 'genisoimage') if not shutil.which(x)]
    if not link.ROM.is_file(): missing.append(str(link.ROM))
    if missing:
        print('SKIP link S-D: unavailable '+', '.join(missing))
        return int(os.environ.get('OS32_REQUIRE_PPC') == '1')
    if not args.build_only:
        try:
            for kind in (socket.SOCK_DGRAM, socket.SOCK_STREAM):
                with socket.socket(socket.AF_INET, kind) as probe: probe.bind(('127.0.0.1', 0))
        except OSError as exc:
            print('未 link S-D: AF_INET unavailable: '+str(exc)); return 3
    base = Path(os.environ['TMPDIR'])/'sd-link'; base.mkdir(parents=True, exist_ok=True)
    work = Path(tempfile.mkdtemp(prefix='run-', dir=base)); print('evidence='+str(work), flush=True)
    control = work/'normal'; control.mkdir()
    elf = build(control/'kernel', *link_settings(control))
    link.check_map(elf.with_suffix('.map'))
    cpus = ['7450', '7455'] if args.cpu == 'both' else [args.cpu]
    if not args.build_only:
        for cpu in cpus: link_accept(elf, cpu, work/cpu)
        link_accept(elf, cpus[0], work/'first-session', 'first-session')
    if args.mutate:
        mutant = work/'no-boot-follow'; mutant.mkdir()
        changed = build(mutant/'kernel', *link_settings(mutant, True))
        link.check_map(changed.with_suffix('.map'))
        if not args.build_only:
            for name, image, expected in (('no-boot-follow', changed, 'restart write'),
                                           ('tmp-target', elf, 'restart persistence')):
                try: link_accept(image, cpus[0], work/(name+'-guest'), name)
                except link.CriterionFailure as exc:
                    link.require(str(exc) == expected, 'unexpected mutation verdict '+name+': '+str(exc))
                    print('RED '+name+': '+str(exc), flush=True)
                else: raise RuntimeError('SURVIVED '+name)
    return 0

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--cpu', choices=['7450', '7455', 'both'], default='both')
    parser.add_argument('--mutate', action='store_true')
    parser.add_argument('--link', action='store_true', help='use link-SH; retain PLN1 default')
    parser.add_argument('--build-only', action='store_true', help='build link acceptance copies without sockets')
    args = parser.parse_args()
    if args.link:
        return link_main(args)
    if args.build_only: parser.error('--build-only requires --link')
    try:
        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as probe:
            probe.bind(('127.0.0.1', 0))
    except PermissionError:
        print('SKIP LAN S-D: AF_INET unavailable (normal and mutations)')
        return 3 if os.environ.get('OS32_REQUIRE_PPC') == '1' else 0
    base = Path(os.environ['TMPDIR'])/'sd-run'; base.mkdir(parents=True, exist_ok=True)
    work = Path(tempfile.mkdtemp(prefix='run-', dir=base))
    print('evidence='+str(work), flush=True)
    elf = build(work/'product', 'KBOOT_SH=1', 'KBOOT_LINK=0')
    cpus = ['7450', '7455'] if args.cpu == 'both' else [args.cpu]
    for cpu in cpus: accept(elf, cpu, work/cpu)
    if args.mutate:
        # The guest's per-boot SFS nonce (boot_token CRC32) is the protection
        # now; the host's PLN1 fs.reset is a second one. no-boot-follow removes
        # both: the host keeps boot1 FS state (accept) and this private product
        # copy restarts the nonce at 1 every boot, as before 41728e43.
        follow = work/'no-boot-follow'; follow.mkdir()
        fixed_nonce = build(follow/'kernel', 'KBOOT_SH=1', 'KBOOT_LINK=0', *mutant_settings(
            follow, 'fixed-nonce', 'fs/serialfs/serialfs_persistent.c',
            'nonce = sfs_crc32(token, sizeof(token));',
            'nonce = 0; (void)token;'))
        accept(fixed_nonce, cpus[0], follow/'guest', 'no-boot-follow')
        accept(elf, cpus[0], work/'tmp-target', 'tmp-target')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
