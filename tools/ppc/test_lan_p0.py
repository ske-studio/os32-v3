#!/usr/bin/env python3
"""Offline OF parser, mutation, and UDP frame tests."""
import argparse
import errno
import os
import signal
import time
from unittest import mock
import re
import importlib.util
import socket
import subprocess
import sys
import tempfile
from pathlib import Path

PRODUCT = Path(__file__).with_name('lan_probe.py')
FIXTURE = Path(__file__).with_name('fixtures') / 'lan_of_sample.txt'


def fixture(p):
    sample = FIXTURE.read_text()
    sections = re.split(r'0 > dev (.+?) (ls|\.properties) *\n', sample)
    replies = {(sections[i], sections[i + 1]): sections[i + 2]
               for i in range(1, len(sections), 3)}
    assert p.children(replies['/', 'ls']) == ['memory@0', 'rom@ff800000', 'nvram@fff04000', 'pci@f2000000', 'uni-n@f8000000']
    assert p.children(replies['/pci@f2000000', 'ls']) == ['mac-io@c', 'usb@d', 'QEMU,VGA@e', 'ethernet@f']
    path = '/pci@f2000000/ethernet@f'
    return ({'path': path, 'properties': p.properties(replies[path, '.properties'])},
            {'path': '/pci@f2000000', 'properties': p.properties(replies['/pci@f2000000', '.properties'])})


MUTATIONS = [
    ('bar', '(r[0] & 0xff) == 0x10 and ', '', 'bar-select', 'BAR0 count'),
    ('range', 'address + size <= (r[1] << 32 | r[2]) + (r[4] << 32 | r[5])', 'True', 'range', 'BAR outside ranges'),
    ('mac', 'if len(mac) != 6:', 'if False:', 'mac', 'MAC length'),
    ('candidates', 'if len(matches) != 1:', 'if len(matches) == 0:', 'candidates', 'GMAC candidate count'),
    ('layout', 'r[3] + r[5] > 0x100000000', 'False', 'layout', 'window bounds'),
    ('union', ' or matches_id(n)', '', 'union', 'GMAC candidate count'),
    ('continuation', "result[key] += ' ' + line.strip()", 'pass', 'continuation', 'continuation'),
    ('window-count', 'if not 0 < len(ranges) <= 8:', 'if not 0 < len(ranges):', 'window-count', 'window count'),
    ('overlap', "raise ValueError('window overlap')", 'pass', 'overlap', 'window overlap'),
    ('high-cell', 'if r[1] or r[4] or not r[5]', 'if not r[5]', 'high-cell', 'window bounds'),
    ('bar-bounds', 'address + size > 0x100000000', 'False', 'bar-bounds', 'BAR bounds'),
    ('phy', "'status': 'deferred'", "'status': 'collected'", 'phy', 'PHY evidence'),
    ('ports', 'for attempt in range(8):', 'for attempt in range(1):', 'ports', 'port retry'),
    ('sigterm', 'signal.signal(signal.SIGTERM, terminate)', 'pass', 'sigterm', 'SIGTERM cleanup'),
    ('mac-invalid', 'if not any(mac) or mac[0] & 1:', 'if False:', 'mac-invalid', 'MAC invalid'),
]


