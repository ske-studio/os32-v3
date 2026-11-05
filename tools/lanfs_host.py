#!/usr/bin/env python3
"""LAN SerialFS: py-raw tools/lanfs_host.py --iface enp1s0 --root DIR.

One foreground AF_PACKET loop, using the existing HostFS/Server.
Read only by default; --allow-write PATH permits a root-relative path and
its descendants. Repeat the option to allow multiple paths.
No bridge process, UART, outer ACK or extra sequence numbers are involved.
SFS seq is LE16: at most 65,534 requests per session, without wrapping.
Split larger transfers and start a new sfs run session after exhaustion.
"""
import argparse
from pathlib import Path
import select
import socket
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
BLOCK_OPS = (0x30, 0x31, 0x32)


class LanServer:
    """Ethernet envelope and one-session duplicate tracking around Server."""

    def __init__(self, mac, server, peer=None, image=None):
        self.mac = mac
        self.server = server
        self.peer = peer
        self.image = image
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
        parsed = sfs.try_frame(packet, ETH_HDR, sfs.LAN_MAX_PAYLOAD)
        if parsed[0] != "frame":
            return None
        fr = parsed[2]
        src, dst = packet[6:12], packet[:6]
        # Keep the first HELLO sender even after BYE, across session changes.
        # An explicit --peer pins this choice before the first HELLO.
        if self.peer is not None and src != self.peer:
            return None
        request = packet[ETH_HDR:ETH_HDR + parsed[1]]
        if fr.type == sfs.T_HELLO:
            if (dst not in (self.mac, BROADCAST) or fr.sid != 0 or
                    fr.seq != 0 or len(fr.payload) < 8):
                return None
            nonce = struct.unpack_from("<I", fr.payload, 4)[0]
            if self.server.sid and src == self.peer and nonce == self.nonce:
                response = self.hello_response
            else:
                old_sid = self.server.sid
                response = self.server.handle(fr)
                if self.image is not None and old_sid != self.server.sid:
                    if old_sid:
                        self.image.close_session(old_sid)
                    if self.server.sid:
                        self.image.open_session(self.server.sid)
                if response is None:
                    return None
                self.peer, self.nonce = src, nonce
                self.hello_response = response
                self.high_seq = 0
                self.last_request = self.last_response = None
        else:
            if dst != self.mac or (self.peer is not None and src != self.peer):
                return None
            if fr.type in (sfs.T_LOG, sfs.T_EXIT, sfs.T_DUMP) or fr.type & sfs.T_RESP:
                return None
            if fr.sid != self.server.sid or not fr.sid:
                response = self.server.handle(fr)
            elif fr.type == sfs.T_BYE:
                self.server.handle(fr)
                if self.image is not None:
                    self.image.close_session(fr.sid)
                self.nonce = self.hello_response = None
                return None
            elif not 1 <= fr.seq <= 0xfffe:
                # LE16 sequence numbers never wrap within a session.
                return None
            elif fr.seq <= self.high_seq:
                if fr.seq != self.high_seq or request != self.last_request:
                    return None
                response = self.last_response
            else:
                if self.image is not None and fr.type in BLOCK_OPS:
                    payload = self.image.dispatch(fr.sid, fr.type, fr.payload)
                    response = sfs.encode(fr.type | sfs.T_RESP, fr.sid, fr.seq, payload,
                                          self.server.max_payload)
                else:
                    response = self.server.handle(fr)
                self.high_seq = fr.seq
                self.last_request, self.last_response = request, response
        if response is None:
            return None
        out = src + self.mac + struct.pack(">H", ETHERTYPE) + response
        return out.ljust(ETH_MIN, b"\0")


class NicPort(lan_bridge.NicPort):
    def recv_frames(self):
        packet, address = self.sock.recvfrom(lan_bridge.MAX_FRAME)
        # sockaddr_ll's pkttype: ignore our own outgoing packet copies.
        if address[2] == getattr(socket, "PACKET_OUTGOING", 4):
            return []
        return [packet]


def log_error(exc):
    print("lanfs: %s: %s" % (type(exc).__name__, exc), file=sys.stderr, flush=True)


def serve(port, server, now=time.monotonic):
    """Bound each send; omit a response if filesystem work took too long."""
    port.sock.settimeout(sfs.FIRST_BYTE_S)
    while True:
        try:
            if not select.select([port], [], [], 1.0)[0]:
                continue
            packets = port.recv_frames()
        except OSError as exc:
            log_error(exc)
            # A down interface can stay readable; bound CPU/log churn as well.
            time.sleep(0.1)
            continue
        for packet in packets:
            start = now()
            try:
                response = server.handle(packet)
                if response is not None and now() - start < sfs.FIRST_BYTE_S:
                    port.send(response)
            except Exception as exc:  # one bad request or transient NIC failure
                log_error(exc)


def link_status(iface):
    values = []
    for name in ("operstate", "carrier"):
        try:
            value = (Path("/sys/class/net") / iface / name).read_text().strip()
        except OSError:
            value = "unknown"
        values.append("%s=%s" % (name, value))
    return " ".join(values)


def peer_mac(value):
    try:
        if len(value) != 17 or any(value[i] != ':' for i in (2, 5, 8, 11, 14)):
            raise ValueError("expected xx:xx:xx:xx:xx:xx")
        return lan_bridge.parse_mac(value)
    except ValueError as exc:
        raise argparse.ArgumentTypeError(str(exc)) from exc


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--iface", required=True)
    parser.add_argument("--root", required=True)
    parser.add_argument("--allow-write", action="append", default=[], metavar="PATH",
                        help="allow writes to this root-relative path and below (repeatable)")
    parser.add_argument("--image", action="append", default=[], metavar="ALIAS=PATH",
                        help="serve an image under this alias (repeatable)")
    parser.add_argument("--peer", type=peer_mac, metavar="MAC",
                        help="accept only this sender (default: first HELLO sender)")
    args = parser.parse_args()
    port = None
    try:
        images = {}
        for item in args.image:
            alias, sep, path = item.partition("=")
            if not sep or not alias or not path or alias in images:
                raise ValueError("--image requires a unique ALIAS=PATH")
            images[alias] = path
        from host_image import ImageStore
        image = ImageStore(images)
        fs = sfs.HostFS(args.root, allow_write=args.allow_write)
        print("lanfs: iface=%s %s" % (args.iface, link_status(args.iface)),
              file=sys.stderr, flush=True)
        port = NicPort(args.iface, ETHERTYPE)
        server = LanServer(port.mac, sfs.Server(fs, max_payload=sfs.LAN_MAX_PAYLOAD, log=lambda msg: print(
            msg, file=sys.stderr, flush=True)), peer=args.peer, image=image)
        print("lanfs: iface=%s mac=%s root=%s (%s)" % (
            args.iface, lan_bridge.mac_str(port.mac), args.root,
            "allow-write=%r" % args.allow_write if args.allow_write else "read only"),
            file=sys.stderr, flush=True)
        serve(port, server)
    except KeyboardInterrupt:
        return 0
    except (OSError, ValueError, lan_bridge.BridgeError) as exc:
        print("lanfs: %s" % exc, file=sys.stderr)
        return 1
    finally:
        if port is not None:
            port.close()


if __name__ == "__main__":
    sys.exit(main())
