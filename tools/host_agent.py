#!/usr/bin/env python3
"""OS32 Host Agent v2 — ワイヤ v2 (docs/archive/network/TASK_N0.md §1b / §2d)。

OS32 リンクプロトコル (raw Ethernet, EtherType 0x88B5) を話すホスト側常駐。
v1 (16B ヘッダ・Stop-and-Wait・セッション無し) を置き換える。

  20B ヘッダ (LE 明示直列化): op u8 @0, flags u8 @1, epoch u16 @2, seq u32 @4,
                              ack u32 @8, length u16 @12, rid u32 @14, sess u16 @18
  3 way HELLO (SYN / SYN-ACK / CONFIRM / ESTABLISHED)、sess は **Agent が採番**
  (永続カウンタ sess.txt、再使用せず 65535 で枯渇停止)
  rid 台帳 (ACTIVE ≤ 2 / RELEASED 墓標 / HOLE、high_water、規則 (1)〜(5))
  RESPONSE flags bit0 = 制御結果 (PROCESSING / TOMBSTONE / NO_SLOT)、
  flags 0 = 業務結果 (HTTP ステータスをそのまま載せる)
  RELEASE → flags bit0 の ACK、WINDOW を受けた rid だけ DATA を流す

トランスポートは 2 つ。どちらも QEMU socket と同じ枠 (4B BE 長 + フレーム):
  - TCP   : ai-debug のソケットバックエンド (NP2NETSOCK=127.0.0.1:8026)
  - UNIX  : --unix <path> (ホスト TDD が実 Agent を子プロセスで結ぶ)

  python3 tools/host_agent.py                            # listen 127.0.0.1:8026
  python3 tools/host_agent.py --connect 127.0.0.1:8026   # NP2NETSOCK=listen:8026
  python3 tools/host_agent.py --unix /tmp/os32.sock --state-dir /tmp/st --quiet
"""
import argparse
import base64
import binascii
import os
import random
import re
import select
import shutil
import socket
import struct
import subprocess
import sys
import tempfile
import time

# ---------------------------------------------------------------- ワイヤ定数
LINK_ETHERTYPE = 0x88B5
ETH_HDR = 14
LINK_HDR = 20
LINK_MAX_PAYLOAD = 1400
LINK_MIN_FRAME = 60

OP_HELLO = 1
OP_REQUEST = 2
OP_RESPONSE = 3
OP_DATA = 4
OP_EOF = 5
OP_ACK = 6
OP_WINDOW = 7
OP_WDATA = 8
OP_STATUS = 9
OP_RELEASE = 10
OPNAME = {1: "HELLO", 2: "REQUEST", 3: "RESPONSE", 4: "DATA", 5: "EOF",
          6: "ACK", 7: "WINDOW", 8: "WDATA", 9: "STATUS", 10: "RELEASE"}

# HELLO の段階 (flags)
HS_SYN = 0
HS_SYNACK = 1
HS_CONFIRM = 2
HS_ESTAB = 3

# flags bit0
F_CTRL = 0x01      # RESPONSE: 制御結果 (業務結果ではない)
F_RELACK = 0x01    # ACK: RELEASE への ACK

# 制御 RESPONSE の status (リンク符号。業務の HTTP ステータスとは別空間)
CTL_PROCESSING = 1
CTL_TOMBSTONE = 2
CTL_NO_SLOT = 3

SESS_MAX = 0xFFFF
RID_MAX = 0xFFFFFFFF
EPOCH_MAX = 0xFFFF
ACTIVE_MAX = 2          # 同時受付 (OS32 のハンドル数と同じ)
TOMB_WINDOW = 8         # 墓標を落としてよい高さ (high_water - 8)
HOLE_FILL_MAX = 64      # 一度に作る HOLE の上限 (異常な rid 跳躍への歯止め)
FRAME_PAGES = 6         # 最大フレームが占める SRAM ページ数 (保守的)
STREAM_PLEN = 512       # 本文ストリームの 1 フレームあたりのバイト数

# op ごとの payload 長 (min, max)。一致しないフレームは捨てる (v2 パーサの検査)
PAYLEN = {
    OP_REQUEST: (1, LINK_MAX_PAYLOAD),
    OP_WDATA: (1, LINK_MAX_PAYLOAD),
    OP_RESPONSE: (6, 6),
    OP_ACK: (0, 0),
    OP_STATUS: (0, 0),
    OP_RELEASE: (0, 0),
    OP_DATA: (1, LINK_MAX_PAYLOAD),
    OP_EOF: (0, 0),
    OP_WINDOW: (2, 2),
}
# HELLO は段階ごとに長さが違う
HELLO_PAYLEN = {HS_SYN: 0, HS_SYNACK: 6, HS_CONFIRM: 0, HS_ESTAB: 2}

# ------------------------------------------------- N2 サービス定数 (PRINT / CLIP)
# 業務結果の HTTP 風ステータス (RESPONSE flags 0)。制御符号 (CTL_*) とは別空間。
HTTP_OK = 200
HTTP_BAD = 400            # 要求行が不正 (宣言長の範囲外、未知 kind 等)
HTTP_CONFLICT = 409       # 未知 / 閉じ済み job、同一 job の並行本文
HTTP_ISE = 500            # スプール追記・出力の失敗
HTTP_NOT_IMPL = 501       # 先送りの要求 (raw kind、PUT /file/)
HTTP_UNAVAIL = 503        # クリップボード backend 無し / 子プロセス失敗・期限超過

DECL_MAX_BYTES = 65536    # ECHO / PRINT DATA の 1 要求あたり宣言長上限
CLIP_PUT_MAX = 4096       # CLIP PUT の宣言長上限
CLIP_GET_MAX = 65536      # CLIP GET は 64KB を UTF-8 境界で切って返す
DEFAULT_LINES_PER_PAGE = 60
FORMFEED = b"\f"
UTF16_BOM = b"\xff\xfe"   # clip.exe へ渡す UTF-16LE のバイト順マーク
SUBPROC_TIMEOUT = 4.0     # CLIP の子プロセスの期限 (秒)。超過で kill + 503
GET_TIMEOUT = 25.0        # GET http(s) の子プロセスの期限 (秒、§7 B4)。
                          # ライブラリの無進捗期限 30 秒より短くして、利用者が
                          # ETIMEOUT でなく業務 503 (error timeout) を見るようにする。
TICK_INTERVAL = 0.05      # 常駐ループの select タイムアウト (子の巡回間隔)
DECL_RE = re.compile(r"^[0-9]+$")   # 宣言長は 10 進のみ (+5 / -5 / 5_0 を弾く)

# GET http(s) を回す子プロセスの本体 (§7 B4)。urllib を主ループの外で回す。
# r.read() で本文を読み切ってから status 行 + 本文を stdout へ書く (途中断を
# 「200 + 短い本文」にしない)。urllib.error.HTTPError は e.code + e.read() を
# 載せて **rc 0 で** 終える (404 を 502 にしない)。それ以外の例外 (URLError・
# タイムアウト等) は捕まえず rc≠0 で死ぬ → Agent 側で 502。
GET_CHILD = (
    "import sys, urllib.request, urllib.error\n"
    "u = sys.argv[1]\n"
    "try:\n"
    "    r = urllib.request.urlopen(u, timeout=%d)\n"
    "    body = r.read()\n"
    "    status = getattr(r, 'status', 200) or 200\n"
    "    r.close()\n"
    "except urllib.error.HTTPError as e:\n"
    "    body = e.read()\n"
    "    status = e.code\n"
    "sys.stdout.buffer.write(('%%d\\n' %% status).encode())\n"
    "sys.stdout.buffer.write(body)\n"
) % int(GET_TIMEOUT)

