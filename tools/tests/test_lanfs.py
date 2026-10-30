#!/usr/bin/env python3
"""LANFS real C SfsClient/SfsLan <-> fake NIC <-> real Python Server.

Record: tools/tests/lanfs_tdd.md
No raw sockets, emulator or UART. --mutate includes the unchanged controls;
compile errors and mutation-anchor failures are errors, never kills.
"""
import ctypes as C
import errno
import importlib.util
import io
import os
import pathlib
import struct
import subprocess
import sys
import tempfile
from contextlib import redirect_stderr
from unittest.mock import patch

ROOT = pathlib.Path(__file__).resolve().parents[2]
HARNESS = ROOT / "tools/tests/lanfs_host.c"
C_SRCS = ["fs/serialfs/sfs_lan.c", "fs/serialfs/sfs_lan.h", "fs/serialfs/sfs_client.c",
          "fs/serialfs/sfs_client.h", "fs/serialfs/sfs_proto.c", "fs/serialfs/sfs_proto.h", "fs/serialfs/serialfs.c"]
PY_SRC = ROOT / "tools/lanfs_host.py"
sys.path.insert(0, str(ROOT / "tools"))
import serialfs_host as sfs

HOST = bytes.fromhex("020000000001")
GUEST = bytes.fromhex("020000000002")
BCAST = b"\xff" * 6
# types.h defines u32 as unsigned long; ctypes must match the native ABI.
SEND = C.CFUNCTYPE(C.c_int, C.c_void_p, C.c_ulong)
RECV = C.CFUNCTYPE(C.c_int, C.c_void_p, C.c_ulong, C.POINTER(C.c_ulong))
MAC = C.CFUNCTYPE(None, C.c_void_p)
DATA = bytes(i % 251 for i in range(508))


def load_host(path):
    spec = importlib.util.spec_from_file_location("lanfs_under_test", path)
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


def packet(raw, dst=HOST, src=GUEST, eth=0x88B5):
    return (dst + src + struct.pack(">H", eth) + raw).ljust(60, b"\0")


def parse(packet):
    fr = sfs.try_frame(packet, 14, sfs.LAN_MAX_PAYLOAD)
    assert fr[0] == "frame", fr
    return fr[2]


class Rig:
    def __init__(self, lib, host, root):
        self.lib = lib
        self.server = sfs.Server(sfs.HostFS(root, allow_write=["out"]), max_payload=sfs.LAN_MAX_PAYLOAD)
        self.host = host.LanServer(HOST, self.server)
        self.queue = []
        self.sent = []
        self.requests = []
        self.error = None
        self.mode = None
        self.busy = 0
        self.received = 0
        self.callbacks = (SEND(self.send), RECV(self.recv), MAC(self.mac))
        lib.test_init(*self.callbacks)

    def mac(self, buf):
        C.memmove(buf, GUEST, 6)

    def send(self, ptr, size):
        try:
            wire = C.string_at(ptr, size)
            assert 60 <= size <= 1058
            assert wire[6:12] == GUEST and wire[12:14] == b"\x88\xb5"
            fr = parse(wire)
            end = 14 + 16 + len(fr.payload)
            assert size == max(60, end)
            assert wire[end:] == b"\0" * (size - end)
            assert wire[:6] == (BCAST if fr.type == sfs.T_HELLO else HOST)
            self.sent.append(wire)
            if self.busy:
                if self.busy > 0:
                    self.busy -= 1
                return -14
            if self.mode == "stopped":
                return 0
            self.requests.append(wire)
            if self.mode == "drop_request":
                self.mode = None
                return 0
            response = self.host.handle(wire)
            if self.mode == "duplicate":
                assert self.host.handle(wire) == response
            if self.mode == "drop_response":
                self.mode = None
                return 0
            if response is not None:
                if self.mode == "crc":
                    bad = bytearray(response)
                    bad[30] ^= 1
                    self.queue.append(bytes(bad))
                    self.mode = None
                    return 0
                if self.mode == "wrong_seq":
                    f = parse(response)
                    bad = sfs.encode(f.type, f.sid, f.seq + 7,
                                     struct.pack("<i", 3) + b"bad")
                    self.queue.append(packet(bad, GUEST, HOST))
                if self.mode == "ethertype":
                    bad = bytearray(response)
                    bad[12:14] = b"\x08\x00"
                    self.queue.append(bytes(bad))
                self.queue.append(response)
            return 0
        except BaseException as exc:
            self.error = exc
            return -1

    def recv(self, buf, cap, size):
        self.received += 1
        assert cap == 1536
        # Hardware clock advances even under a continuous unrelated RX stream.
        self.lib.test_tick()
        if self.mode == "flood":
            wire = b"x" * 60
        elif self.queue:
            wire = self.queue.pop(0)
        else:
            return -14
        C.memmove(buf, wire, len(wire))
        size[0] = len(wire)
        return 0

    def check(self):
        if self.error:
            raise self.error
        assert self.lib.test_gates() == 0, "LAN raised the UART gate"

    def hello(self):
        assert self.lib.test_hello(0x12345678) == 0
        self.check()

    def read(self):
        assert self.lib.test_read() == 508
        assert self.lib.test_len() == 508
        assert C.string_at(self.lib.test_data(), 508) == DATA
        self.check()


