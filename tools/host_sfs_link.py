"""SFS CALL backend for P4b (LAN_OPERATOR_DESIGN §5).

Create one SfsLink(root, allow_write=(), peer=None) per host generation and
retain it across ALL link handles/rids. call(frame, source) takes exactly the
<len> WDATA bytes and the six-byte peer MAC, and returns the DATA body (bytes).
An empty body means no SFS response, including BYE and ignored/stale replays;
P4b must still send 200. ValueError means malformed framing or wrong peer;
P4b must report a failed call, not retry a filesystem operation. The caller
validates the SFS CALL declaration/WDATA length and owns the host wall-clock deadline.
Construct a new instance on host generation change; never reset per rid.

HostFS, Server and LanServer are the raw LAN backend, with their existing
path policy, session and replay rules. The Ethernet envelope here is only an
in-memory bridge to that backend: no socket, new cache or filesystem logic.
SFS codecs are explicitly little endian; no diagnostic BE payload is decoded.
"""
import struct

import lanfs_host
from host_image import ImageStore
import serialfs_host as sfs

MAX_FRAME = sfs.HDR_LEN + sfs.LAN_MAX_PAYLOAD + sfs.CRC_LEN
_LOCAL_MAC = b'\x02\x00\x00\x00\x00\x00'


class SfsLink:
    def __init__(self, root, allow_write=(), peer=None):
        self.fs = sfs.HostFS(root, allow_write=allow_write)
        self.server = sfs.Server(self.fs, max_payload=sfs.LAN_MAX_PAYLOAD)
        self.adapter = lanfs_host.LanServer(_LOCAL_MAC, self.server, peer=peer,
                                             image=ImageStore({}))

    def call(self, frame, source):
        if len(source) != 6:
            raise ValueError('expected six-byte peer MAC')
        if self.adapter.peer is not None and source != self.adapter.peer:
            raise ValueError('unexpected SFS peer')
        if not 16 <= len(frame) <= MAX_FRAME or frame[:3] != lanfs_host.MARK:
            raise ValueError('invalid SFS frame length or mark')
        parsed = sfs.try_frame(frame, 0, sfs.LAN_MAX_PAYLOAD)
        if parsed[0] != 'frame' or parsed[1] != len(frame):
            raise ValueError('invalid SFS frame length or CRC')
        # Choose a distinct local address even when the peer uses our default.
        local = bytes([source[0] ^ 2]) + source[1:]
        self.adapter.mac = local
        packet = local + source + struct.pack('>H', lanfs_host.ETHERTYPE) + frame
        response = self.adapter.handle(packet)
        if response is None:
            return b''
        parsed = sfs.try_frame(response, lanfs_host.ETH_HDR, sfs.LAN_MAX_PAYLOAD)
        return response[lanfs_host.ETH_HDR:lanfs_host.ETH_HDR + parsed[1]]
