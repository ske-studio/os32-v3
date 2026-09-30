#!/usr/bin/env python3
"""serialfs_host.py — SerialFS のホスト側 (票 TASK_SERIAL_HOSTFS 部品 B)。

ゲストの `sfs run <コマンド行>` (常駐シェルの組込み) が、同じシリアル線の上で
SerialFS のセッションを開く。ホストは `tools/rshell_serial.py --serve-host <dir>`
から、この模組の `serve_line` を使って

  1. `sfs run ...` の 1 行を rshell へ送る
  2. **その行の EOT まで**、線を次のように振り分ける
       - `ENQ 'S' 'F'` で始まり長さと CRC32 が合うもの → フレーム
       - EOT (フレームの外)                           → 行の終わり
       - それ以外                                      → rshell の文字
  3. 要求フレームには `<dir>` を根にした応答を返す。LOG フレームは溜めた
     出力として表示し、EXIT フレームで終了コードを受け取る

形式の正典は fs/sfs_proto.h (C)。ここはその写しで、tools/tests/test_serialfs.py が
C の組み立てた列をここで読み、その逆も確かめる。

規則 (票 §1-v2 B-7' / §1-v3):
  - **`sfs run` の行の外ではフレームを解釈しない** (セッション外の `cat` の本文で
    要求が動かない)。この模組は serve_line の中でしか使われない
  - 応答のキャッシュは**直前の 1 件だけ**。一致条件は (セッション ID, 番号,
    要求フレームの CRC32)。同じ要求の再送には**操作を再実行せず**保存した応答を
    返す (RENAME / UNLINK / MKDIR / WRITE の二重実行を防ぐ)。HELLO で消える
  - 要求を受けてから SFS_FIRST_BYTE_S を過ぎた応答は**送らない** (ゲストの
    1 試行の期限を過ぎている = 次の試行の最中に古い番号が届くだけ)
  - BYE を受けたセッション、`sfs run` の行末 EOT の後には何も送らない
  - 未知のセッション ID の要求には SFS_T_ERR (OS32_ERR_STALE) で答える
    (ホストが再起動した)
  - パスは `/` 始まりのルート相対。`..` / `.` / `\\` / NUL の要素は断り、
    **realpath** で根の外 (symlink 経由も) を断る
"""
import errno
import os
import collections
import queue
import random
import stat as statmod
import threading
import struct
import time
import zlib

# ---- fs/sfs_proto.h の写し --------------------------------------------------
ENQ = 0x05
EOT = 0x04
MAGIC = b"SF"
VERSION = 1
HDR_LEN = 12
CRC_LEN = 4
MAX_PAYLOAD = 512
MAX_FRAME = HDR_LEN + MAX_PAYLOAD + CRC_LEN
READ_MAX = MAX_PAYLOAD - 4
PATH_MAX = 255

T_HELLO = 0x01
T_BYE = 0x02
T_STAT = 0x10
T_LIST = 0x11
T_READ = 0x12
T_WRITE = 0x13
T_MKDIR = 0x14
T_RMDIR = 0x15
T_UNLINK = 0x16
T_RENAME = 0x17
T_LOG = 0x20
T_EXIT = 0x21
T_RESP = 0x80
T_ERR = 0xFF

WF_TRUNC = 0x01
KIND_FILE = 1
KIND_DIR = 2
KIND_OTHER = 3
XF_NOT_QUIET = 0x01
XF_DEAD = 0x02

FIRST_BYTE_S = 2.0          # SFS_FIRST_BYTE_MS
GAP_S = 0.1                 # SFS_GAP_MS
# 振り分けで「フレームの途中で途切れた」と見なす長さ。ゲストの隔離 (500ms)
# より短く、バイト間 (100ms) より長く取る。
DEMUX_STALL_S = 0.3

# ---- OS32_ERR_* (sdk/include/os32/os32_kapi_shared.h の写し) ----------------
ERR_IO = -1
ERR_NOTFOUND = -2
ERR_NOSPC = -4
ERR_EXIST = -5
ERR_NOTDIR = -6
ERR_NOTEMPTY = -7
ERR_ISDIR = -8
ERR_INVAL = -9
ERR_STALE = -11
ERR_ROFS = -15
ERR_NAMETOOLONG = -16

ERRNO_MAP = {
    errno.ENOENT: ERR_NOTFOUND,
    errno.EEXIST: ERR_EXIST,
    errno.ENOTDIR: ERR_NOTDIR,
    errno.ENOTEMPTY: ERR_NOTEMPTY,
    errno.EISDIR: ERR_ISDIR,
    errno.ENOSPC: ERR_NOSPC,
    errno.ENAMETOOLONG: ERR_NAMETOOLONG,
    errno.EINVAL: ERR_INVAL,
    # O_NOFOLLOW で symlink に当たった = 辿らない (根の外へ出る道を作らない)
    errno.ELOOP: ERR_INVAL,
    errno.EROFS: ERR_ROFS,
}


