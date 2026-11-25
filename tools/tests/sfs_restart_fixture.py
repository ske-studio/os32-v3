"""PACK PM decision: a new boot uses boot_token-derived nonce.
lanfs_host.py HELLO replay rule (same peer + nonce) must remain idempotent.
Real guest C runs as separate libc-free ILP32 processes; hosts use no sockets.
"""
from pathlib import Path
import struct
import subprocess
import sys
import tempfile

import host32
from kernel_includes import include_flags

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools'))
import serialfs_host as sfs
import lanfs_host as lan
from host_agent import HostAgent


def nonces(work, source):
    product = work / 'persistent.c'
    product.write_text(source)
    exe = work / 'nonce'
    headers = subprocess.check_output(['gcc', '-print-file-name=include'], text=True).strip()
    subprocess.run(['gcc', '-m32', '-std=gnu11', '-O0', '-Wall', '-Wextra', '-Werror',
        '-ffreestanding', '-fno-pie', '-fno-stack-protector', '-nostdlib', '-nostdinc',
        '-isystem', headers, '-static', '-no-pie', '-ffunction-sections',
        '-fdata-sections', '-Wl,--gc-sections', '-I' + str(ROOT), *include_flags(ROOT),
        '-DPRODUCT_SOURCE="' + str(product) + '"',
        str(ROOT / 'tools/tests/sfs_boot_nonce_host.c'),
        str(ROOT / 'sys/fs/serialfs/sfs_proto.c'), str(ROOT / 'lib/crc32.c'), '-o', str(exe)], check=True)
    values = []
    for boot in ('1', '2'):
        result = host32.run([str(exe), boot], capture_output=True, check=True)
        a, b, wrap = struct.unpack('<III', result.stdout)
        assert a != 0 and b == a + 1 and wrap == 1, (a, b, wrap)
        values.append(a)
    return values


def scenario(work, values, link):
    peer = bytes.fromhex('525400123456')
    mac = bytes.fromhex('02005e000001')
    root = work / 'root'
    root.mkdir()
    if link:
        agent = HostAgent(mac, state_dir=str(work), quiet=True, allow_net=False,
                          clip='none', file_root=str(root), allow_write=['/'], peer=peer)
        agent._switch_to(dict(sess=1, epoch=1, mac=peer))
        backend = agent._sfs_backend()
        call = lambda frame: backend.call(frame, peer)
    else:
        backend = lan.LanServer(mac, sfs.Server(sfs.HostFS(root, allow_write=['/']),
                                               max_payload=1028), peer=peer)
        def call(frame):
            response = backend.handle(mac + peer + struct.pack('>H', lan.ETHERTYPE) + frame)
            if response is None:
                return b''
            parsed = sfs.try_frame(response, 14, 1028)
            return response[14:14 + parsed[1]]
    old_sid = None
    for index, value in enumerate(values):
        if index and link:
            agent._switch_to(dict(sess=2, epoch=2, mac=peer))
            assert agent._sfs_backend() is backend
        hello = sfs.encode(sfs.T_HELLO, 0, 0, struct.pack('<HHI', 1, 1028, value), 1028)
        reply = call(hello)
        sid = sfs.try_frame(reply, 0, 1028)[2].sid
        assert call(hello) == reply, 'same-boot HELLO retransmission changed'
        data = b'boot1' if index == 0 else b'boot2'
        write = sfs.encode(sfs.T_WRITE, sid, 1,
            struct.pack('<IBB', 0, sfs.WF_TRUNC, 2) + b'/f' + data, 1028)
        response = call(write)
        assert (root / 'f').read_bytes() == data, 'restart write lost'
        assert response and call(write) == response, 'write retry changed'
        if index:
            assert sid != old_sid, 'restart reused session'
        old_sid = sid


def main(link):
    source = (ROOT / 'sys/fs/serialfs/serialfs_persistent.c').read_text()
    with tempfile.TemporaryDirectory(prefix='sfs-restart-') as td:
        work = Path(td)
        scenario(work, nonces(work, source), link)
        print('GREEN restart write / HELLO retry / write retry / failure advance / wrap', flush=True)
        if '--mutate' in sys.argv:
            anchor = 'nonce = sfs_crc32(token, sizeof(token));'
            assert source.count(anchor) == 1
            mutant = work / 'mutant'
            mutant.mkdir()
            values = nonces(mutant, source.replace(anchor, 'nonce = 0u;'))
            try:
                scenario(mutant, values, link)
            except AssertionError as error:
                assert str(error) == 'restart write lost', error
                print('RED zero initial nonce: runtime restart write lost', flush=True)
            else:
                raise AssertionError('mutation survived')
