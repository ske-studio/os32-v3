#!/usr/bin/env python3
"""N1 段 1: tools/host_agent.py v2 の TDD (TASK_N0 §3 の Agent 側項目)。

贋 OS32 は**フレームを直接組む** (Agent の build を借りない) ので、20B ヘッダの
直列化・op ごとの payload 長検査・sess / epoch の照合もここで踏む。
Agent は実物 (tools/host_agent.py の HostAgent) をそのまま import し、出口だけ
ListSink に差し替える。ネットワークにもエミュレータにも触らない。

  python3 -B tools/tests/test_host_agent.py [ケース名 ...]

ケース名は TASK_N0 §3 の指摘番号 (往復 2 = r2_*, 往復 3 = r3_*, 往復 4 = r4_*)
を頭に付ける。対応表は tools/tests/n1_tdd.md。
"""
import base64
import contextlib
import os
import pathlib
import random
import struct
import subprocess
import sys
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))

import host_agent as HA          # noqa: E402  (パスを通した後で読む)

OS32_MAC = bytes.fromhex("02005e000002")
AGENT_MAC = bytes.fromhex("02005e000001")


# ------------------------------------------------------------- 贋 OS32
class FakeOS32:
    """フレームを直接組む贋 OS32。Agent の直列化を借りない。"""

    def __init__(self, agent, mac=OS32_MAC):
        self.agent = agent
        self.mac = mac
        self.sink = HA.ListSink()
        self.sess = 0
        self.epoch = 1
        self.nonce = 0x1000

    # ---- 直列化 (host_agent.py と独立に組む) ----
    def frame(self, op, flags=0, epoch=None, seq=0, ack=0, rid=0, payload=b"",
              sess=None, dst=None, length=None):
        ep = self.epoch if epoch is None else epoch
        ss = self.sess if sess is None else sess
        ln = len(payload) if length is None else length
        hdr = (struct.pack("<BB", op & 0xFF, flags & 0xFF) +
               struct.pack("<H", ep & 0xFFFF) +
               struct.pack("<I", seq & 0xFFFFFFFF) +
               struct.pack("<I", ack & 0xFFFFFFFF) +
               struct.pack("<H", ln & 0xFFFF) +
               struct.pack("<I", rid & 0xFFFFFFFF) +
               struct.pack("<H", ss & 0xFFFF))
        assert len(hdr) == 20
        f = (dst or AGENT_MAC) + self.mac + struct.pack(">H", HA.LINK_ETHERTYPE) + hdr + payload
        if len(f) < 60:
            f += b"\x00" * (60 - len(f))
        return f

    def send(self, *a, **kw):
        """1 フレーム渡して、その結果 Agent が出したフレームを返す。"""
        self.sink.frames = []
        self.agent.handle(self.frame(*a, **kw), self.sink)
        return [parse(f) for f in self.sink.frames]

    def pump(self):
        """常駐ループの 1 周相当: agent.tick を回して出たフレームを返す。"""
        self.sink.frames = []
        self.agent.tick(self.sink)
        return [parse(f) for f in self.sink.frames]

    # ---- 3 way HELLO ----
    def hello(self, sess=None, epoch=None, nonce=None):
        self.nonce = nonce if nonce is not None else self.nonce + 1
        if epoch is not None:
            self.epoch = epoch
        out = self.send(HA.OP_HELLO, HA.HS_SYN, epoch=self.epoch, seq=self.nonce,
                        sess=self.sess if sess is None else sess)
        if not out:
            return None
        sa = out[0]
        self.sess, self.epoch = sa["sess"], sa["epoch"]
        out2 = self.send(HA.OP_HELLO, HA.HS_CONFIRM, seq=self.nonce, ack=sa["ack"])
        return (sa, out2[0] if out2 else None)


def parse(frame):
    op, flags, epoch, seq, ack, plen, rid, sess = HA.hdr_unpack(frame[HA.ETH_HDR:])
    return {"op": op, "flags": flags, "epoch": epoch, "seq": seq, "ack": ack,
            "len": plen, "rid": rid, "sess": sess,
            "pl": frame[HA.ETH_HDR + HA.LINK_HDR:HA.ETH_HDR + HA.LINK_HDR + plen],
            "src": frame[6:12], "dst": frame[:6]}


def resp(p):
    """RESPONSE の (制御か?, status, length)。"""
    st, ln = struct.unpack("<HI", p["pl"])
    return (bool(p["flags"] & HA.F_CTRL), st, ln)


def new_agent(**kw):
    kw.setdefault("quiet", True)
    kw.setdefault("agent_gen", 0x4242)
    kw.setdefault("rng", random.Random(7))
    kw.setdefault("allow_net", False)
    return HA.HostAgent(AGENT_MAC, **kw)


def up(**kw):
    """HELLO 済みの (agent, os32) を作る。"""
    a = new_agent(**kw)
    o = FakeOS32(a)
    o.hello()
    return a, o


def only(out, op):
    got = [p for p in out if p["op"] == op]
    assert len(got) == 1, "op %d が %d 本 (全部: %s)" % (op, len(got),
                                                        [p["op"] for p in out])
    return got[0]


def read_body(o, rid, credit=64):
    """WINDOW を送って DATA ストリームを読み切り、本文バイト列を返す。"""
    got = b""
    seq = 0
    for _ in range(200):
        out = o.send(HA.OP_WINDOW, rid=rid, ack=seq, payload=struct.pack("<H", credit))
        data = [p for p in out if p["op"] == HA.OP_DATA]
        eof = [p for p in out if p["op"] == HA.OP_EOF]
        for d in data:
            assert d["seq"] == seq + 1, (d["seq"], seq)
            seq = d["seq"]
            got += d["pl"]
        if eof:
            break
        if not data:
            break
    return got


# ---- CLIP wsl backend を叩く subprocess のスタブ -------------------------
class FakeProc:
    """spawn() が返す贋の子プロセス。running=True の間 poll() は None。

    step() で「子を進める」= 完了させる。out は完了後に output() が返す stdout。
    raise_on_output を渡すと output() でその例外を投げる (TimeoutExpired 等)。
    """

    def __init__(self, out=b"", rc=0, raise_on_output=None):
        self.out = out
        self.rc = rc
        self.raise_on_output = raise_on_output
        self.running = True
        self.killed = False
        self.stdin = None

    def step(self):
        self.running = False

    def poll(self):
        return None if self.running else self.rc

    def output(self):
        if self.raise_on_output is not None:
            raise self.raise_on_output
        return self.out

    def kill(self):
        self.killed = True
        self.running = False


class FakeSpawn:
    """HA.spawn の差し替え。起動された子と stdin を記録する。"""

    def __init__(self, script):
        # script: 呼ばれるたびに返す FakeProc のリスト (順に消費)。
        self.script = list(script)
        self.calls = []           # (cmd, stdin_bytes) の記録
        self.procs = []

    def __call__(self, cmd, stdin_bytes=None):
        self.calls.append((cmd, stdin_bytes))
        proc = self.script.pop(0)
        proc.stdin = stdin_bytes
        self.procs.append(proc)
        return proc


class RealB64Spawn:
    """実 _RealProc を起こす spawn (blocker B7 の受入用、FakeProc は背圧を模さない)。

    cmd は無視し、`unit * count` の base64 を stdout に吐く実子プロセスを起こす。
    payload は子の中で生成する (argv 長制限 MAX_ARG_STRLEN を避ける)。stdout は
    _RealProc の一時ファイルへ流れるので base64 が 64KB を超えても子は詰まらない。
    """

    def __init__(self, unit, count):
        self.unit = unit
        self.count = count
        self.calls = []
        self.procs = []

    def __call__(self, cmd, stdin_bytes=None):
        self.calls.append((cmd, stdin_bytes))
        script = ("import sys,base64;"
                  "sys.stdout.buffer.write(base64.b64encode(%r*%d))"
                  % (self.unit, self.count))
        proc = HA._RealProc([sys.executable, "-c", script])
        self.procs.append(proc)
        return proc

    def wait_all(self):
        # nb3: timeout を付け、B7 再発 (パイプ詰まり) でスイートがハングしない。
        for p in self.procs:
            try:
                p.p.wait(timeout=HA.SUBPROC_TIMEOUT * 2)
            except subprocess.TimeoutExpired:
                p.p.kill()
                raise AssertionError("child did not finish within wait_all timeout")


@contextlib.contextmanager
def patched_spawn(fs):
    """HA.spawn を FakeSpawn に差し替える (試験の間だけ)。"""
    orig = HA.spawn
    HA.spawn = fs
    try:
        yield fs
    finally:
        HA.spawn = orig


def open_job(o, rid, name=b"doc", kind=b"text"):
    """PRINT OPEN を送り、job 応答本文を読んで rid を RELEASE する。"""
    out = o.send(HA.OP_REQUEST, rid=rid, seq=0,
                 payload=b"PRINT OPEN " + name + b" " + kind)
    body = read_body(o, rid)
    o.send(HA.OP_RELEASE, rid=rid)
    return body


# =========================================================== ケース (往復 2)
def r2_R1_hello_and_roundtrip():
    """R1: 非ゼロの agent 世代での HELLO → REQUEST → RESPONSE → 本文完了。"""
    a, o = up()
    sa, est = o.hello(sess=o.sess)
    agent_gen, req_sess, req_epoch = struct.unpack("<HHH", sa["pl"])
    assert agent_gen == 0x4242 and agent_gen != 0, "agent 世代が載っていない"
    assert est["op"] == HA.OP_HELLO and est["flags"] == HA.HS_ESTAB
    assert a.established and a.sess == o.sess

    out = o.send(HA.OP_REQUEST, rid=1, seq=0, payload=b"GET /pattern/1024")
    ack = only(out, HA.OP_ACK)
    assert ack["rid"] == 1 and ack["ack"] == 0 and ack["flags"] == 0
    ctl, st, ln = resp(only(out, HA.OP_RESPONSE))
    assert (ctl, st, ln) == (False, 200, 1024), (ctl, st, ln)

    got = b""
    seq = 0
    for _ in range(20):
        out = o.send(HA.OP_WINDOW, rid=1, ack=seq, payload=struct.pack("<H", 64))
        data = [p for p in out if p["op"] == HA.OP_DATA]
        if not data and not [p for p in out if p["op"] == HA.OP_EOF]:
            break
        for d in data:
            assert d["seq"] == seq + 1
            seq = d["seq"]
            got += d["pl"]
        if [p for p in out if p["op"] == HA.OP_EOF]:
            break
    assert len(got) == 1024, len(got)
    assert got == bytes(k & 0xFF for k in range(1024)), "本文が pattern と違う"