class SfsError(Exception):
    def __init__(self, code):
        Exception.__init__(self, code)
        self.code = code


def crc32(data):
    return zlib.crc32(bytes(data)) & 0xFFFFFFFF


def encode(ftype, sid, seq, payload=b""):
    payload = bytes(payload)
    if len(payload) > MAX_PAYLOAD:
        raise ValueError("payload too long")
    head = struct.pack("<BIHH", ftype, sid & 0xFFFFFFFF, seq & 0xFFFF,
                       len(payload))
    body = head + payload
    return bytes([ENQ]) + MAGIC + body + struct.pack("<I", crc32(body))


class Frame(object):
    __slots__ = ("type", "sid", "seq", "payload", "crc")

    def __init__(self, ftype, sid, seq, payload, crc):
        self.type = ftype
        self.sid = sid
        self.seq = seq
        self.payload = payload
        self.crc = crc

    def __repr__(self):
        return "Frame(type=%#x sid=%#x seq=%d len=%d)" % (
            self.type, self.sid, self.seq, len(self.payload))


def try_frame(buf, i):
    """buf[i] が ENQ のとき、そこからのフレームを判定する。

    戻り値:
      ('need', 0)          まだ足りない
      ('text', 1)          フレームではない (ENQ 1 バイトを文字として進める)
      ('bad', n)           印と長さは合うが CRC が違う (n バイトを捨てる)
      ('frame', n, Frame)  フレーム (n バイト)
    """
    avail = len(buf) - i
    if avail < 3:
        if avail >= 2 and buf[i + 1] != MAGIC[0]:
            return ("text", 1)
        return ("need", 0)
    if bytes(buf[i + 1:i + 3]) != MAGIC:
        return ("text", 1)
    if avail < HDR_LEN:
        return ("need", 0)
    ftype, sid, seq, ln = struct.unpack_from("<BIHH", bytes(buf[i + 3:i + HDR_LEN]))
    if ln > MAX_PAYLOAD:
        return ("text", 1)
    total = HDR_LEN + ln + CRC_LEN
    if avail < total:
        return ("need", 0)
    body = bytes(buf[i + 3:i + HDR_LEN + ln])
    want = struct.unpack_from("<I", bytes(buf[i + HDR_LEN + ln:i + total]))[0]
    if crc32(body) != want:
        return ("bad", total)
    return ("frame", total, Frame(ftype, sid, seq,
                                  bytes(buf[i + HDR_LEN:i + HDR_LEN + ln]),
                                  want))


class Demux(object):
    """線のバイトを「文字 / フレーム / EOT」に振り分ける (`sfs run` の行の中だけ)。

    - フレームの外の EOT は行の終わり。**フレームの中の 0x04 は本体**
    - 印 ('SF') と長さが合って CRC だけ違うものは**フレームごと捨てる**
      (中身の 0x04 を行の終わりと読まない)
    - フレームの途中で DEMUX_STALL_S 途切れたら、その ENQ は文字として流す
    """

    def __init__(self, stall_s=DEMUX_STALL_S):
        self.buf = bytearray()
        self.stall_s = stall_s
        self.last_rx = None
        self.bad_frames = 0

    def feed(self, data, now):
        if data:
            self.buf += data
            self.last_rx = now
        return self._run(now)

    def poll(self, now):
        """受信が無いあいだに呼ぶ (途切れたフレームを文字として流す)。"""
        return self._run(now)

    def _run(self, now):
        events = []
        text_start = 0
        i = 0
        buf = self.buf
        while i < len(buf):
            b = buf[i]
            if b == EOT:
                if i > text_start:
                    events.append(("text", bytes(buf[text_start:i])))
                events.append(("eot",))
                i += 1
                text_start = i
                continue
            if b != ENQ:
                i += 1
                continue
            r = try_frame(buf, i)
            if r[0] == "need":
                stalled = (self.last_rx is not None and
                           now - self.last_rx >= self.stall_s)
                if stalled:
                    i += 1           # ENQ を文字として進める
                    continue
                break
            if r[0] == "text":
                i += 1
                continue
            if i > text_start:
                events.append(("text", bytes(buf[text_start:i])))
            if r[0] == "bad":
                self.bad_frames += 1
            else:
                events.append(("frame", r[2]))
            i += r[1]
            text_start = i
        # i が末尾なら全部確定。途中 (待ちのフレーム) なら、その手前まで確定
        if i > text_start:
            events.append(("text", bytes(buf[text_start:i])))
            text_start = i
        del buf[:text_start]
        return events


