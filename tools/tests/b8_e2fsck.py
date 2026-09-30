"""票 B8 往復 5: 本物の `e2fsck -fn` の出力を「許容 (漏れ側)」と「不整合」に分類する。

tools/tests/test_b8_open.py から使う。b8_open_host.c が RAM ディスクの ext2 部分を
像として書き出し、その自前の媒体検査 (media_check) の判定と、ここでの分類が
**食い違ったら試験の失敗**にする — 自前の検査の盲点を後追いで埋めるのをやめるため。

分類の考え方 (理由の本文は tools/tests/b8_tdd.md §10-5):

  許容 = 媒体上のどの参照も壊れておらず、e2fsck が数や印を直すか、辿れない物を
         lost+found へ回収するだけのもの (漏れ側)。OS32 の後続操作から到達できない。
  不整合 = 名前・参照が壊れていて、OS32 の後続操作 (rmdir / unlink / 割り当て) が
         生きたデータを壊し得るもの。

**分類できない行は不整合として扱う** (未知の警告を許容に落とさない)。
"""
import re
import struct

# 行末の「直す? no」(-n なので必ず no) を剥がしてから判定する
_TAIL = re.compile(r"\s+(Fix|Clear|Connect to /lost\+found|Salvage|Allocate|Recreate|"
                   r"Create|Relocate|Ignore error|Abort|Clone multiply-claimed blocks|"
                   r"Delete file|Clear inode)\? no\s*$")

_NOISE = [
    re.compile(r"^e2fsck \d"),
    re.compile(r"^Pass \d"),
    re.compile(r"^\s*$"),
    re.compile(r"^(Fix|Clear|Connect to /lost\+found|Salvage|Allocate|Create|"
               r"Recreate|Ignore error)\? no$"),
    re.compile(r"\*+ WARNING: Filesystem still has errors \*+"),
    re.compile(r"^\S+: \d+/\d+ files \("),
    re.compile(r"^Using EXT2FS Library"),
    re.compile(r"^e2fsck: aborted$"),     # 不整合の診断の後で -n が打ち切る (判定は診断で行う)
]


def _inode_is_dir(img_path, ino):
    """像から inode の種別を読む (1KB ブロック、グループ記述子はブロック 2)。"""
    with open(img_path, "rb") as f:
        f.seek(1024)
        sb = f.read(1024)
        ipg = struct.unpack_from("<I", sb, 40)[0]
        isize = struct.unpack_from("<H", sb, 88)[0] or 128
        group = (ino - 1) // ipg
        idx = (ino - 1) % ipg
        f.seek(2048 + group * 32 + 8)
        itable = struct.unpack_from("<I", f.read(4), 0)[0]
        f.seek(itable * 1024 + idx * isize)
        mode = struct.unpack_from("<H", f.read(2), 0)[0]
    return (mode & 0o170000) == 0o040000


def classify(output, img_path):
    """戻り値: (allowed: [(category, line)], bad: [(category, line)])"""
    allowed, bad = [], []
    unconnected = set()
    lines = [_TAIL.sub("", l.rstrip("\n")) for l in output.splitlines()]

    # 先に孤児のディレクトリを集める ("'..' in ..." の判定に使う)
    for l in lines:
        m = re.match(r"^Unconnected directory inode (\d+)", l)
        if m:
            unconnected.add(int(m.group(1)))

    for l in lines:
        if any(p.search(l) for p in _NOISE):
            continue

        if "contains a file system with errors, check forced" in l:
            allowed.append(("s_state のエラー印 (OS32 のエラー状態が書いた)", l)); continue

        m = re.match(r"^Inode (\d+), i_size is (\d+), should be (\d+)\.", l)
        if m:
            ino, cur, want = int(m.group(1)), int(m.group(2)), int(m.group(3))
            if _inode_is_dir(img_path, ino):
                if cur > want:
                    allowed.append(("ディレクトリの i_size が大きい (末尾の穴)", l))
                else:
                    bad.append(("ディレクトリの i_size より先にブロックがある", l))
            else:
                allowed.append(("ファイルの i_size の修正", l))
            continue

        if re.match(r"^Inode \d+, i_blocks is \d+, should be \d+\.", l):
            allowed.append(("i_blocks の修正", l)); continue

        m = re.match(r"^Inode (\d+) ref count is (\d+), should be (\d+)\.", l)
        if m:
            if int(m.group(2)) > int(m.group(3)):
                allowed.append(("ref count が多い (孤児 / links 余り)", l))
            else:
                bad.append(("ref count が少ない (名前 > links)", l))
            continue

        if re.match(r"^Unattached (zero-length )?inode \d+", l):
            allowed.append(("未接続 inode (孤児)", l)); continue

        if re.match(r"^Unconnected directory inode \d+", l):
            allowed.append(("未接続ディレクトリ (孤児)", l)); continue

        m = re.match(r"^'\.\.' in .*\((\d+)\) is .*, should be .*", l)
        if m:
            if int(m.group(1)) in unconnected:
                allowed.append(("孤児ディレクトリの '..'", l))
            else:
                bad.append(("'..' の誤り", l))
            continue

        m = re.match(r"^(Block|Inode) bitmap differences:\s*(.*)$", l)
        if m:
            kind, toks = m.group(1), m.group(2).split()
            if any(t.startswith("+") for t in toks):
                bad.append(("参照されているのに空き (%s)" % kind.lower(), l))
            else:
                allowed.append(("使用中なのに未参照 (%s の漏れ)" % kind.lower(), l))
            continue

        if re.match(r"^(Free blocks count wrong|Free inodes count wrong|"
                    r"Directories count wrong)", l):
            allowed.append(("空き数 / ディレクトリ数の修正", l)); continue

        if re.match(r"^Padding at end of (inode|block) bitmap is not set", l):
            allowed.append(("ビットマップ末尾の詰め物", l)); continue

        if re.match(r"^Deleted inode \d+ has zero dtime", l):
            allowed.append(("解放済み inode の dtime が 0", l)); continue

        if re.match(r"^/lost\+found not found", l):
            allowed.append(("lost+found が無い", l)); continue

        # ---- 不整合 ----
        if "has deleted/unused inode" in l:
            bad.append(("削除済み inode を指すエントリ", l)); continue
        if "is a link to directory" in l:
            bad.append(("ディレクトリへのリンク (2 名)", l)); continue
        if re.search(r"multiply-claimed|claimed by more than one inode|^Pass 1[BCD]|"
                     r"^Running additional passes", l, re.I):
            bad.append(("multiply-claimed blocks", l)); continue
        if re.search(r"illegal block", l, re.I):
            bad.append(("範囲外のブロック番号", l)); continue
        if re.search(r"directory corrupted|incorrect filetype|has rec_len of|"
                     r"has an unallocated block", l):
            bad.append(("ディレクトリブロックの破損", l)); continue
        if re.search(r"loop", l, re.I):
            bad.append(("ディレクトリの輪", l)); continue

        bad.append(("分類できない行 (不整合として扱う)", l))

    return allowed, bad