def r2_R2_request_ack_dup_and_wdata_loss():
    """R2: REQUEST ACK の遅延重複 + WDATA 喪失を同時に注入しても壊れない。"""
    a, o = up()
    o.send(HA.OP_REQUEST, rid=5, seq=0, payload=b"ECHO 6")
    o.send(HA.OP_REQUEST, rid=5, seq=0, payload=b"ECHO 6")     # 遅延重複
    assert a.ledger[5]["st"] == "ACTIVE" and a.ledger[5]["got"] == b""
    out = o.send(HA.OP_WDATA, rid=5, seq=2, payload=b"xyz")    # seq1 が落ちた体
    assert only(out, HA.OP_ACK)["ack"] == 0, "欠落しているのに累積 ACK が進んだ"
    out = o.send(HA.OP_WDATA, rid=5, seq=1, payload=b"abc")
    assert only(out, HA.OP_ACK)["ack"] == 1
    out = o.send(HA.OP_WDATA, rid=5, seq=2, payload=b"xyz")
    ctl, st, ln = resp(only(out, HA.OP_RESPONSE))
    assert (ctl, st, ln) == (False, 200, 6), (ctl, st, ln)
    assert a.ledger[5]["got"] == b"abcxyz"


def r2_R3_status_repeats_response_zero_len():
    """R3: 転送 ACK 後の RESPONSE 消失 → STATUS で再提示 (0 長も対象)。"""
    a, o = up()
    out = o.send(HA.OP_REQUEST, rid=3, seq=0, payload=b"PING")
    ctl, st, ln = resp(only(out, HA.OP_RESPONSE))
    assert (ctl, st, ln) == (False, 200, 0)
    out = o.send(HA.OP_STATUS, rid=3)                           # 応答が消えた体
    ctl, st, ln = resp(only(out, HA.OP_RESPONSE))
    assert (ctl, st, ln) == (False, 200, 0), "0 長の成功応答が再提示されない"


def r2_R4_release_drops_the_right_rid():
    """R4: A / B の close 順に関わらず RELEASE が正しい rid だけを捨てる。"""
    a, o = up()
    o.send(HA.OP_REQUEST, rid=10, seq=0, payload=b"GET /pattern/512")
    o.send(HA.OP_REQUEST, rid=11, seq=0, payload=b"GET /pattern/512")
    assert a._active_count() == 2
    out = o.send(HA.OP_RELEASE, rid=11)                          # B を先に閉じる
    ack = only(out, HA.OP_ACK)
    assert ack["flags"] & HA.F_RELACK and ack["rid"] == 11 and ack["ack"] == 0
    assert a.ledger[11]["st"] == "RELEASED"
    assert a.ledger[10]["st"] == "ACTIVE", "A まで捨てた"
    out = o.send(HA.OP_STATUS, rid=10)
    assert resp(only(out, HA.OP_RESPONSE)) == (False, 200, 512)
    out = o.send(HA.OP_STATUS, rid=11)
    assert resp(only(out, HA.OP_RESPONSE)) == (True, HA.CTL_TOMBSTONE, 0)


def r2_R5_longlived_a_survives_many_short_b():
    """R5: 長寿命 A + 多数の短命 B。A の結果は残り、8 件境界を越えた遅延
    REQUEST は TOMBSTONE で再実行されない。"""
    a, o = up()
    o.send(HA.OP_REQUEST, rid=1, seq=0, payload=b"GET /pattern/256")   # 長寿命 A
    late = 2
    o.send(HA.OP_REQUEST, rid=late, seq=0, payload=b"PING")
    o.send(HA.OP_RELEASE, rid=late)                                    # B を 1 本閉じる
    for rid in range(3, 24):
        o.send(HA.OP_REQUEST, rid=rid, seq=0, payload=b"PING")
        o.send(HA.OP_RELEASE, rid=rid)
    assert a.ledger[1]["st"] == "ACTIVE", "長寿命 A が watermark で落とされた"
    assert resp(only(o.send(HA.OP_STATUS, rid=1), HA.OP_RESPONSE)) == (False, 200, 256)
    assert late not in a.ledger, "8 件境界を越えた墓標が残っている"
    out = o.send(HA.OP_REQUEST, rid=late, seq=0, payload=b"PING")      # 遅延 REQUEST
    assert resp(only(out, HA.OP_RESPONSE)) == (True, HA.CTL_TOMBSTONE, 0), \
        "落とした墓標の rid が再実行された"


def r2_R7_stale_hello_and_stale_response():
    """R7: 旧 sess / 旧 epoch のフレームは現行セッションに混ざらない。"""
    a, o = up()
    o.send(HA.OP_REQUEST, rid=1, seq=0, payload=b"PING")
    old_sess = o.sess
    out = o.send(HA.OP_STATUS, rid=1, sess=old_sess + 7)
    assert out == [], "別 sess のフレームに答えた"
    out = o.send(HA.OP_STATUS, rid=1, epoch=o.epoch + 5)
    assert out == [], "別 epoch のフレームに答えた"
    out = o.send(HA.OP_STATUS, rid=1)
    assert len(out) == 1, "現行セッションのフレームまで捨てた"


# =========================================================== ケース (往復 3)
def r3_B1_release_ack_vs_request_ack():
    """B1: RELEASE の ACK は flags bit0。REQUEST の ACK (flags 0) と識別できる。"""
    a, o = up()
    o.send(HA.OP_REQUEST, rid=4, seq=0, payload=b"PING")
    out = o.send(HA.OP_REQUEST, rid=4, seq=0, payload=b"PING")   # 旧 REQUEST の再送
    ack = only(out, HA.OP_ACK)
    assert ack["flags"] & HA.F_RELACK == 0, "転送 ACK に RELEASE の印が付いた"
    out = o.send(HA.OP_RELEASE, rid=4)
    assert only(out, HA.OP_ACK)["flags"] & HA.F_RELACK, "RELEASE の ACK に印が無い"


def r3_B2_release_before_and_after_request():
    """B2: RELEASE 直後の同 rid の遅延 REQUEST は TOMBSTONE。
    RELEASE が REQUEST に先着しても後着の REQUEST を止める。"""
    a, o = up()
    o.send(HA.OP_REQUEST, rid=6, seq=0, payload=b"PING")
    o.send(HA.OP_RELEASE, rid=6)
    out = o.send(HA.OP_REQUEST, rid=6, seq=0, payload=b"PING")
    assert resp(only(out, HA.OP_RESPONSE)) == (True, HA.CTL_TOMBSTONE, 0)
    # RELEASE 先着 (規則 (4))
    out = o.send(HA.OP_RELEASE, rid=9)
    assert only(out, HA.OP_ACK)["flags"] & HA.F_RELACK
    out = o.send(HA.OP_REQUEST, rid=9, seq=0, payload=b"PING")
    assert resp(only(out, HA.OP_RESPONSE)) == (True, HA.CTL_TOMBSTONE, 0), \
        "RELEASE 先着の rid が後から受理された"


def r3_B3_same_clock_restart_is_a_new_session():
    """B3: 同じ RTC 秒・同じ初期 tick で OS32 が再起動しても別セッション
    (sess は Agent 採番なので OS32 側の種に依らない)。"""
    a = new_agent()
    o1 = FakeOS32(a)
    o1.hello(sess=0, epoch=1, nonce=0x1000)
    s1 = o1.sess
    o2 = FakeOS32(a)                                  # 再起動 (nonce も epoch も同じ)
    o2.hello(sess=0, epoch=1, nonce=0x1000)
    assert o2.sess != s1, "再起動が同じ sess を貰った (%d)" % s1
    assert a.sess == o2.sess and a.ledger == {}, "旧セッションの台帳が残った"


def r3_B4_delayed_old_syn_cannot_break_session():
    """B4: 遅延した旧 sess の SYN は SYN-ACK (候補) を作るだけで切替が起きず、
    旧 CONFIRM は Agent nonce 不一致で無視される。"""
    a, o = up()
    live_sess, live_epoch = o.sess, o.epoch
    o.send(HA.OP_REQUEST, rid=1, seq=0, payload=b"PING")

    ghost = FakeOS32(a, mac=bytes.fromhex("02005e0000ff"))
    out = ghost.send(HA.OP_HELLO, HA.HS_SYN, epoch=9, seq=0x777, sess=0)
    assert out and out[0]["flags"] == HA.HS_SYNACK
    assert a.sess == live_sess and a.epoch == live_epoch, "SYN-ACK だけで切り替わった"
    # 旧 CONFIRM (別の agent nonce) を投げても切り替わらない
    out = ghost.send(HA.OP_HELLO, HA.HS_CONFIRM, epoch=9, seq=0x777, ack=0xDEAD,
                     sess=out[0]["sess"])
    assert out == [], "nonce 不一致の CONFIRM に答えた"
    assert a.sess == live_sess, "nonce 不一致の CONFIRM で切り替わった"
    assert a.ledger[1]["st"] == "ACTIVE", "現行セッションの受付が消えた"


def r3_B5_epoch_wrap_uses_a_new_session():
    """B5: epoch 65535 の次は sess=0 の SYN (新セッション) で成立する。
    Agent は epoch の大小で HELLO を拒まない。"""
    a = new_agent()
    o = FakeOS32(a)
    o.hello(sess=0, epoch=HA.EPOCH_MAX)
    assert a.epoch == HA.EPOCH_MAX and a.established
    old = o.sess
    o2 = FakeOS32(a)
    o2.hello(sess=0, epoch=1)                        # 周回して新セッション
    assert a.established and a.epoch == 1 and o2.sess != old


def r3_B6_epoch_bump_frees_active_slots():
    """B6: 同 sess の epoch 更新で ACTIVE 2 件が消え、新要求が NO_SLOT に
    ならない。墓標は保たれる (rid はセッション内で単調なので有効)。"""
    a, o = up()
    o.send(HA.OP_REQUEST, rid=1, seq=0, payload=b"PING")
    o.send(HA.OP_RELEASE, rid=1)                                # 墓標を 1 本作る
    o.send(HA.OP_REQUEST, rid=2, seq=0, payload=b"GET /pattern/64")
    o.send(HA.OP_REQUEST, rid=3, seq=0, payload=b"GET /pattern/64")
    assert a._active_count() == 2
    o.hello(sess=o.sess, epoch=o.epoch + 1)                     # 同 sess の再同期
    assert a._active_count() == 0, "epoch 更新で ACTIVE が残った"
    assert a.ledger.get(1, {}).get("st") == "RELEASED", "墓標まで捨てた"
    out = o.send(HA.OP_REQUEST, rid=4, seq=0, payload=b"PING")
    assert resp(only(out, HA.OP_RESPONSE)) == (False, 200, 0), "新要求が NO_SLOT"


def r3_B6_hole_becomes_tombstone_on_epoch_bump():
    """B6 の続き: epoch 切替で HOLE は RELEASED に変わる (埋まらない穴を残さない)。"""
    a, o = up()
    o.send(HA.OP_REQUEST, rid=5, seq=0, payload=b"PING")        # rid 1〜4 が HOLE
    assert [r for r, e in a.ledger.items() if e["st"] == "HOLE"] == [1, 2, 3, 4]
    o.hello(sess=o.sess, epoch=o.epoch + 1)
    assert all(e["st"] != "HOLE" for e in a.ledger.values()), "HOLE が残った"
    out = o.send(HA.OP_REQUEST, rid=2, seq=0, payload=b"PING")
    assert resp(only(out, HA.OP_RESPONSE)) == (True, HA.CTL_TOMBSTONE, 0)