def _err(e):
    if isinstance(e, SfsError):
        return e.code
    if isinstance(e, OSError):
        return ERRNO_MAP.get(e.errno, ERR_IO)
    return ERR_IO


class HostFS(object):
    """根 root の下だけを見せる。パスは bytes のまま扱う (名前を化けさせない)。

    **書き込みは既定で禁止** (票 B-7'、レビュー往復 1 の PM 決定)。書けるのは
    allow_write に並べたルート相対のパスとその下だけ (`--allow-write`)。それ以外の
    WRITE / MKDIR / RMDIR / UNLINK / RENAME は OS32_ERR_ROFS。

    **2 つの作り**:
      - 固定 (POSIX、既定): **起動時に根のディレクトリを `O_NOFOLLOW` で開いて
        fd を保持し** (root_fd、往復 2 の Codex 2)、操作のたびにそれを dup した
        起点から各要素を `O_DIRECTORY | O_NOFOLLOW` で 1 段ずつ開き (openat 相当の
        dir_fd)、最後の操作も dir_fd と O_NOFOLLOW / follow_symlinks=False で行う。
        **symlink は一切辿らない** (根の中を指すものも)。検査と操作の間に
        symlink を差し替えられても、根そのものを差し替えられても、根の外へは
        出ない (Codex 5)。書き込みの許可も辿らない名前で当たるので、リンク越しに
        `--allow-write` の外へ書く道は無い
      - パス (Windows など dir_fd の無いホスト): realpath を取って根と
        **要素単位で** (commonpath / normcase) 比べてから、パスで操作する。
        書き込みの許可は wire の名前と **realpath で解決した実体の両方**に当てる
        (`out/link -> protected` のとき `/out/link/f` は protected への書き込み
        なので ROFS、往復 2 の Codex 1)。
        **検査と操作の間に symlink / junction を差し替えられる競合は残る**
        (man sfs に書いた)。Windows では `:` (代替データストリーム) も断る
    pm に ntpath を渡すと Windows の規則で判定だけを試せる (試験用)。
    """

    def __init__(self, root, allow_write=(), pm=None, secure=None):
        self.pm = pm or os.path
        raw = os.fsencode(root) if not isinstance(root, bytes) else root
        self.root = self.pm.realpath(raw)
        if pm is None and not os.path.isdir(self.root):
            raise ValueError("serve-host root is not a directory: %r" % root)
        if secure is None:
            secure = (self.pm is os.path and os.name == "posix" and
                      hasattr(os, "O_NOFOLLOW") and hasattr(os, "O_DIRECTORY")
                      and os.open in os.supports_dir_fd
                      and os.stat in os.supports_dir_fd)
        self.secure = bool(secure)
        self.root_fd = -1
        if self.secure:
            # 起点は 1 回だけ開いて保持する。根がその後で symlink に差し替えられ
            # ても、この fd は元のディレクトリを指し続ける
            self.root_fd = os.open(self.root, os.O_RDONLY | os.O_DIRECTORY |
                                   os.O_NOFOLLOW)
        self.allow = []
        for a in allow_write:
            raw_a = a
            a = os.fsencode(a) if not isinstance(a, bytes) else a
            a = a.replace(b"\\", b"/")
            try:
                # `/` (または空) は根の全体 = どこでも書ける (明示したときだけ)
                self.allow.append(tuple(self._comps(b"/" + a.lstrip(b"/"))))
            except SfsError:
                raise ValueError("--allow-write: bad path %r (root-relative, "
                                 "no . / .. / backslash)" % (raw_a,))

    def close(self):
        if self.root_fd >= 0:
            os.close(self.root_fd)
            self.root_fd = -1

    def __del__(self):
        try:
            self.close()
        except Exception:  # noqa: BLE001
            pass

    # ---- パスの規則 ----
    def _comps(self, wire):
        if not wire.startswith(b"/"):
            raise SfsError(ERR_INVAL)
        comps = [c for c in wire[1:].split(b"/") if c != b""]
        for c in comps:
            if c in (b".", b"..") or b"\\" in c or b"\x00" in c:
                raise SfsError(ERR_INVAL)
            if self.pm.sep == "\\" and b":" in c:
                raise SfsError(ERR_INVAL)
            if len(c) > PATH_MAX:
                raise SfsError(ERR_NAMETOOLONG)
        return comps

    def real_comps(self, path):
        """path の realpath の、根からの相対の要素 (根の外なら None)。
        **要素単位で**比べる (`C:\\hostile` は `C:\\host` の中でない)。"""
        real = self.pm.realpath(path)
        nc = self.pm.normcase
        try:
            common = self.pm.commonpath([nc(self.root), nc(real)])
        except ValueError:            # 別のドライブなど
            return None
        if common != nc(self.root):
            return None
        rel = nc(real)[len(nc(self.root)):]
        return [c for c in rel.replace(self.pm.sep.encode(), b"/").split(b"/")
                if c != b""]

    def contained(self, path):
        """path (realpath を取る) が根の中か。"""
        return self.real_comps(path) is not None

    def resolve(self, wire, allow_root=True):
        """ワイヤ上のパス → ホストのパス (パスの作りで使う)。断るときは SfsError。"""
        comps = self._comps(wire)
        if not comps and not allow_root:
            raise SfsError(ERR_INVAL)
        path = self.pm.join(self.root, *comps) if comps else self.root
        if not self.contained(path):
            raise SfsError(ERR_INVAL)       # symlink で根の外へ出る
        return path

    def _allowed(self, comps):
        nc = self.pm.normcase
        key = tuple(nc(c) for c in comps)
        for a in self.allow:
            a = tuple(nc(c) for c in a)
            if key[:len(a)] == a:
                return True
        return False

    def _need_write(self, comps):
        if not comps:
            raise SfsError(ERR_INVAL)       # 根そのものは変えない
        if not self._allowed(comps):
            raise SfsError(ERR_ROFS)

    def resolve_write(self, wire):
        """パスの作りの書き込み: wire の名前で許可を見てから、**realpath で解決した
        実体**にも許可を当てる (リンク越しに --allow-write の外へ書かない)。
        親がリンクでも、名前そのものがリンクでも、実体の側で判定する。"""
        comps = self._comps(wire)
        self._need_write(comps)
        path = self.resolve(wire, allow_root=False)
        rc = self.real_comps(path)
        if rc is None:
            raise SfsError(ERR_INVAL)
        if not rc:
            raise SfsError(ERR_INVAL)       # 実体が根そのもの
        if not self._allowed(rc):
            raise SfsError(ERR_ROFS)
        return path

    # ---- 固定の作りの道具 ----
    def _open_dir(self, comps):
        """根から comps を 1 段ずつ辿ったディレクトリの fd (呼び手が閉じる)。
        起点は起動時に固定した root_fd (パスで開き直さない)。"""
        if self.root_fd < 0:
            raise SfsError(ERR_IO)
        fd = os.dup(self.root_fd)
        try:
            for c in comps:
                try:
                    nfd = os.open(c, os.O_RDONLY | os.O_DIRECTORY |
                                  os.O_NOFOLLOW, dir_fd=fd)
                except OSError:
                    # O_NOFOLLOW + O_DIRECTORY の symlink は ELOOP か ENOTDIR。
                    # symlink なら「辿らない」(INVAL) と言い分ける
                    try:
                        lst = os.stat(c, dir_fd=fd, follow_symlinks=False)
                    except OSError:
                        lst = None
                    if lst is not None and statmod.S_ISLNK(lst.st_mode):
                        raise SfsError(ERR_INVAL)
                    raise
                os.close(fd)
                fd = nfd
        except BaseException:
            os.close(fd)
            raise
        return fd

    @staticmethod
    def _kind(st):
        if statmod.S_ISDIR(st.st_mode):
            return KIND_DIR, 0
        if statmod.S_ISREG(st.st_mode):
            return KIND_FILE, st.st_size
        return KIND_OTHER, 0            # symlink も辿らずにここ

    def _stat_raw(self, comps):
        if self.secure:
            if not comps:
                fd = self._open_dir([])
                try:
                    return os.fstat(fd)
                finally:
                    os.close(fd)
            pfd = self._open_dir(comps[:-1])
            try:
                return os.stat(comps[-1], dir_fd=pfd, follow_symlinks=False)
            finally:
                os.close(pfd)
        return os.stat(self.resolve(b"/" + b"/".join(comps)))

    # ---- 操作 (戻り値は (status, 応答の status の後ろ)、または例外) ----
    def stat(self, wire):
        st = self._stat_raw(self._comps(wire))
        kind, size = self._kind(st)
        if size > 0xFFFFFFFF:
            raise SfsError(ERR_IO)
        mtime = int(st.st_mtime)
        if mtime < 0 or mtime > 0xFFFFFFFF:
            mtime = 0
        return 0, struct.pack("<BII", kind, size, mtime)

    def _entries(self, comps):
        if self.secure:
            fd = self._open_dir(comps)
            try:
                # listdir(fd) は str を返す。bytes に戻す (名前を化けさせない)
                names = sorted(os.fsencode(n) for n in os.listdir(fd))
                out = []
                for n in names:
                    try:
                        st = os.stat(n, dir_fd=fd, follow_symlinks=False)
                        out.append((n,) + self._kind(st))
                    except OSError:
                        out.append((n, KIND_OTHER, 0))
                return out
            finally:
                os.close(fd)
        path = self.resolve(b"/" + b"/".join(comps))
        out = []
        for n in sorted(os.listdir(path)):
            try:
                out.append((n,) + self._kind(os.stat(self.pm.join(path, n))))
            except OSError:
                out.append((n, KIND_OTHER, 0))   # 壊れた symlink など
        return out

    def list(self, wire, cookie):
        ents = self._entries(self._comps(wire))
        out = bytearray()
        room = MAX_PAYLOAD - 4 - 4
        idx = cookie
        while idx < len(ents):
            name, kind, size = ents[idx]
            if len(name) > PATH_MAX or b"\\" in name:
                idx += 1                     # 見せられない名前は飛ばす
                continue
            ent = struct.pack("<BIB", kind, min(size, 0xFFFFFFFF),
                              len(name)) + name
            if len(out) + len(ent) > room:
                break
            out += ent
            idx += 1
        nxt = idx if idx < len(ents) else 0
        return 0, struct.pack("<I", nxt) + bytes(out)

    def read(self, wire, offset, count):
        count = min(count, READ_MAX)
        comps = self._comps(wire)
        if self.secure:
            if not comps:
                raise SfsError(ERR_ISDIR)
            pfd = self._open_dir(comps[:-1])
            try:
                fd = os.open(comps[-1], os.O_RDONLY | os.O_NOFOLLOW, dir_fd=pfd)
            finally:
                os.close(pfd)
            try:
                if statmod.S_ISDIR(os.fstat(fd).st_mode):
                    raise SfsError(ERR_ISDIR)
                data = os.pread(fd, count, offset)
            finally:
                os.close(fd)
            return len(data), data
        with open(self.resolve(wire), "rb") as f:
            f.seek(offset)
            data = f.read(count)
        return len(data), data

    @staticmethod
    def _pwrite_all(fd, data, offset):
        """pwrite は一部しか書かずに戻ることがある (往復 2、Codex 5)。書けた分を
        くり返し書き、1 バイトも進まなければ IO。戻りは書けた数 (= len(data))。"""
        done = 0
        data = bytes(data)
        while done < len(data):
            n = os.pwrite(fd, data[done:], offset + done)
            if n <= 0:
                raise SfsError(ERR_IO)
            done += n
        return done

    def write(self, wire, offset, flags, data):
        comps = self._comps(wire)
        self._need_write(comps)
        if self.secure:
            pfd = self._open_dir(comps[:-1])
            try:
                fl = os.O_WRONLY | os.O_CREAT | os.O_NOFOLLOW
                if flags & WF_TRUNC:
                    fl |= os.O_TRUNC
                fd = os.open(comps[-1], fl, 0o644, dir_fd=pfd)
            finally:
                os.close(pfd)
            try:
                n = self._pwrite_all(fd, data, offset)
            finally:
                os.close(fd)
            return n, b""
        path = self.resolve_write(wire)
        if os.path.isdir(path):
            raise SfsError(ERR_ISDIR)
        if flags & WF_TRUNC:
            mode = "wb"
        else:
            mode = "r+b" if os.path.exists(path) else "w+b"
        with open(path, mode) as f:
            f.seek(offset)
            n = f.write(data)
            if n != len(data):
                raise SfsError(ERR_IO)
        return n, b""

    def _at_parent(self, comps, fn):
        pfd = self._open_dir(comps[:-1])
        try:
            fn(comps[-1], pfd)
        finally:
            os.close(pfd)

    def mkdir(self, wire):
        comps = self._comps(wire)
        self._need_write(comps)
        if self.secure:
            self._at_parent(comps, lambda n, fd: os.mkdir(n, 0o755, dir_fd=fd))
        else:
            os.mkdir(self.resolve_write(wire))
        return 0, b""

    def rmdir(self, wire):
        comps = self._comps(wire)
        self._need_write(comps)
        if self.secure:
            self._at_parent(comps, lambda n, fd: os.rmdir(n, dir_fd=fd))
        else:
            os.rmdir(self.resolve_write(wire))
        return 0, b""

    def unlink(self, wire):
        comps = self._comps(wire)
        self._need_write(comps)
        if self.secure:
            def op(n, fd):
                st = os.stat(n, dir_fd=fd, follow_symlinks=False)
                if statmod.S_ISDIR(st.st_mode):
                    raise SfsError(ERR_ISDIR)
                os.unlink(n, dir_fd=fd)
            self._at_parent(comps, op)
            return 0, b""
        path = self.resolve_write(wire)
        if os.path.isdir(path) and not os.path.islink(path):
            raise SfsError(ERR_ISDIR)
        os.unlink(path)
        return 0, b""

    def rename(self, old, new):
        oc, nc = self._comps(old), self._comps(new)
        self._need_write(oc)
        self._need_write(nc)
        if self.secure:
            sfd = self._open_dir(oc[:-1])
            try:
                dfd = self._open_dir(nc[:-1])
                try:
                    os.replace(oc[-1], nc[-1], src_dir_fd=sfd, dst_dir_fd=dfd)
                finally:
                    os.close(dfd)
            finally:
                os.close(sfd)
            return 0, b""
        os.replace(self.resolve_write(old), self.resolve_write(new))
        return 0, b""


