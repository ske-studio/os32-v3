"""PLN1 adapter for the existing LAN SerialFS session contract."""
import struct

import lanfs_host
import serialfs_host as sfs


class HostFS:
    def __init__(self, root, allow_write, mac, peer):
        self.fs = sfs.HostFS(root, allow_write=allow_write)
        self.mac, self.peer = mac, peer
        self.reset()

    def reset(self):
        self.server = sfs.Server(self.fs, max_payload=sfs.LAN_MAX_PAYLOAD)
        self.adapter = lanfs_host.LanServer(self.mac, self.server, peer=self.peer)

    def handle_complete_sfs(self, frame, source):
        parsed = sfs.try_frame(frame, 0, sfs.LAN_MAX_PAYLOAD)
        if parsed[0] != 'frame' or parsed[1] != len(frame):
            return None
        packet = self.mac + source + struct.pack('>H', lanfs_host.ETHERTYPE) + frame
        reply = self.adapter.handle(packet)
        if reply is None:
            return None
        parsed = sfs.try_frame(reply, 14, sfs.LAN_MAX_PAYLOAD)
        return reply[14:14 + parsed[1]] if parsed[0] == 'frame' else None