def r3_B7_business_status_not_confused_with_control():
    """B7: HTTP 503 / 本文付き 410 は業務結果 (flags 0) として届き、
    制御の NO_SLOT / TOMBSTONE (flags bit0) と混ざらない。"""
    a, o = up()
    out = o.send(HA.OP_REQUEST, rid=1, seq=0, payload=b"GET /status/503")
    assert resp(only(out, HA.OP_RESPONSE)) == (False, 503, 0)
    out = o.send(HA.OP_REQUEST, rid=2, seq=0, payload=b"GET /status/410")
    ctl, st, ln = resp(only(out, HA.OP_RESPONSE))
    assert (ctl, st) == (False, 410) and ln == len(b"gone body"), (ctl, st, ln)
    # 制御側 (NO_SLOT) は同じ 3 という数字でも flags bit0 で分かれる
    out = o.send(HA.OP_REQUEST, rid=3, seq=0, payload=b"PING")
    assert resp(only(out, HA.OP_RESPONSE)) == (True, HA.CTL_NO_SLOT, 0)


def r3_no_slot_keeps_a_hole():
    """規則 (1): 枠が無いときの REQUEST は NO_SLOT を返し、その rid は
    墓標ではなく HOLE として残る (再送が (2') で受理される)。"""
    a, o = up()
    o.send(HA.OP_REQUEST, rid=1, seq=0, payload=b"GET /pattern/64")
    o.send(HA.OP_REQUEST, rid=2, seq=0, payload=b"GET /pattern/64")
    out = o.send(HA.OP_REQUEST, rid=3, seq=0, payload=b"PING")
    assert resp(only(out, HA.OP_RESPONSE)) == (True, HA.CTL_NO_SLOT, 0)
    assert a.ledger[3]["st"] == "HOLE", a.ledger[3]
    o.send(HA.OP_RELEASE, rid=1)                                 # 枠を空けて再送
    out = o.send(HA.OP_REQUEST, rid=3, seq=0, payload=b"PING")
    assert resp(only(out, HA.OP_RESPONSE)) == (False, 200, 0), "再送が受理されない"


# =========================================================== ケース (往復 4)
def r4_R1_hole_accepts_the_retransmitted_request():
    """R1: A の REQUEST 初回欠落 → B 受理 → A 再送が **新規** として受理される
    (未受理の穴を墓標にしない)。"""
    a, o = up()
    # rid 1 (A) は落ちた。B = rid 2 が先に着く → rid 1 は HOLE
    o.send(HA.OP_REQUEST, rid=2, seq=0, payload=b"GET /pattern/64")
    assert a.ledger[1]["st"] == "HOLE"
    out = o.send(HA.OP_REQUEST, rid=1, seq=0, payload=b"GET /pattern/32")
    assert resp(only(out, HA.OP_RESPONSE)) == (False, 200, 32), "穴が墓標になっていた"
    assert a.ledger[1]["st"] == "ACTIVE"


def r4_R2_restart_does_not_fall_back_to_old_session():
    """R2: 再起動 (req_sess = 0) の SYN と、旧セッションの再同期 SYN (req_sess = S)
    の応答が先後逆転しても、写しの照合で旧セッションへ戻らない。"""
    a, o = up()
    old = o.sess
    # 旧起動が「セッション内の再同期」の SYN を出していた (nonce N)
    n = 0x5555
    out = o.send(HA.OP_HELLO, HA.HS_SYN, epoch=o.epoch + 1, seq=n, sess=old)
    sa_resync = out[0]
    assert struct.unpack("<HHH", sa_resync["pl"])[1] == old, "req_sess の写しが無い"
    # そこへ OS32 が再起動し、同じ nonce で新セッションの SYN を出す
    o2 = FakeOS32(a)
    out = o2.send(HA.OP_HELLO, HA.HS_SYN, epoch=1, seq=n, sess=0)
    sa_new = out[0]
    gen, req_sess, req_epoch = struct.unpack("<HHH", sa_new["pl"])
    assert req_sess == 0 and req_epoch == 1, (req_sess, req_epoch)
    assert sa_new["sess"] != old, "再起動に旧 sess を割り当てた"
    # 遅延していた再同期用 SYN-ACK は req_sess = old なので再起動側は採用しない。
    # Agent 側は候補が 1 件なので、旧 SYN-ACK の CONFIRM は nonce が合っても
    # 候補の sess / epoch と合わず無視される。
    out = o2.send(HA.OP_HELLO, HA.HS_CONFIRM, epoch=sa_resync["epoch"], seq=n,
                  ack=sa_resync["ack"], sess=sa_resync["sess"])
    assert out == [], "旧セッションの CONFIRM を受理した"
    assert a.sess == old, "無視すべき CONFIRM でセッションが動いた"
    # 再起動側が自分の SYN-ACK の写しで CONFIRM すると、新 sess で成立する
    o2.sess, o2.epoch = sa_new["sess"], sa_new["epoch"]
    out = o2.send(HA.OP_HELLO, HA.HS_CONFIRM, seq=n, ack=sa_new["ack"])
    assert out and out[0]["flags"] == HA.HS_ESTAB
    assert a.sess == sa_new["sess"] != old, "再起動後に旧セッションへ戻った"
    assert a.ledger == {}, "新セッションに旧セッションの台帳が残った"


def r4_R3_sess_exhaustion_stops_the_agent():
    """R3: sess が枯渇したら新セッションの SYN に応答せず止まる。
    旧 (sess, epoch, rid) のフレームを注いでも新しい要求は成立しない。"""
    with tempfile.TemporaryDirectory(prefix="os32-sess-") as d:
        with open(os.path.join(d, "sess.txt"), "w") as f:
            f.write("%d\n" % (HA.SESS_MAX - 1))
        a = new_agent(state_dir=d)
        o1 = FakeOS32(a)
        o1.hello(sess=0, epoch=1)
        assert o1.sess == HA.SESS_MAX, o1.sess
        o1.send(HA.OP_REQUEST, rid=1, seq=0, payload=b"PING")
        o2 = FakeOS32(a, mac=bytes.fromhex("02005e0000ee"))
        out = o2.send(HA.OP_HELLO, HA.HS_SYN, epoch=1, seq=1, sess=0)
        assert out == [] and a.stopped, "枯渇後も新セッションに応答した"
        # 旧 (sess, epoch, rid) を騙っても、別の OS32 は現行セッションに乗れない
        out = o2.send(HA.OP_REQUEST, rid=2, seq=0, payload=b"PING",
                      sess=HA.SESS_MAX, epoch=o1.epoch)
        assert out == [], "旧セッション偽装に答えた"
        assert 2 not in a.ledger, "偽装した rid が台帳に入った"
        # 生きている旧セッションはそのまま動く (枯渇は新規受付だけを止める)
        out = o1.send(HA.OP_STATUS, rid=1)
        assert resp(only(out, HA.OP_RESPONSE)) == (False, 200, 0)
        # sess.txt は再起動をまたいで残る
        a2 = new_agent(state_dir=d)
        assert a2.sessions.exhausted()


def r4_sess_not_reused_across_restart():
    """sess は Agent 再起動をまたいでも再使用しない (永続採番)。"""
    with tempfile.TemporaryDirectory(prefix="os32-sess-") as d:
        a = new_agent(state_dir=d)
        o = FakeOS32(a)
        o.hello(sess=0, epoch=1)
        first = o.sess
        a2 = new_agent(state_dir=d)                      # Agent 再起動
        o2 = FakeOS32(a2)
        o2.hello(sess=0, epoch=1)
        assert o2.sess == first + 1, (first, o2.sess)


def r4_hello_stage_losses():
    """HELLO 各段階の消失: SYN / SYN-ACK / CONFIRM / ESTABLISHED。
    再送は冪等で、同じ sess・同じ Agent nonce が返る。"""
    a = new_agent()
    o = FakeOS32(a)
    # SYN が落ちた = Agent は何も見ていない
    assert not a.established
    # SYN-ACK が落ちた → OS32 は **nonce を +1 して** SYN を再送する。
    # 候補は最新の SYN で上書きされ、古い SYN-ACK の CONFIRM はもう通らない。
    out1 = o.send(HA.OP_HELLO, HA.HS_SYN, epoch=1, seq=0x99, sess=0)
    out2 = o.send(HA.OP_HELLO, HA.HS_SYN, epoch=1, seq=0x9A, sess=0)
    assert out1[0]["ack"] != out2[0]["ack"], "SYN 再送で Agent nonce が変わらない"
    o.sess, o.epoch = out1[0]["sess"], out1[0]["epoch"]
    assert o.send(HA.OP_HELLO, HA.HS_CONFIRM, seq=0x99, ack=out1[0]["ack"]) == [], \
        "上書きされた候補の CONFIRM が通った"
    # CONFIRM が落ちた / ESTABLISHED が落ちた → 同じ CONFIRM を再送すると
    # Agent は冪等に ESTABLISHED を返す
    o.sess, o.epoch = out2[0]["sess"], out2[0]["epoch"]
    e1 = o.send(HA.OP_HELLO, HA.HS_CONFIRM, seq=0x9A, ack=out2[0]["ack"])
    e2 = o.send(HA.OP_HELLO, HA.HS_CONFIRM, seq=0x9A, ack=out2[0]["ack"])
    assert e1[0]["flags"] == HA.HS_ESTAB and e2[0]["flags"] == HA.HS_ESTAB, \
        "重複 CONFIRM に冪等な ESTABLISHED を返さない"
    assert a.established and a.sess == o.sess


# ============================================================== その他
def paylen_mismatch_is_dropped():
    """op ごとの payload 長と一致しないフレームは捨てる (v2 パーサの検査)。"""
    a, o = up()
    before = dict(a.counts)
    o.send(HA.OP_ACK, rid=1, payload=b"xx")                  # ACK は 0B
    o.send(HA.OP_WINDOW, rid=1, payload=b"x")                # WINDOW は 2B
    o.send(HA.OP_STATUS, rid=1, payload=b"x")                # STATUS は 0B
    o.send(HA.OP_RELEASE, rid=1, payload=b"x")               # RELEASE は 0B
    o.send(HA.OP_REQUEST, rid=1, seq=0, payload=b"")         # REQUEST は 1B 以上
    assert a.counts["dropped"] - before["dropped"] == 5, a.counts
    assert a.ledger == {}, "捨てたはずのフレームが台帳を動かした"
    # 宣言長がフレームに収まらない (length だけ大きい)
    out = o.send(HA.OP_REQUEST, rid=1, seq=0, payload=b"PING", length=900)
    assert out == [] and a.ledger == {}


