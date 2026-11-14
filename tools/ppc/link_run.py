#!/usr/bin/env python3
"""P1g mac99 acceptance (7450 and 7455), explicit diagnostic ELF only.

Reuse ppc_lan_host.UDP as the QEMU socket transport. HostAgent runs in this
process, with isolated session state; no physical bridge or daemon is modified.
rc=3 means socket/load prerequisites prevented acceptance, never a PASS.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import select
import shutil
import socket
import subprocess
import sys
import tempfile
import time

sys.dont_write_bytecode = True
ROOT = Path(__file__).resolve().parents[2]
sys.path[:0] = [str(ROOT / 'tools'), str(ROOT / 'tools/ppc')]
from run import ROM, QMP, command, require
from ppc_lan_host import UDP
from host_agent import HostAgent
from host_operator_socket import OperatorSocket

PREFIX = '/home/hight/opt/cross-ppc/bin/powerpc-elf-'

def log_ready(logs, body):
    require([r['seq'] for r in logs] == list(range(len(logs))), 'LOG sequence from zero')
    require(all(r['drop_records'] == r['drop_bytes'] == 0 for r in logs), 'LOG drop')
    start, end = body.find(b'KBOOT OF-CAPTURE'), body.find(b'LINK HEAP used=')
    return 0 <= start < end and b'\n' in body[end:]

def duplicate_requests(lines):
    counts = {}
    for line in lines:
        if not line or line.startswith('> '): continue
        frame = bytes.fromhex(line)
        if len(frame) >= 34 and frame[12:14] == b'\x88\xb5' and frame[14] == 2:
            rid = int.from_bytes(frame[28:32], 'little')
            counts[rid] = counts.get(rid, 0) + 1
    return sum(n - 1 for n in counts.values())

def tick_rate(first, last):
    require(last['tb'] > first['tb'], 'TB did not advance')
    hz = ((last['tick'] - first['tick']) & 0xffffffff) * 100000000 / (last['tb'] - first['tb'])
    require(90 <= hz <= 110, 'link_platform_now outside 90..110Hz')
    return hz

class PumpQMP(QMP):
    def __init__(self, process, deadline, pump):
        self.pump = pump
        super().__init__(process, deadline)
    def read(self):
        while b'\n' not in self.buffer:
            self.pump()
            require(time.monotonic() < self.deadline, 'QMP deadline')
            if select.select([self.process.stdout], [], [], 0.005)[0]:
                data = os.read(self.process.stdout.fileno(), 4096)
                require(data, 'QMP EOF')
                self.buffer += data
        self.pump()
        line, self.buffer = self.buffer.split(b'\n', 1)
        return json.loads(line)

def accept(elf, cpu, parent, timeout, require_ack=False):
    out = Path(tempfile.mkdtemp(prefix=cpu+'-', dir=parent))
    symbols = {p[2]: int(p[0], 16) for line in
               command([PREFIX+'nm', '-n', elf]).splitlines()
               if len(p := line.split()) == 3}
    for name in ('link_hello_ok', 'pm_link_time_ok', 'alive_state',
                 'reached', 'kmalloc_peak_bytes', 'tick_count'):
        require(name in symbols, 'missing link observation '+name)
    cd = out / 'cd'
    cd.mkdir()
    shutil.copyfile(elf, cd / 'PPCKERN.ELF')
    iso = out / 'link.iso'
    command(['genisoimage', '-quiet', '-hfs', '-r', '-part', '-no-desktop',
             '-o', iso, cd])
    reservation = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    io = None
    display = operator = None
    cli_output = None
    try:
        reservation.bind(('127.0.0.1', 0))
        port = reservation.getsockname()[1]
        io = UDP(f'127.0.0.1:0,127.0.0.1:{port}')
        (out / 'agent').mkdir()
        agent = HostAgent(bytes.fromhex('02005e000001'),
                          state_dir=str(out / 'agent'), quiet=True,
                          allow_net=False, clip='none')
        deadline_subscribe = time.monotonic() + 5
        operator = OperatorSocket(('127.0.0.1', 0), agent.operator, lambda: {})
        agent.operator_socket = operator
        cli_output = (out / 'agent-display.log').open('w')
        display = subprocess.Popen([sys.executable, str(ROOT / 'tools/ppc_lan_host.py'),
            '--agent', '127.0.0.1:'+str(operator.listener.getsockname()[1]),
            '--out', str(out / 'logs')], stdout=cli_output, stderr=subprocess.STDOUT)
        # Subscribe before boot so the first LOG cannot race the observer.
        while not any(c['log'] for c in operator.clients.values()):
            require(display.poll() is None, 'LOG display exited before subscription')
            require(time.monotonic() < deadline_subscribe, 'LOG subscription deadline')
            for fd in select.select(operator.fileno_list(), [], [], 0.05)[0]:
                operator.on_readable(fd)
            operator.poll(time.monotonic())
        args = ['qemu-system-ppc', '-M', 'mac99', '-cpu', cpu, '-m', '128M',
                '-smp', '1', '-accel', 'tcg', '-bios', str(ROM),
                '-cdrom', str(iso), '-boot', 'd',
                '-prom-env', 'boot-device=cd:,\\PPCKERN.ELF',
                '-prom-env', 'auto-boot?=true', '-g', '800x600x8', '-display', 'none',
                '-serial', 'file:'+str(out / 'serial-a.log'),
                '-serial', 'file:'+str(out / 'serial-b.log'),
                '-monitor', 'none', '-qmp', 'stdio', '-no-reboot',
                '-nic', 'none', '-device',
                'sungem,netdev=n0,mac=52:54:00:12:34:56', '-netdev',
                f'socket,id=n0,udp=127.0.0.1:{io.sock.getsockname()[1]},localaddr=127.0.0.1:{port}']
        (out / 'metadata.json').write_text(json.dumps(dict(
            command=args, cpu=cpu, elf_sha256=hashlib.sha256(elf.read_bytes()).hexdigest()),
            indent=2))
        deadline = time.monotonic() + timeout
        reservation.close()
        with (out / 'qemu.stderr').open('w') as err, (out / 'frames.log').open('w') as frames:
            p = subprocess.Popen(args, stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                                 stderr=err, bufsize=0)
            class Tx:
                def send(self, frame):
                    frames.write('> '+bytes(frame).hex()+'\n')
                    return io.send(frame)
                def __getattr__(self, name):
                    return getattr(io, name)
            tx = Tx()
            try:
                def word(name, offset=0):
                    result = q.call('human-monitor-command', {
                        'command-line': f'x /1wx 0x{symbols[name]+offset:x}'})
                    match = re.search(r':\s*0x([0-9a-fA-F]+)', result)
                    require(match is not None, 'unreadable '+name)
                    return int(match[1], 16)

                def pump():
                    while select.select([io], [], [], 0)[0]:
                        packet = io.receive()
                        frames.write(packet.hex()+'\n')
                        agent.handle(packet, tx)
                    agent.tick(tx)
                    for fd in select.select(operator.fileno_list(), [], [], 0)[0]:
                        operator.on_readable(fd)
                    operator.poll(time.monotonic())
                q = PumpQMP(p, deadline, pump)
                next_observe, samples = 0, []
                while time.monotonic() < deadline:
                    pump()
                    logfiles = list((out / 'logs').glob('*.jsonl'))
                    logs = [json.loads(line) for f in logfiles for line in f.read_text().split('\n')[:-1]]
                    if logs:
                        require(len(logfiles) == 1 and re.fullmatch(r'[0-9a-f]{32}', logfiles[0].stem), 'LOG boot')
                    body = logfiles[0].with_suffix('.log').read_bytes() if logs else b''
                    ready = log_ready(logs, body)
                    if agent.established and time.monotonic() >= next_observe:
                        next_observe = time.monotonic() + 0.5
                        q.call('stop')
                        try:
                            tick = word('tick_count')
                            regs = q.call('human-monitor-command', {'command-line': 'info registers'})
                            tb = re.search(r'TB\s+([0-9a-fA-F]+)\s+(\d+)', regs)
                            require(tb is not None, 'unreadable TB')
                            samples.append(dict(tick=tick, tb=(int(tb[1], 16) << 32) | int(tb[2])))
                            hello, done = word('link_hello_ok'), word('pm_link_time_ok')
                            alive, marks = word('alive_state', 8), word('reached')
                            peak = word('kmalloc_peak_bytes')
                        finally:
                            q.call('cont')
                        measured = samples[-1]['tb'] - samples[0]['tb'] >= 100000000
                        if ready and measured and hello and done and alive and marks & 0x30000 == 0x30000:
                            require((out / 'agent-display.log').stat().st_size > 0, '--agent display empty')
                            hz = tick_rate(samples[0], samples[-1])
                            require(peak < 192*1024, 'heap exhausted')
                            q.call('screendump', {'filename': str(out / 'hello.ppm')})
                            header, dims, maximum, pixels = (out / 'hello.ppm').read_bytes().split(b'\n', 3)
                            require(header == b'P6' and maximum == b'255', 'FB PPM format')
                            width, height = map(int, dims.split())
                            cell = width // 18
                            scale = min(4, cell // 6, min(height, 64) // 14)
                            require(scale > 0, 'FB glyph scale')
                            for slot, column in ((16, 1), (17, 2)):
                                off = 3 * slot * cell
                                on = 3 * (slot * cell + column * scale)
                                require(pixels[off:off+3] != pixels[on:on+3],
                                        'FB HELLO glyph '+str(slot))
                            frames.flush()
                            duplicates = duplicate_requests((out / 'frames.log').read_text().splitlines())
                            require(duplicates == 0, 'duplicate REQ rid')
                            require(not require_ack or agent.counts['ack'] > 0, 'guest ACK missing')
                            (out / 'result.json').write_text(json.dumps(dict(
                                hello=hello, time=done, alive=alive, marks=marks,
                                tick_samples=samples, tick_hz=hz, duplicate_requests=duplicates,
                                require_ack=require_ack, heap_peak=peak, agent_counts=agent.counts, logs=logs, boot=logfiles[0].stem), indent=2))
                            print(f'PASS {cpu}: HELLO/ALIVE/TIME/LOG, FB 16/17; heap_peak={peak}')
                            print('ppc_lan_host.py --agent display: '+str(out / 'agent-display.log'))
                            print('LOG seq=0..%d drop=0; loss after last frame remains unknown' % (len(logs)-1))
                            return
                        last = (hello, done, alive, marks)
                    time.sleep(0.01)
                raise RuntimeError(cpu+': HELLO/ALIVE/TIME/FB deadline; last hello,time,alive,marks='+repr(locals().get('last'))+' agent='+repr(agent.counts))
            finally:
                if p.poll() is None:
                    p.terminate()
                try:
                    p.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    p.kill()
                    p.wait()
    finally:
        if display is not None:
            display.terminate()
            display.wait(timeout=5)
        if operator is not None:
            operator.close()
        if cli_output is not None:
            cli_output.close()
        reservation.close()
        if io:
            io.sock.close()

def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--elf', type=Path, default=ROOT / 'build/out/ppc-kboot/link/PPCKERN.ELF')
    ap.add_argument('--out', type=Path, required=True)
    ap.add_argument('--timeout', type=int, default=60)
    ap.add_argument('--require-ack', action='store_true')
    args = ap.parse_args()
    args.out.mkdir(parents=True, exist_ok=True)
    if os.getloadavg()[0] >= 4:
        print('NOT RUN: load >= 4; retry after load falls')
        return 3
    try:
        for cpu in ('7450', '7455'):
            accept(args.elf.resolve(), cpu, args.out.resolve(), args.timeout, args.require_ack)
    except PermissionError as exc:
        print('NOT RUN: sandbox socket denied: '+str(exc))
        return 3
    return 0

if __name__ == '__main__':
    sys.exit(main())