def run_suite(lib_path, py_path, root):
    lib = C.CDLL(str(lib_path))
    lib.test_init.argtypes = [SEND, RECV, MAC]
    lib.test_data.restype = C.POINTER(C.c_ubyte)
    lib.test_hello.argtypes = [C.c_ulong]
    lib.test_peer.argtypes = [C.c_char_p]
    for name in ("test_len", "test_ticks", "test_resends", "test_gates", "test_pending"):
        getattr(lib, name).restype = C.c_ulong
    host = load_host(py_path)
    for cap in (1028, 512):
        rig = Rig(lib, host, root)
        rig.server.payload_cap = cap
        rig.hello()
        assert lib.test_limit() == cap
        (pathlib.Path(root) / "data").write_bytes(bytes(i % 251 for i in range(10 * 1024)))
        lib.test_clock(0)
        size = 10 * 1024 if cap == 1028 else 512
        assert lib.test_stream(size) == size
        assert C.string_at(lib.test_data(), size) == bytes(i % 251 for i in range(size))
        counts = [struct.unpack_from("<H", parse(w).payload, 4)[0]
                  for w in rig.requests if parse(w).type == sfs.T_READ]
        assert counts == ([1024] * 10 if cap == 1028 else [508, 4]), counts
        samples = len(counts)
        assert lib.test_trial(115200) == (270 if cap == 1028 else 261)
        assert lib.test_trial(9600) == (469 if cap == 1028 else 362)
        assert lib.test_samples() == samples and lib.test_avg() == 400 and lib.test_max() == 400
        assert lib.test_read_max() == cap - 4
        assert lib.test_len() == cap - 4
        assert len(rig.host.handle(rig.requests[-1])) == 14 + 12 + cap + 4
        assert C.string_at(lib.test_data(), cap - 4) == bytes(i % 251 for i in range(cap - 4))
        lib.test_clock(1)
        assert lib.test_stream(1) == 1 and lib.test_samples() == samples + 1
        assert lib.test_write() == 1024
        assert (pathlib.Path(root) / "out").read_bytes() == bytes(i % 251 for i in range(1024))
        writes = [len(parse(w).payload) for w in rig.requests if parse(w).type == sfs.T_WRITE]
        assert writes == ([1028, 16] if cap == 1028 else [512, 512, 30]), writes
        # WRITE includes offset, flags and length-prefixed path: /out costs 10 B.
        assert [n - 10 for n in writes] == ([1018, 6] if cap == 1028 else [502, 502, 20])
        rig.check()
        (pathlib.Path(root) / "data").write_bytes(DATA)
    # An intermediate peer limit also falls back to a usable serial size.
    rig = Rig(lib, host, root)
    rig.server.payload_cap = 513
    rig.hello()
    assert lib.test_limit() == 512
    # Default serial offer stays 512 even with a 1028-capable host.
    rig = Rig(lib, host, root)
    lib.test_offer(512)
    rig.hello()
    assert lib.test_limit() == 512
    assert lib.test_read_max() == 508
    rig.check()
    print("GREEN payload negotiation, sector reads, writes, old host, microsecond timing")

    for mode in (None, "drop_request", "drop_response", "duplicate", "crc",
                 "wrong_seq", "ethertype"):
        rig = Rig(lib, host, root)
        rig.hello()
        rig.mode = mode
        rig.read()
        assert rig.server.executed == 1, (mode, rig.server.executed)
        if mode in ("drop_request", "drop_response", "crc"):
            assert lib.test_resends() >= 1, mode
        rig.mode = None
        rig.read()
        before = rig.server.executed
        old = next(w for w in rig.requests if parse(w).type == sfs.T_READ)
        assert rig.host.handle(old) is None, "old seq replayed"
        assert rig.server.executed == before
        # Current nonce stays idempotent after ordinary requests, without a
        # growing set of historical nonces.
        sid = rig.server.sid
        assert parse(rig.host.handle(rig.requests[0])).sid == sid
        rig.check()
        print("PASS exchange", mode, flush=True)

    # KPEEK T5 (L1): kdebug's one-way T_DUMP carries a peek tag in the session
    # ID field. Even when the tag equals the live sid and its seq is the next
    # one, it must be dropped before the sid/seq tracking (no high_seq advance).
    rig = Rig(lib, host, root)
    rig.hello()
    rig.read()
    sid, high, before = rig.server.sid, rig.host.high_seq, rig.server.executed
    dump = sfs.encode(sfs.T_DUMP, sid, high + 1,
                      struct.pack("<I", 0x100000) + bytes(1024), sfs.LAN_MAX_PAYLOAD)
    assert rig.host.handle(packet(dump)) is None
    assert rig.host.high_seq == high and rig.server.executed == before
    rig.read()
    assert rig.server.executed == before + 1 and rig.host.high_seq == high + 1
    rig.check()
    print("PASS one-way T_DUMP dropped before sid/seq tracking", flush=True)

    rig = Rig(lib, host, root)
    lib.test_wait(0)
    assert lib.test_hello(1) < 0 and not rig.sent and lib.test_ticks() == 0
    assert lib.test_put() < 0 and not rig.sent and lib.test_ticks() == 0
    rig.check()
    print("PASS IF=0", flush=True)

    for mode in ("stopped", "flood"):
        rig = Rig(lib, host, root)
        rig.hello()
        rig.mode = mode
        start = lib.test_ticks()
        assert lib.test_read() < 0
        assert lib.test_ticks() - start < 1800, (mode, lib.test_ticks())
        rig.check()
        print("PASS finite", mode, flush=True)

    rig = Rig(lib, host, root)
    rig.hello()
    rig.busy = -1
    start = lib.test_ticks()
    assert lib.test_put() < 0
    assert 100 <= lib.test_ticks() - start <= 120
    rig.check()
    print("PASS bounded put", flush=True)

    # A response received during AGAIN survives in get's one-frame buffer.
    rig = Rig(lib, host, root)
    rig.hello()
    rig.read()
    response = rig.host.handle(rig.requests[-1])
    bad = bytearray(response)
    bad[-1] ^= 1
    rig.queue = [bytes(bad), response]
    rig.busy = 2
    assert lib.test_put() == 0
    assert lib.test_pending() > 0 and len(rig.queue) == 1
    assert lib.test_get() == 5  # bad datagram rejected, next complete one read
    rig.check()
    print("PASS put preserves SFS, CRC frame boundary", flush=True)

    # Datagram boundaries, padding and a finite per-get receive budget.
    rig = Rig(lib, host, root)
    rig.hello()
    rig.read()
    response = rig.host.handle(rig.requests[-1])
    rig.queue = [response[:25], response]
    assert lib.test_get() == 5
    before = rig.received
    while lib.test_pending():
        lib.test_get()
    rig.mode = "flood"
    before = rig.received
    assert lib.test_get() < 0 and rig.received - before == 8
    rig.check()
    print("PASS datagram boundary and RX budget", flush=True)

    # Same seq with different body cannot execute; gaps are permitted.
    rig = Rig(lib, host, root)
    rig.hello()
    rig.read()
    fr = parse(rig.requests[-1])
    altered = sfs.encode(fr.type, fr.sid, fr.seq, b"different")
    assert rig.host.handle(packet(altered)) is None
    gap = sfs.encode(fr.type, fr.sid, fr.seq + 3, fr.payload)
    assert rig.host.handle(packet(gap)) is not None
    assert rig.server.executed == 2
    # HostFS defaults remain read only.
    write = sfs.encode(sfs.T_WRITE, fr.sid, fr.seq + 4,
                       struct.pack("<IB", 0, 1) + b"\x05/datachanged")
    reply = parse(rig.host.handle(packet(write)))
    assert struct.unpack_from("<i", reply.payload)[0] < 0
    assert (pathlib.Path(root) / "data").read_bytes() == DATA
    rig.check()
    print("PASS duplicate contents, sequence gap, read only", flush=True)

    # Wrong destinations, peer MACs, and marker bytes never enter the client.
    rig = Rig(lib, host, root)
    rig.hello()
    rig.read()
    response = rig.host.handle(rig.requests[-1])
    wrong_dst = b"x" * 6 + response[6:]
    wrong_src = response[:6] + b"y" * 6 + response[12:]
    wrong_mark = response[:14] + b"\x05XX" + response[17:]
    rig.queue = [wrong_dst, wrong_src, wrong_mark, response]
    assert lib.test_get() == 5
    assert rig.received >= 4 and not rig.queue
    rig.check()
    print("PASS Ethernet destination, peer and SFS marker", flush=True)

    # The configured guest-side peer applies to HELLO responses too.
    rig = Rig(lib, host, root)
    assert lib.test_peer(b"02:00:00:00:00:03") == 0
    assert lib.test_hello(7) != 0 and lib.test_ticks() < 2000
    assert lib.test_peer(b"02:00:00:00:00:01") == 0
    rig.hello()
    rig.read()
    for bad in (b"x", b"02:00:00:00:00", b"02:00:00:00:00:01:00", b"gg:00:00:00:00:01"):
        assert lib.test_peer(bad) != 0
    print("PASS configured client HELLO peer", flush=True)

    hello = sfs.encode(sfs.T_HELLO, 0, 0, struct.pack("<HHI", 1, 512, 123))
    other = bytes.fromhex("020000000003")
    for explicit in (None, GUEST):
        server = sfs.Server(sfs.HostFS(root))
        lan = host.LanServer(HOST, server, peer=explicit)
        if explicit:
            assert lan.handle(packet(hello, BCAST, other)) is None
            assert server.sid == 0
        # Non-HELLO traffic must not claim the first peer.
        lan.handle(packet(sfs.encode(sfs.T_STAT, 999, 1, b""), HOST, other))
        assert lan.peer == explicit
        assert lan.handle(packet(hello, BCAST)) is not None
        sid = server.sid
        assert lan.peer == GUEST
        new_hello = sfs.encode(sfs.T_HELLO, 0, 0, struct.pack("<HHI", 1, 512, 124))
        assert lan.handle(packet(new_hello, BCAST, other)) is None
        assert server.sid == sid
        assert lan.handle(packet(sfs.encode(sfs.T_BYE, sid, 0), HOST)) is None
        assert lan.handle(packet(new_hello, BCAST, other)) is None
        assert lan.peer == GUEST
        assert lan.handle(packet(new_hello, BCAST)) is not None
        assert lan.peer == GUEST
        # Reserve 0xffff and never let seq wrap within this session.
        assert lan.handle(packet(sfs.encode(sfs.T_STAT, server.sid, 0xffff, b""))) is None
    print("PASS explicit/first-HELLO host peer survives BYE and replacement", flush=True)

    # sockaddr_ll pkttype, rather than MAC coincidence, rejects outgoing copies.
    class Sock:
        def recvfrom(self, cap):
            assert cap == host.lan_bridge.MAX_FRAME
            return b"frame", ("eth0", 0x88b5, self.kind, 1, b"")
    port = host.NicPort.__new__(host.NicPort)
    port.sock = Sock()
    port.sock.kind = 4
    assert port.recv_frames() == []
    port.sock.kind = 0
    assert port.recv_frames() == [b"frame"]
    assert host.peer_mac("02:00:00:00:00:02") == GUEST
    for bad in ("0:0:0:0:0:0", "02-00-00-00-00-02", "zz:00:00:00:00:02"):
        try:
            host.peer_mac(bad)
        except host.argparse.ArgumentTypeError:
            pass
        else:
            raise AssertionError("invalid --peer accepted")
    with patch.object(host.Path, "read_text", side_effect=["down\n", "0\n"]):
        assert host.link_status("enp1s0") == "operstate=down carrier=0"
    with patch.object(host.Path, "read_text", side_effect=OSError()):
        assert host.link_status("enp1s0") == "operstate=unknown carrier=unknown"
    print("PASS AF_PACKET outgoing filter and startup link state", flush=True)

    # Exercise the foreground AF_PACKET serving loop through its port seam.
    # Only the socket is fake; framing and Server execution remain real.
    rig = Rig(lib, host, root)
    hello = sfs.encode(sfs.T_HELLO, 0, 0,
                       struct.pack("<HHI", 1, 512, 123))
    wire = packet(hello, BCAST)

    class Done(Exception):
        pass

    class Port:
        def __init__(self):
            self.sock = self
            self.response = None
            self.timeout = None
            self.reads = 0

        def settimeout(self, timeout):
            self.timeout = timeout

        def recv_frames(self):
            self.reads += 1
            if self.reads > 1:
                raise Done()
            return [wire]

        def send(self, response):
            self.response = response

    port = Port()
    original = host.select.select
    host.select.select = lambda *args: ([port], [], [])
    try:
        try:
            host.serve(port, rig.host, now=lambda: 0.0)
        except Done:
            pass
    finally:
        host.select.select = original
    assert port.timeout == sfs.FIRST_BYTE_S
    assert parse(port.response).type == sfs.T_HELLO | sfs.T_RESP
    assert port.response[:12] == GUEST + HOST
    rig.check()
    print("PASS host foreground serve loop", flush=True)

    # Real server continues after recv/send OSError and unexpected handle errors.
    class Stop(BaseException):
        pass

    class FlakyPort(Port):
        def recv_frames(self):
            self.reads += 1
            if self.reads == 1:
                raise OSError(errno.ENETDOWN, "link down")
            if self.reads == 6:
                raise Stop()
            return [wire]

        def send(self, response):
            if self.reads == 3:
                raise OSError(errno.ENOBUFS, "queue full")
            self.response = response

    port = FlakyPort()
    real_handle = rig.host.handle
    def handle(frame):
        if port.reads == 2:
            raise ValueError("request failed")
        return real_handle(frame)
    errors = io.StringIO()
    with patch.object(host.select, "select", return_value=([port], [], [])), \
            patch.object(host.time, "sleep"), \
            patch.object(rig.host, "handle", side_effect=handle), redirect_stderr(errors):
        try:
            host.serve(port, rig.host, now=lambda: 0.0)
        except Stop:
            pass
    assert port.reads == 6 and port.response is not None
    assert errors.getvalue().count("lanfs:") == 3
    assert "ENETDOWN" not in errors.getvalue() or "link down" in errors.getvalue()
    print("PASS serve survives receive/send/handler failures", flush=True)