# 子プロセス経路で 503 + 本文に落とす例外種 (往復 3 B-1 / nb2)。
SUBPROC_ERRORS = (OSError, subprocess.TimeoutExpired,
                  subprocess.CalledProcessError, UnicodeDecodeError,
                  ValueError, binascii.Error)


CLIP_BACKENDS = ("auto", "win32", "wsl", "none")   # file:<path> は接頭辞で別扱い


def valid_clip_arg(value):
    """--clip の値が既知の backend か file:<非空パス> か (nb(b))。"""
    if value in CLIP_BACKENDS:
        return True
    return value.startswith("file:") and len(value) > len("file:")


def write_int_atomic(path, value):
    """tmp へ書いて fsync → os.replace で原子的に整数を書き出す (sess.txt / job.txt)。"""
    tmp = path + ".tmp"
    with open(tmp, "w") as f:
        f.write("%d\n" % value)
        f.flush()
        os.fsync(f.fileno())
    os.replace(tmp, path)


def hdr_pack(op, flags, epoch, seq, ack, length, rid, sess):
    """20B リンクヘッダを LE で明示的に直列化する ("<" で詰め物を作らない)。"""
    return struct.pack("<BBHIIHIH", op & 0xFF, flags & 0xFF, epoch & 0xFFFF,
                       seq & 0xFFFFFFFF, ack & 0xFFFFFFFF, length & 0xFFFF,
                       rid & 0xFFFFFFFF, sess & 0xFFFF)


def hdr_unpack(buf):
    return struct.unpack("<BBHIIHIH", buf[:LINK_HDR])


def mac_str(b):
    return ":".join("%02x" % x for x in b)


def parse_mac(s):
    return bytes(int(x, 16) for x in s.split(":"))


# ------------------------------------------------------------ トランスポート
class FrameStream:
    """4B BE 長 + raw Ethernet フレーム (QEMU socket と同じ枠)。"""

    def __init__(self, sock):
        self.sock = sock
        self.buf = b""

    def send(self, frame):
        self.sock.sendall(struct.pack(">I", len(frame)) + frame)

    def recv_frames(self):
        data = self.sock.recv(65536)
        if not data:
            raise ConnectionError("peer closed")
        self.buf += data
        out = []
        while len(self.buf) >= 4:
            n = struct.unpack(">I", self.buf[:4])[0]
            if n == 0 or n > 65536:
                raise ConnectionError("bad frame length %d" % n)
            if len(self.buf) < 4 + n:
                break
            out.append(self.buf[4:4 + n])
            self.buf = self.buf[4 + n:]
        return out


class ListSink:
    """試験用: 送ったフレームを溜めるだけの出口。"""

    def __init__(self):
        self.frames = []

    def send(self, frame):
        self.frames.append(frame)


class Pcap:
    def __init__(self, path):
        self.f = open(path, "wb")
        self.f.write(struct.pack("<IHHiIII", 0xA1B2C3D4, 2, 4, 0, 0, 65535, 1))

    def write(self, frame):
        t = time.time()
        self.f.write(struct.pack("<IIII", int(t), int((t % 1) * 1e6), len(frame), len(frame)))
        self.f.write(frame)
        self.f.flush()


# ------------------------------------------------------------------ sess 採番
class SessCounter:
    """セッション ID の**永続**採番 (TASK_N0 §1b、往復 4 の R3)。

    Agent 再起動をまたいでも再使用しない。65535 に達したら新セッションの SYN に
    応答せず、ログに枯渇を出して止まる (運用者が全 OS32 を止めて sess.txt を
    消して再開する)。state_dir が None のときはメモリ内だけ (試験の既定)。
    """

    def __init__(self, state_dir=None, start=0):
        self.path = os.path.join(state_dir, "sess.txt") if state_dir else None
        self.value = start
        if self.path and os.path.exists(self.path):
            try:
                with open(self.path) as f:
                    self.value = int(f.read().strip() or "0")
            except (OSError, ValueError):
                self.value = start

    def exhausted(self):
        return self.value >= SESS_MAX

    def alloc(self):
        if self.exhausted():
            return None
        self.value += 1
        if self.path:
            write_int_atomic(self.path, self.value)
        return self.value


# ---------------------------------------------------------------- job 採番
class MonotonicCounter:
    """job.txt に永続する単調カウンタ (sess.txt と同じ原子的書き込み)。

    枯渇の概念は無い (印刷ジョブ id は使い回さないだけでよい)。path が None の
    ときはメモリ内 (試験の既定)。Agent 再起動をまたいで最後の値から続ける。
    """

    def __init__(self, path=None, start=0):
        self.path = path
        self.value = start
        if path and os.path.exists(path):
            try:
                with open(path) as f:
                    self.value = int(f.read().strip() or "0")
            except (OSError, ValueError):
                self.value = start

    def alloc(self):
        self.value += 1
        if self.path:
            write_int_atomic(self.path, self.value)
        return self.value


# --------------------------------------------------------- 非同期子プロセス
class _RealProc:
    """subprocess.Popen を Agent の巡回に載せる薄い包み。

    stdin_bytes があれば起動直後に書き込んで閉じる (CLIP は ≤4KB でパイプに収まる)。
    poll() は None で実行中。output() は完了後に stdout を返す。kill() は冪等。

    stdout は **パイプでなく一時ファイル**にする (blocker B7): パイプだと Agent が
    poll() 非 None まで読まないため、base64 後 >64KB (≒クリップボード 49KB 超) で
    子が書き込みブロック → 4 秒で kill → 503 になる。一時ファイルなら容量非依存。
    """

    def __init__(self, cmd, stdin_bytes=None):
        stdin = subprocess.PIPE if stdin_bytes is not None else subprocess.DEVNULL
        self._outfile = tempfile.TemporaryFile()
        self._errfile = tempfile.TemporaryFile()   # nb6: 失敗時に stderr を診断へ
        self.p = subprocess.Popen(cmd, stdin=stdin, stdout=self._outfile,
                                  stderr=self._errfile)
        if stdin_bytes is not None:
            try:
                self.p.stdin.write(stdin_bytes)
            finally:
                self.p.stdin.close()
        self._out = None
        self._err = None
        self._closed = False

    def poll(self):
        return self.p.poll()

    def output(self):
        # tick は poll() が非 None になってから呼ぶので、子は終了済み =
        # stdout は一時ファイルに出揃っている。先頭から読み切る (close は
        # 別建て — 完了経路が必ず close() する、nb1)。
        if self._out is None:
            self._outfile.seek(0)
            self._out = self._outfile.read()
        return self._out

    def stderr(self):
        if self._err is None:
            self._errfile.seek(0)
            self._err = self._errfile.read()
        return self._err

    def close(self):
        """一時ファイル (stdout/stderr) の fd を閉じる。冪等 (nb1)。"""
        if self._closed:
            return
        self._closed = True
        for f in (self._outfile, self._errfile):
            try:
                f.close()
            except OSError:
                pass

    def kill(self):
        try:
            self.p.kill()
        except OSError:
            pass
        self.close()                                 # kill 経路でも fd を閉じる (nb1)


def spawn(cmd, stdin_bytes=None):
    """子プロセスを起こす唯一の口 (試験はこの名前を差し替えてスタブにする)。"""
    return _RealProc(cmd, stdin_bytes)


