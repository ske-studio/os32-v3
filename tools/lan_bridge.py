#!/usr/bin/env python3
"""OS32 LAN bridge — 物理 NIC (AF_PACKET) ↔ host_agent.py の FrameStream。

票: docs/tasks/realhw/TASK_LAN_82557.md §2 の **L-D** / §5-1
記録: tools/tests/lan_bridge_tdd.md

実機 PC-9821Ra266 とホスト (Ubuntu ノート) をローカルのスイッチで直結し、
OS32 リンクプロトコル (raw Ethernet, EtherType 0x88B5、docs/tasks/network/
LINK_PLAN.md §4) のフレームをホストの `tools/host_agent.py` まで運ぶ。
NP21/W では ai-debug のソケットバックエンド (NP2NETSOCK) がこの役をしていた。
橋はその**同じ枠付け** (4B BE 長 + raw Ethernet フレーム) で Agent と話すので、
Agent 側は 1 行も変えずに実機へ差し替えられる。

  実機 --[スイッチ]-- NIC --[AF_PACKET]-- lan_bridge.py --[TCP]-- host_agent.py

Python 標準ライブラリのみ (scapy も Npcap も要らない。Linux 専用)。

── 使い方 (Ubuntu) ───────────────────────────────────────────────────────
AF_PACKET の raw ソケットは root か CAP_NET_RAW が要る。どちらかを選ぶ:

  sudo python3 tools/lan_bridge.py --iface enp3s0
  sudo setcap cap_net_raw+ep "$(readlink -f "$(which python3)")"   # 1 回だけ
      ※ setcap は**その python3 で動く全スクリプト**に権限を与える。共用機では
        root で回すか、専用の python3 を置くこと。

起動順は **host_agent.py が先** (橋は Agent へ繋ぎに行く側が既定):

  python3 tools/host_agent.py --listen 127.0.0.1:8026 --mac <NIC の MAC>
  sudo python3 tools/lan_bridge.py --iface enp3s0 --agent-mac <NIC の MAC>

── MAC が一致していないと片道しか通らない ──────────────────────────────
**この橋は透過で、Ethernet ヘッダを一切書き換えない。**
Host Agent は HELLO (SYN-ACK / ESTABLISHED) で `--mac` の値を送信元として名乗り、
OS32 は以後その MAC 宛にフレームを送る。橋が書き換えない以上、
`host_agent.py --mac` の値が **NIC の MAC と一致していないと**、実機からの返信の
宛先が NIC の MAC でなくなり、NIC (非 promiscuous) が落とす — OS32 → ホストだけ
通ってホスト → OS32 が死ぬ、という分かりにくい片道になる。

そこで `--agent-mac` に Agent へ渡したのと同じ値を教えると、起動時に NIC の MAC と
照合して違えば警告する。どうしても別 MAC で回すときは `--promisc` で逃げられるが、
既定にはしない (取り違えを黙らせてしまうため)。

  python3 tools/lan_bridge.py --iface eth0 --connect 127.0.0.1:8026
  python3 tools/lan_bridge.py --iface eth0 --listen 127.0.0.1:8026   # 橋が待つ側
  python3 tools/lan_bridge.py --iface eth0 --pcap /tmp/os32.pcap     # 両方向を記録
  kill -USR1 <pid>          # 統計を出す (終了時にも出る)
"""
import argparse
import os
import select
import signal
import socket
import struct
import sys
import time

# ---------------------------------------------------------------- ワイヤ定数
LINK_ETHERTYPE = 0x88B5
ETH_HDR = 14
ETH_MIN_FRAME = 60          # Ethernet の最小フレーム (padding の下限)
LINK_HDR = 20
MAX_FRAME = 65536
OPNAME = {1: "HELLO", 2: "REQUEST", 3: "RESPONSE", 4: "DATA", 5: "EOF",
          6: "ACK", 7: "WINDOW", 8: "WDATA", 9: "STATUS", 10: "RELEASE"}

