#!/usr/bin/env python3
"""LAN SerialFS: py-raw tools/lanfs_host.py --iface enp1s0 --root DIR.

One foreground AF_PACKET loop, using the existing read-only HostFS/Server.
No bridge process, UART, outer ACK or sequence numbers are involved.
"""
import argparse
import select
import struct
import sys
import time

import lan_bridge
import serialfs_host as sfs

ETHERTYPE = 0x88B5
ETH_HDR = 14
ETH_MIN = 60
BROADCAST = b"\xff" * 6
MARK = b"\x05SF"


class LanServer:
    """Ethernet envelope and one-session duplicate tracking around Server."""

    def __init__(self, mac, server):
        self.mac = mac
        self.server = server
        self.peer = None
        self.nonce = None
        self.hello_response = None
        self.high_seq = 0
        self.last_request = None
        self.last_response = None

    def handle(self, packet):
        if (len(packet) < ETH_HDR + len(MARK) or
                packet[12:14] != struct.pack(">H", ETHERTYPE) or
                packet[14:17] != MARK or packet[6:12] == self.mac):
            return None
        parsed = sfs.try_frame(packet, ETH_HDR)
        if parsed[0] != "frame":
            return None
        fr = parsed[2]
        src, dst = packet[6:12], packet[:6]
        request = packet[ETH_HDR:ETH_HDR + parsed[1]]
        if fr.type == sfs.T_HELLO:
            if (dst not in (self.mac, BROADCAST) or fr.sid != 0 or
                    fr.seq != 0 or len(fr.payload) < 8):
                return None
            nonce = struct.unpack_from("<I", fr.payload, 4)[0]
            if self.server.sid and src == self.peer and nonce == self.nonce:
                response = self.hello_response
            else:
                response = self.server.handle(fr)
                if response is None:
                    return None
                self.peer, self.nonce = src, nonce
                self.hello_response = response
                self.high_seq = 0
                self.last_request = self.last_response = None
        else:
            if dst != self.mac or (self.peer is not None and src != self.peer):
                return None
            if fr.type in (sfs.T_LOG, sfs.T_EXIT) or fr.type & sfs.T_RESP:
                return None
            if fr.sid != self.server.sid or not fr.sid:
                response = self.server.handle(fr)
            elif fr.type == sfs.T_BYE:
                self.server.handle(fr)
                self.nonce = self.hello_response = None
                return None
            elif fr.seq <= self.high_seq:
                if fr.seq != self.high_seq or request != self.last_request:
                    return None
                response = self.last_response
            else:
                response = self.server.handle(fr)
                self.high_seq = fr.seq
                self.last_request, self.last_response = request, response
        if response is None:
            return None
        out = src + self.mac + struct.pack(">H", ETHERTYPE) + response
        return out.ljust(ETH_MIN, b"\0")


def serve(port, server, now=time.monotonic):
    """Bound each send; omit a response if filesystem work took too long."""
    port.sock.settimeout(sfs.FIRST_BYTE_S)
    while True:
        if not select.select([port], [], [], 1.0)[0]:
            continue
        for packet in port.recv_frames():
            start = now()
            response = server.handle(packet)
            if response is not None and now() - start < sfs.FIRST_BYTE_S:
                port.send(response)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--iface", required=True)
    parser.add_argument("--root", required=True)
    args = parser.parse_args()
    port = None
    try:
        fs = sfs.HostFS(args.root)
        port = lan_bridge.NicPort(args.iface, ETHERTYPE)
        server = LanServer(port.mac, sfs.Server(fs, log=lambda msg: print(
            msg, file=sys.stderr, flush=True)))
        print("lanfs: iface=%s mac=%s root=%s (read only)" % (
            args.iface, lan_bridge.mac_str(port.mac), args.root),
            file=sys.stderr, flush=True)
        serve(port, server)
    except KeyboardInterrupt:
        return 0
    except (OSError, lan_bridge.BridgeError) as exc:
        print("lanfs: %s" % exc, file=sys.stderr)
        return 1
    finally:
        if port is not None:
            port.close()


if __name__ == "__main__":
    sys.exit(main())
