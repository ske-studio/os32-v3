#!/usr/bin/env python3
"""OF-loaded core-ready acceptance. Input ELF is never rebuilt or modified.

Observation symbols required of positive images (not extra KBOOT log lines):
  kboot_ready_tb: big-endian u64 TB sampled immediately before emitting READY.
  kboot_tick: big-endian u32 live logical 100 Hz tick_count (physical DEC 1 kHz), including idle.
Both must remain mapped/readable at each QMP stop. H3 must provide these symbols.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import socket
import select
import struct
import subprocess
import sys
import time
import tempfile
sys.dont_write_bytecode = True
from run import ROOT, ROM, QMP, command, require, CriterionFailure
from kboot_mutations import NEGATIVES
sys.path.insert(0, str(ROOT / "tools"))
import ppc_lan_host as lan

PREFIX = str(Path(os.environ.get('PPC_CROSS_DIR', '/home/hight/opt/cross-ppc')) / 'bin/powerpc-elf-')
PATTERNS = [
    r'OF-CAPTURE ram=(\d+) admitted=(\d+) tb_hz=(\d+)',
    r'OF-OFF calls=(\d+)',
    r'MMU-D root=(0x[0-9a-fA-F]+|\d+) hpt=(0x[0-9a-fA-F]+|\d+) bytes=(\d+) ram_bat=0',
    r'MEMORY online=1 heap=1 shm=1',
    r'TRAP dsi=pass isi=pass guard=pass flush=pass',
    r'MPIC sources=(\d+) priority=0 cpu=0 delivery=pass',
    r'CLOCK hz=100 tick=(\d+) us=(\d+)',
    r'READY stage=pre-user',
    r'ALIVE tick_delta=(\d+) tb_us=(\d+) of_calls_delta=0',
]


PRODUCT_QUIET_STAGES = {pattern.partition(' ')[0] for pattern in PATTERNS}

ROOT_PATTERNS = [
    r'ROOT rc=(-?\d+) registered=([01]) bytes=(\d+) staging_pages=(\d+)',
    r'ROOT owner_before=(\d+) owner_after=(\d+) free_before=(\d+) free_after=(\d+)',
]


def validate_root(a, b, complete=False):
    require('KBOOT' not in b, 'serial B contains KBOOT')
    require(not re.search(r'FAIL|FATAL|UNSUPPORTED', a+b), 'forbidden diagnostic')
    complete_lines = a[:a.rfind('\n')+1].splitlines()
    for line in complete_lines:
        if line.startswith('KBOOT ') and line[6:].partition(' ')[0] in PRODUCT_QUIET_STAGES:
            raise CriterionFailure('unexpected product diagnostic: '+line)
    lines = [line for line in complete_lines if line.startswith('KBOOT ROOT')]
    require(len(lines) <= len(ROOT_PATTERNS), 'duplicate ROOT milestone')
    values = []
    for line, pattern in zip(lines, ROOT_PATTERNS):
        match = re.fullmatch('KBOOT '+pattern, line)
        require(match is not None, 'ROOT milestone order/format')
        values.append(tuple(map(int, match.groups())))
    if values:
        rc, registered, size, pages = values[0]
        require(rc == 0 and registered == 1 and size > 0 and pages > 0, 'rd0 registration')
    if len(values) == 2:
        before, after, free_before, free_after = values[1]
        require(after == before - pages + (size + 4095) // 4096 and
                free_after - free_before == pages, 'ROOT page accounting')
        return True
    require(not complete, 'missing ROOT milestone')
    return False


def validate(a, b, complete=False):
    patterns = PATTERNS
    require('KBOOT' not in b, 'serial B contains KBOOT')
    require(not re.search(r'FAIL|FATAL|UNSUPPORTED', a+b), 'forbidden diagnostic')
    lines = [line for line in a[:a.rfind('\n')+1].splitlines() if line.startswith('KBOOT ')]
    require(len(lines) == len(set(lines)), 'duplicate/extra milestone')
    require(len(lines) <= len(patterns), 'duplicate/extra milestone')
    values = []
    for i, line in enumerate(lines):
        match = re.fullmatch('KBOOT '+patterns[i], line)
        require(match is not None, f'milestone {i+1} order/format')
        values.append(match.groups())
    if values:
        ram, admitted, hz = map(int, values[0])
        require(0 < admitted <= ram and hz > 0, 'capture values')
    if len(values) > 2:
        root, hpt, size = values[2]
        require(int(root, 16 if root.startswith('0x') else 10) > 0 and
                int(hpt, 16 if hpt.startswith('0x') else 10) > 0 and int(size) > 0,
                'MMU values')
    if len(values) > 5:
        require(int(values[5][0]) > 0, 'MPIC sources')
    if len(values) >= len(PATTERNS):
        ticks, us = map(int, values[8])
        require(us >= 2000000, 'ALIVE duration')
        require(abs(ticks*10000-us)*100 <= us*5, 'ALIVE tick/TB mismatch')
    if complete:
        require(len(values) == len(patterns), 'missing milestone')
    return len(values) == len(patterns)


def serial(path):
    raw = path.read_bytes() if path.exists() else b''
    return raw.decode('ascii', errors='replace')


def entry_bytes(path):
    data = path.read_bytes()
    if data[:6] != b'\x7fELF\x01\x02':
        raise ValueError('invalid PPC ELF')
    entry, phoff = struct.unpack_from('>II', data, 24)
    size, count = struct.unpack_from('>HH', data, 42)
    for i in range(count):
        kind, off, va, _, length = struct.unpack_from('>IIIII', data, phoff+i*size)
        if kind == 1 and va <= entry and entry+256 <= va+length:
            return entry, data[off+entry-va:off+entry-va+256]
    raise ValueError('entry is not in a file-backed PT_LOAD')


def lan_judge(serial_bytes, capture_bytes, host_lines, elf_path, map_text, boot):
    try:
        text = serial_bytes.decode('ascii')
        if not re.search(r'^LAN STATE state=5 link=1\r?$', text, re.M):
            return 'LAN state missing'
        start = serial_bytes.index(b'KBOOT OF-CAPTURE')
        end = re.search(rb'^KBOOT ALIVE [^\r\n]*\r\n', serial_bytes, re.M).end()
        expected = serial_bytes[start:end]
        if capture_bytes[:len(expected)] != expected:
            return 'LAN capture mismatch'
        if any(re.search(r'gap|duplicate|out of order|rollback', line) for line in host_lines):
            return 'LAN sequence error'
        counts = [re.search(r'drop_records=(\d+) drop_bytes=(\d+)', line) for line in host_lines]
        counts = [m for m in counts if m]
        if not counts or any(int(m[1]) or int(m[2]) for m in counts):
            return 'LAN drops'
        responses = {}
        for line in host_lines:
            m = re.fullmatch(r'response (\d+) OK (.*)', line)
            if m:
                responses[int(m[1])] = m[2]
        if set(responses) != {1, 2, 3, 4, 5}:
            return 'LAN responses'
        status = [int(x) for x in responses[1].removeprefix('u32=').split(',')]
        if len(status) != 17 or status[0] != 5:
            return 'LAN status'
        clock = re.fullmatch(r'tick=\d+ tb_hz=(\d+) tb=\d+ us=\d+', responses[2])
        capture = re.search(r'OF-CAPTURE ram=\d+ admitted=(\d+) tb_hz=(\d+)', text)
        mmu = re.search(r'MMU-D root=\S+ hpt=(\S+) bytes=(\d+)', text)
        pool = re.search(r'^\s*\.bss\.pool\s+(0x[0-9a-fA-F]+)\s+(0x[0-9a-fA-F]+).*gmac_dma\.o', map_text, re.M)
        if not clock or int(clock[1]) != int(capture[2]):
            return 'LAN clock'
        pairs = re.findall(r'base=(0x[0-9a-fA-F]+) size=(0x[0-9a-fA-F]+)', responses[3])
        pairs = [(int(a, 16), int(b, 16)) for a, b in pairs]
        if len(pairs) != 4 or pairs[0] != (0, int(capture[1])) or pairs[2] != (int(mmu[1], 0), int(mmu[2])) or pairs[3] != (int(pool[1], 16), int(pool[2], 16)):
            return 'LAN memory'
        if bytes.fromhex(responses[4]) != boot + bytes.fromhex('525400123456ffffffffffff'):
            return 'LAN diagnostic identity'
        if bytes.fromhex(responses[5]) != entry_bytes(elf_path)[1]:
            return 'LAN peek'
    except (ValueError, IndexError, AttributeError, TypeError, struct.error):
        return 'LAN malformed evidence'
    return None


def link_judge(a, b, logs, payload, replies, elf, mapping, result):
    """Assess recorded link evidence; expectations: PPC_P5_REST_DESIGN §2 K/§5."""
    import link_run
    validate(a.decode('ascii'), b.decode('ascii'), True)
    require(not b, 'link serial B output')
    require(logs and [r['seq'] for r in logs] == list(range(len(logs))), 'link LOG sequence')
    require(all(r['drop_records'] == r['drop_bytes'] == 0 for r in logs), 'link LOG drops')
    require(link_run.log_ready(logs, payload), 'link LOG tail')
    # Compare milestone meaning, allowing transport-specific newline encoding.
    marks = lambda raw: [line for line in raw.decode('ascii').splitlines()
                         if line.startswith('KBOOT ')]
    require(marks(payload) == marks(a), 'link LOG milestones')
    require(result['hello'] and result['alive'], 'link HELLO/ALIVE')
    link_run.tick_rate(result['tick_samples'][0], result['tick_samples'][-1])
    require(set(replies) == {'status', 'clock', 'memory', 'read-diag', 'peek'}, 'link diagnostics')
    # Connection is HELLO/ALIVE, never a PLN1 state word or word count.
    # Stable PHY identity also appears in the READ_DIAG snapshot.
    require(replies['status'] and len(replies['status']) % 4 == 0, 'link status')
    capture = re.search(rb'OF-CAPTURE ram=(\d+) admitted=(\d+) tb_hz=(\d+)', a)
    mmu = re.search(rb'MMU-D root=\S+ hpt=(\S+) bytes=(\d+)', a)
    tick, hz, tb, us = struct.unpack('>IIQQ', replies['clock'])
    require(hz == int(capture[3]) and tb > 0 and us > 0, 'link clock')
    mem = struct.unpack('>8I', replies['memory'])
    pool = re.search(r'^\s*\.bss\.pool\s+(0x[0-9a-fA-F]+)\s+(0x[0-9a-fA-F]+).*gmac_dma\.o', mapping, re.M)
    image = re.search(r'(0x[0-9a-fA-F]+)\s+__kernel_image_start', mapping)
    resident = re.search(r'(0x[0-9a-fA-F]+)\s+__kernel_resident_end', mapping)
    require(pool is not None and image is not None and resident is not None, 'link map evidence')
    require(mem[:2] == (0, int(capture[2])) and mem[2] == int(image[1], 16) and
            mem[2] + mem[3] >= int(resident[1], 16) and mem[2] + mem[3] <= mem[1] and
            mem[4:6] == (int(mmu[1], 0), int(mmu[2])) and
            mem[6:] == (int(pool[1], 16), int(pool[2], 16)), 'link memory')
    # Dynamic READ_DIAG snapshot, not the legacy fixed boot+MAC byte string.
    diag = replies['read-diag']
    require(len(diag) == 128 and diag[72:104] == replies['memory'], 'link read-diag memory')
    require(struct.unpack_from('>I', diag, 108)[0] == hz, 'link read-diag clock')
    require(len(replies['status']) >= 12 and replies['status'][4:12] == diag[4:12],
            'link status identity')
    entry, expected = entry_bytes(elf)
    require(mem[2] <= entry < mem[2]+mem[3] and replies['peek'] == expected, 'link peek')


from link_diag_run import Diagnostics


class LinkDiagnostics(Diagnostics):
    def __init__(self, elf):
        super().__init__()
        self.raw = {}
        # Read the structured diagnostic snapshot and the actual ELF entry.
        self.cases = [('status', 0, None), ('clock', 0, 24), ('memory', 0, 32),
                      ('read-diag 0 128', 0, 128),
                      (f'peek 0x{entry_bytes(elf)[0]:x} 256', 0, 256)]

    def __call__(self, agent, out):
        # Delegate the entire submit/ID/status/ACK lifecycle to Diagnostics.
        # Its take_result result is observed through a forwarding facade.
        owner, operator = self, agent.operator
        class Operator:
            def __getattr__(self, name): return getattr(operator, name)
            def take_result(self):
                result = operator.take_result()
                if result is not None:
                    line, status, length = owner.cases[owner.index]
                    body = result['result']
                    if length is None:
                        owner.cases[owner.index] = (line, status, len(body))
                    owner.raw[line.split()[0]] = body
                return result
        class Agent:
            def __getattr__(self, name): return getattr(agent, name)
            operator = Operator()
        ready = super().__call__(Agent(), out)
        if ready:
            (out/'diagnostic-bytes.json').write_text(json.dumps(
                {k: v.hex() for k, v in self.raw.items()}, indent=2))
        return ready


def check_image(names, link, negative=None):
    require(('link_hello_ok' in names) == link, 'ELF link selection mismatch')
    require(link or 'pm_lan_poll' in names or 'kboot_ready_tb' in names or
            (negative and NEGATIVES[negative].stop in names), 'unknown ELF image')


def link_tx0(path):
    """Count guest PLN1 TX in link_run's complete directional Ethernet log."""
    text = path.read_text(encoding='ascii')
    require(bool(text) and text.endswith('\n'), 'TX0 empty/incomplete capture')
    guest, host, pln1 = 0, 0, 0
    for line in text.splitlines():
        outgoing = line.startswith('> ')
        raw = line[2:] if outgoing else line
        require(re.fullmatch(r'(?:[0-9a-f]{2}){14,}', raw) is not None,
                'TX0 malformed/short frame')
        frame = bytes.fromhex(raw)
        if outgoing:
            host += 1
        else:
            guest += 1
            pln1 += frame[12:14] == b'\x88\xb6'
    require(guest > 0, 'TX0 no guest frames')
    require(pln1 == 0, 'TX0 guest PLN1 transmission')
    return dict(guest_frames=guest, host_frames=host, guest_pln1_tx=pln1)