# AF_PACKET の promiscuous 参加 (Python の socket に定数が無いので直値)
SOL_PACKET = 263
PACKET_ADD_MEMBERSHIP = 1
PACKET_MR_PROMISC = 1

RECONNECT_WAIT = 1.0        # Agent への再接続の間合い (秒)
SELECT_TIMEOUT = 1.0


class BridgeError(Exception):
    """運用者に見せる止まり方 (トレースバックを出さない)。"""


class NicGone(ConnectionError):
    """NIC 側の口が閉じた。**Agent の切断とは別物** — 繋ぎ直しても戻らない。

    ConnectionError をそのまま上げると「host_agent が切れた」と表示して Agent へ
    繋ぎ直しに行ってしまう (再接続を回しつづけ、NIC の異常が運用者に見えない)。
    """


def parse_mac(s):
    parts = s.replace("-", ":").split(":")
    if len(parts) != 6:
        raise ValueError("MAC は 6 オクテット: %r" % s)
    return bytes(int(x, 16) for x in parts)


def mac_str(b):
    return ":".join("%02x" % x for x in b)


def describe(frame):
    """デバッグ行用の 1 行要約 (ヘッダが載っていなければ長さだけ)。"""
    if len(frame) < ETH_HDR + LINK_HDR:
        return "%dB" % len(frame)
    op = frame[ETH_HDR]
    sess = struct.unpack("<H", frame[ETH_HDR + 18:ETH_HDR + 20])[0]
    rid = struct.unpack("<I", frame[ETH_HDR + 14:ETH_HDR + 18])[0]
    return "%dB %s flags=%d sess=%d rid=%d %s->%s" % (
        len(frame), OPNAME.get(op, "op%d" % op), frame[ETH_HDR + 1], sess, rid,
        mac_str(frame[6:12]), mac_str(frame[:6]))


# ------------------------------------------------------------ トランスポート
class FrameStream:
    """4B BE 長 + raw Ethernet フレーム (QEMU socket / NP2NETSOCK と同じ枠)。

    **tools/host_agent.py の FrameStream と同じ枠付け**。写しを持つのは橋を
    host_agent.py から独立に動かすため。ずれは試験 framing_matches_host_agent が
    両方の直列化を突き合わせて止める。
    """

    def __init__(self, sock):
        self.sock = sock
        self.buf = b""

    def fileno(self):
        return self.sock.fileno()

    def send(self, frame):
        self.sock.sendall(struct.pack(">I", len(frame)) + frame)

    def recv_frames(self):
        data = self.sock.recv(MAX_FRAME)
        if not data:
            raise ConnectionError("peer closed")
        self.buf += data
        out = []
        while len(self.buf) >= 4:
            n = struct.unpack(">I", self.buf[:4])[0]
            if n == 0 or n > MAX_FRAME:
                raise ConnectionError("bad frame length %d" % n)
            if len(self.buf) < 4 + n:
                break
            out.append(self.buf[4:4 + n])
            self.buf = self.buf[4 + n:]
        return out

    def close(self):
        try:
            self.sock.close()
        except OSError:
            pass


