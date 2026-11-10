#!/usr/bin/env python3
"""Offline OF parser, mutation, and UDP frame tests."""
import argparse
import errno
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
    node, host = fixture(p)
    result = p.decode(p.choose_node([node]), host)
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
    expect_error('malformed cells', lambda: p.decode(bad, host))
    print('reached=bar-select', flush=True)
    bad = {'properties': dict(props, **{'assigned-addresses': '02007814 0 81000000 0 00100000 02007810 0 82200000 0 00200000'})}
    assert p.decode(bad, host)['bar0']['pci'] == 0x82200000, 'BAR0 count'
    print('reached=range', flush=True)
    bad = {'properties': dict(props, **{'assigned-addresses': '02007810 0 90000000 0 00200000'})}
    expect_error('BAR outside ranges', lambda: p.decode(bad, host))
    print('reached=mac', flush=True)
    bad = {'properties': dict(props, **{'local-mac-address': '-- 5 : 52 54 00 12 34'})}
    expect_error('MAC length', lambda: p.decode(bad, host))
    print('reached=candidates', flush=True)
    expect_error('GMAC candidate count', lambda: p.choose_node([node, node]))
    expect_error('GMAC candidate count', lambda: p.choose_node([]))


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
