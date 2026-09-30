#!/usr/bin/env python3
"""L-D: tools/lan_bridge.py (AF_PACKET ↔ FrameStream の橋) のホスト TDD。

票:   docs/tasks/realhw/TASK_LAN_82557.md §2 L-D / §5-1
記録: tools/tests/lan_bridge_tdd.md

**root も実 NIC も要らない。** 橋の `--fake-nic PATH` (NIC の差し替え口) に
贋 OS32 を UNIX ソケットで繋ぎ、反対側には **実物の host_agent.py** を
`--unix` で子プロセス起動する (tools/tests/test_net_link.py と同じ作法)。
通るのは実際の中継経路そのもの:

    贋 OS32 --[UNIX]-- lan_bridge.py --[UNIX]-- host_agent.py (実物)

贋 OS32 は **フレームも枠付けも自前で組む** (lan_bridge.py の直列化を借りない)。
借りると「橋と試験が同じだけ間違っている」を見逃す。

実 NIC (AF_PACKET) の経路はここでは踏めない — 実機と Ubuntu ノートで PM が見る。

  python3 -B tools/tests/test_lan_bridge.py            # 全ケース
  python3 -B tools/tests/test_lan_bridge.py --mutate   # 否定側 (変異が RED か)
"""
import importlib.util
import os
import pathlib
import re
import select
import signal
import socket
import struct
import subprocess
import sys
import tempfile
import time

ROOT = pathlib.Path(__file__).resolve().parents[2]
BRIDGE_SRC = ROOT / "tools/lan_bridge.py"
HOST_AGENT = ROOT / "tools/host_agent.py"

LINK_ETHERTYPE = 0x88B5
AGENT_MAC = bytes.fromhex("02005e000001")     # host_agent.py の既定
NIC_MAC = bytes.fromhex("02005e000001")       # 橋が名乗る NIC の MAC (既定で一致)
OS32_MAC = bytes.fromhex("02005e000002")

OP_HELLO, OP_REQUEST, OP_RESPONSE = 1, 2, 3
HS_SYN, HS_SYNACK, HS_CONFIRM, HS_ESTAB = 0, 1, 2, 3

# 変異のときだけ差し替わる「試験対象の lan_bridge.py」。
BRIDGE = BRIDGE_SRC


def mac_str(b):
    return ":".join("%02x" % x for x in b)


def load_bridge(path):
    """試験対象の lan_bridge.py を (写しでも) 読み込む。"""
    spec = importlib.util.spec_from_file_location("lan_bridge_under_test", str(path))
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