class NicPort:
    """物理 NIC (AF_PACKET SOCK_RAW、EtherType で bind)。"""

    kind = "af_packet"

    def __init__(self, iface, ethertype, promisc=False):
        if not hasattr(socket, "AF_PACKET"):
            raise BridgeError(
                "AF_PACKET が無い (Linux 専用)。この橋は Ubuntu ノートで回す — "
                "Windows では NP21/W の NP2NETSOCK を使うこと")
        self.iface = iface
        self.ethertype = ethertype
        try:
            self.sock = socket.socket(socket.AF_PACKET, socket.SOCK_RAW,
                                      socket.htons(ethertype))
        except PermissionError:
            raise BridgeError(
                "AF_PACKET の raw ソケットを開けない (root か CAP_NET_RAW が要る)。\n"
                "  sudo python3 tools/lan_bridge.py ...            か\n"
                "  sudo setcap cap_net_raw+ep \"$(readlink -f \"$(which python3)\")\"")
        try:
            self.sock.bind((iface, 0))     # proto 0 = socket() の EtherType を保つ
        except OSError as e:
            self.sock.close()
            raise BridgeError("%s を bind できない: %s" % (iface, e))
        self.mac = read_nic_mac(iface)
        if self.mac is None:
            raise BridgeError("%s の MAC を読めない (/sys/class/net/%s/address)"
                              % (iface, iface))
        if promisc:
            self._join_promisc()

    def _join_promisc(self):
        mreq = struct.pack("IHH8s", socket.if_nametoindex(self.iface),
                           PACKET_MR_PROMISC, 0, b"")
        self.sock.setsockopt(SOL_PACKET, PACKET_ADD_MEMBERSHIP, mreq)

    def fileno(self):
        return self.sock.fileno()

    def recv_frames(self):
        return [self.sock.recv(MAX_FRAME)]

    def send(self, frame):
        self.sock.sendto(frame, (self.iface, self.ethertype))

    def close(self):
        try:
            self.sock.close()
        except OSError:
            pass


class FakeNicPort:
    """試験専用の NIC 差し替え口 (`--fake-nic PATH`)。

    root も CAP_NET_RAW も要らずに**中継そのもの**を回すための口。UNIX
    SOCK_STREAM に FrameStream と同じ枠で raw Ethernet フレームを流す。相手
    (試験の贋 OS32) が NIC の代わりになる。実運用では使わない。
    """

    kind = "fake"

    def __init__(self, path, mac):
        self.iface = "fake:%s" % path
        self.mac = mac
        s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        s.connect(path)
        self.stream = FrameStream(s)

    def fileno(self):
        return self.stream.fileno()

    def recv_frames(self):
        return self.stream.recv_frames()

    def send(self, frame):
        self.stream.send(frame)

    def close(self):
        self.stream.close()


class Pcap:
    """両方向を 1 本の pcap (linktype 1 = Ethernet) に記録する。"""

    def __init__(self, path):
        self.f = open(path, "wb")
        self.f.write(struct.pack("<IHHiIII", 0xA1B2C3D4, 2, 4, 0, 0, 65535, 1))

    def write(self, frame):
        t = time.time()
        self.f.write(struct.pack("<IIII", int(t), int((t % 1) * 1e6),
                                 len(frame), len(frame)))
        self.f.write(frame)
        self.f.flush()

    def close(self):
        self.f.close()


