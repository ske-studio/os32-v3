"""Shared deterministic clock/fault/transport seams; UNIT wiring is stage-owned.

No product is imported or replaced here. Each unit attaches real dependencies
and consumes these seams. Limits come from that product, never a fixture cap.
Reference frames use the stage-0 wire constants (design §§2/4/5), not captures.
"""
from collections import deque
import struct

# #12: stage -> dependencies/observations. N/R denote guest/real hardware gates.
UNITS = {
    'NIC': ('A2/A3/A8-A11', 'adapter, IF/clock, Ethernet RX/TX, EOF vs MARK'),
    'ADAPTER': ('A4-A7', 'existing NE2K/E82557 stubs, refs/credit; hardware N/R'),
    'NOTICE': ('B2-B4', 'link controls, heap/copyout faults, token/race hook'),
    'GUI': ('B5', 'existing GUI host fixture, serial/pending; delivery N'),
    'SFS': ('C1-C4', 'one SfsIo, virtual replies/clock, real VFS; retain UART'),
    'BLOCK': ('D2/D3', 'SFS, size/mtime changes, sid, short BREAD'),
    'MEDIA': ('D4-D8', 'real Device/VFS/ISO, fake ATAPI/netcd, reference images'),
    'PY_IMAGE': ('H1/H2', 'temporary files, real LanServer/HostFS/ImageStore'),
    'PY_LINK': ('H3/H5/H6/H7', 'real Agent/bridge/Registry, memory frames/clock'),
    'PY_PROC': ('H4', 'Proc fake, missing dependencies; real outputs R'),
    'WIRING': ('I1-I5/I', 'real boot construction/imports, real FD images; N/R'),
    'SDK': ('S1-S4', 'fake KAPI/yield, real LIST, C89/ILP32/Rust; CPL3 N'),
}


class Fixture:
    def __init__(self, unit: str):
        if unit not in UNITS:
            raise ValueError(unit)
        self.unit = unit
        self.reset()

    def reset(self, start_tick: int = 0) -> None:
        self.tick = start_tick & 0xffffffff
        self.ms = self.tick * 10
        self.if_enabled = True
        self.bound = False
        self.hello = False
        self.alive_valid = False
        self.rx = deque()
        self.tx = deque()
        self.failures = {}

    def advance(self, ms: int) -> None:
        if ms < 0:
            raise ValueError(ms)
        self.ms += ms
        self.tick = (self.ms // 10) & 0xffffffff

    def inject(self, frame: bytes) -> None:
        self.rx.append(bytes(frame))

    def take(self) -> list[bytes]:
        frames = list(self.tx)
        self.tx.clear()
        return frames

    def fail(self, point: str, value: object) -> None:
        if point not in ('drop', 'duplicate', 'delay', 'proc_missing',
                         'proc_exit', 'image_replace'):
            raise ValueError(point)
        self.failures[point] = value

    def failure_take(self, point: str) -> object:
        return self.failures.pop(point, None)

    def close(self) -> None:
        self.reset()


def ethernet(payload: bytes, local: bytes, peer: bytes) -> bytes:
    """§2 EtherType 0x88B5, complete frame without FCS, Ethernet padding."""
    if len(local) != 6 or len(peer) != 6:
        raise ValueError('MAC length')
    return (local + peer + b'\x88\xb5' + payload).ljust(60, b'\0')


def notice(generation: int, service: int, kind: int, data: bytes) -> bytes:
    """§5 NOTIFY payload, reserved=0, maximum 236 bytes."""
    if len(data) > 236:
        raise ValueError('notice length')
    return struct.pack('<I4H', generation, service, kind, len(data), 0) + data