def _take_path(p, at):
    if at >= len(p):
        raise SfsError(ERR_INVAL)
    n = p[at]
    if at + 1 + n > len(p):
        raise SfsError(ERR_INVAL)
    return bytes(p[at + 1:at + 1 + n]), at + 1 + n


class Server(object):
    """要求フレーム → 応答フレーム。時計と乱数は差し替えられる (試験用)。"""

    def __init__(self, fs, now=time.monotonic, rng=None, log=None):
        self.fs = fs
        self.now = now
        self.rng = rng or random.SystemRandom()
        self.log = log or (lambda msg: None)
        self.sid = 0                 # 今のセッション (0 = 無い)
        self.closed = set()          # BYE を受けたセッション
        self.last_closed = 0
        self.cache_key = None        # (sid, seq, 要求の CRC32)
        self.cache_resp = None
        self.executed = 0            # 実際に操作した回数 (試験用)
        self.replayed = 0            # 保存した応答を返した回数
        self.late = 0                # 期限を過ぎて送らなかった応答
        self.stale = 0
        self.log_text = bytearray()
        self.exit = None             # (code, dropped, flags)

    def begin_line(self):
        """`sfs run` の 1 行ごとに結果を空にする (REPL で前の行の EXIT を
        今の行の結果と取り違えない、レビュー往復 1 の Codex 7)。"""
        self.log_text = bytearray()
        self.exit = None

    # 要求 → (応答 bytes または None)
    def handle(self, fr):
        t = fr.type
        if t == T_HELLO:
            return self._hello(fr)
        if t == T_BYE:
            if fr.sid and fr.sid == self.sid:
                self.closed.add(fr.sid)
                self.last_closed = fr.sid
                self.sid = 0
                self.cache_key = None
                self.cache_resp = None
            return None
        if t in (T_LOG, T_EXIT):
            if fr.sid and fr.sid in (self.sid, self.last_closed):
                if t == T_LOG:
                    self.log_text += fr.payload
                elif len(fr.payload) >= 12:
                    self.exit = struct.unpack_from("<iII", fr.payload)
            return None
        if t & T_RESP:
            return None                  # 応答の向きのもの (こちらには来ない)
        if fr.sid in self.closed:
            return None                  # BYE の後は何も送らない
        if fr.sid == 0 or fr.sid != self.sid:
            self.stale += 1
            return encode(T_ERR, fr.sid, fr.seq, struct.pack("<i", ERR_STALE))
        key = (fr.sid, fr.seq, fr.crc)
        if key == self.cache_key:
            self.replayed += 1
            return self.cache_resp
        resp = self._execute(fr)
        self.cache_key = key
        self.cache_resp = resp
        return resp

    def _hello(self, fr):
        key = (0, fr.seq, fr.crc)
        if key == self.cache_key and self.sid:
            self.replayed += 1
            return self.cache_resp
        if len(fr.payload) < 8 or fr.sid != 0 or fr.seq != 0:
            return None
        ver, maxp, nonce = struct.unpack_from("<HHI", fr.payload)
        if self.sid:
            # 前のセッションが BYE 無しで終わった (ゲストの再起動など)。
            # そちらへは以後何も送らない
            self.closed.add(self.sid)
        sid = 0
        while sid == 0 or sid in self.closed:
            sid = self.rng.getrandbits(32)
        self.sid = sid
        resp = encode(T_HELLO | T_RESP, sid, 0,
                      struct.pack("<iHHI", 0, VERSION, MAX_PAYLOAD, nonce))
        self.cache_key = key
        self.cache_resp = resp
        self.log("[serve-host] HELLO v%d max=%d -> session %08x" % (ver, maxp, sid))
        return resp

    def _execute(self, fr):
        p = fr.payload
        t = fr.type
        body = b""
        self.executed += 1
        try:
            if t == T_STAT:
                path, _ = _take_path(p, 0)
                st, body = self.fs.stat(path)
            elif t == T_LIST:
                if len(p) < 4:
                    raise SfsError(ERR_INVAL)
                cookie = struct.unpack_from("<I", p)[0]
                path, _ = _take_path(p, 4)
                st, body = self.fs.list(path, cookie)
            elif t == T_READ:
                if len(p) < 6:
                    raise SfsError(ERR_INVAL)
                off, cnt = struct.unpack_from("<IH", p)
                path, _ = _take_path(p, 6)
                st, body = self.fs.read(path, off, cnt)
            elif t == T_WRITE:
                if len(p) < 5:
                    raise SfsError(ERR_INVAL)
                off, flags = struct.unpack_from("<IB", p)
                path, at = _take_path(p, 5)
                st, body = self.fs.write(path, off, flags, p[at:])
            elif t in (T_MKDIR, T_RMDIR, T_UNLINK):
                path, _ = _take_path(p, 0)
                op = {T_MKDIR: self.fs.mkdir, T_RMDIR: self.fs.rmdir,
                      T_UNLINK: self.fs.unlink}[t]
                st, body = op(path)
            elif t == T_RENAME:
                old, at = _take_path(p, 0)
                new, _ = _take_path(p, at)
                st, body = self.fs.rename(old, new)
            else:
                raise SfsError(ERR_INVAL)
        except Exception as e:          # noqa: BLE001 — 何でも status にする
            st, body = _err(e), b""
        return encode(t | T_RESP, fr.sid, fr.seq, struct.pack("<i", st) + body)