def link_accept(elf, cpu, timeout, out):
    import link_run
    link_run.PREFIX = PREFIX
    out.mkdir(parents=True, exist_ok=True)
    before = set(out.iterdir())
    names = {p[-1] for line in command([PREFIX+'nm', '-n', elf]).splitlines()
             if len(p := line.split()) == 3}
    check_image(names, True)
    require('pm_link_diag_poll' in names, 'link diagnostic image required (KBOOT_SH=0)')
    diagnostic = LinkDiagnostics(elf)
    link_run.accept(elf, cpu, out, timeout, require_ack=True, diagnostic=diagnostic)
    created = set(out.iterdir()) - before
    require(len(created) == 1, 'link acceptance output directory')
    run = created.pop()
    files = list((run/'logs').glob('*.jsonl'))
    require(len(files) == 1, 'link LOG boot')
    logs = [json.loads(line) for line in files[0].read_text().splitlines()]
    link_judge((run/'serial-a.log').read_bytes(), (run/'serial-b.log').read_bytes(),
               logs, files[0].with_suffix('.log').read_bytes(), diagnostic.raw, elf,
               elf.with_suffix('.map').read_text(), json.loads((run/'result.json').read_text()))
    (run/'tx0.json').write_text(json.dumps(link_tx0(run/'frames.log'), indent=2)+'\n')
    return run


