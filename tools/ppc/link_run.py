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
import struct
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

PREFIX = '/home/hight/opt/cross-ppc/bin/powerpc-elf-'


def accept(elf, cpu, parent, timeout):
    out = Path(tempfile.mkdtemp(prefix=cpu+'-', dir=parent))
    symbols = {p[2]: int(p[0], 16) for line in
               command([PREFIX+'nm', '-n', elf]).splitlines()
               if len(p := line.split()) == 3}
    for name in ('link_hello_ok', 'pm_link_time_ok', 'alive_state',
                 'reached', 'kmalloc_peak_bytes'):
        require(name in symbols, 'missing link observation '+name)
    cd = out / 'cd'
    cd.mkdir()
    shutil.copyfile(elf, cd / 'PPCKERN.ELF')
    iso = out / 'link.iso'
    command(['genisoimage', '-quiet', '-hfs', '-r', '-part', '-no-desktop',
             '-o', iso, cd])
    reservation = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    io = None
    try:
        reservation.bind(('127.0.0.1', 0))
        port = reservation.getsockname()[1]
        io = UDP(f'127.0.0.1:0,127.0.0.1:{port}')
        (out / 'agent').mkdir()
        agent = HostAgent(bytes.fromhex('02005e000001'),
                          state_dir=str(out / 'agent'), quiet=True,
                          allow_net=False, clip='none')
        args = ['qemu-system-ppc', '-M', 'mac99', '-cpu', cpu, '-m', '128M',
                '-smp', '1', '-accel', 'tcg', '-bios', str(ROM),
                '-cdrom', str(iso), '-boot', 'd',
                '-prom-env', 'boot-device=cd:,\\PPCKERN.ELF',
                '-prom-env', 'auto-boot?=true', '-nographic',
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
                q = QMP(p, deadline)

                def word(name, offset=0):
                    result = q.call('human-monitor-command', {
                        'command-line': f'x /1wx 0x{symbols[name]+offset:x}'})
                    match = re.search(r':\s*0x([0-9a-fA-F]+)', result)
                    require(match is not None, 'unreadable '+name)
                    return int(match[1], 16)

                while time.monotonic() < deadline:
                    while select.select([io], [], [], 0)[0]:
                        packet = io.receive()
                        frames.write(packet.hex()+'\n')
                        agent.handle(packet, tx)
                    agent.tick(tx)
                    if agent.established:
                        q.call('stop')
                        hello, done = word('link_hello_ok'), word('pm_link_time_ok')
                        alive = word('alive_state', 8)
                        marks = word('reached')
                        if hello and done and alive and marks & 0x30000 == 0x30000:
                            peak = word('kmalloc_peak_bytes')
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
                            (out / 'result.json').write_text(json.dumps(dict(
                                hello=hello, time=done, alive=alive, marks=marks,
                                heap_peak=peak, agent_counts=agent.counts), indent=2))
                            print(f'PASS {cpu}: HELLO/ALIVE/TIME, FB 16/17; heap_peak={peak}')
                            return
                        last = (hello, done, alive, marks)
                        tb = word('pm_link_time_bytes') if 'pm_link_time_bytes' in symbols else None
                        q.call('cont')
                        time.sleep(0.5)
                    time.sleep(0.01)
                raise RuntimeError(cpu+': HELLO/ALIVE/TIME/FB deadline; last hello,time,alive,marks='+repr(locals().get('last'))+' time_bytes='+repr(locals().get('tb'))+' agent='+repr(agent.counts))
            finally:
                if p.poll() is None:
                    p.terminate()
                try:
                    p.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    p.kill()
                    p.wait()
    finally:
        reservation.close()
        if io:
            io.sock.close()


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--elf', type=Path, default=ROOT / 'build/out/ppc-kboot/link/PPCKERN.ELF')
    ap.add_argument('--out', type=Path, required=True)
    ap.add_argument('--timeout', type=int, default=60)
    args = ap.parse_args()
    args.out.mkdir(parents=True, exist_ok=True)
    if os.getloadavg()[0] >= 4:
        print('NOT RUN: load >= 4; retry after load falls')
        return 3
    try:
        for cpu in ('7450', '7455'):
            accept(args.elf.resolve(), cpu, args.out.resolve(), args.timeout)
    except PermissionError as exc:
        print('NOT RUN: sandbox socket denied: '+str(exc))
        return 3
    return 0


if __name__ == '__main__':
    sys.exit(main())