def sfs_child(line):
    """`sfs run <コマンド行>` の子を取り出す (userland/shell/serial_watchdog.c の
    rsh_sfs_child と同じ規則)。形が違えば None。"""
    s = line.lstrip(" \t")
    if not s.startswith("sfs") or s[3:4] not in (" ", "\t"):
        return None
    s = s[3:].lstrip(" \t")
    if not s.startswith("run") or s[3:4] not in (" ", "\t"):
        return None
    s = s[3:].lstrip(" \t")
    return s or None


REQUEST_TYPES = (T_HELLO, T_STAT, T_LIST, T_READ, T_WRITE, T_MKDIR, T_RMDIR,
                 T_UNLINK, T_RENAME)


def read_size(port):
    """来ている分だけ読む大きさ。pyserial の read(n) は n バイト揃うか timeout
    まで戻らないので、**read(MAX_FRAME) だと要求ごとに 200ms 止まる**
    (レビュー往復 1、Fable M1)。in_waiting が無い / 0 なら 1 (最初の 1 バイトを
    待つ)。"""
    try:
        n = int(getattr(port, "in_waiting", 0) or 0)
    except Exception:  # noqa: BLE001 — 閉じたポートなど
        n = 0
    return n if n > 0 else 1


class PortReceiver(object):
    """受信を**常に回す** (別スレッド)。(到着時刻, bytes) を積む。

    ファイル操作と受信を同じループで回すと、ホストの操作が長く止まったあいだに
    届いた再送や BYE を「今届いた」と読み、期限切れの応答を返してしまう
    (レビュー往復 1、Codex 1)。到着時刻は受け取った時点で刻む。
    """

    def __init__(self, port, now=time.monotonic):
        self.port = port
        self.now = now
        self.q = queue.Queue()
        self.stop_ev = threading.Event()
        self.error = None
        self.thread = threading.Thread(target=self._run, daemon=True)
        self.thread.start()

    def step(self):
        """1 回読む。戻りは読めたバイト (空もあり)。"""
        d = self.port.read(read_size(self.port))
        if d:
            self.q.put((self.now(), bytes(d)))
        return d

    def _run(self):
        try:
            while not self.stop_ev.is_set():
                try:
                    self.step()
                except Exception as e:  # noqa: BLE001
                    if self.stop_ev.is_set():
                        break           # cancel_read で起こされた
                    self.error = e
                    break
        finally:
            self.stop_ev.set()

    def get(self, timeout):
        items = []
        try:
            items.append(self.q.get(timeout=timeout) if timeout > 0
                         else self.q.get_nowait())
        except queue.Empty:
            return items
        while True:
            try:
                items.append(self.q.get_nowait())
            except queue.Empty:
                return items

    def close(self, timeout=30.0):
        """受信を止め、**スレッドが終わったことを確かめてから**戻る (往復 2、
        Codex 4)。read の中で待っているスレッドは port.cancel_read() で起こす
        (pyserial の Serial と AidebugPort が持つ。無ければ port の timeout まで
        待つ)。終わらないうちにポートを次の行へ渡すと、この受信器が次の行の
        応答を奪う。戻り値: True = 終わった / False = timeout 内に終わらなかった
        (呼び手は port を使ってはいけない)。"""
        self.stop_ev.set()
        cancel = getattr(self.port, "cancel_read", None)
        deadline = time.monotonic() + timeout
        while self.thread.is_alive():
            if cancel is not None:
                try:
                    cancel()
                except Exception:  # noqa: BLE001
                    pass
            self.thread.join(0.05)
            if time.monotonic() >= deadline:
                break
        return not self.thread.is_alive()

    def stopped(self):
        return not self.thread.is_alive()