def hello_paylen_per_stage():
    """HELLO は段階ごとに payload 長が決まっている (SYN 0 / SYN-ACK 6 /
    CONFIRM 0 / ESTABLISHED 2)。違うものは捨てる。"""
    a = new_agent()
    o = FakeOS32(a)
    assert o.send(HA.OP_HELLO, HA.HS_SYN, epoch=1, seq=1, sess=0,
                  payload=b"xx") == [], "SYN に payload を付けても通った"
    assert not a.established
    out = o.send(HA.OP_HELLO, HA.HS_SYN, epoch=1, seq=1, sess=0)
    assert out and out[0]["len"] == 6, "SYN-ACK の payload が 6B でない"


def wdata_dedup_by_sess_rid_seq():
    """WDATA の重複排除は (sess, rid, seq)。重複は本文を二重に足さない。"""
    a, o = up()
    o.send(HA.OP_REQUEST, rid=1, seq=0, payload=b"ECHO 4")
    o.send(HA.OP_WDATA, rid=1, seq=1, payload=b"ab")
    o.send(HA.OP_WDATA, rid=1, seq=1, payload=b"ab")          # 重複
    assert a.ledger[1]["got"] == b"ab", a.ledger[1]["got"]
    out = o.send(HA.OP_WDATA, rid=1, seq=2, payload=b"cd")
    assert resp(only(out, HA.OP_RESPONSE)) == (False, 200, 4)
    assert a.ledger[1]["got"] == b"abcd"


def window_gates_delivery():
    """WINDOW を受けるまで DATA は 1 本も流れない (配送開始の許可)。"""
    a, o = up()
    out = o.send(HA.OP_REQUEST, rid=1, seq=0, payload=b"GET /pattern/4096")
    assert [p for p in out if p["op"] == HA.OP_DATA] == [], "WINDOW 前に流れた"
    out = o.send(HA.OP_WINDOW, rid=1, ack=0, payload=struct.pack("<H", 12))
    data = [p for p in out if p["op"] == HA.OP_DATA]
    assert len(data) == 2, "credit 12 ページ (6 ページ/フレーム) で %d 本" % len(data)
    out = o.send(HA.OP_WINDOW, rid=1, ack=0, payload=struct.pack("<H", 12))
    assert [p for p in out if p["op"] == HA.OP_DATA] == [], "credit を超えて流れた"
    out = o.send(HA.OP_WINDOW, rid=2, ack=0, payload=struct.pack("<H", 99))
    assert [p for p in out if p["op"] == HA.OP_DATA] == [], "別 rid の WINDOW で流れた"


def time_format_is_19_bytes():
    """TIME の標準形は `YYYY-MM-DD HH:MM:SS` (19B、HOST_SERVICES_PLAN §2)。"""
    a, o = up()
    out = o.send(HA.OP_REQUEST, rid=1, seq=0, payload=b"TIME")
    ctl, st, ln = resp(only(out, HA.OP_RESPONSE))
    assert (ctl, st, ln) == (False, 200, 19), (ctl, st, ln)


# =========================================================== N2: PRINT / CLIP
def n2_print_roundtrip():
    """PRINT OPEN → DATA×2 → CLOSE。pages と スプール内容が一致 (票 §2)。"""
    with tempfile.TemporaryDirectory() as d:
        a, o = up(state_dir=d)
        assert open_job(o, 1) == b"job 1"
        o.send(HA.OP_REQUEST, rid=2, seq=0, payload=b"PRINT DATA 1 6")
        out = o.send(HA.OP_WDATA, rid=2, seq=1, payload=b"hello\n")
        assert resp(only(out, HA.OP_RESPONSE)) == (False, 200, 0)
        o.send(HA.OP_RELEASE, rid=2)
        o.send(HA.OP_REQUEST, rid=3, seq=0, payload=b"PRINT DATA 1 6")
        out = o.send(HA.OP_WDATA, rid=3, seq=1, payload=b"world\n")
        assert resp(only(out, HA.OP_RESPONSE)) == (False, 200, 0)
        o.send(HA.OP_RELEASE, rid=3)
        out = o.send(HA.OP_REQUEST, rid=4, seq=0, payload=b"PRINT CLOSE 1")
        assert resp(only(out, HA.OP_RESPONSE))[:2] == (False, 200)
        assert read_body(o, 4) == b"pages 1"
        with open(a.jobs[1]["spool"], "rb") as f:
            assert f.read() == b"hello\nworld\n"


def n2_wdata_len_mismatch():
    """宣言長 < 実 WDATA は宣言長で切る (N1 の WDATA 契約を壊さない、票 §2)。"""
    with tempfile.TemporaryDirectory() as d:
        a, o = up(state_dir=d)
        open_job(o, 1)
        o.send(HA.OP_REQUEST, rid=2, seq=0, payload=b"PRINT DATA 1 6")
        o.send(HA.OP_WDATA, rid=2, seq=1, payload=b"abcd")
        out = o.send(HA.OP_WDATA, rid=2, seq=2, payload=b"efgh")   # 実 8 > 宣言 6
        assert resp(only(out, HA.OP_RESPONSE)) == (False, 200, 0)
        o.send(HA.OP_RELEASE, rid=2)
        o.send(HA.OP_REQUEST, rid=3, seq=0, payload=b"PRINT CLOSE 1")
        assert read_body(o, 3) == b"pages 1"
        with open(a.jobs[1]["spool"], "rb") as f:
            assert f.read() == b"abcdef", "宣言長で切っていない"


def n2_unknown_id_409():
    """未知 id への DATA / CLOSE / STATUS は要求行で 409 (nb)。"""
    with tempfile.TemporaryDirectory() as d:
        a, o = up(state_dir=d)
        for rid, pl in [(1, b"PRINT DATA 99 3"), (2, b"PRINT CLOSE 99"),
                        (3, b"PRINT STATUS 99")]:
            out = o.send(HA.OP_REQUEST, rid=rid, seq=0, payload=pl)
            assert resp(only(out, HA.OP_RESPONSE)) == (False, 409, 0), pl
            o.send(HA.OP_RELEASE, rid=rid)


def n2_status_and_reclose():
    """STATUS = queued/done、done への再 CLOSE は冪等 200 pages n (票 §0)。"""
    with tempfile.TemporaryDirectory() as d:
        a, o = up(state_dir=d)
        open_job(o, 1)
        out = o.send(HA.OP_REQUEST, rid=2, seq=0, payload=b"PRINT STATUS 1")
        assert resp(only(out, HA.OP_RESPONSE))[:2] == (False, 200)
        assert read_body(o, 2) == b"queued"
        o.send(HA.OP_RELEASE, rid=2)
        o.send(HA.OP_REQUEST, rid=3, seq=0, payload=b"PRINT DATA 1 4")
        o.send(HA.OP_WDATA, rid=3, seq=1, payload=b"abc\n")
        o.send(HA.OP_RELEASE, rid=3)
        o.send(HA.OP_REQUEST, rid=4, seq=0, payload=b"PRINT CLOSE 1")
        assert read_body(o, 4) == b"pages 1"
        o.send(HA.OP_RELEASE, rid=4)
        out = o.send(HA.OP_REQUEST, rid=5, seq=0, payload=b"PRINT STATUS 1")
        assert read_body(o, 5) == b"done"
        o.send(HA.OP_RELEASE, rid=5)
        out = o.send(HA.OP_REQUEST, rid=6, seq=0, payload=b"PRINT CLOSE 1")   # 再 CLOSE
        assert resp(only(out, HA.OP_RESPONSE))[:2] == (False, 200)
        assert read_body(o, 6) == b"pages 1"


def n2_open_kind_and_name():
    """raw → 501、未知 kind → 400、name 空白 → 400、name に空白は許す (nb7)。"""
    with tempfile.TemporaryDirectory() as d:
        a, o = up(state_dir=d)
        out = o.send(HA.OP_REQUEST, rid=1, seq=0, payload=b"PRINT OPEN doc raw")
        assert resp(only(out, HA.OP_RESPONSE)) == (False, 501, 0)
        o.send(HA.OP_RELEASE, rid=1)
        out = o.send(HA.OP_REQUEST, rid=2, seq=0, payload=b"PRINT OPEN doc pdf")
        assert resp(only(out, HA.OP_RESPONSE)) == (False, 400, 0)   # 未知 kind
        o.send(HA.OP_RELEASE, rid=2)
        out = o.send(HA.OP_REQUEST, rid=3, seq=0, payload=b"PRINT OPEN text")
        assert resp(only(out, HA.OP_RESPONSE)) == (False, 400, 0)   # name 空白
        o.send(HA.OP_RELEASE, rid=3)
        out = o.send(HA.OP_REQUEST, rid=4, seq=0, payload=b"PRINT OPEN my file text")
        assert resp(only(out, HA.OP_RESPONSE))[:2] == (False, 200)
        assert read_body(o, 4) == b"job 1"        # 最初に成功したジョブが 1


def n2_empty_job_pages_zero():
    """空ジョブの CLOSE は pages 0 (票 §0)。"""
    with tempfile.TemporaryDirectory() as d:
        a, o = up(state_dir=d)
        open_job(o, 1)
        out = o.send(HA.OP_REQUEST, rid=2, seq=0, payload=b"PRINT CLOSE 1")
        assert read_body(o, 2) == b"pages 0"


def n2_pages_formfeed():
    """pages = \\f の数 + 1 (票 §0)。"""
    with tempfile.TemporaryDirectory() as d:
        a, o = up(state_dir=d)
        open_job(o, 1)
        o.send(HA.OP_REQUEST, rid=2, seq=0, payload=b"PRINT DATA 1 6")
        o.send(HA.OP_WDATA, rid=2, seq=1, payload=b"a\fb\fc\n")     # \f×2 → 3 ページ
        o.send(HA.OP_RELEASE, rid=2)
        o.send(HA.OP_REQUEST, rid=3, seq=0, payload=b"PRINT CLOSE 1")
        assert read_body(o, 3) == b"pages 3"


def n2_b2_decl_range():
    """B2: 0 / 非数 (+5) / 超過は宣言長 verb 一律で要求行 400 (WDATA 無し)。"""
    with tempfile.TemporaryDirectory() as d:
        a, o = up(state_dir=d)
        open_job(o, 1)
        cases = [(2, b"PRINT DATA 1 0"), (3, b"PRINT DATA 1 65537"),
                 (4, b"PRINT DATA 1 +5"), (5, b"CLIP PUT 0"),
                 (6, b"CLIP PUT 4097"), (7, b"ECHO 0")]
        for rid, pl in cases:
            out = o.send(HA.OP_REQUEST, rid=rid, seq=0, payload=pl)
            assert resp(only(out, HA.OP_RESPONSE)) == (False, 400, 0), pl
            assert [p for p in out if p["op"] == HA.OP_ACK], "REQUEST の ACK が無い"
            o.send(HA.OP_RELEASE, rid=rid)


def n2_new1_wdata_after_400_acks_only():
    """新1: 400 した rid の WDATA は ACK だけ返り、書かず RESPONSE も再送しない。"""
    a, o = up(clip="none")
    out = o.send(HA.OP_REQUEST, rid=1, seq=0, payload=b"CLIP PUT 4097")
    assert resp(only(out, HA.OP_RESPONSE)) == (False, 400, 0)
    out = o.send(HA.OP_WDATA, rid=1, seq=1, payload=b"x")
    assert only(out, HA.OP_ACK)["ack"] == 1
    assert [p for p in out if p["op"] == HA.OP_RESPONSE] == [], "RESPONSE を再送した"