# -------------------------------------------------------------------- Agent
class HostAgent:
    def __init__(self, mac, state_dir=None, agent_gen=None, quiet=False,
                 rng=None, sess_start=0, allow_net=True, file_root=None,
                 spool_dir=None, print_dir=None, clip="none", printer=False,
                 lines_per_page=DEFAULT_LINES_PER_PAGE):
        self.mac = mac
        self.quiet = quiet
        self.rng = rng or random.Random()
        self.agent_gen = agent_gen if agent_gen is not None else self.rng.randint(1, 0xFFFF)
        self.sessions = SessCounter(state_dir, sess_start)
        self.allow_net = allow_net
        self.file_root = file_root
        # ---- N2: PRINT / CLIP サービスの設定 ----
        # ジョブ保存 (job.txt + スプール) は最初の PRINT OPEN で用意する (印刷を
        # 使わない試験に空の一時ディレクトリを撒かない)。clip の既定は none で、
        # auto は解決する (win32 が import できれば win32、WSL 道具があれば wsl)。
        self._cfg_state_dir = state_dir
        self._cfg_spool_dir = spool_dir
        self.print_dir = print_dir
        self.printer = printer
        self.lines_per_page = lines_per_page or DEFAULT_LINES_PER_PAGE
        self.clip = self._resolve_auto_clip() if clip == "auto" else clip
        self.spool_dir = None
        self.jobs_counter = None
        self.jobs = {}              # job_id -> dict(kind, state, spool, pages, error)
        # ---- 非同期の子プロセス (CLIP wsl backend、往復 3 B-1) ----
        self.pending = {}           # rid -> dict(proc, ent, finish, deadline, sess, epoch)
        self.now = time.monotonic   # 期限判定の時計 (NTP ジャンプ耐性。試験は差し替える)
        self.subproc_timeout = SUBPROC_TIMEOUT
        # ---- 現行セッション (CONFIRM で切り替わる) ----
        self.sess = 0
        self.epoch = 0
        self.peer = None            # OS32 の MAC
        self.established = False
        # ---- 候補 (SYN-ACK で 1 件だけ保持。新しい SYN で上書き) ----
        self.cand = None
        # ---- rid 台帳 ----
        self.ledger = {}            # rid -> dict(st=ACTIVE/RELEASED/HOLE, ...)
        self.high_water = 0
        # ---- 観測 ----
        self.stopped = False        # sess 枯渇で停止した
        self.counts = {"hello": 0, "request": 0, "wdata": 0, "ack": 0,
                       "window": 0, "status": 0, "release": 0, "dropped": 0,
                       "no_slot": 0, "tombstone": 0, "processing": 0}

    # ------------------------------------------------------------ ログ
    def log(self, *a):
        if not self.quiet:
            print(*a, flush=True)

    # ------------------------------------------------------ フレーム組み立て
    def build(self, dst, op, flags=0, seq=0, ack=0, rid=0, payload=b"",
              sess=None, epoch=None):
        hdr = hdr_pack(op, flags,
                       self.epoch if epoch is None else epoch,
                       seq, ack, len(payload), rid,
                       self.sess if sess is None else sess)
        frame = dst + self.mac + struct.pack(">H", LINK_ETHERTYPE) + hdr + payload
        if len(frame) < LINK_MIN_FRAME:
            frame += b"\x00" * (LINK_MIN_FRAME - len(frame))
        return frame

    # ------------------------------------------------------------- 入口
    def handle(self, frame, out, pcap=None):
        if pcap:
            pcap.write(frame)
        if len(frame) < ETH_HDR + LINK_HDR:
            self.counts["dropped"] += 1
            return
        dst, src = frame[:6], frame[6:12]
        et = struct.unpack(">H", frame[12:14])[0]
        if et != LINK_ETHERTYPE:
            self.counts["dropped"] += 1
            return
        op, flags, epoch, seq, ack, plen, rid, sess = hdr_unpack(frame[ETH_HDR:])
        payload = frame[ETH_HDR + LINK_HDR:ETH_HDR + LINK_HDR + plen]
        if len(payload) != plen:
            self.counts["dropped"] += 1        # 宣言長がフレームに収まらない
            return
        if not self._paylen_ok(op, flags, plen):
            self.counts["dropped"] += 1
            return

        if op == OP_HELLO:
            self._on_hello(src, flags, epoch, seq, ack, sess, payload, out)
            return
        # HELLO 以外は sess / epoch が現行と一致するものだけ
        if not self.established or sess != self.sess or epoch != self.epoch:
            self.counts["dropped"] += 1
            return
        if src != self.peer:
            self.counts["dropped"] += 1
            return

        if op == OP_REQUEST:
            self.counts["request"] += 1
            self._on_request(rid, seq, payload, out)
        elif op == OP_WDATA:
            self.counts["wdata"] += 1
            self._on_wdata(rid, seq, payload, out)
        elif op == OP_STATUS:
            self.counts["status"] += 1
            self._on_status(rid, out)
        elif op == OP_RELEASE:
            self.counts["release"] += 1
            self._on_release(rid, out)
        elif op == OP_WINDOW:
            self.counts["window"] += 1
            self._on_window(rid, ack, struct.unpack("<H", payload[:2])[0], out)
        elif op == OP_ACK:
            self.counts["ack"] += 1
            self.note_rid(rid)
        else:
            self.counts["dropped"] += 1

    def _paylen_ok(self, op, flags, plen):
        if op == OP_HELLO:
            want = HELLO_PAYLEN.get(flags)
            return want is not None and plen == want
        lim = PAYLEN.get(op)
        if lim is None:
            return False
        return lim[0] <= plen <= lim[1]

    # ------------------------------------------------------- 3 way HELLO
    def _on_hello(self, src, flags, epoch, seq, ack, sess, payload, out):
        self.counts["hello"] += 1
        if flags == HS_SYN:
            self._on_syn(src, epoch, seq, sess, out)
        elif flags == HS_CONFIRM:
            self._on_confirm(src, epoch, seq, ack, sess, out)
        # SYN-ACK / ESTABLISHED は Agent 発なので受けたら無視

    def _on_syn(self, src, epoch, nonce, req_sess, out):
        """候補を 1 件だけ作って SYN-ACK を返す。**現行セッションは変えない**。"""
        # **SYN のたびに候補を作り直す** (nonce は HELLO ごとに +1 されるので、
        # 同じ nonce の SYN は「同じ RTC 秒に再起動した別の OS32」でもありうる
        # = 往復 3 の B3。候補を使い回すと再起動が旧 sess を貰ってしまう)。
        # 採番するのは新セッションの SYN だけで、セッション内の再同期 (req_sess
        # が現行 sess) は同じ sess のまま — 再送で sess を減らさない。
        if req_sess != 0 and self.established and req_sess == self.sess:
            assigned = self.sess                      # セッション内の再同期
        else:
            assigned = self.sessions.alloc()
            if assigned is None:
                self.stopped = True
                self.log("sess exhausted (%d): refusing new sessions. "
                         "stop every OS32, remove sess.txt, restart." % SESS_MAX)
                return
        c = {"mac": src, "sess": assigned, "epoch": epoch,
             "os32_nonce": nonce, "agent_nonce": self.rng.randint(1, 0xFFFFFFFF),
             "req_sess": req_sess, "req_epoch": epoch}
        self.cand = c
        pl = struct.pack("<HHH", self.agent_gen, c["req_sess"], c["req_epoch"])
        out.send(self.build(src, OP_HELLO, HS_SYNACK, seq=c["os32_nonce"],
                            ack=c["agent_nonce"], payload=pl,
                            sess=c["sess"], epoch=c["epoch"]))
        self.log("SYN from %s (sess %d epoch %d) -> SYN-ACK sess %d"
                 % (mac_str(src), req_sess, epoch, c["sess"]))

    def _on_confirm(self, src, epoch, nonce, ack, sess, out):
        c = self.cand
        if c is None or c["mac"] != src or c["os32_nonce"] != nonce or \
                c["agent_nonce"] != ack or c["sess"] != sess or c["epoch"] != epoch:
            return                                    # 遅延した旧 CONFIRM
        if not (self.established and self.sess == c["sess"] and self.epoch == c["epoch"]):
            self._switch_to(c)
        out.send(self.build(src, OP_HELLO, HS_ESTAB, seq=c["agent_nonce"],
                            ack=c["os32_nonce"],
                            payload=struct.pack("<H", self.agent_gen)))
        self.log("CONFIRM ok -> ESTABLISHED sess %d epoch %d" % (self.sess, self.epoch))

    def _switch_to(self, c):
        # セッション / epoch が動く = 旧 rid は失効する。実行中の子プロセスも
        # 捨てる (結果を旧セッションへ返さない。往復 3 B-1)。
        for job in self.pending.values():
            job["proc"].kill()
        self.pending = {}
        new_sess = c["sess"] != self.sess
        self.sess, self.epoch, self.peer = c["sess"], c["epoch"], c["mac"]
        self.established = True
        if new_sess:
            self.ledger = {}                          # セッションが変われば墓標も捨てる
            self.high_water = 0
        else:
            # 同じ sess の epoch 更新: ACTIVE は捨て、HOLE は墓標へ、RELEASED は保つ
            for rid in list(self.ledger):
                st = self.ledger[rid]["st"]
                if st == "ACTIVE":
                    del self.ledger[rid]
                elif st == "HOLE":
                    self.ledger[rid] = {"st": "RELEASED"}

    # --------------------------------------------------------- rid 台帳
    def note_rid(self, rid):
        """どのフレームで見た rid でも high_water を上げ、隙間を HOLE にする。"""
        if rid == 0 or rid <= self.high_water:
            return
        first = self.high_water + 1
        if rid - first > HOLE_FILL_MAX:
            first = rid - HOLE_FILL_MAX               # 異常な跳躍への歯止め
        for r in range(first, rid):
            self.ledger.setdefault(r, {"st": "HOLE"})
        self.high_water = rid

    def _active_count(self):
        return sum(1 for e in self.ledger.values() if e["st"] == "ACTIVE")

    def _reap_tombstones(self):
        """規則 (5): RELEASED は rid <= high_water - 8 で落としてよい。
        ACTIVE / HOLE は落とさない (長寿命の結果と未受理の穴を守る)。"""
        cut = self.high_water - TOMB_WINDOW
        for rid in [r for r, e in self.ledger.items()
                    if e["st"] == "RELEASED" and r <= cut]:
            del self.ledger[rid]

    def _ctl(self, rid, status, out):
        out.send(self.build(self.peer, OP_RESPONSE, F_CTRL, rid=rid,
                            payload=struct.pack("<HI", status, 0)))

    def _biz(self, rid, status, length, out):
        out.send(self.build(self.peer, OP_RESPONSE, 0, rid=rid,
                            payload=struct.pack("<HI", status, length)))

    def _ack(self, rid, seq, out, flags=0):
        out.send(self.build(self.peer, OP_ACK, flags, rid=rid, ack=seq))

    # -------------------------------------------------------- REQUEST
    def _on_request(self, rid, seq, payload, out):
        if rid == 0 or seq != 0:
            self.counts["dropped"] += 1
            return
        known = self.ledger.get(rid)
        new = rid > self.high_water
        self.note_rid(rid)
        if known is not None and known["st"] == "ACTIVE":
            self._ack(rid, known["last_seq"], out)     # (2) 重複: 再実行しない
            if known.get("resp") is not None:
                self._biz(rid, known["resp"][0], known["resp"][1], out)
            return
        if known is not None and known["st"] == "RELEASED":
            self.counts["tombstone"] += 1              # (3) 墓標
            self._ctl(rid, CTL_TOMBSTONE, out)
            return
        if known is None and not new:
            self.counts["tombstone"] += 1              # (3) 落とした墓標
            self._ctl(rid, CTL_TOMBSTONE, out)
            return
        # (1) 新規 / (2') HOLE の受理
        if self._active_count() >= ACTIVE_MAX:
            self.counts["no_slot"] += 1
            self.ledger[rid] = {"st": "HOLE"}          # 穴として残す (墓標にしない)
            self._ctl(rid, CTL_NO_SLOT, out)
            return
        ent = {"st": "ACTIVE", "req": payload, "last_seq": 0, "decl": 0,
               "got": b"", "resp": None, "body": None, "gen": None,
               "deliver": None, "job": None, "is_data": False,
               "body_done": False}
        self.ledger[rid] = ent
        self._ack(rid, 0, out)
        self._reap_tombstones()
        self._serve(rid, ent, out)

    # ---------------------------------------------------------- WDATA
    def _on_wdata(self, rid, seq, payload, out):
        ent = self.ledger.get(rid)
        self.note_rid(rid)
        if ent is None or ent["st"] != "ACTIVE":
            if ent is not None and ent["st"] == "RELEASED":
                self._ctl(rid, CTL_TOMBSTONE, out)
            return
        # B1 (往復 2 新 1): ACTIVE な rid の WDATA は decl / resp に依らず必ず
        # 累積 ACK する。本文を got に足すのは resp 未定・宣言長ありのときだけ
        # (要求行で 400 した rid = decl 0 でも WDATA は ACK し本文は捨てる)。
        if seq == ent["last_seq"] + 1:
            ent["last_seq"] = seq
            if ent["resp"] is None and ent["decl"] > 0:
                room = ent["decl"] - len(ent["got"])
                ent["got"] += payload[:room]
        # 重複 / 先行は本文を触らず累積 ACK だけ返す (Go-Back-N)
        self._ack(rid, ent["last_seq"], out)
        if (ent["resp"] is None and ent["decl"] > 0
                and not ent["body_done"] and len(ent["got"]) >= ent["decl"]):
            ent["body_done"] = True
            self._finish_body(rid, ent, out)

    # --------------------------------------------------------- STATUS
    def _on_status(self, rid, out):
        ent = self.ledger.get(rid)
        self.note_rid(rid)
        if ent is not None and ent["st"] == "ACTIVE":
            if ent["resp"] is not None:
                self._biz(rid, ent["resp"][0], ent["resp"][1], out)   # 再提示
            else:
                self.counts["processing"] += 1
                self._ctl(rid, CTL_PROCESSING, out)
            return
        self.counts["tombstone"] += 1
        self._ctl(rid, CTL_TOMBSTONE, out)             # RELEASED / HOLE / 未知

    # -------------------------------------------------------- RELEASE
    def _on_release(self, rid, out):
        self.note_rid(rid)
        job = self.pending.pop(rid, None)              # 実行中の子は結果ごと捨てる
        if job is not None:
            job["proc"].kill()
        self.ledger[rid] = {"st": "RELEASED"}          # (4) 先着でも墓標を作る
        self._reap_tombstones()
        self._ack(rid, 0, out, flags=F_RELACK)

    # --------------------------------------------------------- WINDOW
    def _on_window(self, rid, ack, credit, out):
        ent = self.ledger.get(rid)
        self.note_rid(rid)
        if ent is None or ent["st"] != "ACTIVE" or ent["deliver"] is None:
            return
        d = ent["deliver"]
        if ack > d["acked"]:
            d["acked"] = ack
            d["stall"] = 0
        else:
            d["stall"] += 1
            if d["stall"] >= 8 and d["sent"] > d["acked"]:
                d["retx"] += d["sent"] - d["acked"]    # ack が進まない = 欠落
                d["sent"] = d["acked"]
                d["stall"] = 0
        d["credit"] = credit
        self._pump(rid, ent, out)

    def _pump(self, rid, ent, out):
        d = ent["deliver"]
        while d["sent"] < d["nframes"]:
            inflight = FRAME_PAGES * (d["sent"] - d["acked"])
            if inflight + FRAME_PAGES > d["credit"]:
                break
            seq = d["sent"] + 1
            if seq == d["dropseq"] and not d["dropped"]:
                d["dropped"] = True                    # 1 回だけ落とす (Go-Back-N)
                d["sent"] = seq
                continue
            off = (seq - 1) * d["plen"]
            ln = min(d["plen"], d["total"] - off)
            body = ent["gen"](off, ln) if ent["gen"] else ent["body"][off:off + ln]
            out.send(self.build(self.peer, OP_DATA, seq=seq, rid=rid, payload=body))
            d["sent"] = seq
            d["max_inflight"] = max(d["max_inflight"],
                                    FRAME_PAGES * (d["sent"] - d["acked"]))
        if d["sent"] >= d["nframes"] and not d["eof"]:
            out.send(self.build(self.peer, OP_EOF, seq=d["nframes"] + 1, rid=rid))
            d["eof"] = True

    # -------------------------------------------------------- サービス
    def _decl(self, s, hi):
        """宣言長を 10 進のみで読み 1〜hi に収める。範囲外 / 非数は None。"""
        if not DECL_RE.match(s or ""):
            return None
        v = int(s)
        return v if 1 <= v <= hi else None

    def _serve(self, rid, ent, out):
        """要求行を読む。宣言長のある要求は本文を待ち、無い要求はすぐ答える。"""
        line = ent["req"].decode("latin1").strip()
        parts = line.split()
        verb = parts[0].upper() if parts else ""
        # --- 宣言長 (WDATA) のある要求。範囲は要求行の時点で検査 (B2) ---
        if verb == "ECHO":
            decl = self._decl(parts[1] if len(parts) > 1 else "", DECL_MAX_BYTES)
            if decl is None:
                self._answer(rid, ent, HTTP_BAD, b"", None, out)
                return
            ent["decl"] = decl
            return                                       # WDATA を待つ
        if verb == "CLIP" and len(parts) >= 3 and parts[1].upper() == "PUT":
            decl = self._decl(parts[2], CLIP_PUT_MAX)
            if decl is None:
                self._answer(rid, ent, HTTP_BAD, b"", None, out)
                return
            ent["decl"] = decl
            return
        if verb == "PRINT" and len(parts) >= 2 and parts[1].upper() == "DATA":
            self._serve_print_data(rid, ent, parts, out)
            return
        if verb == "PUT":
            decl = self._decl(parts[-1] if len(parts) > 1 else "", DECL_MAX_BYTES)
            if decl is None:
                self._answer(rid, ent, HTTP_BAD, b"", None, out)
                return
            ent["decl"] = decl                           # 本文を受け切って 501 (B6)
            return
        self._service_now(rid, ent, verb, parts, line, out)

    def _serve_print_data(self, rid, ent, parts, out):
        """PRINT DATA <id> <len>: 本文を待つ前に id / 状態 / 並行を検査 (B-2)。"""
        job_id = None
        if len(parts) >= 3 and DECL_RE.match(parts[2]):
            job_id = int(parts[2])
        job = self.jobs.get(job_id) if job_id is not None else None
        if job is None:                                  # 未知 id → 409
            self._answer(rid, ent, HTTP_CONFLICT, b"", None, out)
            return
        if job["state"] != "open":                       # done / error への DATA → 409
            self._answer(rid, ent, HTTP_CONFLICT, b"", None, out)
            return
        if self._job_pending_data(job_id, rid):          # 本文未完了の DATA が別に居る
            self._answer(rid, ent, HTTP_CONFLICT, b"", None, out)
            return
        decl = self._decl(parts[3] if len(parts) >= 4 else "", DECL_MAX_BYTES)
        if decl is None:                                 # 0 / 非数 / 超過 → 400
            self._answer(rid, ent, HTTP_BAD, b"", None, out)
            return
        ent["decl"] = decl
        ent["job"] = job_id
        ent["is_data"] = True
        # WDATA を待つ

    def _job_pending_data(self, job_id, skip_rid):
        """同一 job に本文未完了 (resp is None) の DATA rid が居るか (自 rid 除外)。"""
        for r, e in self.ledger.items():
            if (r != skip_rid and e.get("st") == "ACTIVE" and e.get("is_data")
                    and e.get("job") == job_id and e.get("resp") is None):
                return True
        return False

    def _service_now(self, rid, ent, verb, parts, line, out):
        if verb == "PING":
            self._answer(rid, ent, 200, b"", None, out)
        elif verb == "PRINT":
            self._service_print(rid, ent, parts, out)
        elif verb == "CLIP":
            self._service_clip(rid, ent, parts, out)
        elif verb == "TIME":
            body = time.strftime("%Y-%m-%d %H:%M:%S").encode()
            self._answer(rid, ent, 200, body, None, out)
        elif verb == "GET":
            self._service_get(rid, ent, parts[1] if len(parts) > 1 else "", out)
        elif verb == "BULK":
            count, plen = int(parts[1]), int(parts[2])
            self._answer(rid, ent, 200, None,
                         (lambda off, ln: bytes((off + k) & 0xFF for k in range(ln))),
                         out, total=count * plen, plen=plen)
        elif verb == "STREAM":
            total, plen, drop = int(parts[1]), int(parts[2]), int(parts[3])
            self._answer(rid, ent, 200, None,
                         (lambda off, ln: bytes((off + k) & 0xFF for k in range(ln))),
                         out, total=total, plen=plen, dropseq=drop)
        else:
            self.log("unknown request: %r" % line)
            self._answer(rid, ent, 400, b"", None, out)

    def _service_get(self, rid, ent, resource, out):
        if resource.startswith("/pattern/"):
            try:
                n = int(resource[len("/pattern/"):])
            except ValueError:
                n = 0
            self._answer(rid, ent, 200, None,
                         (lambda off, ln: bytes((off + k) & 0xFF for k in range(ln))),
                         out, total=n)
        elif resource.startswith("http://") or resource.startswith("https://"):
            self._service_get_http(rid, ent, resource, out)
        elif resource.startswith("/file/"):
            self._service_get_file(rid, ent, resource[len("/file/"):], out)
        elif resource == "/status/503":
            self._answer(rid, ent, 503, b"", None, out)     # B7: 業務の 503
        elif resource == "/status/410":
            self._answer(rid, ent, 410, b"gone body", None, out)  # B7: 本文付き 410
        else:
            self._answer(rid, ent, 404, b"", None, out)

    def _service_get_http(self, rid, ent, resource, out):
        """GET http(s): 主ループを塞がぬよう子プロセスで urllib を回す (§7 B4)。
        --offline (allow_net False) は実 HTTP を出さず即 502。"""
        if not self.allow_net:
            self._answer(rid, ent, 502, b"", None, out)
            return
        try:
            proc = spawn([sys.executable, "-c", GET_CHILD, resource])
        except SUBPROC_ERRORS as e:
            self.log("GET %s spawn failed: %s" % (resource, e))
            self._answer(rid, ent, 502, b"", None, out)
            return
        self._start_async(rid, ent, proc, self._finish_get, timeout=GET_TIMEOUT)

    def _finish_get(self, proc):
        """子の stdout = "status\\n" + 本文。子 rc≠0 は status 行があっても 502。"""
        if proc.poll():                                  # 途中断 → 502 (200 に化けさせない)
            return (502, b"")
        raw = proc.output()
        nl = raw.find(b"\n")
        if nl < 0:                                       # status 行が無い = 壊れた
            return (502, b"")
        try:
            status = int(raw[:nl])
        except ValueError:
            return (502, b"")
        return (status, raw[nl + 1:])

    def _service_get_file(self, rid, ent, rel, out):
        """GET /file/: --file-root 配下だけを読む (§7 パストラバーサル修正)。
        ルート未指定は 403、.. / symlink 脱出・別ドライブは 403、前方一致も落とす。"""
        if not self.file_root:
            self._answer(rid, ent, 403, b"", None, out)  # ルート未指定なら開けない
            return
        try:
            root = os.path.realpath(self.file_root)
            target = os.path.realpath(os.path.join(root, rel.lstrip("/")))
            inside = os.path.commonpath([root, target]) == root
        except (ValueError, OSError):                    # 埋め込み NUL / 別ドライブ (commonpath) など
            inside = False
        if not inside:
            self._answer(rid, ent, 403, b"", None, out)  # root2 のような前方一致も落ちる
            return
        try:
            with open(target, "rb") as fh:
                self._answer(rid, ent, 200, fh.read(), None, out)
        except OSError:
            self._answer(rid, ent, 404, b"", None, out)

    def _finish_body(self, rid, ent, out):
        """宣言長ぶんの WDATA を受け切った要求に答える (verb で分岐)。"""
        parts = ent["req"].decode("latin1").strip().split()
        verb = parts[0].upper() if parts else ""
        sub = parts[1].upper() if len(parts) > 1 else ""
        if verb == "ECHO":
            self._answer(rid, ent, 200, ent["got"], None, out)   # 折り返し
        elif verb == "PRINT" and sub == "DATA":
            self._print_data_finish(rid, ent, out)
        elif verb == "CLIP" and sub == "PUT":
            self._clip_put_finish(rid, ent, out)
        else:
            self._answer(rid, ent, HTTP_NOT_IMPL, b"", None, out)  # PUT ほか (B6)

    def _answer(self, rid, ent, status, body, gen, out,
                total=None, plen=STREAM_PLEN, dropseq=0):
        if total is None:
            total = len(body) if body is not None else 0
        ent["body"] = body
        ent["gen"] = gen
        ent["resp"] = (status, total)
        # B3: 200 に限らず total>0 なら本文を配送できる (409 / 500 / 503 も本文可)。
        if total > 0:
            nframes = (total + plen - 1) // plen
            ent["deliver"] = {"total": total, "plen": plen, "nframes": nframes,
                              "sent": 0, "acked": 0, "credit": 0, "eof": False,
                              "dropseq": dropseq, "dropped": False, "stall": 0,
                              "retx": 0, "max_inflight": 0}
        self._biz(rid, status, total, out)
        self.log("rid %d -> %d, %d bytes" % (rid, status, total))

    # ----------------------------------------------------- N2: 非同期の巡回
    def tick(self, out):
        """常駐ループが毎周呼ぶ。子プロセスの完了 / 期限を拾って応答する。"""
        if not self.pending:
            return
        now = self.now()
        for rid in list(self.pending):
            job = self.pending[rid]
            ent = job["ent"]
            cur = self.ledger.get(rid)
            # RELEASE / epoch 切替でこの rid が失効していたら結果を捨てる
            if (cur is not ent or cur.get("st") != "ACTIVE" or not self.established
                    or self.sess != job["sess"] or self.epoch != job["epoch"]):
                job["proc"].kill()
                del self.pending[rid]
                continue
            try:
                rc = job["proc"].poll()
            except SUBPROC_ERRORS as e:
                del self.pending[rid]
                self._answer(rid, ent, HTTP_UNAVAIL, self._err_body(e), None, out)
                continue
            if rc is None:
                if now >= job["deadline"]:               # 期限超過 → kill + 503
                    job["proc"].kill()
                    del self.pending[rid]
                    self._answer(rid, ent, HTTP_UNAVAIL,
                                 self._err_body("timeout"), None, out)
                continue
            del self.pending[rid]                        # 完了 → finisher で判定
            proc = job["proc"]
            try:
                status, body = job["finish"](proc)
            except SUBPROC_ERRORS as e:
                status, body = HTTP_UNAVAIL, self._err_body(self._exc_msg(e))
            finally:
                closer = getattr(proc, "close", None)
                if closer is not None:
                    closer()                             # nb1: 完了経路でも fd を閉じる
            self._answer(rid, ent, status, body, None, out)

    def _start_async(self, rid, ent, proc, finish, timeout=None):
        # 期限は要求種別ごと (§7 B4): CLIP は subproc_timeout (4 秒)、GET は 25 秒。
        deadline = self.now() + (self.subproc_timeout if timeout is None else timeout)
        self.pending[rid] = {"proc": proc, "ent": ent, "finish": finish,
                             "deadline": deadline,
                             "sess": self.sess, "epoch": self.epoch}

    @staticmethod
    def _err_body(msg):
        return ("error %s" % msg).encode("utf-8", "replace")

    @staticmethod
    def _exc_msg(e):
        """503 本文に載せる文言。子の stderr を含む診断があればそれを使う (nb6)。"""
        return getattr(e, "os32_msg", None) or e

    @staticmethod
    def _subproc_failed(proc, name, rc):
        """rc≠0 の子プロセスを CalledProcessError にする。stderr を診断へ (nb6)。"""
        err = b""
        get_err = getattr(proc, "stderr", None)
        if get_err is not None:
            try:
                err = get_err() or b""
            except OSError:
                err = b""
        msg = "%s exit %d" % (name, rc)
        tail = err.decode("utf-8", "replace").strip()
        if tail:
            msg += ": " + tail
        e = subprocess.CalledProcessError(rc, name)
        e.os32_msg = msg
        return e

    # ----------------------------------------------------- N2: PRINT
    def _ensure_jobs(self):
        """最初の PRINT OPEN でジョブ保存 (job.txt + スプール) を用意する。"""
        if self.jobs_counter is not None:
            return
        base = self._cfg_state_dir or tempfile.mkdtemp(prefix="os32-jobstate-")
        self.spool_dir = self._cfg_spool_dir or os.path.join(base, "spool")
        os.makedirs(self.spool_dir, exist_ok=True)
        if self.print_dir:
            os.makedirs(self.print_dir, exist_ok=True)
        self.jobs_counter = MonotonicCounter(os.path.join(base, "job.txt"))

    def _service_print(self, rid, ent, parts, out):
        """PRINT OPEN / CLOSE / STATUS (DATA は _serve で本文を待つ)。"""
        sub = parts[1].upper() if len(parts) > 1 else ""
        if sub == "OPEN":
            self._print_open(rid, ent, parts, out)
        elif sub == "CLOSE":
            self._print_close(rid, ent, parts, out)
        elif sub == "STATUS":
            self._print_status(rid, ent, parts, out)
        else:
            self._answer(rid, ent, HTTP_BAD, b"", None, out)

    def _print_open(self, rid, ent, parts, out):
        # PRINT OPEN <name...> <kind>。name は空白を許す (nb7)、パスには使わない。
        if len(parts) < 4:
            self._answer(rid, ent, HTTP_BAD, b"", None, out)     # name が空
            return
        kind = parts[-1].lower()
        name = " ".join(parts[2:-1]).strip()
        if not name:
            self._answer(rid, ent, HTTP_BAD, b"", None, out)
            return
        if kind == "raw":
            self._answer(rid, ent, HTTP_NOT_IMPL, b"", None, out)  # raw は v2
            return
        if kind != "text":
            self._answer(rid, ent, HTTP_BAD, b"", None, out)       # 未知 kind
            return
        # nb(c): ジョブ保存の用意 (makedirs) / id 採番 (job.txt 書き込み) /
        # スプール作成のどれが失敗しても Agent は落とさず 500 + error で返す。
        try:
            self._ensure_jobs()
            jid = self.jobs_counter.alloc()
            spool = os.path.join(self.spool_dir,
                                 "%d-%d.txt" % (int(time.time()), jid))
            with open(spool, "wb"):                      # 空のスプールを作る
                pass
        except OSError as e:
            self._answer(rid, ent, HTTP_ISE, self._err_body(e), None, out)
            return
        self.jobs[jid] = {"kind": kind, "state": "open", "spool": spool,
                          "pages": 0, "error": None}
        self._answer(rid, ent, 200, ("job %d" % jid).encode(), None, out)

    def _print_data_finish(self, rid, ent, out):
        """PRINT DATA の本文をスプールに追記する (追記失敗 → 500、state=error)。"""
        job = self.jobs.get(ent["job"])
        if job is None or job["state"] != "open":
            self._answer(rid, ent, HTTP_CONFLICT, b"", None, out)
            return
        try:
            with open(job["spool"], "ab") as f:
                f.write(ent["got"])
        except OSError as e:                             # 満杯 / 権限 (nb5)
            job["state"] = "error"
            job["error"] = str(e)
            self._answer(rid, ent, HTTP_ISE, self._err_body(e), None, out)
            return
        self._answer(rid, ent, 200, b"", None, out)

    def _print_status(self, rid, ent, parts, out):
        job = self._lookup_job(parts, 2)
        if job is None:
            self._answer(rid, ent, HTTP_CONFLICT, b"", None, out)
            return
        st = job["state"]
        if st == "open":
            body = b"queued"
        elif st == "done":
            body = b"done"
        else:
            body = self._err_body(job["error"] or "print failed")
        self._answer(rid, ent, 200, body, None, out)

    def _print_close(self, rid, ent, parts, out):
        job_id = self._parse_job_id(parts, 2)
        job = self.jobs.get(job_id) if job_id is not None else None
        if job is None:                                  # 未知 id → 409
            self._answer(rid, ent, HTTP_CONFLICT, b"", None, out)
            return
        if self._job_pending_data(job_id, rid):          # 本文未完了の DATA → 409
            self._answer(rid, ent, HTTP_CONFLICT, b"", None, out)
            return
        if job["state"] == "done":                       # 再 CLOSE は冪等 200
            self._answer(rid, ent, 200, b"pages %d" % job["pages"], None, out)
            return
        status, body = self._print_output(job)           # open / error を出力
        self._answer(rid, ent, status, body, None, out)

    def _lookup_job(self, parts, idx):
        job_id = self._parse_job_id(parts, idx)
        return self.jobs.get(job_id) if job_id is not None else None

    @staticmethod
    def _parse_job_id(parts, idx):
        if len(parts) > idx and DECL_RE.match(parts[idx]):
            return int(parts[idx])
        return None

    def _print_output(self, job):
        """スプールを確定して出力する。成功 (200 pages n) / 失敗 (500 error)。"""
        try:
            with open(job["spool"], "rb") as f:
                data = f.read()
        except OSError as e:
            job["state"] = "error"
            job["error"] = str(e)
            return (HTTP_ISE, self._err_body(e))
        pages = self._count_pages(data)
        try:
            if self.printer:
                self._print_win32(data)                  # 既定プリンタ (任意依存)
            elif self.print_dir:
                dest = os.path.join(self.print_dir, os.path.basename(job["spool"]))
                shutil.move(job["spool"], dest)          # EXDEV 耐性 (nb3)
                job["spool"] = dest
            # --to-file 既定 (--print-dir 無し) はスプールに残すだけ
        except (OSError, ImportError) as e:
            job["state"] = "error"
            job["error"] = str(e)
            return (HTTP_ISE, self._err_body(e))
        job["state"] = "done"
        job["pages"] = pages
        return (200, b"pages %d" % pages)

    def _count_pages(self, data):
        # ページ数 = \f の数 + 1、無ければ ceil(行数 / lines_per_page)、空は 0。
        if not data:
            return 0
        ff = data.count(FORMFEED)
        if ff:
            return ff + 1
        lines = data.count(b"\n")
        if not data.endswith(b"\n"):
            lines += 1
        return max(1, (lines + self.lines_per_page - 1) // self.lines_per_page)

    def _print_win32(self, data):
        import win32print                                # 任意依存 (pywin32)
        name = win32print.GetDefaultPrinter()
        h = win32print.OpenPrinter(name)
        try:
            win32print.StartDocPrinter(h, 1, ("os32", None, "RAW"))
            win32print.StartPagePrinter(h)
            win32print.WritePrinter(h, data)
            win32print.EndPagePrinter(h)
            win32print.EndDocPrinter(h)
        finally:
            win32print.ClosePrinter(h)

    # ----------------------------------------------------- N2: CLIP
    def _service_clip(self, rid, ent, parts, out):
        """CLIP GET (PUT は _serve で本文を待つ)。"""
        sub = parts[1].upper() if len(parts) > 1 else ""
        if sub == "GET":
            self._clip_get(rid, ent, out)
        else:
            self._answer(rid, ent, HTTP_BAD, b"", None, out)

    def _resolve_auto_clip(self):
        try:
            import win32clipboard                         # noqa: F401
            return "win32"
        except ImportError:
            pass
        if shutil.which("clip.exe") or shutil.which("powershell.exe"):
            return "wsl"
        return "none"

    @staticmethod
    def _clip_trim_get(data):
        # CRLF→LF、64KB 超は UTF-8 境界で切る (CLAUDE.md §4-27)。
        data = data.replace(b"\r\n", b"\n")
        if len(data) > CLIP_GET_MAX:
            cut = CLIP_GET_MAX
            while cut > 0 and (data[cut] & 0xC0) == 0x80:
                cut -= 1                                  # 継続バイトの上へ戻す
            data = data[:cut]
        return data

    def _clip_get(self, rid, ent, out):
        b = self.clip
        if b == "none":
            self._answer(rid, ent, HTTP_UNAVAIL,
                         self._err_body("no clipboard backend"), None, out)
            return
        if b.startswith("file:"):
            try:
                with open(b[len("file:"):], "rb") as f:
                    data = f.read()
            except OSError:
                data = b""                               # 空クリップ相当
            self._answer(rid, ent, 200, self._clip_trim_get(data), None, out)
            return
        if b == "win32":
            try:
                data = self._clip_get_win32()
            except SUBPROC_ERRORS + (ImportError,) as e:
                self._answer(rid, ent, HTTP_UNAVAIL, self._err_body(e), None, out)
                return
            self._answer(rid, ent, 200, self._clip_trim_get(data), None, out)
            return
        # wsl: powershell が base64(UTF-8) を吐く → 非同期に受けてデコード (B-1)
        cmd = ["powershell.exe", "-NoProfile", "-Command",
               "[Convert]::ToBase64String([Text.Encoding]::UTF8.GetBytes("
               "[string](Get-Clipboard -Raw)))"]
        try:
            proc = spawn(cmd)
        except SUBPROC_ERRORS as e:
            self._answer(rid, ent, HTTP_UNAVAIL, self._err_body(e), None, out)
            return
        self._start_async(rid, ent, proc, self._finish_clip_get)

    def _finish_clip_get(self, proc):
        rc = proc.poll()
        if rc:                                           # powershell が失敗 → 503
            raise self._subproc_failed(proc, "powershell.exe", rc)
        raw = base64.b64decode(proc.output().strip(), validate=True)
        return (200, self._clip_trim_get(raw))

    def _clip_get_win32(self):
        import win32clipboard
        win32clipboard.OpenClipboard()
        try:
            text = win32clipboard.GetClipboardData(win32clipboard.CF_UNICODETEXT)
        finally:
            win32clipboard.CloseClipboard()
        return (text or "").encode("utf-8")

    def _clip_put_finish(self, rid, ent, out):
        # PUT: CRLF→LF に正規化してから LF→CRLF。
        norm = ent["got"].replace(b"\r\n", b"\n").replace(b"\n", b"\r\n")
        b = self.clip
        if b == "none":
            self._answer(rid, ent, HTTP_UNAVAIL,
                         self._err_body("no clipboard backend"), None, out)
            return
        if b.startswith("file:"):
            try:
                with open(b[len("file:"):], "wb") as f:
                    f.write(norm)
            except OSError as e:
                self._answer(rid, ent, HTTP_UNAVAIL, self._err_body(e), None, out)
                return
            self._answer(rid, ent, 200, b"", None, out)
            return
        if b == "win32":
            try:
                self._clip_put_win32(norm)
            except SUBPROC_ERRORS + (ImportError,) as e:
                self._answer(rid, ent, HTTP_UNAVAIL, self._err_body(e), None, out)
                return
            self._answer(rid, ent, 200, b"", None, out)
            return
        # wsl: clip.exe に UTF-16LE + BOM を stdin (非同期)
        try:
            payload16 = UTF16_BOM + norm.decode("utf-8").encode("utf-16-le")
            proc = spawn(["clip.exe"], stdin_bytes=payload16)
        except SUBPROC_ERRORS as e:
            self._answer(rid, ent, HTTP_UNAVAIL, self._err_body(e), None, out)
            return
        self._start_async(rid, ent, proc, self._finish_clip_put)

    def _finish_clip_put(self, proc):
        rc = proc.poll()
        if rc:                                           # clip.exe は 0 で成功
            raise self._subproc_failed(proc, "clip.exe", rc)
        return (200, b"")

    def _clip_put_win32(self, norm):
        import win32clipboard
        text = norm.decode("utf-8")
        win32clipboard.OpenClipboard()
        try:
            win32clipboard.EmptyClipboard()
            win32clipboard.SetClipboardData(win32clipboard.CF_UNICODETEXT, text)
        finally:
            win32clipboard.CloseClipboard()


# --------------------------------------------------------------- 常駐ループ
def open_stream(args):
    if args.unix:
        if os.path.exists(args.unix):
            os.unlink(args.unix)
        srv = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        srv.bind(args.unix)
        srv.listen(1)
        print("listening on unix:%s" % args.unix, flush=True)
        conn, _ = srv.accept()
        srv.close()
        return FrameStream(conn)
    if args.connect:
        host, port = args.connect.rsplit(":", 1)
        s = socket.create_connection((host, int(port)))
        print("connected to %s" % args.connect, flush=True)
        return FrameStream(s)
    host, port = args.listen.rsplit(":", 1)
    srv = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    srv.bind((host, int(port)))
    srv.listen(1)
    print("listening on %s" % args.listen, flush=True)
    conn, peer = srv.accept()
    print("peer attached from %s:%d" % peer, flush=True)
    return FrameStream(conn)


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--listen", default="127.0.0.1:8026", metavar="HOST:PORT")
    ap.add_argument("--connect", metavar="HOST:PORT")
    ap.add_argument("--unix", metavar="PATH", help="UNIX ソケットで待ち受ける (ホスト TDD 用)")
    ap.add_argument("--state-dir", metavar="DIR", help="sess.txt (永続採番) を置く場所")
    ap.add_argument("--agent-gen", type=int, metavar="N", help="Agent 世代を固定する (試験用)")
    ap.add_argument("--mac", default="02:00:5e:00:00:01")
    ap.add_argument("--pcap", metavar="FILE")
    ap.add_argument("--quiet", action="store_true")
    ap.add_argument("--offline", action="store_true", help="実 HTTP を出さない (常に 502)")
    ap.add_argument("--file-root", metavar="DIR", help="GET /file/ の許可ルート")
    ap.add_argument("--once", action="store_true", help="exit after the peer disconnects")
    # ---- N2: PRINT / CLIP ----
    ap.add_argument("--spool-dir", metavar="DIR",
                    help="印刷スプールの置き場 (既定 <state-dir>/spool)")
    ap.add_argument("--print-dir", metavar="DIR",
                    help="to-file 印刷の出力先 (指定でスプールを一意名で移す)")
    ap.add_argument("--printer", action="store_true",
                    help="CLOSE で win32print により既定プリンタへ送る (任意依存)")
    ap.add_argument("--lines-per-page", type=int, default=DEFAULT_LINES_PER_PAGE,
                    metavar="N", help="\\f が無いときのページ換算行数 (既定 60)")
    ap.add_argument("--clip", default="auto",
                    metavar="{auto,win32,wsl,file:<path>,none}",
                    help="クリップボード backend (既定 auto)")
    args = ap.parse_args()
    if not valid_clip_arg(args.clip):
        ap.error("--clip は {auto,win32,wsl,none} か file:<path> "
                 "(不正: %r)" % args.clip)

    if args.state_dir:
        os.makedirs(args.state_dir, exist_ok=True)
    agent = HostAgent(parse_mac(args.mac), state_dir=args.state_dir,
                      agent_gen=args.agent_gen, quiet=args.quiet,
                      allow_net=not args.offline, file_root=args.file_root,
                      spool_dir=args.spool_dir, print_dir=args.print_dir,
                      clip=args.clip, printer=args.printer,
                      lines_per_page=args.lines_per_page)
    pcap = Pcap(args.pcap) if args.pcap else None
    print("host_agent v2 mac %s agent-gen %d (wire v2, 3-way HELLO, rid ledger)"
          % (args.mac, agent.agent_gen), flush=True)

    while True:
        try:
            stream = open_stream(args)
        except OSError as e:
            print("connect failed: %s" % e, flush=True)
            if args.connect:
                time.sleep(1.0)
                continue
            return 1
        try:
            while True:
                # select で待つことで子プロセス (CLIP wsl) の完了 / 期限を巡回で
                # 拾える (往復 3 B-1)。読めるものが無ければ tick だけ回す。
                try:
                    ready, _, _ = select.select([stream.sock], [], [], TICK_INTERVAL)
                except OSError as e:
                    raise ConnectionError(str(e))
                if ready:
                    for frame in stream.recv_frames():
                        agent.handle(frame, stream, pcap)
                agent.tick(stream)
        except ConnectionError as e:
            print("disconnected: %s (%s)" % (e, agent.counts), flush=True)
            if args.once:
                return 0
            if args.connect or args.unix:
                if args.unix:
                    continue
                time.sleep(1.0)


if __name__ == "__main__":
    sys.exit(main())
