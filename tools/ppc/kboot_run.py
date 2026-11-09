#!/usr/bin/env python3
"""OF-loaded core-ready acceptance. Input ELF is never rebuilt or modified.

Observation symbols required of positive images (not extra KBOOT log lines):
  kboot_ready_tb: big-endian u64 TB sampled immediately before emitting READY.
  kboot_tick: big-endian u32 live 1 kHz interrupt count, including during idle.
Both must remain mapped/readable at each QMP stop. H3 must provide these symbols.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import time
import tempfile
sys.dont_write_bytecode = True
from run import ROOT, ROM, QMP, command, require, CriterionFailure
from kboot_mutations import NEGATIVES

PREFIX = '/home/hight/opt/cross-ppc/bin/powerpc-elf-'
PATTERNS = [
    r'OF-CAPTURE ram=(\d+) admitted=(\d+) tb_hz=(\d+)',
    r'OF-OFF calls=(\d+)',
    r'MMU-D root=(0x[0-9a-fA-F]+|\d+) hpt=(0x[0-9a-fA-F]+|\d+) bytes=(\d+) ram_bat=0',
    r'MEMORY online=1 heap=1 shm=1',
    r'TRAP dsi=pass isi=pass guard=pass flush=pass',
    r'MPIC sources=(\d+) priority=0 cpu=0 delivery=pass',
    r'CLOCK hz=1000 tick=(\d+) us=(\d+)',
    r'READY stage=pre-user',
    r'ALIVE tick_delta=(\d+) tb_us=(\d+) of_calls_delta=0',
]


def validate(a, b, complete=False):
    require('KBOOT' not in b, 'serial B contains KBOOT')
    require(not re.search(r'FAIL|FATAL|UNSUPPORTED', a+b), 'forbidden diagnostic')
    lines = [line for line in a[:a.rfind('\n')+1].splitlines() if line.startswith('KBOOT ')]
    require(len(lines) == len(set(lines)), 'duplicate/extra milestone')
    require(len(lines) <= len(PATTERNS), 'duplicate/extra milestone')
    values = []
    for i, line in enumerate(lines):
        match = re.fullmatch('KBOOT '+PATTERNS[i], line)
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
    if len(values) == 9:
        ticks, us = map(int, values[8])
        require(us >= 2000000, 'ALIVE duration')
        require(abs(ticks*1000-us)*100 <= us*5, 'ALIVE tick/TB mismatch')
    if complete:
        require(len(values) == 9, 'missing milestone')
    return len(values) == 9


def serial(path):
    raw = path.read_bytes() if path.exists() else b''
    return raw.decode('ascii', errors='replace')


def accept(elf, cpu, timeout, out, negative=None):
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
    contract = NEGATIVES[negative] if negative else None
    if contract and contract.stop not in symbols:
        raise RuntimeError('negative stop symbol missing')
    metadata = dict(command=args, cpu=cpu, elf_sha256=hashlib.sha256(elf.read_bytes()).hexdigest(),
                    rom_sha256=hashlib.sha256(ROM.read_bytes()).hexdigest(),
                    qemu=command(['qemu-system-ppc', '--version']),
                    toolchain=command([PREFIX+'gcc', '--version']))
    (out/'metadata.json').write_text(json.dumps(metadata, indent=2))
    if elf.with_suffix('.map').exists():
        shutil.copyfile(elf.with_suffix('.map'), out/'kernel.map')
    # H3 observation ABI: big-endian u64 READY TB, and live u32 millisecond tick.
    if not contract:
        for name in ('kboot_ready_tb', 'kboot_tick'):
            require(name in symbols, 'missing observation symbol '+name)
    q = None; snapshots = []; stable = 0; ready_seen = False

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
        p = subprocess.Popen(args, stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=err, bufsize=0)
        try:
            q = QMP(p, deadline)
            while time.monotonic() < deadline:
                aa, bb = serial(a), serial(b)
                done = False if contract else validate(aa, bb)
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
                                aa.count('FATAL') == 1 and
                                not re.search(r'FAIL|UNSUPPORTED', aa+bb),
                                'negative diagnostic/progress')
                        require('KBOOT' not in bb, 'serial B contains KBOOT')
                        break
                elif ready_seen:
                    running_state(regs)
                    if done:
                        ready_tb = (word('kboot_ready_tb') << 32) | word('kboot_ready_tb', 4)
                        ticks_before = word('kboot_tick')
                        hz = int(re.search(r'tb_hz=(\d+)', aa)[1])
                        require(tb_now-ready_tb >= 2*hz, 'QMP READY duration')
                        # Observe at least 100 ms of guest TB, independent of TCG speed.
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
                            if elapsed >= hz//10 and ee:
                                break
                            require(elapsed < hz, 'interrupts disabled after READY')
                        tick_delta = (word('kboot_tick')-ticks_before) & 0xffffffff
                        require(tick_delta > 0, 'QMP no tick progress')
                        validate(serial(a), serial(b), True)
                        break
                q.call('cont')
                time.sleep(0.05)
            else:
                raise RuntimeError('QEMU timeout: incomplete acceptance')
            q.call('quit')
            p.wait(timeout=2)
            if p.returncode != 0:
                raise RuntimeError(f'QEMU exit rc={p.returncode}')
        finally:
            (out/'qmp.log').write_text('\nRESTART/RESTOP\n'.join(snapshots))
            if q: q.close()
            if p.poll() is None:
                p.terminate()
                try: p.wait(timeout=2)
                except subprocess.TimeoutExpired: p.kill(); p.wait()
    return out


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--elf', type=Path, required=True)
    parser.add_argument('--cpu', choices=['7450', '7455', 'both'], default='both')
    parser.add_argument('--timeout', type=float, default=60)
    parser.add_argument('--negative', choices=NEGATIVES)
    args = parser.parse_args()
    if args.timeout <= 0: parser.error('timeout must be positive')
    out = Path(os.environ['TMPDIR'])/'kboot-run'
    for cpu in (['7450', '7455'] if args.cpu == 'both' else [args.cpu]):
        accept(args.elf.resolve(), cpu, args.timeout, out/cpu, args.negative)
        print('PASS '+cpu, flush=True)


if __name__ == '__main__':
    main()