# ------------------------------------------------------------------ 中継本体
class Bridge:
    """NIC ↔ Agent の双方向中継。**Ethernet ヘッダは書き換えない (透過)**。"""

    def __init__(self, nic, agent, ethertype=LINK_ETHERTYPE, pcap=None, quiet=False):
        self.nic = nic
        self.agent = agent
        self.ethertype = ethertype
        self.pcap = pcap
        self.quiet = quiet
        self.stats = {"nic_to_agent": 0, "agent_to_nic": 0,
                      "drop_ethertype": 0, "drop_short": 0,
                      "padded": 0, "tx_error": 0, "reconnect": 0}
        # SIGUSR1 は**旗を立てるだけ**。ハンドラから print すると、主流が print の
        # 途中だったときに "reentrant call inside <_io.BufferedWriter>" で
        # **橋ごと落ちる** (負荷をかけると 20 回に 1 回ほど踏んだ)。
        self.report_request = None

    def log(self, *a):
        if not self.quiet:
            print(*a, flush=True)

    def accept(self, frame):
        """EtherType 0x88B5 のフレームだけ通す (他所の通信を Agent へ流さない)。"""
        if len(frame) < ETH_HDR:
            self.stats["drop_short"] += 1
            return False
        if struct.unpack(">H", frame[12:14])[0] != self.ethertype:
            self.stats["drop_ethertype"] += 1
            return False
        return True

    def from_nic(self):
        try:
            frames = self.nic.recv_frames()
        except NicGone:
            raise
        except ConnectionError as e:
            raise NicGone(str(e))
        for frame in frames:
            if not self.accept(frame):
                continue
            if self.pcap:
                self.pcap.write(frame)
            self.agent.send(frame)
            self.stats["nic_to_agent"] += 1
            self.log("NIC -> agent %s" % describe(frame))

    def from_agent(self):
        for frame in self.agent.recv_frames():
            if not self.accept(frame):
                continue
            if len(frame) < ETH_MIN_FRAME:
                frame += b"\x00" * (ETH_MIN_FRAME - len(frame))
                self.stats["padded"] += 1
            if self.pcap:
                self.pcap.write(frame)
            try:
                self.nic.send(frame)
            except ConnectionError as e:
                raise NicGone(str(e))            # 口が閉じた = 繋ぎ直しでは戻らない
            except OSError as e:
                # 線が落ちている / 送信バッファが溢れた等。**橋は止めない** —
                # Agent も実機も生きているので、数えて次のフレームへ進む。
                self.stats["tx_error"] += 1
                self.log("NIC への送信に失敗: %s" % e)
                continue
            self.stats["agent_to_nic"] += 1
            self.log("agent -> NIC %s" % describe(frame))

    def pump(self, timeout=SELECT_TIMEOUT):
        """1 回だけ select して読めた側を捌く。戻り値は動いたかどうか。"""
        try:
            ready, _, _ = select.select([self.nic, self.agent], [], [], timeout)
        except OSError as e:
            raise ConnectionError(str(e))
        for src in ready:
            if src is self.nic:
                self.from_nic()
            else:
                self.from_agent()
        return bool(ready)

    def request_report(self, why):
        """シグナルハンドラから呼ぶ。**代入だけ** (I/O もロックも取らない)。"""
        self.report_request = why

    def run(self):
        while True:
            self.pump()
            if self.report_request:
                why, self.report_request = self.report_request, None
                self.report(why)

    def report(self, why=""):
        print("lan_bridge stats%s: %s" % (" (%s)" % why if why else "",
                                          " ".join("%s=%d" % kv for kv in
                                                   sorted(self.stats.items()))),
              flush=True)


# ------------------------------------------------------------------ 口を開く
def read_nic_mac(iface):
    try:
        with open("/sys/class/net/%s/address" % iface) as f:
            return parse_mac(f.read().strip())
    except (OSError, ValueError):
        return None


def open_agent(args):
    """host_agent.py の FrameStream へ繋ぐ (既定) か、待つ (`--listen`)。"""
    if args.listen:
        if args.listen.startswith("unix:"):
            path = args.listen[len("unix:"):]
            if os.path.exists(path):
                os.unlink(path)
            srv = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
            srv.bind(path)
        else:
            host, port = args.listen.rsplit(":", 1)
            srv = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
            srv.bind((host, int(port)))
        srv.listen(1)
        print("waiting for host_agent on %s" % args.listen, flush=True)
        conn, _ = srv.accept()
        srv.close()
        print("host_agent attached", flush=True)
        return FrameStream(conn)
    if args.connect.startswith("unix:"):
        s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        s.connect(args.connect[len("unix:"):])
    else:
        host, port = args.connect.rsplit(":", 1)
        s = socket.create_connection((host, int(port)))
    print("connected to host_agent at %s" % args.connect, flush=True)
    return FrameStream(s)


def open_nic(args, ethertype):
    if args.fake_nic:
        return FakeNicPort(args.fake_nic, parse_mac(args.nic_mac))
    return NicPort(args.iface, ethertype, promisc=args.promisc)