def n2_b1_wdata_after_done_acks_no_dup():
    """B1: 完了後の最終 WDATA 再送は ACK のみ、スプール不変・RESPONSE 再送なし。"""
    with tempfile.TemporaryDirectory() as d:
        a, o = up(state_dir=d)
        open_job(o, 1)
        o.send(HA.OP_REQUEST, rid=2, seq=0, payload=b"PRINT DATA 1 3")
        out = o.send(HA.OP_WDATA, rid=2, seq=1, payload=b"abc")
        assert resp(only(out, HA.OP_RESPONSE)) == (False, 200, 0)
        spool = a.jobs[1]["spool"]
        with open(spool, "rb") as f:
            assert f.read() == b"abc"
        out = o.send(HA.OP_WDATA, rid=2, seq=1, payload=b"abc")       # 再送
        assert only(out, HA.OP_ACK)["ack"] == 1
        assert [p for p in out if p["op"] == HA.OP_RESPONSE] == []
        with open(spool, "rb") as f:
            assert f.read() == b"abc", "スプールが二重に伸びた"


def n2_b2_close_after_data_complete_no_release():
    """B-2: DATA 完了 (200) だが RELEASE 未着 → 同 job の CLOSE は 200 (409 でない)。"""
    with tempfile.TemporaryDirectory() as d:
        a, o = up(state_dir=d)
        open_job(o, 1)
        o.send(HA.OP_REQUEST, rid=2, seq=0, payload=b"PRINT DATA 1 4")
        out = o.send(HA.OP_WDATA, rid=2, seq=1, payload=b"abc\n")
        assert resp(only(out, HA.OP_RESPONSE)) == (False, 200, 0)     # rid2 は未 RELEASE
        out = o.send(HA.OP_REQUEST, rid=3, seq=0, payload=b"PRINT CLOSE 1")
        assert resp(only(out, HA.OP_RESPONSE))[:2] == (False, 200)
        assert read_body(o, 3) == b"pages 1"


def n2_b2_close_while_data_incomplete_409():
    """B-2: DATA 本文未完了 → CLOSE は 409、RELEASE 後の再 CLOSE は 200。"""
    with tempfile.TemporaryDirectory() as d:
        a, o = up(state_dir=d)
        open_job(o, 1)
        o.send(HA.OP_REQUEST, rid=2, seq=0, payload=b"PRINT DATA 1 6")   # 本文未完了
        out = o.send(HA.OP_REQUEST, rid=3, seq=0, payload=b"PRINT CLOSE 1")
        assert resp(only(out, HA.OP_RESPONSE)) == (False, 409, 0)
        o.send(HA.OP_WDATA, rid=2, seq=1, payload=b"hello\n")            # 本文を完了
        o.send(HA.OP_RELEASE, rid=2)
        o.send(HA.OP_RELEASE, rid=3)
        out = o.send(HA.OP_REQUEST, rid=4, seq=0, payload=b"PRINT CLOSE 1")
        assert resp(only(out, HA.OP_RESPONSE))[:2] == (False, 200)


def n2_b2_second_data_409():
    """B-2: 本文未完了の DATA が居る間の 2 本目 DATA → 409。"""
    with tempfile.TemporaryDirectory() as d:
        a, o = up(state_dir=d)
        open_job(o, 1)
        o.send(HA.OP_REQUEST, rid=2, seq=0, payload=b"PRINT DATA 1 6")   # 未完了
        out = o.send(HA.OP_REQUEST, rid=3, seq=0, payload=b"PRINT DATA 1 3")
        assert resp(only(out, HA.OP_RESPONSE)) == (False, 409, 0)


def n2_data_to_done_job_409():
    """done ジョブへの DATA → 409 (票 §0)。"""
    with tempfile.TemporaryDirectory() as d:
        a, o = up(state_dir=d)
        open_job(o, 1)
        o.send(HA.OP_REQUEST, rid=2, seq=0, payload=b"PRINT CLOSE 1")     # done に
        o.send(HA.OP_RELEASE, rid=2)
        out = o.send(HA.OP_REQUEST, rid=3, seq=0, payload=b"PRINT DATA 1 3")
        assert resp(only(out, HA.OP_RESPONSE)) == (False, 409, 0)


def n2_b3_body_on_non_200():
    """B3: 503 に本文を付けて FakeOS32 が読み切る。既存 GET /status/410 も読める。"""
    a, o = up(clip="none")
    out = o.send(HA.OP_REQUEST, rid=1, seq=0, payload=b"CLIP GET")
    ctl, st, ln = resp(only(out, HA.OP_RESPONSE))
    assert (ctl, st) == (False, 503) and ln > 0
    assert read_body(o, 1).startswith(b"error")
    o.send(HA.OP_RELEASE, rid=1)
    out = o.send(HA.OP_REQUEST, rid=2, seq=0, payload=b"GET /status/410")
    ctl, st, ln = resp(only(out, HA.OP_RESPONSE))
    assert (ctl, st, ln) == (False, 410, len(b"gone body"))
    assert read_body(o, 2) == b"gone body"


def n2_nb5_append_failure_500():
    """nb5: スプール追記失敗 → 500 + 本文、state=error、STATUS が error。"""
    with tempfile.TemporaryDirectory() as d:
        a, o = up(state_dir=d)
        open_job(o, 1)
        a.jobs[1]["spool"] = os.path.join(d, "no", "such", "dir", "x.txt")
        o.send(HA.OP_REQUEST, rid=2, seq=0, payload=b"PRINT DATA 1 3")
        out = o.send(HA.OP_WDATA, rid=2, seq=1, payload=b"abc")
        ctl, st, ln = resp(only(out, HA.OP_RESPONSE))
        assert (ctl, st) == (False, 500) and ln > 0
        assert a.jobs[1]["state"] == "error"
        o.send(HA.OP_RELEASE, rid=2)
        out = o.send(HA.OP_REQUEST, rid=3, seq=0, payload=b"PRINT STATUS 1")
        assert read_body(o, 3).startswith(b"error")


def n2_nb4_error_reclose_retries():
    """nb4: error ジョブへの再 CLOSE は出力を再試行し、成功で done/pages n。"""
    with tempfile.TemporaryDirectory() as d:
        a, o = up(state_dir=d)
        open_job(o, 1)
        o.send(HA.OP_REQUEST, rid=2, seq=0, payload=b"PRINT DATA 1 4")
        o.send(HA.OP_WDATA, rid=2, seq=1, payload=b"abc\n")
        o.send(HA.OP_RELEASE, rid=2)
        good = a.jobs[1]["spool"]
        a.jobs[1]["spool"] = os.path.join(d, "gone.txt")             # 出力を失敗させる
        out = o.send(HA.OP_REQUEST, rid=3, seq=0, payload=b"PRINT CLOSE 1")
        assert resp(only(out, HA.OP_RESPONSE))[:2] == (False, 500)
        assert a.jobs[1]["state"] == "error"
        o.send(HA.OP_RELEASE, rid=3)
        a.jobs[1]["spool"] = good                                    # 戻して再試行
        out = o.send(HA.OP_REQUEST, rid=4, seq=0, payload=b"PRINT CLOSE 1")
        assert resp(only(out, HA.OP_RESPONSE))[:2] == (False, 200)
        assert read_body(o, 4) == b"pages 1"
        assert a.jobs[1]["state"] == "done"


def n2_b4_two_agents_unique_files():
    """B4: state_dir 共有で 2 回起動 → 各 1 ジョブ、2 台目が job 2、出力 2 つ。"""
    with tempfile.TemporaryDirectory() as d:
        pdir = os.path.join(d, "out")
        for expect_job, payload in [(1, b"aaa\n"), (2, b"bbb\n")]:
            a = new_agent(state_dir=d, print_dir=pdir)
            o = FakeOS32(a)
            o.hello()
            out = o.send(HA.OP_REQUEST, rid=1, seq=0, payload=b"PRINT OPEN doc text")
            assert read_body(o, 1) == b"job %d" % expect_job
            o.send(HA.OP_RELEASE, rid=1)
            o.send(HA.OP_REQUEST, rid=2, seq=0,
                   payload=b"PRINT DATA %d 4" % expect_job)
            o.send(HA.OP_WDATA, rid=2, seq=1, payload=payload)
            o.send(HA.OP_RELEASE, rid=2)
            o.send(HA.OP_REQUEST, rid=3, seq=0,
                   payload=b"PRINT CLOSE %d" % expect_job)
            assert read_body(o, 3) == b"pages 1"
        files = sorted(os.listdir(pdir))
        assert len(files) == 2, files                                # 一意名で 2 つ残る


def n2_to_file_default_keeps_spool():
    """--to-file 既定はスプールを残す。--print-dir は一意名で移す。"""
    with tempfile.TemporaryDirectory() as d:
        a, o = up(state_dir=d)                                       # print_dir 無し
        open_job(o, 1)
        o.send(HA.OP_REQUEST, rid=2, seq=0, payload=b"PRINT DATA 1 4")
        o.send(HA.OP_WDATA, rid=2, seq=1, payload=b"abc\n")
        o.send(HA.OP_RELEASE, rid=2)
        o.send(HA.OP_REQUEST, rid=3, seq=0, payload=b"PRINT CLOSE 1")
        assert read_body(o, 3) == b"pages 1"
        assert os.path.exists(a.jobs[1]["spool"])
        assert os.path.dirname(a.jobs[1]["spool"]) == a.spool_dir
    with tempfile.TemporaryDirectory() as d:
        pdir = os.path.join(d, "out")
        a, o = up(state_dir=d, print_dir=pdir)
        open_job(o, 1)
        o.send(HA.OP_REQUEST, rid=2, seq=0, payload=b"PRINT DATA 1 4")
        o.send(HA.OP_WDATA, rid=2, seq=1, payload=b"abc\n")
        o.send(HA.OP_RELEASE, rid=2)
        o.send(HA.OP_REQUEST, rid=3, seq=0, payload=b"PRINT CLOSE 1")
        assert read_body(o, 3) == b"pages 1"
        assert os.path.dirname(a.jobs[1]["spool"]) == pdir           # 移された
        with open(a.jobs[1]["spool"], "rb") as f:
            assert f.read() == b"abc\n"


def n2_b5_clip_none_503():
    """B5: --clip none で GET / PUT が 503 (黙って捨てない)。"""
    a, o = up(clip="none")
    out = o.send(HA.OP_REQUEST, rid=1, seq=0, payload=b"CLIP GET")
    assert resp(only(out, HA.OP_RESPONSE))[:2] == (False, 503)
    o.send(HA.OP_RELEASE, rid=1)
    o.send(HA.OP_REQUEST, rid=2, seq=0, payload=b"CLIP PUT 3")
    out = o.send(HA.OP_WDATA, rid=2, seq=1, payload=b"abc")
    assert resp(only(out, HA.OP_RESPONSE))[:2] == (False, 503)


