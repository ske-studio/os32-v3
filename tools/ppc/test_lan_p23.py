#!/usr/bin/env python3
"""Exercise host capture, request matching, and mutation detection."""
import importlib.util
import pathlib
import socket
import subprocess
import sys
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[2]
SOURCE = ROOT / 'tools/ppc_lan_host.py'
CHECK = dict(name='ppc-lan-p23', tier=1, budget=60, needs=[],
             covers=['tools/ppc_lan_host.py'],
             inputs=['tools/ppc/test_lan_p23.py', 'platform/powermac/lan_boot.h',
                     'platform/powermac/lan_wire.c', 'include/types.h', 'lib/kstring.h'])
MUTATIONS = [
    ('foreign-boot', 'if boot != self.boot:', 'if False:', 'H:A9 foreign-boot'),
    ('double-drop', "seq - self.high - 1}')", "seq - self.high - 1 + m['drop_records']}')", 'H:gap double-drop'),
    ('nonce', "m['nonce'] != self.nonce or pending is None", "False or pending is None", 'H:nonce mismatch'),
    ('extra', 'if len(data) != HEADER_BYTES + size:', 'if False:', 'H:extra bytes'),
    ('request-id', 'self.next_id += 1', 'self.next_id += 0', 'H:request-id'),
]


def load(path):
    spec = importlib.util.spec_from_file_location('lan_host', path)
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


class Fake:
    local_mac = bytes.fromhex('525400123401')
    def __init__(self):
        self.sent = []
    def send(self, frame):
        self.sent.append(frame)


def require(condition, label):
    if not condition:
        print(label)
        sys.exit(1)


def exercise(path, directory):
    m = load(path)
    header = (ROOT / 'platform/powermac/lan_boot.h').read_text()
    for name, value in [('MAGIC', m.MAGIC), ('VERSION', m.VERSION), ('ETHERTYPE', m.ETHERTYPE),
                        ('HEADER_BYTES', m.HEADER_BYTES), ('PAYLOAD_MAX', m.PAYLOAD_MAX)]:
        import re
        match = re.search(r'^#define PLN1_' + name + r' (\S+)', header, re.M)
        require(match and int(match.group(1).rstrip('u'), 0) == value, 'H:constants')
    peer = bytes.fromhex('525400123456')
    boot, other, nonce = bytes(range(16)), bytes(range(16, 32)), bytes(range(32, 48))
    fake, messages = Fake(), []
    h = m.Host(fake, peer, directory, nonce=nonce, clock=lambda: 10, output=messages.append)
    def frame(data, source=peer):
        return fake.local_mac + source + b'\x88\xb6' + data
    beacon = m.encode(m.BEACON, boot)
    h.receive(frame(beacon))
    print('reached=A9')
    h.receive(frame(m.encode(m.LOG, other, payload=b'foreign')))
    require(not (pathlib.Path(directory) / (other.hex() + '.log')).exists() and h.high is None, 'H:A9 foreign-boot')
    h.receive(frame(m.encode(m.LOG, boot, payload=b'bad'), b'123456'))
    h.receive(frame(b'short'))
    wrong = bytearray(m.encode(m.LOG, boot, payload=b'bad')); wrong[4] = 2
    h.receive(frame(wrong))
    h.receive(frame(m.encode(m.LOG, boot, payload=b'bad') + b'X'))
    require(h.high is None, 'H:extra bytes')
    driver = pathlib.Path(directory) / 'driver.c'
    driver.write_text("""#include "lan_boot.h"
extern long write(int, const void *, unsigned long);
int main(void) { struct lan_message m = {0}; unsigned char b[400]; unsigned int n, i;
for (i = 0; i < 16; i++) m.boot_id[i] = i; m.type = PLN1_TYPE_LOG; m.seq = 1;
m.payload_bytes = 2; m.payload[0] = 0xe3; m.payload[1] = 0x81;
if (pm_lan_encode(b, sizeof(b), &m, &n)) return 1; return write(1, b, n) != n; }
""")
    binary = pathlib.Path(directory) / 'driver'
    subprocess.run(['gcc', '-fno-builtin', '-I', str(ROOT), '-I', str(ROOT / 'include'),
                    '-I', str(ROOT / 'platform/powermac'), str(driver),
                    str(ROOT / 'platform/powermac/lan_wire.c'), '-o', str(binary)],
                   check=True, capture_output=True, timeout=60)
    guest_frame = subprocess.run([str(binary)], check=True, capture_output=True, timeout=60).stdout
    h.receive(frame(guest_frame))
    h.receive(frame(m.encode(m.LOG, boot, seq=3, drop_records=2, payload=b'\x82')))
    require('gap: 1' in messages, 'H:gap double-drop')
    h.receive(frame(m.encode(m.LOG, boot, seq=3, payload=b'D')))
    h.receive(frame(m.encode(m.LOG, boot, seq=2, payload=b'R')))
    require('initial gap: 1' in messages and 'duplicate: 3' in messages and 'out of order: 2' in messages, 'H:sequence')
    require((pathlib.Path(directory) / (boot.hex() + '.log')).read_bytes() == b'\xe3\x81\x82DR', 'H:raw')
    h.request('read-diag 2 3')
    golden = bytes.fromhex('504c4e3101030050') + boot + bytes(24) + bytes.fromhex('00000001000400000004000000000000') + nonce + bytes.fromhex('00020003')
    require(fake.sent[-1][14:] == golden, 'H:golden')
    h.receive(frame(m.encode(m.RSP, boot, request_id=1, opcode=4, nonce=bytes(16))))
    h.receive(frame(m.encode(m.RSP, boot, request_id=99, opcode=4, nonce=nonce)))
    require(1 in h.pending, 'H:nonce mismatch')
    h.request('status')
    require(2 in h.pending and m.decode(fake.sent[-1][14:])['request_id'] == 2, 'H:request-id')
    h.receive(frame(m.encode(m.RSP, boot, request_id=2, opcode=1, nonce=nonce, payload=bytes(4))))
    require(2 not in h.pending and any('response 2 OK' in x for x in messages), 'H:response')
    h.clock = lambda: 10.5
    h.expire()
    require('request 1 timeout' in messages, 'H:timeout')
    print('PASS')