def build(work, changes=None, target=False):
    changes = changes or {}
    work.mkdir()
    for rel in C_SRCS:
        (work / pathlib.Path(rel).name).write_text(
            changes.get(rel, (ROOT / rel).read_text()))
    py_path = work / "lanfs_host.py"
    py_path.write_text(changes.get("tools/lanfs_host.py", PY_SRC.read_text()))
    inc = ["-I" + str(work), "-I" + str(ROOT / "include"),
           "-I" + str(ROOT / "lib"), "-I" + str(ROOT / "fs"), "-I" + str(ROOT / "sdk/include/os32")]
    flags = ["-std=gnu11", "-Wall", "-Wextra", "-D__cdecl="]
    if not changes:
        flags.append("-Werror")
    sources = [str(HARNESS), str(work / "sfs_lan.c"),
               str(work / "sfs_client.c"), str(work / "sfs_proto.c"),
               str(work / "serialfs.c"),
               str(ROOT / "lib/crc32.c")]
    output = work / "lanfs.so"
    subprocess.run(["gcc", *flags, *inc, "-shared", "-fPIC", *sources,
                    "-o", str(output)], check=True)
    if target:
        cc = pathlib.Path(os.environ["CROSS_DIR"]) / "bin/i386-elf-gcc"
        for src in sources:
            subprocess.run([str(cc), *flags, *inc, "-ffreestanding", "-c", src,
                            "-o", str(work / (pathlib.Path(src).stem + ".o"))],
                           check=True)
    return output, py_path