def load(path):
    spec = importlib.util.spec_from_file_location('lan_product', path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def expect_error(label, func):
    try:
        func()
    except ValueError as exc:
        if str(exc) == label:
            return
        raise AssertionError(f'{label}: unexpected {exc}') from exc
    raise AssertionError(label)


def checks(p):
    print('reached=continuation', flush=True)
    assert p.properties('reg  1\n    2')['reg'] == '1 2', 'continuation'
    node, host = fixture(p)
    parent = {'properties': {'#address-cells': '1', '#size-cells': '1'}}
    result = p.decode(p.choose_node([node]), host, parent)
    assert result['bar0'] == {'pci': 0x82200000, 'cpu': 0x82200000, 'size': 0x200000}
    assert result['mac'] == '52:54:00:12:34:56'
    props, hp = node['properties'], host['properties']
    assert props['compatible'] == '"gmac"' and props['vendor-id'] == '106b' and props['device-id'] == '21'
    assert props['interrupts'] == '1' and props['fast-back-to-back'] == '<empty>'
    assert p.cells(props['assigned-addresses']) == [0x02007810, 0, 0x82200000, 0, 0x200000]
    assert len(p.cells(props['reg'])) == 10
    assert hp['compatible'] == '"uni-north"' and p.cells(hp['reg']) == [0xf2000000, 0x02000000]
    assert p.cells(hp['#address-cells']) == [3] and p.cells(hp['#size-cells']) == [2]
    assert p.cells(hp['bus-range']) == [0, 0]
    assert result['ranges_cells'] == [[0x01000000, 0, 0, 0xf2000000, 0, 0x800000], [0x02000000, 0, 0x80000000, 0x80000000, 0, 0x10000000]]
    bad = {'properties': dict(props, **{'assigned-addresses': '02007810 0 1 0'})}
    expect_error('malformed cells', lambda: p.decode(bad, host, parent))
    print('reached=bar-select', flush=True)
    bad = {'properties': dict(props, **{'assigned-addresses': '02007814 0 81000000 0 00100000 02007810 0 82200000 0 00200000'})}
    assert p.decode(bad, host, parent)['bar0']['pci'] == 0x82200000, 'BAR0 count'
    print('reached=range', flush=True)
    bad = {'properties': dict(props, **{'assigned-addresses': '02007810 0 90000000 0 00200000'})}
    expect_error('BAR outside ranges', lambda: p.decode(bad, host, parent))
    print('reached=mac', flush=True)
    bad = {'properties': dict(props, **{'local-mac-address': '-- 5 : 52 54 00 12 34'})}
    expect_error('MAC length', lambda: p.decode(bad, host, parent))
    print('reached=candidates', flush=True)
    expect_error('GMAC candidate count', lambda: p.choose_node([node, node]))
    expect_error('GMAC candidate count', lambda: p.choose_node([]))
    print('reached=continuation', flush=True)
    parsed = p.properties('assigned-addresses  02007810 0 82200000 0 200000\n                02007810 0 82200000 0 200000')
    bad = {'properties': dict(props, **parsed)}
    expect_error('BAR0 count', lambda: p.decode(bad, host, parent))
    expect_error('unparseable property continuation', lambda: p.properties('reg  1\n    invalid!'))
    print('reached=layout', flush=True)
    expect_error('cell layout', lambda: p.decode(node, host, {'properties': {'#address-cells': '2', '#size-cells': '1'}}))
    broken_ranges = p.cells(hp['ranges'])
    broken_ranges[9] = 0xfff00000
    broken = {'properties': dict(hp, ranges=' '.join(f'{v:x}' for v in broken_ranges))}
    expect_error('window bounds', lambda: p.decode(node, broken, parent))
    def host_ranges(rows):
        return {'properties': dict(hp, ranges=' '.join(f'{v:x}' for row in rows for v in row))}
    rows = result['ranges_cells']
    print('reached=window-count', flush=True)
    expect_error('window count', lambda: p.decode(node, host_ranges(rows * 5), parent))
    print('reached=overlap', flush=True)
    expect_error('window overlap', lambda: p.decode(node, host_ranges(rows + [rows[1]]), parent))
    print('reached=high-cell', flush=True)
    high = [row[:] for row in rows]
    high[0][1] = 1
    expect_error('window bounds', lambda: p.decode(node, host_ranges(high), parent))
    print('reached=bar-bounds', flush=True)
    bad = {'properties': dict(props, **{'assigned-addresses': '02007810 0 fff00000 0 00200000'})}
    expect_error('BAR bounds', lambda: p.decode(bad, host, parent))
    print('reached=bar-edge', flush=True)
    bad = {'properties': dict(props, **{'assigned-addresses': '02007810 0 8ff00000 0 00200000'})}
    expect_error('BAR outside ranges', lambda: p.decode(bad, host, parent))
    print('reached=phy', flush=True)
    phy = p.evidence_base()['phy']
    assert phy.get('status') == 'deferred' and phy.get('reason') and set(phy) == {'status', 'reason'}, 'PHY evidence'
    print('reached=ports', flush=True)
    with mock.patch.object(p, 'free_ports', side_effect=[OSError(errno.EADDRINUSE, 'busy'), [1, 2]]) as alloc:
        try:
            ports = p.allocate_ports()
        except OSError as exc:
            raise AssertionError('port retry') from exc
        assert ports == [1, 2] and alloc.call_count == 2, 'port retry'
    with mock.patch.object(p, 'free_ports', side_effect=OSError(errno.EADDRINUSE, 'busy')) as alloc:
        try:
            p.allocate_ports()
            raise AssertionError('port exhaustion')
        except OSError:
            assert alloc.call_count == 8
    with tempfile.TemporaryDirectory() as directory, mock.patch.object(p, 'free_ports', side_effect=OSError(errno.EADDRINUSE, 'busy')), mock.patch.object(p.subprocess, 'check_output', return_value='QEMU'), mock.patch.object(p, 'ROM', PRODUCT):
        args = argparse.Namespace(out=Path(directory), cpu='7450', no_netdev=False)
        assert p.capture(args) == 2, 'port exhaustion rc'
    print('reached=sigterm', flush=True)
    signal_fixture(p)
    print('reached=union', flush=True)
    other = {'properties': {'vendor-id': '0000106b', 'device-id': '00000021'}}
    expect_error('GMAC candidate count', lambda: p.choose_node([node, other]))
    print('reached=mac-invalid', flush=True)
    for value in ('-- 6 : 00 00 00 00 00 00', '-- 6 : ff ff ff ff ff ff'):
        bad = {'properties': dict(props, **{'local-mac-address': value})}
        expect_error('MAC invalid', lambda: p.decode(bad, host, parent))



def signal_fixture(p):
    # Run the real handler and capture cleanup around a stand-in child session.
    code = """import os, subprocess, sys
from pathlib import Path
from unittest import mock
import importlib.util
spec = importlib.util.spec_from_file_location('product', sys.argv[1])
p = importlib.util.module_from_spec(spec); spec.loader.exec_module(p)
real = subprocess.Popen
def spawn(*a, **kw):
    child = real(['sleep', '60'], start_new_session=True, stdout=subprocess.PIPE, stdin=subprocess.PIPE, stderr=subprocess.DEVNULL)
    print(child.pid, flush=True)
    return child
with mock.patch.object(p.subprocess, 'check_output', return_value='QEMU'), mock.patch.object(p, 'ROM', Path(sys.argv[1])), mock.patch.object(p.subprocess, 'Popen', side_effect=spawn):
    sys.argv = ['probe', '--cpu', '7450', '--out', sys.argv[2], '--no-netdev']
    p.main()
"""
    with tempfile.TemporaryDirectory() as directory:
        proc = subprocess.Popen([sys.executable, '-c', code, str(p.__file__), directory], stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
        pid = int(proc.stdout.readline())
        os.kill(proc.pid, signal.SIGTERM)
        proc.communicate(timeout=5)
        try:
            os.killpg(pid, 0)
        except ProcessLookupError:
            return
        finally:
            try:
                os.killpg(pid, signal.SIGKILL)
            except ProcessLookupError:
                pass
        raise AssertionError('SIGTERM cleanup')


def udp():
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as left, socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as right:
        left.bind(('127.0.0.1', 0))
        right.bind(('127.0.0.1', 0))
        right.settimeout(2)
        frame = bytes.fromhex('ffffffffffff52540012345688b6') + bytes(range(46))
        left.sendto(frame, right.getsockname())
        received, sender = right.recvfrom(2048)
        assert received == frame and sender == left.getsockname(), 'UDP frame or endpoint'


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--mutate', action='store_true')
    parser.add_argument('--product', type=Path, default=PRODUCT)
    parser.add_argument('--parser-only', action='store_true')
    args = parser.parse_args()
    checks(load(args.product))
    if args.mutate:
        source = PRODUCT.read_text()
        for name, before, after, marker, diagnostic in MUTATIONS:
            assert source.count(before) == 1, name
            with tempfile.TemporaryDirectory() as directory:
                mutant = Path(directory)/'lan_probe.py'
                mutant.write_text(source.replace(before, after))
                proc = subprocess.run([sys.executable, '-B', __file__, '--parser-only', '--product', str(mutant)], capture_output=True, text=True, timeout=10)
            assert proc.returncode == 1 and proc.stdout.rstrip().endswith('reached=' + marker) and (f'AssertionError: {diagnostic}' in proc.stderr or (name == 'bar' and f'ValueError: {diagnostic}' in proc.stderr)), f'{name}: unexpected result rc={proc.returncode}\n{proc.stdout}{proc.stderr}'
            print(f'{name}: RED ({diagnostic})', flush=True)
    if not args.parser_only:
        try:
            udp()
        except OSError as exc:
            if exc.errno != errno.EPERM:
                raise
            print("SKIP udp-loop (EPERM)", flush=True)
            return 3
    print('PASS lan P0', flush=True)


if __name__ == '__main__':
    sys.exit(main())