def n2_b5_clip_file_roundtrip():
    """B5: --clip file:<p> で PUT (CRLF 化) → GET (CRLF→LF) が往復一致。"""
    with tempfile.TemporaryDirectory() as d:
        p = os.path.join(d, "clip.txt")
        a, o = up(clip="file:" + p)
        o.send(HA.OP_REQUEST, rid=1, seq=0, payload=b"CLIP PUT 6")
        out = o.send(HA.OP_WDATA, rid=1, seq=1, payload=b"hello\n")
        assert resp(only(out, HA.OP_RESPONSE)) == (False, 200, 0)
        with open(p, "rb") as f:
            assert f.read() == b"hello\r\n", "PUT が LF→CRLF にしていない"
        o.send(HA.OP_RELEASE, rid=1)
        out = o.send(HA.OP_REQUEST, rid=2, seq=0, payload=b"CLIP GET")
        assert resp(only(out, HA.OP_RESPONSE)) == (False, 200, 6)
        assert read_body(o, 2) == b"hello\n", "GET が CRLF→LF にしていない"


def n2_clip_get_trim_utf8_boundary():
    """B5 / 切り詰め: CLIP GET は 64KB を UTF-8 境界で切って返す。"""
    with tempfile.TemporaryDirectory() as d:
        p = os.path.join(d, "clip.txt")
        big = "あ".encode("utf-8") * 21846                            # 65538 バイト
        with open(p, "wb") as f:
            f.write(big)
        a, o = up(clip="file:" + p)
        out = o.send(HA.OP_REQUEST, rid=1, seq=0, payload=b"CLIP GET")
        ctl, st, ln = resp(only(out, HA.OP_RESPONSE))
        assert (ctl, st) == (False, 200)
        assert ln <= HA.CLIP_GET_MAX and ln % 3 == 0, ln
        body = read_body(o, 1, credit=512)
        assert body == big[:ln] and body.decode("utf-8")


def n2_b5_clip_wsl_get_base64():
    """B5 (新2/新3): --clip wsl の GET は base64(UTF-8) を非同期に受けてデコード。"""
    text = "日本語".encode("utf-8")
    with patched_spawn(FakeSpawn([FakeProc(out=base64.b64encode(text))])) as fs:
        a, o = up(clip="wsl")
        out = o.send(HA.OP_REQUEST, rid=1, seq=0, payload=b"CLIP GET")
        assert [p for p in out if p["op"] == HA.OP_RESPONSE] == []    # 子未完了
        assert fs.calls[0][0][0] == "powershell.exe"
        fs.procs[0].step()
        out = o.pump()
        assert resp(only(out, HA.OP_RESPONSE))[:2] == (False, 200)
        assert read_body(o, 1) == text
        assert o.pump() == [], "完了後に再送した"


def n2_b5_clip_wsl_put_utf16le():
    """B5 (新2): --clip wsl の PUT は clip.exe に UTF-16LE + BOM を stdin で渡す。"""
    with patched_spawn(FakeSpawn([FakeProc(rc=0)])) as fs:
        a, o = up(clip="wsl")
        o.send(HA.OP_REQUEST, rid=1, seq=0, payload=b"CLIP PUT 4")
        o.send(HA.OP_WDATA, rid=1, seq=1, payload=b"abc\n")
        assert fs.calls[0][0] == ["clip.exe"]
        want = b"\xff\xfe" + "abc\r\n".encode("utf-16-le")
        assert fs.procs[0].stdin == want, fs.procs[0].stdin
        fs.procs[0].step()
        out = o.pump()
        assert resp(only(out, HA.OP_RESPONSE)) == (False, 200, 0)


def n2_b5_clip_wsl_empty_200():
    """新2: 空クリップは powershell が空 → base64 空 → 200 + 0 長 (503 でない)。"""
    with patched_spawn(FakeSpawn([FakeProc(out=b"\r\n")])) as fs:
        a, o = up(clip="wsl")
        o.send(HA.OP_REQUEST, rid=1, seq=0, payload=b"CLIP GET")
        fs.procs[0].step()
        out = o.pump()
        assert resp(only(out, HA.OP_RESPONSE)) == (False, 200, 0)


def n2_b5_clip_wsl_non_base64_503():
    """nb2: GET の stdout が base64 でなければ 503 (binascii.Error)。"""
    with patched_spawn(FakeSpawn([FakeProc(out=b"not@@base64!!")])) as fs:
        a, o = up(clip="wsl")
        o.send(HA.OP_REQUEST, rid=1, seq=0, payload=b"CLIP GET")
        fs.procs[0].step()
        out = o.pump()
        assert resp(only(out, HA.OP_RESPONSE))[:2] == (False, 503)


def n2_b5_clip_wsl_timeout_exception_503():
    """新3: 子の output() が TimeoutExpired → 503 + 本文、Agent は次に答える。"""
    err = subprocess.TimeoutExpired("clip.exe", 4)
    with patched_spawn(FakeSpawn([FakeProc(raise_on_output=err)])) as fs:
        a, o = up(clip="wsl")
        o.send(HA.OP_REQUEST, rid=1, seq=0, payload=b"CLIP GET")
        fs.procs[0].step()
        out = o.pump()
        ctl, st, ln = resp(only(out, HA.OP_RESPONSE))
        assert (ctl, st) == (False, 503) and ln > 0
        out = o.send(HA.OP_REQUEST, rid=2, seq=0, payload=b"PING")
        assert resp(only(out, HA.OP_RESPONSE)) == (False, 200, 0)


def n2_b1_async_processing_and_other_rid():
    """B-1: 子未完了中に別 rid が即答、同 rid の STATUS は PROCESSING、完了で 1 応答。"""
    with patched_spawn(FakeSpawn([FakeProc(out=base64.b64encode(b"hi"))])) as fs:
        a, o = up(clip="wsl")
        out = o.send(HA.OP_REQUEST, rid=1, seq=0, payload=b"CLIP GET")
        assert only(out, HA.OP_ACK)["ack"] == 0
        assert [p for p in out if p["op"] == HA.OP_RESPONSE] == []
        out = o.send(HA.OP_REQUEST, rid=2, seq=0, payload=b"PING")     # 別 rid 即答
        assert resp(only(out, HA.OP_RESPONSE)) == (False, 200, 0)
        out = o.send(HA.OP_STATUS, rid=1)                              # PROCESSING
        assert resp(only(out, HA.OP_RESPONSE)) == (True, HA.CTL_PROCESSING, 0)
        assert o.pump() == []                                         # まだ未完了
        fs.procs[0].step()
        out = o.pump()
        assert resp(only(out, HA.OP_RESPONSE))[:2] == (False, 200)
        assert read_body(o, 1) == b"hi"


def n2_b1_release_discards_child():
    """B-1: 完了前の RELEASE で子を kill し結果を捨てる (応答は出ない)。"""
    with patched_spawn(FakeSpawn([FakeProc(out=base64.b64encode(b"hi"))])) as fs:
        a, o = up(clip="wsl")
        o.send(HA.OP_REQUEST, rid=1, seq=0, payload=b"CLIP GET")
        assert 1 in a.pending
        o.send(HA.OP_RELEASE, rid=1)
        assert 1 not in a.pending and fs.procs[0].killed
        fs.procs[0].step()
        assert o.pump() == [], "破棄したはずの結果を返した"


def n2_b1_deadline_503():
    """B-1: 期限超過で kill + 503 + 本文。"""
    base = 1000.0
    with patched_spawn(FakeSpawn([FakeProc()])) as fs:          # 完了しない子
        a, o = up(clip="wsl")
        a.now = lambda: base
        o.send(HA.OP_REQUEST, rid=1, seq=0, payload=b"CLIP GET")
        assert o.pump() == []                                  # 期限内
        a.now = lambda: base + HA.SUBPROC_TIMEOUT + 1.0         # 期限超過
        out = o.pump()
        ctl, st, ln = resp(only(out, HA.OP_RESPONSE))
        assert (ctl, st) == (False, 503) and ln > 0
        assert fs.procs[0].killed


def n2_b6_put_501():
    """B6: PUT /file/ は本文を受理しつつ 501 (何も書かない)。"""
    a, o = up()
    o.send(HA.OP_REQUEST, rid=1, seq=0, payload=b"PUT /file/x 3")
    out = o.send(HA.OP_WDATA, rid=1, seq=1, payload=b"abc")
    assert resp(only(out, HA.OP_RESPONSE)) == (False, 501, 0)


def n2_two_jobs_interleaved():
    """並行: 2 ジョブ同時 open で交互 DATA が正しいスプールへ入る。"""
    with tempfile.TemporaryDirectory() as d:
        a, o = up(state_dir=d)
        open_job(o, 1)
        open_job(o, 2)
        o.send(HA.OP_REQUEST, rid=3, seq=0, payload=b"PRINT DATA 1 2")
        o.send(HA.OP_WDATA, rid=3, seq=1, payload=b"A1")
        o.send(HA.OP_RELEASE, rid=3)
        o.send(HA.OP_REQUEST, rid=4, seq=0, payload=b"PRINT DATA 2 2")
        o.send(HA.OP_WDATA, rid=4, seq=1, payload=b"B1")
        o.send(HA.OP_RELEASE, rid=4)
        o.send(HA.OP_REQUEST, rid=5, seq=0, payload=b"PRINT DATA 1 2")
        o.send(HA.OP_WDATA, rid=5, seq=1, payload=b"A2")
        o.send(HA.OP_RELEASE, rid=5)
        with open(a.jobs[1]["spool"], "rb") as f:
            assert f.read() == b"A1A2"
        with open(a.jobs[2]["spool"], "rb") as f:
            assert f.read() == b"B1"


# =========================================================== N2-fix (§7)
def n2fix_b7_real_large_clip_get():
    """B7: 実 _RealProc で >64KB (base64 後 >パイプ容量) を吐く子が期限内に完了し、
    全量読める (一時ファイル stdout でパイプ詰まりしない)。"""
    unit = "あ".encode("utf-8")                        # 3 バイト
    count = 40000                                      # payload 120000 バイト
    payload = unit * count
    fs = RealB64Spawn(unit, count)
    with patched_spawn(fs):
        a, o = up(clip="wsl")
        # nb4: REQUEST 自身は ACK のみで RESPONSE を出さない (これは時間非依存)。
        # 実子の完了を pump で覗く時間依存の assert は外す (子が速いと偽陰性)。
        out = o.send(HA.OP_REQUEST, rid=1, seq=0, payload=b"CLIP GET")
        assert [p for p in out if p["op"] == HA.OP_RESPONSE] == []
        assert 1 in a.pending
        fs.wait_all()                                  # 実子の完了を待つ (期限 4s 内)
        out = o.pump()
        ctl, st, ln = resp(only(out, HA.OP_RESPONSE))
        assert (ctl, st) == (False, 200), (ctl, st)
        assert ln <= HA.CLIP_GET_MAX and ln % 3 == 0, ln   # 64KB を UTF-8 境界で切る
        assert read_body(o, 1, credit=2048) == payload[:ln]