def check_macs(nic, agent_mac):
    """Agent の名乗る MAC と NIC の MAC がずれていないか (片道になる罠)。"""
    if agent_mac is None:
        print("note: host_agent.py には --mac %s (この NIC の MAC) を渡すこと。"
              "橋は Ethernet ヘッダを書き換えないので、ずれると実機からの返信が "
              "NIC に落とされて片道になる" % mac_str(nic.mac), flush=True)
        return True
    if parse_mac(agent_mac) != nic.mac:
        print("WARNING: host_agent の MAC (%s) が NIC %s の MAC (%s) と違う。\n"
              "         橋は透過なので、実機からの返信は NIC の MAC 宛にならず "
              "落とされる。\n"
              "         host_agent.py --mac %s で揃えるか、--promisc で受けること。"
              % (agent_mac, nic.iface, mac_str(nic.mac), mac_str(nic.mac)),
              flush=True)
        return False
    return True


def main(argv=None):
    ap = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--iface", default="eth0", metavar="NAME",
                    help="OS32 が繋がっている物理 NIC (既定 eth0)")
    ap.add_argument("--connect", default="127.0.0.1:8026", metavar="HOST:PORT",
                    help="host_agent.py --listen へ繋ぐ (既定 127.0.0.1:8026、"
                         "unix:PATH も可)")
    ap.add_argument("--listen", metavar="HOST:PORT",
                    help="逆に橋が待つ (host_agent.py --connect と組む)")
    ap.add_argument("--ethertype", default="0x88B5", metavar="HEX",
                    help="拾う EtherType (既定 0x88B5 = OS32 リンク)")
    ap.add_argument("--agent-mac", metavar="MAC",
                    help="host_agent.py --mac に渡した値。NIC の MAC と照合する")
    ap.add_argument("--promisc", action="store_true",
                    help="NIC を promiscuous にする (MAC がずれたときの逃げ道)")
    ap.add_argument("--pcap", metavar="FILE", help="両方向を pcap に記録する")
    ap.add_argument("--quiet", action="store_true", help="フレームごとの行を出さない")
    ap.add_argument("--once", action="store_true",
                    help="Agent が切れたら終わる (再接続しない)")
    ap.add_argument("--fake-nic", metavar="PATH",
                    help="**試験専用** NIC の代わりに UNIX ソケットを使う")
    ap.add_argument("--nic-mac", default="02:00:5e:00:00:01", metavar="MAC",
                    help="--fake-nic のときに名乗る MAC (試験用)")
    args = ap.parse_args(argv)

    try:
        ethertype = int(args.ethertype, 0)
        nic = open_nic(args, ethertype)
    except BridgeError as e:
        print("lan_bridge: %s" % e, file=sys.stderr, flush=True)
        return 2
    except (OSError, ValueError) as e:
        print("lan_bridge: NIC を開けない: %s" % e, file=sys.stderr, flush=True)
        return 2

    print("lan_bridge on %s mac %s ethertype 0x%04X (%s)"
          % (nic.iface, mac_str(nic.mac), ethertype, nic.kind), flush=True)
    check_macs(nic, args.agent_mac)

    pcap = Pcap(args.pcap) if args.pcap else None
    bridge = Bridge(nic, None, ethertype, pcap=pcap, quiet=args.quiet)
    if hasattr(signal, "SIGUSR1"):
        signal.signal(signal.SIGUSR1,
                      lambda *_: bridge.request_report("SIGUSR1"))

    rc = 0
    try:
        while True:
            try:
                bridge.agent = open_agent(args)
            except OSError as e:
                print("host_agent への接続に失敗: %s" % e, flush=True)
                if args.once:
                    rc = 1
                    break
                time.sleep(RECONNECT_WAIT)
                continue
            try:
                bridge.run()
            except NicGone as e:
                print("NIC の口が閉じた: %s" % e, flush=True)
                break
            except ConnectionError as e:
                print("host_agent が切れた: %s" % e, flush=True)
                bridge.agent.close()
                bridge.agent = None
                if args.once:
                    break
                bridge.stats["reconnect"] += 1
                time.sleep(RECONNECT_WAIT)
    except KeyboardInterrupt:
        pass
    finally:
        bridge.report("exit")
        nic.close()
        if pcap:
            pcap.close()
    return rc


if __name__ == "__main__":
    sys.exit(main())