def udp_test():
    m = load(SOURCE)
    try:
        a = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        b = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    except PermissionError:
        print('SKIP: AF_INET socket not permitted in sandbox')
        return 3
    with a, b:
        a.bind(('127.0.0.1', 0)); b.bind(('127.0.0.1', 0))
        address_a, address_b = a.getsockname(), b.getsockname()
        a.close(); b.close()
        left = m.UDP(f'{address_a[0]}:{address_a[1]},{address_b[0]}:{address_b[1]}')
        right = m.UDP(f'{address_b[0]}:{address_b[1]},{address_a[0]}:{address_a[1]}')
        try:
            left.sock.settimeout(2); right.sock.settimeout(2)
            left.send(b'frame')
            require(right.receive() == b'frame', 'H:udp')
            right.send(b'reply')
            require(left.receive() == b'reply', 'H:udp')
        finally:
            left.sock.close(); right.sock.close()
    print('UDP PASS')
    return 0


def main():
    if len(sys.argv) > 1 and sys.argv[1] == '--exercise':
        exercise(pathlib.Path(sys.argv[2]), sys.argv[3]); return 0
    source = SOURCE.read_text()
    with tempfile.TemporaryDirectory() as temp:
        def check(path):
            with tempfile.TemporaryDirectory(dir=temp) as out:
                return subprocess.run([sys.executable, '-B', __file__, '--exercise', str(path), out], capture_output=True, text=True, timeout=60)
        result = check(SOURCE)
        if result.returncode != 0 or result.stdout != 'reached=A9\nPASS\n':
            raise RuntimeError(f'baseline: {result.returncode} {result.stdout} {result.stderr}')
        print('baseline PASS')
        if '--mutate' in sys.argv:
            for name, old, new, expected in MUTATIONS:
                if source.count(old) != 1:
                    raise RuntimeError(f'{name}: anchor count {source.count(old)}')
                path = pathlib.Path(temp) / 'mutant.py'
                path.write_text(source.replace(old, new))
                result = check(path)
                if result.returncode != 1 or result.stdout != f'reached=A9\n{expected}\n':
                    raise RuntimeError(f'{name}: {result.returncode} {result.stdout} {result.stderr}')
                print(f'{name} RED (rc=1, reached=A9, {expected})')
    return udp_test()


if __name__ == '__main__':
    sys.exit(main())