def n2fix_b7_real_clip_get_sizes():
    """B7: Agent 経由で 60000B → (200, 60000)、100000B → (200, 65536) (境界切り)。"""
    for raw_len, expect_ln in [(60000, 60000), (100000, HA.CLIP_GET_MAX)]:
        payload = b"a" * raw_len                        # ascii なので境界は自明
        fs = RealB64Spawn(b"a", raw_len)
        with patched_spawn(fs):
            a, o = up(clip="wsl")
            o.send(HA.OP_REQUEST, rid=1, seq=0, payload=b"CLIP GET")
            fs.wait_all()
            out = o.pump()
            ctl, st, ln = resp(only(out, HA.OP_RESPONSE))
            assert (ctl, st, ln) == (False, 200, expect_ln), (raw_len, ctl, st, ln)
            assert read_body(o, 1, credit=2048) == payload[:ln]


def n2fix_a_clip_get_rc_nonzero_503():
    """nb(a): powershell が rc≠0 + stdout 空でも 200+0 長に化けず 503。"""
    with patched_spawn(FakeSpawn([FakeProc(out=b"", rc=1)])) as fs:
        a, o = up(clip="wsl")
        o.send(HA.OP_REQUEST, rid=1, seq=0, payload=b"CLIP GET")
        fs.procs[0].step()
        out = o.pump()
        assert resp(only(out, HA.OP_RESPONSE))[:2] == (False, 503)


def n2fix_b_clip_arg_validation():
    """nb(b): --clip の未知値を弾く (関数と CLI の両方)。"""
    assert HA.valid_clip_arg("auto") and HA.valid_clip_arg("none")
    assert HA.valid_clip_arg("wsl") and HA.valid_clip_arg("win32")
    assert HA.valid_clip_arg("file:/tmp/x")
    assert not HA.valid_clip_arg("bogus") and not HA.valid_clip_arg("")
    assert not HA.valid_clip_arg("file:"), "空パスの file: を受理した (nb2)"
    r = subprocess.run([sys.executable, str(ROOT / "tools" / "host_agent.py"),
                        "--clip", "bogus"], capture_output=True)
    assert r.returncode != 0, "不正な --clip を受理した"
    assert b"--clip" in r.stderr, r.stderr


def n2fix_c_open_oserror_500():
    """nb(c): スプール/ state が書けないと PRINT OPEN で落ちず 500 + error。"""
    if hasattr(os, "geteuid") and os.geteuid() == 0:
        return                                           # nb5: root は権限を無視する
    with tempfile.TemporaryDirectory() as d:
        blocked = os.path.join(d, "ro", "spool")        # 親が無い書けない場所
        a, o = up(state_dir=d, spool_dir=blocked)
        os.makedirs(os.path.join(d, "ro"))
        os.chmod(os.path.join(d, "ro"), 0o500)          # 書き込み不可
        try:
            out = o.send(HA.OP_REQUEST, rid=1, seq=0, payload=b"PRINT OPEN doc text")
            ctl, st, ln = resp(only(out, HA.OP_RESPONSE))
            assert (ctl, st) == (False, 500) and ln > 0, (ctl, st, ln)
            assert read_body(o, 1).startswith(b"error")
            # Agent は生きていて次の要求に答える
            o.send(HA.OP_RELEASE, rid=1)
            out = o.send(HA.OP_REQUEST, rid=2, seq=0, payload=b"PING")
            assert resp(only(out, HA.OP_RESPONSE)) == (False, 200, 0)
        finally:
            os.chmod(os.path.join(d, "ro"), 0o700)      # cleanup 用に戻す


def n2fix_d_now_is_monotonic():
    """nb(d): 期限判定の時計は time.monotonic (NTP ジャンプ耐性)。"""
    import time as _t
    a, o = up()
    assert a.now is _t.monotonic


def n2fix_clip_wsl_powershell_cmd():
    """nb: wsl GET の powershell コマンドが base64 ラッパ本文を持つことの照合。"""
    with patched_spawn(FakeSpawn([FakeProc(out=base64.b64encode(b"x"))])) as fs:
        a, o = up(clip="wsl")
        o.send(HA.OP_REQUEST, rid=1, seq=0, payload=b"CLIP GET")
        cmd = fs.calls[0][0]
        assert cmd[0] == "powershell.exe" and "-NoProfile" in cmd
        joined = " ".join(cmd)
        assert "ToBase64String" in joined and "Get-Clipboard -Raw" in joined
        fs.procs[0].step()
        o.pump()


def n2fix_epoch_switch_discards_pending():
    """nb: epoch 切替で実行中の子が kill され pending が破棄される。"""
    with patched_spawn(FakeSpawn([FakeProc(out=base64.b64encode(b"hi"))])) as fs:
        a, o = up(clip="wsl")
        o.send(HA.OP_REQUEST, rid=1, seq=0, payload=b"CLIP GET")
        assert 1 in a.pending
        o.hello(sess=o.sess, epoch=o.epoch + 1)          # 同 sess の epoch 更新
        assert a.pending == {} and fs.procs[0].killed
        fs.procs[0].step()
        assert o.pump() == [], "破棄したはずの結果を返した"


def n2fix_sess_switch_discards_pending():
    """nb: 新セッションへの切替でも実行中の子が kill され pending が破棄される。"""
    with patched_spawn(FakeSpawn([FakeProc(out=base64.b64encode(b"hi"))])) as fs:
        a = new_agent(clip="wsl")
        o = FakeOS32(a)
        o.hello(sess=0, epoch=1)
        o.send(HA.OP_REQUEST, rid=1, seq=0, payload=b"CLIP GET")
        proc = fs.procs[0]
        assert 1 in a.pending
        o2 = FakeOS32(a, mac=bytes.fromhex("02005e0000aa"))
        o2.hello(sess=0, epoch=1)                        # 別 OS32 の新セッション
        assert a.pending == {} and proc.killed


# =========================================================== N3 §7 (Agent, host Python)
def n3_get_async_processing_then_200():
    """B4: GET http は非同期。子未完了中は STATUS→PROCESSING、subproc_timeout を
    1 秒に下げても GET は 25 秒期限で生き、完了で 200 + 本文 (4 秒で切れない)。"""
    body = b"hello world"
    with patched_spawn(FakeSpawn([FakeProc(out=b"200\n" + body)])) as fs:
        a, o = up(allow_net=True)
        a.subproc_timeout = 1.0                          # CLIP は 1 秒でも GET は別期限
        base = 1000.0
        a.now = lambda: base
        out = o.send(HA.OP_REQUEST, rid=1, seq=0, payload=b"GET http://h/f")
        assert only(out, HA.OP_ACK)["ack"] == 0
        assert [p for p in out if p["op"] == HA.OP_RESPONSE] == []    # 子未完了
        assert fs.calls[0][0][0] == sys.executable                   # urllib の子
        a.now = lambda: base + 5.0                       # subproc_timeout(1s) は超えるが
        assert o.pump() == []                            # GET 期限(25s)内なので生きる
        out = o.send(HA.OP_STATUS, rid=1)
        assert resp(only(out, HA.OP_RESPONSE)) == (True, HA.CTL_PROCESSING, 0)
        fs.procs[0].step()
        out = o.pump()
        assert resp(only(out, HA.OP_RESPONSE))[:2] == (False, 200)
        assert read_body(o, 1) == body


def n3_get_async_404_not_502():
    """B4: 子が status 行 404 を出したら 404 (502 に化けさせない)。"""
    with patched_spawn(FakeSpawn([FakeProc(out=b"404\nnope")])) as fs:
        a, o = up(allow_net=True)
        o.send(HA.OP_REQUEST, rid=1, seq=0, payload=b"GET http://h/missing")
        fs.procs[0].step()
        out = o.pump()
        ctl, st, ln = resp(only(out, HA.OP_RESPONSE))
        assert (ctl, st) == (False, 404), (ctl, st)
        assert read_body(o, 1) == b"nope"


def n3_get_async_child_rc_nonzero_502():
    """B4: 子が rc≠0 なら status 行があっても 502 (途中断を 200 にしない)。"""
    with patched_spawn(FakeSpawn([FakeProc(out=b"200\npartial", rc=1)])) as fs:
        a, o = up(allow_net=True)
        o.send(HA.OP_REQUEST, rid=1, seq=0, payload=b"GET http://h/f")
        fs.procs[0].step()
        out = o.pump()
        assert resp(only(out, HA.OP_RESPONSE)) == (False, 502, 0)


def n3_get_offline_502_sync():
    """B4: allow_net False (--offline) は子を起こさず即 502 (同期)。"""
    a, o = up(allow_net=False)
    out = o.send(HA.OP_REQUEST, rid=1, seq=0, payload=b"GET http://h/f")
    assert resp(only(out, HA.OP_RESPONSE)) == (False, 502, 0)
    assert 1 not in a.pending


def n3_get_deadline_503_timeout():
    """B4: GET 期限 (25s) 超過で kill + 503 error timeout。"""
    base = 1000.0
    with patched_spawn(FakeSpawn([FakeProc()])) as fs:       # 完了しない子
        a, o = up(allow_net=True)
        a.now = lambda: base
        o.send(HA.OP_REQUEST, rid=1, seq=0, payload=b"GET http://h/slow")
        assert o.pump() == []                                # 期限内
        a.now = lambda: base + HA.GET_TIMEOUT + 1.0
        out = o.pump()
        ctl, st, ln = resp(only(out, HA.OP_RESPONSE))
        assert (ctl, st) == (False, 503) and ln > 0
        assert read_body(o, 1) == b"error timeout"
        assert fs.procs[0].killed


def n3_file_no_root_403():
    """§7: --file-root 未指定なら /file/ は 403 (任意ファイルを開かない)。"""
    a, o = up()
    out = o.send(HA.OP_REQUEST, rid=1, seq=0, payload=b"GET /file/etc/hostname")
    assert resp(only(out, HA.OP_RESPONSE)) == (False, 403, 0)


def n3_file_in_root_200():
    """§7: root 配下のファイルは 200 + 本文。"""
    with tempfile.TemporaryDirectory() as d:
        with open(os.path.join(d, "a.txt"), "wb") as f:
            f.write(b"hi")
        a, o = up(file_root=d)
        out = o.send(HA.OP_REQUEST, rid=1, seq=0, payload=b"GET /file/a.txt")
        assert resp(only(out, HA.OP_RESPONSE)) == (False, 200, 2)
        assert read_body(o, 1) == b"hi"


def n3_file_dotdot_escape_403():
    """§7: .. でルート外へ出る要求は 403。"""
    with tempfile.TemporaryDirectory() as d:
        root = os.path.join(d, "root")
        os.makedirs(root)
        with open(os.path.join(d, "secret.txt"), "wb") as f:
            f.write(b"S")
        a, o = up(file_root=root)
        out = o.send(HA.OP_REQUEST, rid=1, seq=0, payload=b"GET /file/../secret.txt")
        assert resp(only(out, HA.OP_RESPONSE)) == (False, 403, 0)