def serve_line(port, line, server, timeout_s, out, now=time.monotonic,
               sleep=time.sleep, rx=None):
    """`sfs run ...` の 1 行を送り、行末の EOT まで線を振り分けて答える。

    - 受信は PortReceiver が常に回し、フレームごとに**到着時刻**を持つ
    - 要求は**到着から** FIRST_BYTE_S を過ぎていたら実行もしない (ゲストは
      その試行をあきらめて再送している。再送の方に答える)
    - 応答を送る**直前に**、処理中に届いた分を取り込み、(a) 同じセッションの
      BYE か行末の EOT が来ている (b) 到着から期限を過ぎた、なら送らない
    - 時間切れは「進捗が無い時間」(何か届くたびに延ばす)
    - `sfs run` の行から EOT までのフレームでない部分は rshell の出力として出す
      (HELLO が通らなかった回にゲストが生で流す文字もここ、決定 11)
    port は write / flush / reset_input_buffer を持つもの。rx を渡さなければ
    PortReceiver(port) を作る (read / in_waiting を使う)。
    戻り値 dict: text / eot / exit / sent / late / after_bye / bad_frames。
    """
    dm = Demux()
    text = bytearray()
    server.begin_line()
    port.reset_input_buffer()            # 送る前だけ (配信中は使わない)
    port.write(line.encode("utf-8") + b"\n")
    port.flush()
    own_rx = rx is None
    if own_rx:
        rx = PortReceiver(port, now)
    events = collections.deque()
    stats = {"sent": 0, "late": 0, "after_bye": 0}

    def pump(timeout):
        got = rx.get(timeout)
        for t, d in got:
            for e in dm.feed(d, t):
                events.append((t, e))
        if not got:
            t = now()
            for e in dm.poll(t):
                events.append((t, e))
        return bool(got)

    def closed_ahead(sid):
        for _, e in events:
            if e[0] == "eot":
                return True
            if (e[0] == "frame" and e[1].type == T_BYE and
                    e[1].sid and e[1].sid in (sid, server.sid)):
                return True
        return False

    eot = False
    last_progress = now()
    try:
        while not eot:
            if not events:
                if pump(0.05):
                    last_progress = now()
                elif now() - last_progress >= timeout_s:
                    break
                continue
            t, ev = events.popleft()
            if ev[0] == "text":
                text += ev[1]
                out.write(ev[1].decode("utf-8", errors="replace"))
            elif ev[0] == "eot":
                eot = True
            elif ev[0] == "frame":
                fr = ev[1]
                if fr.type in REQUEST_TYPES and now() - t > FIRST_BYTE_S:
                    stats["late"] += 1          # 実行もしない
                    continue
                had_log = len(server.log_text)
                resp = server.handle(fr)
                if len(server.log_text) != had_log:
                    out.write(bytes(server.log_text[had_log:]).decode(
                        "utf-8", errors="replace"))
                if resp is None:
                    continue
                pump(0)                         # 処理中に届いた分
                if fr.sid in server.closed or closed_ahead(fr.sid):
                    stats["after_bye"] += 1
                    continue
                if now() - t > FIRST_BYTE_S:
                    stats["late"] += 1
                    continue
                port.write(resp)
                port.flush()
                stats["sent"] += 1
    finally:
        if own_rx and not rx.close():
            raise RuntimeError("serialfs_host: receiver thread did not stop; "
                               "the port must not be reused")
    # 行が終わった = セッションも終わっている。以後このセッションには答えない
    if server.sid:
        server.closed.add(server.sid)
        server.last_closed = server.sid
        server.sid = 0
    server.late += stats["late"]
    return {"text": bytes(text), "eot": eot, "exit": server.exit,
            "sent": stats["sent"], "late": stats["late"],
            "after_bye": stats["after_bye"], "bad_frames": dm.bad_frames}
