#!/usr/bin/env python3
"""Capture an Open Firmware GMAC tree without booting a guest kernel."""
import argparse
import hashlib
import json
import os
import re
import select
import signal
import socket
import subprocess
import sys
import time
from pathlib import Path

ROM = Path('/usr/share/qemu/openbios-ppc')
PROMPT = re.compile(rb'(?:^|[\r\n])\s*\d+ > $')


def properties(text):
    result = {}
    key = None
    for line in text.splitlines():
        match = re.match(r'^([#\w,.-]+)\s{2,}(.*)$', line)
        if match:
            key, value = match.groups()
            result[key] = value.strip()
        elif key and line.startswith(' ' * 20):
            result[key] += ' ' + line.strip()
    return result


def children(text):
    return re.findall(r'^\s*[0-9a-fA-F]+\s+([\w,.-]+@[\w,.-]+)\s*$', text, re.M)


def raw_bytes(value):
    match = re.fullmatch(r'--\s*([0-9a-fA-F]+)\s*:\s*((?:[0-9a-fA-F]{2}\s*)*)', value)
    if match:
        data = bytes.fromhex(match[2])
        if len(data) != int(match[1], 16):
            raise ValueError('property byte count')
        return data
    words = value.split()
    if not words or any(not re.fullmatch(r'[0-9a-fA-F]{1,8}', w) for w in words):
        raise ValueError('not hexadecimal cells')
    return b''.join(int(w, 16).to_bytes(4, 'big') for w in words)


def cells(value):
    data = raw_bytes(value)
    if len(data) % 4:
        raise ValueError('unaligned cells')
    return [int.from_bytes(data[i:i+4], 'big') for i in range(0, len(data), 4)]


def records(values, width):
    if len(values) % width:
        raise ValueError('malformed cells')
    return [values[i:i+width] for i in range(0, len(values), width)]


def choose_node(nodes):
    matches = [n for n in nodes if 'gmac' in n['properties'].get('compatible', '').strip('"').split('\x00')]
    if not matches:
        matches = [n for n in nodes if n['properties'].get('vendor-id') == '106b' and n['properties'].get('device-id') in ('21', '0021')]
    if len(matches) != 1:
        raise ValueError('GMAC candidate count')
    return matches[0]


def decode(node, host):
    p, h = node['properties'], host['properties']
    mac = raw_bytes(p.get('local-mac-address', p.get('mac-address', '')))
    if len(mac) != 6:
        raise ValueError('MAC length')
    reg = records(cells(p['reg']), 5)
    assigned = records(cells(p['assigned-addresses']), 5)
    ranges = records(cells(h['ranges']), 6)
    bars = [r for r in assigned if (r[0] & 0xff) == 0x10 and (r[0] & 0x03000000) == 0x02000000]
    if len(bars) != 1:
        raise ValueError('BAR0 count')
    bar = bars[0]
    address, size = (bar[1] << 32) | bar[2], (bar[3] << 32) | bar[4]
    windows = [r for r in ranges if (r[0] & 0x03000000) == (bar[0] & 0x03000000) and
               address >= (r[1] << 32 | r[2]) and address + size <= (r[1] << 32 | r[2]) + (r[4] << 32 | r[5])]
    if len(windows) != 1 or size == 0:
        raise ValueError('BAR outside ranges')
    window = windows[0]
    return {'reg_cells': reg, 'assigned_cells': assigned, 'ranges_cells': ranges,
            'bar0': {'pci': address, 'cpu': window[3] + address - (window[1] << 32 | window[2]), 'size': size},
            'mac': ':'.join(f'{b:02x}' for b in mac)}


def free_ports():
    sockets = []
    try:
        for _ in range(2):
            s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
            sockets.append(s)
            s.bind(('127.0.0.1', 0))
        return [s.getsockname()[1] for s in sockets]
    finally:
        for s in sockets:
            s.close()


def capture(args):
    args.out.mkdir(parents=True, exist_ok=True)
    log = bytearray()
    evidence = {'commands': [], 'phy': {'status': 'deferred', 'reason': 'MIF transactions require MMIO writes; probe is read-only'}}
    process = None
    try:
        version = subprocess.check_output(['qemu-system-ppc', '--version'], timeout=5, text=True).splitlines()[0]
        evidence.update(qemu_version=version, rom_sha256=hashlib.sha256(ROM.read_bytes()).hexdigest())
        network = []
        device = 'sungem,mac=52:54:00:12:34:56'
        if not args.no_netdev:
            a, b = free_ports()
            device = 'sungem,netdev=n0,mac=52:54:00:12:34:56'
            network = ['-netdev', f'socket,id=n0,udp=127.0.0.1:{a},localaddr=127.0.0.1:{b}']
        command = ['qemu-system-ppc', '-M', 'mac99', '-cpu', args.cpu, '-m', '128M', '-smp', '1', '-accel', 'tcg', '-bios', str(ROM), '-nographic', '-prom-env', 'auto-boot?=false', '-nic', 'none', '-device', device, *network, '-serial', 'stdio', '-monitor', 'none', '-no-reboot']
        evidence['argv'] = command
        process = subprocess.Popen(command, stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, start_new_session=True, bufsize=0)
        deadline = time.monotonic() + 45
        def response():
            start = len(log)
            end = min(deadline, time.monotonic() + 8)
            while time.monotonic() < end:
                if not select.select([process.stdout], [], [], max(0, end-time.monotonic()))[0]:
                    break
                data = os.read(process.stdout.fileno(), 4096)
                if not data:
                    raise RuntimeError('QEMU exited: ' + bytes(log[start:]).decode(errors='replace').strip())
                log.extend(data)
                if PROMPT.search(log[start:]):
                    return bytes(log[start:]).decode(errors='replace')
            raise RuntimeError('OF prompt deadline')
        response()
        def execute(command):
            process.stdin.write((command+'\r').encode())
            output = response()
            evidence['commands'].append({'command': command, 'response': output})
            return output
        roots = children(execute('dev / ls'))
        hosts = ['/'+name for name in roots if name.startswith('pci@')]
        nodes = []
        for host_path in hosts:
            names = children(execute('dev ' + host_path + ' ls'))
            if len(nodes) + len(names) > 1024:
                raise RuntimeError('tree limit')
            for path in [host_path] + [host_path + '/' + name for name in names]:
                props = properties(execute('dev ' + path + ' .properties'))
                nodes.append({'path': path, 'properties': props})
        node = choose_node(nodes)
        parent = node['path'].rsplit('/', 1)[0] or '/'
        host = next(n for n in nodes if n['path'] == parent)
        evidence.update(gmac=node, host=host, interpreted=decode(node, host))
        return 0
    except (ValueError, KeyError, StopIteration) as exc:
        evidence['error'] = str(exc)
        print('capture failed: ' + str(exc), file=sys.stderr)
        return 1
    except (OSError, RuntimeError, subprocess.SubprocessError) as exc:
        evidence['error'] = str(exc)
        print('tool failed: ' + str(exc), file=sys.stderr)
        return 2
    finally:
        if process is not None:
            try:
                os.killpg(process.pid, signal.SIGKILL)
            except ProcessLookupError:
                pass
            process.wait()
        (args.out/'serial.log').write_bytes(log)
        (args.out/'lan_probe.json').write_text(json.dumps(evidence, indent=2) + '\n')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--cpu', choices=('7450', '7455'), required=True)
    parser.add_argument('--out', type=Path, required=True)
    parser.add_argument('--no-netdev', action='store_true')
    return capture(parser.parse_args())


if __name__ == '__main__':
    sys.exit(main())