def trial(exe, py_path, root, timeout=10):
    return subprocess.run([sys.executable, "-B", __file__, "--run", str(exe),
                           str(py_path), str(root)], timeout=timeout,
                          stdout=subprocess.PIPE, stderr=subprocess.STDOUT)


def run_mutations(tmp, root):
    mutations = [
        ("hello_limit", "fs/serialfs/sfs_client.c",
         "        c->max_payload = SFS_MAX_PAYLOAD;", "        c->max_payload = SFS_LAN_MAX_PAYLOAD;"),
        ("seq", "fs/serialfs/sfs_client.c", "f->sid != c->sid || f->seq != seq",
         "f->sid != c->sid"),
        ("old_seq", "tools/lanfs_host.py", "elif fr.seq <= self.high_seq:",
         "elif False:"),
        ("dump_oneway", "tools/lanfs_host.py",
         "(sfs.T_LOG, sfs.T_EXIT, sfs.T_DUMP)", "(sfs.T_LOG, sfs.T_EXIT)"),
        ("host_peer", "tools/lanfs_host.py",
         "if self.peer is not None and src != self.peer:", "if False:"),
        ("put_deadline", "fs/serialfs/sfs_lan.c",
         "(u32)(lan_now(ctx) - start) >= sfs_ms_ticks(SFS_LAN_PUT_MS)",
         "0"),
        ("uart_gate", "fs/serialfs/sfs_lan.c", "    nic->mac(lan->mac);",
         "    extern void serial_gate_set(int on);\n"
         "    serial_gate_set(1);\n    nic->mac(lan->mac);"),
    ]
    for name, rel, old, new in mutations:
        text = (ROOT / rel).read_text()
        assert text.count(old) == 1, (name, "mutation anchor")
        exe, py_path = build(tmp / name, {rel: text.replace(old, new)})
        try:
            result = trial(exe, py_path, root)
            assert result.returncode != 0, name + " survived"
            print("RED", name, result.stdout.decode().splitlines()[-1])
        except subprocess.TimeoutExpired:
            assert name == "put_deadline", name + " unexpected timeout"
            print("RED put_deadline timeout")


def main():
    if "--run" in sys.argv:
        run_suite(*sys.argv[2:])
        return
    with tempfile.TemporaryDirectory(prefix="lanfs-") as tmp:
        tmp = pathlib.Path(tmp)
        root = tmp / "root"
        root.mkdir()
        (root / "data").write_bytes(DATA)
        exe, py_path = build(tmp / "control", target="--target" in sys.argv)
        control = trial(exe, py_path, root)
        print(control.stdout.decode(), end="")
        assert control.returncode == 0, "unchanged control failed"
        if "--mutate" in sys.argv:
            run_mutations(tmp, root)



if __name__ == "__main__":
    main()