# --------------------------------------------------------------- 贋 OS32
class FakeOS32:
    """NIC の代わりの UNIX ソケットへ raw Ethernet フレームを流す贋 OS32。

    枠付け (4B BE 長 + フレーム) もリンクヘッダ (20B LE) も**自前で組む**。
    """

    def __init__(self, conn):
        self.conn = conn
        self.buf = b""
        self.sess = 0
        self.epoch = 1
        self.nonce = 0x1000

    def frame(self, op, flags=0, seq=0, ack=0, rid=0, payload=b"",
              sess=None, epoch=None, ethertype=LINK_ETHERTYPE, dst=AGENT_MAC):
        hdr = (struct.pack("<BB", op, flags) +
               struct.pack("<H", self.epoch if epoch is None else epoch) +
               struct.pack("<I", seq) + struct.pack("<I", ack) +
               struct.pack("<H", len(payload)) + struct.pack("<I", rid) +
               struct.pack("<H", self.sess if sess is None else sess))
        assert len(hdr) == 20
        f = dst + OS32_MAC + struct.pack(">H", ethertype) + hdr + payload
        if len(f) < 60:
            f += b"\x00" * (60 - len(f))
        return f

    def send_raw(self, frame):
        self.conn.sendall(struct.pack(">I", len(frame)) + frame)

    def send(self, *a, **kw):
        self.send_raw(self.frame(*a, **kw))

    def recv(self, timeout=2.0):
        """次の 1 フレームを返す。期限切れは None。"""
        end = time.monotonic() + timeout
        while True:
            if len(self.buf) >= 4:
                n = struct.unpack(">I", self.buf[:4])[0]
                assert 0 < n <= 65536, "橋から出た枠の長さが壊れている: %d" % n
                if len(self.buf) >= 4 + n:
                    f, self.buf = self.buf[4:4 + n], self.buf[4 + n:]
                    return f
            left = end - time.monotonic()
            if left <= 0:
                return None
            r, _, _ = select.select([self.conn], [], [], left)
            if not r:
                return None
            data = self.conn.recv(65536)
            if not data:
                return None
            self.buf += data

    @staticmethod
    def parse(frame):
        """(ethertype, op, flags, epoch, seq, ack, rid, sess, payload)。"""
        et = struct.unpack(">H", frame[12:14])[0]
        op, flags, epoch, seq, ack, plen, rid, sess = \
            struct.unpack("<BBHIIHIH", frame[14:34])
        return (et, op, flags, epoch, seq, ack, rid, sess, frame[34:34 + plen])

    # ---- L0 (3 way HELLO) ----
    def hello(self):
        self.send(OP_HELLO, HS_SYN, seq=self.nonce, sess=0)
        f = self.recv()
        assert f is not None, "SYN-ACK が橋を通って返ってこない (agent -> NIC が死んでいる)"
        et, op, flags, epoch, seq, ack, rid, sess, pl = self.parse(f)
        assert et == LINK_ETHERTYPE, "EtherType が 0x88B5 でない: 0x%04X" % et
        assert (op, flags) == (OP_HELLO, HS_SYNACK), (op, flags)
        assert seq == self.nonce and len(pl) == 6, (seq, len(pl))
        self.sess = sess
        self.send(OP_HELLO, HS_CONFIRM, seq=self.nonce, ack=ack, sess=sess)
        f = self.recv()
        assert f is not None, "ESTABLISHED が返ってこない"
        et, op, flags, epoch, seq, ack2, rid, sess2, pl = self.parse(f)
        assert (op, flags) == (OP_HELLO, HS_ESTAB), (op, flags)
        assert sess2 == sess and len(pl) == 2, (sess2, sess, len(pl))
        return sess

    def ping(self, rid=1):
        """REQUEST PING -> (制御か, status, length)。ACK が先に来るので読み飛ばす。"""
        self.send(OP_REQUEST, rid=rid, seq=0, payload=b"PING")
        for _ in range(4):
            f = self.recv()
            assert f is not None, "RESPONSE が返ってこない"
            et, op, flags, epoch, seq, ack, r, sess, pl = self.parse(f)
            if op != OP_RESPONSE:
                continue                         # ACK (6) などは飛ばす
            assert r == rid, (op, r)
            status, length = struct.unpack("<HI", pl)
            return flags & 1, status, length
        raise AssertionError("RESPONSE が 4 フレーム以内に来ない")


