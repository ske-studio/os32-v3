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

    def close(self):
        if self.socket:
            self.socket.close()
            self.socket = None
        if hasattr(self, "agent"):
            for job in self.agent.pending.values():
                proc = job["proc"]
                proc.kill()
                proc.p.wait()
            self.agent.pending.clear()

    def restart(self):
        self.close()
        self.gates.clear()
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
        release = agent._on_release

        def released(rid, out):
            release(rid, out)
            gate = self.gates.pop(rid, None)
            if gate is not None:
                gate["release"] = self.vm.word("tick_count", 0, self.vm.deadline)
                self.vm.record("gate", dict(rid=rid, **gate))

        agent._on_release = released

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
                self.agent.handle(frame, self)
        if self.enabled:
            for rid in self.gates:
                entry = self.agent.ledger.get(rid, {})
                if "sfs_deadline" in entry:
                    entry["sfs_deadline"] = float("inf")
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
        generation = self.operator.generation
        def delivered():
            if self.outputs(rid, generation):
                raise Failure(f"early OUT before release: generation={generation} rid={rid}")
            p = self.operator.pending
            return p and p["id"] == rid and p["execute"].delivery == state
        self.wait(delivered, deadline, "delivery deadline")

    def expect_pending(self, rid, state, seconds):
        generation = self.operator.generation
        until = time.monotonic()+seconds
        while time.monotonic() < until:
            self.pump()
            p = self.operator.pending
            if self.outputs(rid, generation):
                raise Failure(f"early OUT before release: generation={generation} rid={rid}")
            if self.operator.generation != generation or not p or p["id"] != rid or p["state"] != state or p["result"] is not None:
                raise Failure("pending state lost before release")
            time.sleep(.005)

    def outputs(self, rid, generation):
        return [e for e in self.events if e["generation"] == generation and
                e["parts"][:2] == ["RSH", "OUT"] and e["parts"][3] == rid]

    def expect_once(self, rid, generation, payload, deadline):
        self.wait(lambda: self.outputs(rid, generation), deadline, f"missing OUT {rid}")
        event = self.outputs(rid, generation)[0]
        data = bytes.fromhex(event["body"])
        line = self.lines[generation, rid].encode()
        prefix = b"> " + line + b"\n"
        if payload is not None:
            normal = prefix + payload + b"\x04"
            # Command echo uses three PRINT calls. p3-output writes its payload in one call.
            doubled = b"> > " + line * 2 + b"\n\n" + payload * 2 + b"\x04"
            if line == b"p3-output" and data == doubled and event["parts"][4:] == ["0", str(len(data))]:
                self.vm.record("duplicate PRINT", dict(rid=rid, generation=generation,
                               normal=normal.hex(), observed=data.hex()))
                raise Failure("duplicate PRINT payload")
            if data != normal:
                raise Failure(f"unexpected output for {rid}: {data[:120]!r}")
        self.wait(lambda: rid in self.operator.results or self.operator.take_result(), deadline, "OUT rejected")
        result = self.operator.results[rid]
        if (result["state"] not in ("done", "truncated") or result["result"] != data or
                result["drop"] != int(event["parts"][4]) or not data.endswith(b"\x04") or
                (result["state"] == "done" and not data.startswith(prefix))):
            raise Failure("incomplete operator result")
        tick = self.vm.word("tick_count", 0, deadline)
        self.wait(lambda: (self.vm.word("tick_count", 0, deadline)-tick) % 2**32 >= 100,
                  deadline, "capture settlement")
        if len(self.outputs(rid, generation)) != 1:
            raise Failure("multiple OUT records")
        return dict(state=result["state"], drop=result["drop"], body=data)

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
        op.close()
