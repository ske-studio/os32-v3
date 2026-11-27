"""Loopback peer for one mac99 guest using the candidate host agent."""
from contextlib import contextmanager
import json
import os
from pathlib import Path
import select
import socket
import sys
import time

from tests.support.media import Failure
from tests.support.ppc_vm import remaining

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "tools"))
import host_agent as wire
from host_agent import HostAgent
from host_operator_socket import OperatorSocket
from ppc_lan_host import UDP


@contextmanager
def network():
    io = UDP("127.0.0.1:0,127.0.0.1:0")
    try:
        port = io.sock.getsockname()[1]
        yield io, ["-nic", "none", "-device", "sungem,netdev=lan,mac=52:54:00:12:34:56",
                   "-netdev", f"socket,id=lan,udp=127.0.0.1:{port},localaddr=127.0.0.1:0",
                   "-usb", "-device", "usb-kbd,id=kbd", "-icount", "shift=auto"]
    finally:
        io.sock.close()


class Peer:
    def __init__(self, vm, root):
        self.vm = vm
        self.root = root
        self.io = vm.network
        self.socket = None
        self.events = []
        self.lines = {}
        self.gates = {}
        self.enabled = True
        self.restart()

    def restart(self):
        if self.socket:
            self.socket.close()
        agent = HostAgent(bytes.fromhex("02005e000001"), state_dir=str(self.vm.directory),
                          quiet=True, allow_net=False, file_root=str(self.root))
        self.agent = agent
        self.operator = agent.operator
        self.socket = OperatorSocket(("127.0.0.1", 0), self.operator, lambda: dict(
            commit=os.getenv("GITHUB_SHA") or os.getenv("OS32_BUILD_COMMIT"),
            config="link-sh", peer=agent.peer.hex() if agent.peer else None))
        agent.operator_socket = self.socket
        service = agent._service_rshell

        def record(rid, entry, parts, out):
            event = dict(parts=list(parts), body=bytes(entry["got"]).hex(), link_rid=rid,
                         generation=agent.host_generation, at=time.monotonic())
            self.events.append(event)
            with (self.vm.directory / "operator.jsonl").open("a") as log:
                print(json.dumps(event), file=log)
            service(rid, entry, parts, out)

        agent._service_rshell = record
        sfs = agent._service_sfs

        def call(rid, entry, out):
            if rid in self.gates:
                self.gates[rid].setdefault("wdata", self.vm.word("tick_count", 0, self.vm.deadline))
            else:
                sfs(rid, entry, out)

        agent._service_sfs = call

    def gate_call(self, rid):
        self.gates[rid] = {}
        return self.gates[rid]

    def send(self, frame):
        self.io.send(frame)

    def pump(self):
        remaining(self.vm.deadline)
        if self.vm.process.poll() is not None:
            raise OSError("QEMU exited during LAN session")
        while select.select([self.io], [], [], 0)[0]:
            if self.io.remote[1] == 0:
                _, addr = self.io.sock.recvfrom(1, socket.MSG_PEEK)
                if addr[0] != "127.0.0.1":
                    self.io.sock.recvfrom(65535)
                    continue
                self.io.remote = addr
            frame = self.io.receive()
            if frame and self.enabled:
                if self.gates and len(frame) >= wire.ETH_HDR + wire.LINK_HDR:
                    header = wire.hdr_unpack(frame[wire.ETH_HDR:])
                    if header[0] == wire.OP_RELEASE and header[6] in self.gates:
                        self.gates[header[6]].setdefault("release", self.vm.word("tick_count", 0, self.vm.deadline))
                self.agent.handle(frame, self)
        if self.enabled:
            self.agent.tick(self)
        for fd in select.select(self.socket.fileno_list(), [], [], 0)[0]:
            self.socket.on_readable(fd)
        self.socket.poll(time.monotonic())
        self.vm.pump(0)

    def wait(self, predicate, deadline, label):
        while True:
            self.pump()
            if predicate():
                return
            if time.monotonic() >= deadline:
                raise Failure(label)
            time.sleep(.005)

    def snapshot(self, deadline):
        remaining(deadline)
        p = self.operator.pending
        ready = self.operator.ready
        return dict(ready=ready, mode=ready["mode"] if ready else None,
                    generation=self.operator.generation, rid=p["id"] if p else None,
                    delivery=p["execute"].delivery if p else None,
                    pending=p["state"] if p else None,
                    results=sum(e["parts"][:2] == ["RSH", "OUT"] for e in self.events))

    def submit(self, line, deadline):
        ident = self.operator.submit("cui", line, "test", max(1, int(remaining(deadline)*1000)))
        key = (self.operator.generation, ident)
        self.lines[key] = line
        return ident, key[0]

    def wait_delivery(self, rid, state, seconds):
        deadline = min(self.vm.deadline, time.monotonic()+seconds)
        self.wait(lambda: self.snapshot(deadline)["rid"] == rid and
                  self.operator.pending["execute"].delivery == state, deadline, "delivery deadline")

    def expect_pending(self, rid, state, seconds):
        until = time.monotonic()+seconds
        while time.monotonic() < until:
            self.pump()
            p = self.operator.pending
            if not p or p["id"] != rid or p["state"] != state or p["result"] is not None:
                raise Failure("command completed before release")
            time.sleep(.005)

    def expect_once(self, rid, generation, payload, deadline):
        def outputs():
            return [e for e in self.events if e["generation"] == generation and
                    e["parts"][:2] == ["RSH", "OUT"] and e["parts"][3] == rid]
        self.wait(outputs, deadline, f"missing OUT {rid}")
        data = bytes.fromhex(outputs()[0]["body"])
        prefix = b"> " + self.lines[generation, rid].encode() + b"\n"
        line = self.lines[generation, rid].encode()
        if data.startswith(b"> > " + line*2 + b"\n\n") and data.endswith(b"\x04"):
            raise Failure("duplicate PRINT payload")
        if payload is not None and data != prefix + payload + b"\x04":
            if ((payload and data == prefix + payload*2 + b"\x04") or
                    data == prefix*2 + payload + b"\x04"):
                raise Failure("duplicate PRINT payload")
            raise Failure(f"unexpected output for {rid}: {data[:120]!r}")
        self.wait(lambda: rid in self.operator.results or self.operator.take_result(), deadline, "OUT rejected")
        result = self.operator.results[rid]
        if result["state"] != "done" or result["result"] != data or not data.startswith(prefix) or not data.endswith(b"\x04"):
            raise Failure("incomplete operator result")
        tick = self.vm.word("tick_count", 0, deadline)
        self.wait(lambda: (self.vm.word("tick_count", 0, deadline)-tick) % 2**32 >= 100,
                  deadline, "capture settlement")
        if len(outputs()) != 1:
            raise Failure("multiple OUT records")
        return data[len(prefix):-1]

    def notify_number(self, channel, opcode, data, deadline):
        remaining(deadline)
        return self.agent.notifier.notify_number(channel, opcode, data)

    raw = notify_number


@contextmanager
def peer(vm, root):
    op = Peer(vm, root)
    try:
        yield op
    finally:
        op.socket.close()