# ------------------------------------------------------------------- 足場
class Rig:
    """贋 OS32 ↔ 橋 (子プロセス) ↔ 実 host_agent (子プロセス) を組む。"""

    def __init__(self, tmp, bridge_args=(), agent_mac=AGENT_MAC, nic_mac=NIC_MAC):
        self.tmp = pathlib.Path(tmp)
        self.nic_path = str(self.tmp / "nic.sock")
        self.agent_path = str(self.tmp / "agent.sock")
        self.srv = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        self.srv.bind(self.nic_path)
        self.srv.listen(1)
        self.srv.settimeout(10)
        self.agent = subprocess.Popen(
            [sys.executable, "-B", str(HOST_AGENT), "--unix", self.agent_path,
             "--offline", "--quiet", "--once", "--mac", mac_str(agent_mac),
             "--state-dir", str(self.tmp / "state"), "--agent-gen", "1"],
            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        self._wait_for(self.agent_path)
        self.bridge = subprocess.Popen(
            [sys.executable, "-B", str(BRIDGE), "--fake-nic", self.nic_path,
             "--connect", "unix:" + self.agent_path,
             "--nic-mac", mac_str(nic_mac), *bridge_args],
            stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
        conn, _ = self.srv.accept()
        conn.settimeout(10)
        self.os32 = FakeOS32(conn)

    def _wait_for(self, path, timeout=10.0):
        end = time.monotonic() + timeout
        while time.monotonic() < end:
            if os.path.exists(path):
                return
            assert self.agent.poll() is None, "host_agent が起動直後に死んだ"
            time.sleep(0.02)
        raise AssertionError("host_agent が %s を作らない" % path)

    def close(self, sigusr1=False):
        """統計を出させて畳み、橋の標準出力を返す。

        NIC の口を閉じれば橋は**自分で**終わる (NicGone)。SIGINT は保険だけ —
        print の本文と改行の間に割り込むと統計行が前の行に繋がる。
        """
        out = ""
        try:
            if sigusr1 and self.bridge.poll() is None:
                self.bridge.send_signal(signal.SIGUSR1)
                # 橋はハンドラでは旗を立てるだけ (再入する print で落ちないため)。
                # 主ループが select の期限 (1 秒) で起きてから統計を出す。
                time.sleep(1.8)
            try:
                self.os32.conn.close()
            except OSError:
                pass
            out = self.bridge.communicate(timeout=10)[0] or ""
        except subprocess.TimeoutExpired:
            self.bridge.kill()
            out = self.bridge.communicate()[0] or ""
        finally:
            for p in (self.agent,):
                if p.poll() is None:
                    p.terminate()
                    try:
                        p.wait(timeout=5)
                    except subprocess.TimeoutExpired:
                        p.kill()
            try:
                self.srv.close()
            except OSError:
                pass
        return out


def stats_of(out):
    """`lan_bridge stats (...): a=1 b=2` の最後の行を辞書にする。"""
    m = re.findall(r"lan_bridge stats[^:]*:([^\n]*)", out)
    assert m, "統計行が出ていない:\n%s" % out
    return {k: int(v) for k, v in re.findall(r"(\w+)=(-?\d+)", m[-1])}


# ------------------------------------------------------------------ ケース
def framing_matches_host_agent():
    """橋の枠付けが host_agent.py の FrameStream と**バイト単位で**同じか。

    橋は自前の写しを持つ (host_agent.py に依存させない) ので、ずれをここで止める。
    """
    sys.path.insert(0, str(ROOT / "tools"))
    import host_agent as HA                       # noqa: E402
    LB = load_bridge(BRIDGE)
    a, b = socket.socketpair()
    ha, lb = HA.FrameStream(a), LB.FrameStream(b)
    payload = bytes(range(64))
    lb.send(payload)                              # 橋が組んだ枠を Agent が解く
    assert ha.recv_frames() == [payload], "Agent が橋の枠を解けない"
    ha.send(payload)                              # Agent が組んだ枠を橋が解く
    assert lb.recv_frames() == [payload], "橋が Agent の枠を解けない"
    # 枠そのもの: 4B BE 長 + フレーム
    c, d = socket.socketpair()
    LB.FrameStream(c).send(b"\xaa" * 7)
    raw = d.recv(64)
    assert raw == struct.pack(">I", 7) + b"\xaa" * 7, raw
    for s in (a, b, c, d):
        s.close()


def l0_hello_roundtrip():
    """L0 相当: 3 way HELLO + PING が橋を**両方向**通る。"""
    with tempfile.TemporaryDirectory(prefix="os32-lanbr-") as tmp:
        rig = Rig(tmp)
        try:
            sess = rig.os32.hello()
            assert sess != 0, "sess が採番されていない"
            assert rig.os32.ping() == (0, 200, 0), "PING が 200 で返らない"
        finally:
            out = rig.close()
        st = stats_of(out)
        assert st["nic_to_agent"] >= 3 and st["agent_to_nic"] >= 3, st


def foreign_ethertype_is_dropped():
    """0x88B5 以外は Agent へ流さない (他所の通信で Agent を叩かない)。"""
    with tempfile.TemporaryDirectory(prefix="os32-lanbr-") as tmp:
        rig = Rig(tmp)
        try:
            # IPv4 を騙った HELLO SYN。素通りすると Agent が SYN-ACK を返す。
            rig.os32.send_raw(rig.os32.frame(OP_HELLO, HS_SYN, seq=0x2000,
                                             sess=0, ethertype=0x0800))
            assert rig.os32.recv(timeout=1.0) is None, \
                "EtherType 0x0800 のフレームが Agent まで通った"
            rig.os32.hello()                      # 橋は生きている
        finally:
            out = rig.close()
        st = stats_of(out)
        assert st["drop_ethertype"] >= 1, st


def short_frame_is_dropped():
    """Ethernet ヘッダにも満たないフレームは捨てる (index エラーで死なない)。"""
    with tempfile.TemporaryDirectory(prefix="os32-lanbr-") as tmp:
        rig = Rig(tmp)
        try:
            rig.os32.send_raw(b"\x01\x02\x03\x04\x05")
            assert rig.os32.recv(timeout=1.0) is None
            rig.os32.hello()
        finally:
            out = rig.close()
        st = stats_of(out)
        assert st["drop_short"] >= 1, st


def stats_on_sigusr1_and_exit():
    """統計は SIGUSR1 でも終了時でも出る (運用中に覗ける)。"""
    with tempfile.TemporaryDirectory(prefix="os32-lanbr-") as tmp:
        rig = Rig(tmp)
        try:
            rig.os32.hello()
        finally:
            out = rig.close(sigusr1=True)
        assert "lan_bridge stats (SIGUSR1)" in out, out
        assert "lan_bridge stats (exit)" in out, out
        assert stats_of(out)["nic_to_agent"] >= 2


def pcap_records_both_directions():
    """--pcap が両方向を 1 本の pcap (linktype 1) に残す。"""
    with tempfile.TemporaryDirectory(prefix="os32-lanbr-") as tmp:
        path = os.path.join(tmp, "cap.pcap")
        rig = Rig(tmp, bridge_args=("--pcap", path))
        try:
            rig.os32.hello()
        finally:
            rig.close()
        blob = open(path, "rb").read()
        magic, _, _, _, _, snap, link = struct.unpack("<IHHiIII", blob[:24])
        assert magic == 0xA1B2C3D4 and link == 1, (hex(magic), link)
        n, off = 0, 24
        while off + 16 <= len(blob):
            _, _, caplen, _ = struct.unpack("<IIII", blob[off:off + 16])
            off += 16 + caplen
            n += 1
        assert n >= 4, "pcap のレコードが %d 本しかない" % n


def mac_mismatch_warns():
    """--agent-mac が NIC の MAC と違えば警告する (片道になる罠)。"""
    with tempfile.TemporaryDirectory(prefix="os32-lanbr-") as tmp:
        rig = Rig(tmp, bridge_args=("--agent-mac", "02:00:5e:00:00:09"))
        out = rig.close()
        assert "WARNING" in out and "02:00:5e:00:00:09" in out, out
    with tempfile.TemporaryDirectory(prefix="os32-lanbr-") as tmp:
        rig = Rig(tmp, bridge_args=("--agent-mac", mac_str(NIC_MAC)))
        out = rig.close()
        assert "WARNING" not in out, out


def af_packet_needs_root():
    """実 NIC を開けないときは分かる形で止まる (CAP_NET_RAW を名指す)。"""
    if hasattr(os, "geteuid") and os.geteuid() == 0:
        print("     SKIP af_packet_needs_root (root なので権限で止まらない)", flush=True)
        return
    r = subprocess.run([sys.executable, "-B", str(BRIDGE), "--iface", "lo",
                        "--once"], capture_output=True, text=True, timeout=30)
    assert r.returncode == 2, (r.returncode, r.stdout, r.stderr)
    msg = r.stdout + r.stderr
    assert "CAP_NET_RAW" in msg and "setcap" in msg, msg


def sigusr1_while_busy_keeps_the_bridge_alive():
    """**忙しいとき**の SIGUSR1 で橋が落ちない。

    シグナルハンドラから直に `print` すると、主流がちょうど `print` の中だった
    ときに `RuntimeError: reentrant call inside <_io.BufferedWriter>` が
    **割り込まれた側の print から**上がり、橋ごと死ぬ。並列で回すと踏んだ
    (20 回に 1 回ほど)。ハンドラは旗を立てるだけにしてある。
    """
    with tempfile.TemporaryDirectory(prefix="os32-lanbr-") as tmp:
        rig = Rig(tmp)
        try:
            rig.os32.hello()
            for _ in range(400):
                if rig.bridge.poll() is not None:
                    break
                # rid 0 は Agent が捨てる (返信が溜まらない)。橋は 1 行出す。
                rig.os32.send(OP_REQUEST, rid=0, seq=0, payload=b"PING")
                try:
                    rig.bridge.send_signal(signal.SIGUSR1)
                except (ProcessLookupError, OSError):
                    break
            assert rig.bridge.poll() is None, "SIGUSR1 の連打で橋が落ちた"
            assert rig.os32.ping(rid=2) == (0, 200, 0), "嵐の後に中継が死んでいる"
        finally:
            out = rig.close()
        assert "Traceback" not in out, out
        assert "reentrant" not in out, out


CASES = [
    framing_matches_host_agent,
    l0_hello_roundtrip,
    foreign_ethertype_is_dropped,
    short_frame_is_dropped,
    stats_on_sigusr1_and_exit,
    sigusr1_while_busy_keeps_the_bridge_alive,
    pcap_records_both_directions,
    mac_mismatch_warns,
    af_packet_needs_root,
]

# 否定側。**実物は 1 バイトも触らず**写しの上で 1 か所だけ壊す (= check-par で
# 並列に回せる)。(パターン, 置換, 説明)
MUTATIONS = [
    (r'if struct\.unpack\(">H", frame\[12:14\]\)\[0\] != self\.ethertype:',
     "if False:",
     "EtherType のフィルタを外す (0x88B5 以外も Agent へ流す)"),
    (r'struct\.pack\(">I", len\(frame\)\)',
     'struct.pack(">I", len(frame) + 1)',
     "枠付けの長さ前置きを 1 バイトずらす"),
    (r'self\.nic\.send\(frame\)',
     "pass",
     "逆向き (agent -> NIC) を落とす"),
    (r'n = struct\.unpack\(">I", self\.buf\[:4\]\)\[0\]',
     'n = struct.unpack("<I", self.buf[:4])[0]',
     "長さ前置きのバイト順を LE にする"),
    (r"self\.report_request = why",
     "self.report(why)",
     "SIGUSR1 のハンドラから直に print する (再入で橋が落ちる)"),
]


def run(cases):
    failed = 0
    for c in cases:
        try:
            c()
            print("  ok   %s" % c.__name__, flush=True)
        except AssertionError as e:
            print("  FAIL %s: %s" % (c.__name__, e), flush=True)
            failed += 1
        except Exception as e:                       # noqa: BLE001
            print("  ERROR %s: %r" % (c.__name__, e), flush=True)
            failed += 1
    print("SUMMARY %d/%d PASS" % (len(cases) - failed, len(cases)), flush=True)
    return failed


RACE_RETRIES = 30


def mutate(tmp):
    global BRIDGE
    original = BRIDGE_SRC.read_text(encoding="utf-8")
    bad = 0
    # 変異に関係しうるケースだけ回す (実 NIC の権限ケースは無関係)。
    cases = [framing_matches_host_agent, l0_hello_roundtrip,
             foreign_ethertype_is_dropped, short_frame_is_dropped,
             sigusr1_while_busy_keeps_the_bridge_alive]
    for i, (pattern, repl, why) in enumerate(MUTATIONS, 1):
        text, n = re.subn(pattern, repl, original, count=1)
        if n != 1:
            print("MUTATION %d NOT APPLICABLE: %s" % (i, why), flush=True)
            bad += 1
            continue
        copy = pathlib.Path(tmp) / ("lan_bridge_mut%d.py" % i)
        copy.write_text(text, encoding="utf-8")
        BRIDGE = copy
        hits = 0
        for c in cases:
            try:
                c()
            except Exception:                        # noqa: BLE001
                hits += 1
        # 再入 (sigusr1_while_busy_…) は競合を確率で踏む。CPU が埋まっていると 1 回では
        # 踏まないことがある (make check の check-par が変異を並列に回すようになって
        # 2 回続けて見逃した、2026-09-26 TASK_CHECK_MUT_PARALLEL)。どのケースも落ちなければ
        # その 1 ケースだけ回し直す — 1 回でも落ちれば RED (実物の側は 1 回で通ること)。
        for _ in range(RACE_RETRIES if not hits else 0):
            try:
                sigusr1_while_busy_keeps_the_bridge_alive()
            except Exception:                        # noqa: BLE001
                hits += 1
                break
        status = "RED" if hits else "**GREEN (見逃し)**"
        print("MUTATION %d %s (%d 件): %s" % (i, status, hits, why), flush=True)
        bad += not hits
    BRIDGE = BRIDGE_SRC
    assert BRIDGE_SRC.read_text(encoding="utf-8") == original, \
        "実物の lan_bridge.py が書き換わっている"
    return bad


def main():
    args = sys.argv[1:]
    want = [a for a in args if not a.startswith("--")]
    rc = run([c for c in CASES if not want or c.__name__ in want])
    if "--mutate" in args:
        with tempfile.TemporaryDirectory(prefix="os32-lanbr-mut-") as tmp:
            rc += mutate(tmp)
    return 1 if rc else 0


if __name__ == "__main__":
    sys.exit(main())