def accept(elf, cpu, timeout, out, negative=None, lan_off=False, link=False):
    out.mkdir(parents=True, exist_ok=True)
    out = Path(tempfile.mkdtemp(prefix=(negative or 'normal')+'-'+cpu+'-', dir=out))
    cd = out/'cd'; cd.mkdir(exist_ok=True)
    shutil.copyfile(elf, cd/'PPCKERN.ELF')
    iso = out/'kboot.iso'
    command(['genisoimage', '-quiet', '-hfs', '-r', '-part', '-no-desktop', '-o', iso, cd])
    a, b = out/'serial-a.log', out/'serial-b.log'
    a.write_bytes(b''); b.write_bytes(b'')
    args = ['qemu-system-ppc', '-M', 'mac99', '-cpu', cpu, '-m', '128M',
            '-smp', '1', '-accel', 'tcg', '-bios', str(ROM), '-cdrom', str(iso),
            '-boot', 'd', '-prom-env', 'boot-device=cd:,\\PPCKERN.ELF',
            '-prom-env', 'auto-boot?=true', '-nographic', '-serial', 'file:'+str(a),
            '-serial', 'file:'+str(b), '-monitor', 'none', '-qmp', 'stdio', '-no-reboot']
    symbols = {m[2]: int(m[0], 16) for line in command([PREFIX+'nm', '-n', elf]).splitlines()
               if len(m := line.split()) == 3}
    check_image(symbols, link, negative)
    contract = NEGATIVES[negative] if negative else None
    has_lan = 'pm_lan_poll' in symbols and not contract
    io = host = reservation = None
    host_lines = []
    skipped = False
    if has_lan and lan_off:
        args += ['-nic', 'none']
    elif has_lan:
        try:
            reservation = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
            reservation.bind(('127.0.0.1', 0))
            port = reservation.getsockname()[1]
            io = lan.UDP(f'127.0.0.1:0,127.0.0.1:{port}')
            host = lan.Host(io, bytes.fromhex('525400123456'), out/'capture', output=host_lines.append)
            args += ['-nic', 'none', '-device', 'sungem,netdev=n0,mac=52:54:00:12:34:56',
                     '-netdev', f'socket,id=n0,udp=127.0.0.1:{io.sock.getsockname()[1]},localaddr=127.0.0.1:{port}']
        except PermissionError:
            skipped = True
            if reservation: reservation.close()
            reservation = None
    if contract and contract.stop not in symbols:
        raise RuntimeError('negative stop symbol missing')
    metadata = dict(command=args, cpu=cpu, elf_sha256=hashlib.sha256(elf.read_bytes()).hexdigest(),
                    rom_sha256=hashlib.sha256(ROM.read_bytes()).hexdigest(),
                    qemu=command(['qemu-system-ppc', '--version']),
                    toolchain=command([PREFIX+'gcc', '--version']))
    (out/'metadata.json').write_text(json.dumps(metadata, indent=2))
    if elf.with_suffix('.map').exists():
        shutil.copyfile(elf.with_suffix('.map'), out/'kernel.map')
    # H3 observation ABI: big-endian u64 READY TB, and live u32 10ms tick.
    if not contract:
        for name in ('kboot_ready_tb', 'kboot_tick'):
            require(name in symbols, 'missing observation symbol '+name)
    q = None; snapshots = []; stable = 0; ready_seen = False
    observed = False; alive_at = None; sent = 0

    def word(name, offset=0):
        result = q.call('human-monitor-command', {
            'command-line': f'x /1wx 0x{symbols[name]+offset:x}'})
        snapshots.append(name+': '+result)
        match = re.search(r':\s*0x([0-9a-fA-F]+)', result)
        if not match:
            raise RuntimeError('QMP missing memory word '+name)
        return int(match[1], 16)

    def running_state(regs):
        address = int(re.search(r'NIP\s+([0-9a-fA-F]+)', regs)[1], 16)
        msr = int(re.search(r'MSR\s+([0-9a-fA-F]+)', regs)[1], 16)
        ins = q.call('human-monitor-command', {'command-line': f'x /1wx 0x{address:x}'})
        halted = address == symbols.get('ppc_kernel_entry_stop') or bool(
            re.search(r':\s*0x48000000\b', ins))
        require(not halted or bool(msr & 0x8000), 'stopped after READY')

    deadline = time.monotonic()+min(timeout, 30 if negative else 60)
    with (out/'qemu.stderr').open('w') as err:
        if reservation: reservation.close()
        p = subprocess.Popen(args, stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=err, bufsize=0)
        try:
            q = QMP(p, deadline)
            while time.monotonic() < deadline:
                aa, bb = serial(a), serial(b)
                done = False if contract else validate(aa, bb)
                if host:
                    while select.select([io], [], [], 0)[0]:
                        host.receive(io.receive())
                    host.expire()
                    if done and alive_at is None: alive_at = time.monotonic()
                    if alive_at and host.boot and not host.pending and sent < 5:
                        requests = ['status', 'clock', 'memory', 'read-diag 128 28',
                                    f'peek 0x{entry_bytes(elf)[0]:x} 256']
                        host.request(requests[sent]); sent += 1
                    if alive_at and time.monotonic()-alive_at >= 5:
                        raise RuntimeError('LAN capture/diagnostic timeout')
                ready_seen |= 'KBOOT READY stage=pre-user\n' in aa.replace('\r', '')
                q.call('stop')
                regs = q.call('human-monitor-command', {'command-line': 'info registers'})
                snapshots.append(regs)
                pc = re.search(r'NIP\s+([0-9a-fA-F]+)', regs)
                if not pc:
                    raise RuntimeError('QMP missing NIP')
                address = int(pc[1], 16)
                # HMP prints the two TB halves in decimal (unlike NIP).
                tb = re.search(r'TB\s+(\d+)\s+(\d+)', regs)
                if not tb:
                    raise RuntimeError('QMP missing TB')
                tb_now = (int(tb[1]) << 32) | int(tb[2])
                if contract:
                    stable = stable+1 if address == symbols[contract.stop] else 0
                    if stable == 2:
                        lines = aa[:aa.rfind('\n')+1].splitlines()
                        require(lines.count(contract.fatal) == 1 and
                                lines.count(contract.reached) == 1 and
                                lines.index(contract.reached) < lines.index(contract.fatal) and
                                aa.count('FATAL') == 1 and not ready_seen and
                                not re.search(r'FAIL|UNSUPPORTED', aa+bb),
                                'negative diagnostic/progress')
                        require('KBOOT' not in bb, 'serial B contains KBOOT')
                        break
                elif ready_seen:
                    running_state(regs)
                    if done and not observed:
                        ready_tb = (word('kboot_ready_tb') << 32) | word('kboot_ready_tb', 4)
                        ticks_before = word('kboot_tick')
                        hz = int(re.search(r'tb_hz=(\d+)', aa)[1])
                        require(tb_now-ready_tb >= 2*hz, 'QMP READY duration')
                        # Observe at least 200 ms of guest TB, independent of TCG speed.
                        # An interrupt handler can temporarily have EE clear: resample it.
                        while True:
                            q.call('cont')
                            require(q.call('query-status')['running'], 'QMP not running')
                            time.sleep(0.05)
                            q.call('stop')
                            later = q.call('human-monitor-command', {'command-line': 'info registers'})
                            snapshots.append(later)
                            running_state(later)
                            halves = re.search(r'TB\s+(\d+)\s+(\d+)', later)
                            later_tb = (int(halves[1]) << 32) | int(halves[2])
                            elapsed = later_tb-tb_now
                            ee = int(re.search(r'MSR\s+([0-9a-fA-F]+)', later)[1], 16) & 0x8000
                            if elapsed >= hz//5 and ee:
                                break
                            require(elapsed < hz, 'interrupts disabled after READY')
                            if time.monotonic() >= deadline:
                                raise RuntimeError('QEMU timeout: continuation window')
                        # Design §2: the 100 Hz tick must *continue*, not merely advance.
                        # Guest TB is the reference (never wall time); 2 ticks of slack
                        # cover the sampling edges of the >=200 ms window.
                        tick_delta = (word('kboot_tick')-ticks_before) & 0xffffffff
                        snapshots.append(f'rate: tick_delta={tick_delta} elapsed={elapsed} hz={hz}')
                        require(abs(tick_delta*hz-elapsed*100)*100 <= elapsed*100*5+2*hz*100, 'QMP tick rate')
                        validate(serial(a), serial(b), True)
                        observed = True
                if observed:
                    if has_lan:
                        require(('LAN OFF code=-2' if lan_off else 'LAN STATE state=5 link=1') in serial(a), 'LAN state missing')
                    if not host:
                        break
                    caps = list((out/'capture').glob('*.log'))
                    raw = a.read_bytes()
                    alive = re.search(rb'^KBOOT ALIVE [^\r\n]*\r\n', raw, re.M)
                    if sent == 5 and not host.pending and caps and alive:
                        expected = raw[raw.index(b'KBOOT OF-CAPTURE'):alive.end()]
                        if len(caps[0].read_bytes()) >= len(expected):
                            reason = lan_judge(raw, caps[0].read_bytes(), host_lines, elf,
                                               elf.with_suffix('.map').read_text(), host.boot)
                            require(reason is None, reason)
                            break
                q.call('cont')
                time.sleep(0.05)
            else:
                raise RuntimeError('QEMU timeout: incomplete acceptance')
            if link and lan_off:
                # Read with the CPU stopped; a sample taken with translation off
                # (OF call, exception entry) cannot see kernel VAs, so retry briefly.
                q.call('stop')
                until = time.monotonic() + 2
                while True:
                    try:
                        hello = word('link_hello_ok'); break
                    except RuntimeError:
                        if time.monotonic() >= until: raise
                        q.call('cont'); time.sleep(0.02); q.call('stop')
                require(hello == 0, 'link connected without NIC')
            q.call('quit')
            p.wait(timeout=2)
            if p.returncode != 0:
                raise RuntimeError(f'QEMU exit rc={p.returncode}')
        finally:
            if host: (out/'host.txt').write_text('\n'.join(host_lines)+'\n')
            if io: io.sock.close()
            (out/'qmp.log').write_text('\nRESTART/RESTOP\n'.join(snapshots))
            if q: q.close()
            if p.poll() is None:
                p.terminate()
                try: p.wait(timeout=2)
                except subprocess.TimeoutExpired: p.kill(); p.wait()
    if skipped:
        (out/'lan-skip').touch()
    return out


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--elf', type=Path, required=True)
    parser.add_argument('--cpu', choices=['7450', '7455', 'both'], default='both')
    parser.add_argument('--timeout', type=float, default=60)
    parser.add_argument('--negative', choices=NEGATIVES)
    parser.add_argument('--lan-off', action='store_true')
    parser.add_argument('--link', action='store_true')
    args = parser.parse_args()
    if args.timeout <= 0: parser.error('timeout must be positive')
    missing = [tool for tool in (PREFIX+'nm', PREFIX+'gcc', 'qemu-system-ppc', 'genisoimage')
               if shutil.which(tool) is None]
    if missing:
        label = 'FAIL' if os.environ.get('OS32_REQUIRE_PPC') == '1' else 'SKIP'
        print(label+': missing PowerPC prerequisites: '+', '.join(missing))
        return 1 if os.environ.get('OS32_REQUIRE_PPC') == '1' else 0
    out = Path(os.environ['TMPDIR'])/'kboot-run'
    skipped = False
    for cpu in (['7450', '7455'] if args.cpu == 'both' else [args.cpu]):
        try:
            if args.link and not args.negative and not args.lan_off:
                result = link_accept(args.elf.resolve(), cpu, args.timeout, out/cpu)
            elif args.link:
                result = accept(args.elf.resolve(), cpu, args.timeout, out/cpu,
                                args.negative, args.lan_off, link=True)
            else:
                result = accept(args.elf.resolve(), cpu, args.timeout, out/cpu, args.negative, args.lan_off)
        except PermissionError as exc:
            label = 'FAIL' if os.environ.get('OS32_REQUIRE_PPC') == '1' else 'SKIP'
            print(label+': PowerPC socket unavailable: '+str(exc))
            return 1 if os.environ.get('OS32_REQUIRE_PPC') == '1' else 0
        skipped |= (result/'lan-skip').exists()
    if skipped:
        print('SKIP LAN socket rc=3', flush=True)
        return 3
    for cpu in (['7450', '7455'] if args.cpu == 'both' else [args.cpu]):
        print('PASS '+cpu, flush=True)
    return 0


if __name__ == '__main__':
    sys.exit(main())