def n3_file_symlink_escape_403():
    """§7: root 内の symlink がルート外を指しても 403。"""
    with tempfile.TemporaryDirectory() as d:
        root = os.path.join(d, "root")
        os.makedirs(root)
        with open(os.path.join(d, "outside.txt"), "wb") as f:
            f.write(b"O")
        try:
            os.symlink(os.path.join(d, "outside.txt"), os.path.join(root, "link.txt"))
        except (OSError, NotImplementedError, AttributeError):
            return                                           # symlink 不可の環境は skip
        a, o = up(file_root=root)
        out = o.send(HA.OP_REQUEST, rid=1, seq=0, payload=b"GET /file/link.txt")
        assert resp(only(out, HA.OP_RESPONSE)) == (False, 403, 0)


def n3_file_prefix_sibling_403():
    """§7: 前方一致する兄弟 (root2) は root 配下でないので 403。"""
    with tempfile.TemporaryDirectory() as d:
        root = os.path.join(d, "root")
        sib = os.path.join(d, "root2")
        os.makedirs(root)
        os.makedirs(sib)
        with open(os.path.join(sib, "x.txt"), "wb") as f:
            f.write(b"X")
        a, o = up(file_root=root)
        out = o.send(HA.OP_REQUEST, rid=1, seq=0, payload=b"GET /file/../root2/x.txt")
        assert resp(only(out, HA.OP_RESPONSE)) == (False, 403, 0)


def n3fix_realproc_close_frees_fds():
    """nb1: 完了経路でも kill 経路でも _RealProc の一時ファイル fd を閉じる。"""
    p = HA._RealProc([sys.executable, "-c", "import sys; sys.stdout.write('x')"])
    p.p.wait()
    assert p.output() == b"x"
    p.close()
    assert p._outfile.closed and p._errfile.closed
    p.close()                                            # 冪等 (二度目でも壊れない)
    p2 = HA._RealProc([sys.executable, "-c", "import time; time.sleep(30)"])
    p2.kill()
    assert p2._outfile.closed and p2._errfile.closed


def n3fix_wait_all_timeout_raises():
    """nb3: RealB64Spawn.wait_all は完了しない子を timeout で kill し AssertionError。"""
    orig = HA.SUBPROC_TIMEOUT
    HA.SUBPROC_TIMEOUT = 0.2                              # 0.4s で諦める
    fs = RealB64Spawn(b"a", 1)
    fs.procs.append(HA._RealProc([sys.executable, "-c", "import time; time.sleep(30)"]))
    try:
        raised = False
        try:
            fs.wait_all()
        except AssertionError:
            raised = True
        assert raised, "完了しない子で wait_all がハング / 例外なし"
    finally:
        HA.SUBPROC_TIMEOUT = orig
        for p in fs.procs:
            p.kill()


def n3fix_clip_get_stderr_in_503():
    """nb6: rc≠0 の 503 本文に子の stderr を載せる (診断性)。"""
    class _ErrSpawn:
        def __init__(self):
            self.calls = []
            self.procs = []

        def __call__(self, cmd, stdin_bytes=None):
            self.calls.append((cmd, stdin_bytes))
            p = HA._RealProc([sys.executable, "-c",
                              "import sys; sys.stderr.write('boom detail'); "
                              "sys.exit(3)"])
            self.procs.append(p)
            return p

    fs = _ErrSpawn()
    with patched_spawn(fs):
        a, o = up(clip="wsl")
        o.send(HA.OP_REQUEST, rid=1, seq=0, payload=b"CLIP GET")
        fs.procs[0].p.wait()
        out = o.pump()
        ctl, st, ln = resp(only(out, HA.OP_RESPONSE))
        assert (ctl, st) == (False, 503) and ln > 0, (ctl, st, ln)
        body = read_body(o, 1, credit=64)
        assert b"exit 3" in body and b"boom detail" in body, body


def n3fix_get_child_real_http():
    """N3-fix(2): GET_CHILD を実子プロセスで localhost fixture に当てる。
    全 GET 試験は FakeProc で、host_agent.py の GET_CHILD (子の urllib スクリプト)
    は従来一度も実行されない。壊れれば全 GET が 502。ここは実ネットワークを
    叩かず localhost の http.server に対して sys.executable -c GET_CHILD を
    実際に起動し、status 行 + 本文 / HTTPError 404 (rc0) / 切断 (rc≠0) を実測する。"""
    import http.server
    import threading
    import time

    class H(http.server.BaseHTTPRequestHandler):
        def log_message(self, *a):
            pass

        def _send(self, code, body):
            self.send_response(code)
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            self.wfile.write(body)

        def do_GET(self):
            if self.path == "/ok":
                self._send(200, b"hello-from-fixture")
            elif self.path == "/slow":
                time.sleep(0.5)                          # 遅延応答でも子は読み切る
                self._send(200, b"slow-body-0123456789")
            elif self.path == "/nope":
                self._send(404, b"no such thing")
            elif self.path == "/drop":
                self.close_connection = True             # 応答を書かずに切る
                return
            else:
                self._send(404, b"")

    srv = http.server.ThreadingHTTPServer(("127.0.0.1", 0), H)
    srv.daemon_threads = True
    port = srv.server_address[1]
    th = threading.Thread(target=srv.serve_forever, daemon=True)
    th.start()

    def run_child(path):
        r = subprocess.run([sys.executable, "-c", HA.GET_CHILD,
                            "http://127.0.0.1:%d%s" % (port, path)],
                           stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        head, _, body = r.stdout.partition(b"\n")
        return r.returncode, head, body, r.stderr

    try:
        rc, head, body, err = run_child("/ok")
        assert rc == 0, err
        assert head == b"200" and body == b"hello-from-fixture", (head, body)

        rc, head, body, err = run_child("/slow")           # 遅延 200 でも本文完走
        assert rc == 0 and head == b"200" and body == b"slow-body-0123456789", \
            (rc, head, body)

        rc, head, body, err = run_child("/nope")           # HTTPError -> rc0 + 404 本文
        assert rc == 0, err
        assert head == b"404" and body == b"no such thing", (head, body)

        rc, head, body, err = run_child("/drop")           # 切断 (URLError) は捕まえず rc≠0
        assert rc != 0, "切断でも rc 0 (Agent が 200 に化けさせてしまう)"
    finally:
        srv.shutdown()
        srv.server_close()


def n3fix_file_nul_byte_403():
    """N3-fix(3): /file/ のパスに埋め込み NUL があっても Agent は落ちず 403。
    _service_get_file の realpath が try の外だと GET /file/a\\0b の
    ValueError(embedded null) が主ループへ抜けて (主ループは ConnectionError
    しか受けない) Agent プロセスが落ちる。修正後は (ValueError, OSError) → 403。"""
    with tempfile.TemporaryDirectory() as d:
        a, o = up(file_root=d)
        out = o.send(HA.OP_REQUEST, rid=1, seq=0, payload=b"GET /file/a\x00b")
        assert resp(only(out, HA.OP_RESPONSE)) == (False, 403, 0)


CASES = [
    n3_get_async_processing_then_200,
    n3_get_async_404_not_502,
    n3_get_async_child_rc_nonzero_502,
    n3_get_offline_502_sync,
    n3_get_deadline_503_timeout,
    n3_file_no_root_403,
    n3_file_in_root_200,
    n3_file_dotdot_escape_403,
    n3_file_symlink_escape_403,
    n3_file_prefix_sibling_403,
    n3fix_realproc_close_frees_fds,
    n3fix_wait_all_timeout_raises,
    n3fix_clip_get_stderr_in_503,
    n3fix_get_child_real_http,
    n3fix_file_nul_byte_403,
    n2fix_b7_real_large_clip_get,
    n2fix_b7_real_clip_get_sizes,
    n2fix_a_clip_get_rc_nonzero_503,
    n2fix_b_clip_arg_validation,
    n2fix_c_open_oserror_500,
    n2fix_d_now_is_monotonic,
    n2fix_clip_wsl_powershell_cmd,
    n2fix_epoch_switch_discards_pending,
    n2fix_sess_switch_discards_pending,
    n2_print_roundtrip,
    n2_wdata_len_mismatch,
    n2_unknown_id_409,
    n2_status_and_reclose,
    n2_open_kind_and_name,
    n2_empty_job_pages_zero,
    n2_pages_formfeed,
    n2_b2_decl_range,
    n2_new1_wdata_after_400_acks_only,
    n2_b1_wdata_after_done_acks_no_dup,
    n2_b2_close_after_data_complete_no_release,
    n2_b2_close_while_data_incomplete_409,
    n2_b2_second_data_409,
    n2_data_to_done_job_409,
    n2_b3_body_on_non_200,
    n2_nb5_append_failure_500,
    n2_nb4_error_reclose_retries,
    n2_b4_two_agents_unique_files,
    n2_to_file_default_keeps_spool,
    n2_b5_clip_none_503,
    n2_b5_clip_file_roundtrip,
    n2_clip_get_trim_utf8_boundary,
    n2_b5_clip_wsl_get_base64,
    n2_b5_clip_wsl_put_utf16le,
    n2_b5_clip_wsl_empty_200,
    n2_b5_clip_wsl_non_base64_503,
    n2_b5_clip_wsl_timeout_exception_503,
    n2_b1_async_processing_and_other_rid,
    n2_b1_release_discards_child,
    n2_b1_deadline_503,
    n2_b6_put_501,
    n2_two_jobs_interleaved,
    r2_R1_hello_and_roundtrip,
    r2_R2_request_ack_dup_and_wdata_loss,
    r2_R3_status_repeats_response_zero_len,
    r2_R4_release_drops_the_right_rid,
    r2_R5_longlived_a_survives_many_short_b,
    r2_R7_stale_hello_and_stale_response,
    r3_B1_release_ack_vs_request_ack,
    r3_B2_release_before_and_after_request,
    r3_B3_same_clock_restart_is_a_new_session,
    r3_B4_delayed_old_syn_cannot_break_session,
    r3_B5_epoch_wrap_uses_a_new_session,
    r3_B6_epoch_bump_frees_active_slots,
    r3_B6_hole_becomes_tombstone_on_epoch_bump,
    r3_B7_business_status_not_confused_with_control,
    r3_no_slot_keeps_a_hole,
    r4_R1_hole_accepts_the_retransmitted_request,
    r4_R2_restart_does_not_fall_back_to_old_session,
    r4_R3_sess_exhaustion_stops_the_agent,
    r4_sess_not_reused_across_restart,
    r4_hello_stage_losses,
    paylen_mismatch_is_dropped,
    hello_paylen_per_stage,
    wdata_dedup_by_sess_rid_seq,
    window_gates_delivery,
    time_format_is_19_bytes,
]


def main():
    want = [x for x in sys.argv[1:] if not x.startswith("--")]
    cases = [c for c in CASES if not want or c.__name__ in want]
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
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
