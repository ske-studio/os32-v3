"""FD のトラック単位の読み出し (drivers/fdc_track.c + drivers/fdc.c) のホスト試験。

記録: tools/tests/fdc_track_tdd.md
票  : docs/archive/realhw_v21/TASK_FDC_REALHW.md (実機 PC-9821Ra266 の FD 起動が遅い件)

実物の drivers/fdc_track.c / fdc_decide.c / fdc.c を 1 行も写さずに #include
して回す。fdc.c のポートは tools/tests/fdc_hostshim/io.h が試験側の µPD765A
の模型へ回し、tick_count は「読むたびに 1 進む」時計に差し替える。

  python3 -B tools/tests/test_fdc_track.py            # ホストで全ケース
  python3 -B tools/tests/test_fdc_track.py --target   # + i386-elf で通す
  python3 -B tools/tests/test_fdc_track.py --mutate   # 否定側 (変異が RED になるか)
  --require-image  images/os32_boot.d88 が無ければ SKIP でなく FAIL (make check)

変異の判定: RED = どれかのケースが落ちた / ERROR = コンパイルできなかった
(**RED に数えない**) / SURVIVED = 全ケースが通った (見逃し)。対照 (何も変え
ない変異) は SURVIVED でなければならない。
"""
import os
import pathlib
import re
import struct
import subprocess
import sys
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[2]
HARNESS = ROOT / "tools/tests/fdc_track_host.c"
SHIM = ROOT / "tools/tests/fdc_hostshim"
SOURCES = ["drivers/fdc_track.c", "drivers/fdc.c", "drivers/fdc_decide.c",
           "fs/fatfs/diskio.c"]

CASES = ["split_2hd", "split_144", "readahead_count1", "cross_boundary",
         "fallback_single", "single_fail", "cache_rules", "oversize_geom",
         "timeout_math", "fdc_multi_cmd", "fdc_seek_skip", "fdc_forget_rules",
         "fdc_end_to_end", "fat_data_interleave", "gen_and_lru",
         "seek_edge_foreign", "drain_before_skip", "readychange_invalidates",
         "multi_nr_quiet", "diskio_rw", "buf_layout", "font_replay",
         "recal_settle", "idle_rule", "track_nr_stop", "single_nr_no_recover",
         "sis_edge_limit", "write_nr_no_recover"]

# font_replay の材料 (実物の FD イメージ)。make all が作る。
# 読むファイル: もとは既定フォント /sys/font/default.kcg (188KB) だったが、
# 2026-09-25 にフォントを MINIMAL から NORMAL へ移して FD に載らなくなった
# (build/packages.yaml)。読み方 (kcg_load_font と同じ 16B → 1KB ずつ) はそのまま、
# FD に残る大きいファイルで起動時にカーネルが FatFs で読む /sys/unicode.bin
# (128KB、トラック 16 本分) に替えた。
D88 = ROOT / "images/os32_boot.d88"
FONT_GUEST = ("SYS", "UNICODE.BIN")
_ENV = {}


def d88_to_raw(data, spt=8, heads=2, bps=1024, cyls=77):
    """D88 を LBA 順の生イメージに直す (セクタの見出しの C/H/R/N で置く)。"""
    raw = bytearray(cyls * heads * spt * bps)
    offs = struct.unpack_from("<164I", data, 0x20)
    seen = 0
    for toff in offs:
        if toff == 0:
            continue
        pos = toff
        nsec = struct.unpack_from("<H", data, pos + 4)[0]
        for _ in range(nsec):
            c, h, r, n = data[pos:pos + 4]
            size = struct.unpack_from("<H", data, pos + 14)[0]
            body = data[pos + 16:pos + 16 + size]
            if (128 << n) == bps and c < cyls and h < heads and 1 <= r <= spt:
                lba = (c * heads + h) * spt + (r - 1)
                raw[lba * bps:(lba + 1) * bps] = body[:bps]
                seen += 1
            pos += 16 + size
    if seen != cyls * heads * spt:
        raise ValueError(f"D88 のセクタ数が合わない: {seen}")
    return bytes(raw)


def fat12_extract(img, names):
    """FAT12 をたどって names (ディレクトリ..., ファイル) の中身を返す。"""
    bps, spc, rsv, nfat, nroot, _tot, _m, spf = struct.unpack_from("<HBHBHHBH", img, 11)
    fat = img[rsv * bps:(rsv + spf) * bps]
    root = rsv + nfat * spf
    data0 = root + (nroot * 32 + bps - 1) // bps

    def nxt(c):
        o = c * 3 // 2
        v = fat[o] | fat[o + 1] << 8
        return v >> 4 if c & 1 else v & 0xFFF

    def chain_bytes(c):
        out = b""
        while 2 <= c < 0xFF8:
            lba = data0 + (c - 2) * spc
            out += img[lba * bps:(lba + spc) * bps]
            c = nxt(c)
        return out

    def find(dirbytes, name):
        for i in range(0, len(dirbytes), 32):
            e = dirbytes[i:i + 32]
            if e[0] == 0:
                break
            if e[0] == 0xE5 or e[11] == 0x0F:
                continue
            base = e[0:8].decode("latin1").strip()
            ext = e[8:11].decode("latin1").strip()
            if (base + ("." + ext if ext else "")) == name:
                return struct.unpack_from("<H", e, 26)[0], struct.unpack_from("<I", e, 28)[0]
        raise KeyError(name)

    cur = img[root * bps:data0 * bps]
    for d in names[:-1]:
        clst, _ = find(cur, d)
        cur = chain_bytes(clst)
    clst, size = find(cur, names[-1])
    return chain_bytes(clst)[:size]


def fat12_layout(img, names, spt=8, heads=2):
    """names (ディレクトリ..., ファイル) の置き場所を LBA で返す。

    戻り値: {"meta": [FAT・ルート・途中のディレクトリの LBA...],
             "data": [ファイル本体の LBA...]}。
    font_replay の上限 (シーク回数・まとめ読みの回数) をこの配置から導く —
    ローダの大きさが変わって配置がずれても意味が保たれるように (PM 判断
    2026-09-24。以前は 14 / 27 の固定値で、ローダが 1 クラスタ増えただけで落ちた)。"""
    bps, spc, rsv, nfat, nroot, _tot, _m, spf = struct.unpack_from("<HBHBHHBH", img, 11)
    fat = img[rsv * bps:(rsv + spf) * bps]
    root = rsv + nfat * spf
    data0 = root + (nroot * 32 + bps - 1) // bps

    def nxt(c):
        o = c * 3 // 2
        v = fat[o] | fat[o + 1] << 8
        return v >> 4 if c & 1 else v & 0xFFF

    def chain_lbas(c):
        out = []
        while 2 <= c < 0xFF8:
            out += list(range(data0 + (c - 2) * spc, data0 + (c - 1) * spc))
            c = nxt(c)
        return out

    def find(lbas, name):
        raw = b"".join(img[l * bps:(l + 1) * bps] for l in lbas)
        for i in range(0, len(raw), 32):
            e = raw[i:i + 32]
            if e[0] == 0:
                break
            if e[0] == 0xE5 or e[11] == 0x0F:
                continue
            base = e[0:8].decode("latin1").strip()
            ext = e[8:11].decode("latin1").strip()
            if (base + ("." + ext if ext else "")) == name:
                return struct.unpack_from("<H", e, 26)[0]
        raise KeyError(name)

    meta = list(range(rsv, rsv + spf)) + list(range(root, data0))
    cur = list(range(root, data0))
    for d in names[:-1]:
        cur = chain_lbas(find(cur, d))
        meta += cur
    return {"meta": meta, "data": chain_lbas(find(cur, names[-1]))}


# FAT (シリンダ 0) への往復。FatFs はクラスタ列を辿るとき FAT の区画を読み直し、
# 先読みの表から追い出されていれば 1 回だけシリンダ 0 へ戻って帰ってくる
# (実測: 基点の配置でも今の配置でも SEEK 64 -> 0 -> 64 が 1 回)。
# まとめ読みはその往復で FAT / ルートのトラックを 1 本読み直す。
FONT_FAT_ROUNDTRIP_SEEKS = 2
FONT_FAT_ROUNDTRIP_READS = 1


def font_bounds(img, spt=8, heads=2):
    """font_replay の上限を配置から出す。

    シーク ≦ データが跨ぐシリンダの数
            + データの外にあるメタデータのシリンダの数 (シリンダ 0 を除く —
              マウントの後でヘッドはシリンダ 0 に居る)
            + FAT への往復 (FONT_FAT_ROUNDTRIP_SEEKS)
    まとめ読み ≦ データが跨ぐトラックの数
                + データの外にあるメタデータのトラックの数
                + FAT への往復で読み直す分 (FONT_FAT_ROUNDTRIP_READS)"""
    lay = fat12_layout(img, FONT_GUEST, spt, heads)
    cyl = lambda l: l // (spt * heads)
    trk = lambda l: l // spt
    dcyl = {cyl(l) for l in lay["data"]}
    dtrk = {trk(l) for l in lay["data"]}
    mcyl = {cyl(l) for l in lay["meta"]} - dcyl - {0}
    mtrk = {trk(l) for l in lay["meta"]} - dtrk
    seek_max = len(dcyl) + len(mcyl) + FONT_FAT_ROUNDTRIP_SEEKS
    multi_max = len(dtrk) + len(mtrk) + FONT_FAT_ROUNDTRIP_READS
    desc = (f"data {len(dtrk)} trk / {len(dcyl)} cyl (C{min(dcyl)}-C{max(dcyl)}), "
            f"meta {len(mtrk)} trk / {len(mcyl)} cyl 外 {sorted(mcyl)}")
    return seek_max, multi_max, desc


def check_read_stream_shape():
    """fatfs_vfs_read_stream が今も「開いて、動いて、読んで、閉じる」か。
    font_replay はこの形を写して回すので、形が変われば試験を直す合図。"""
    src = (ROOT / "fs/fatfs_vfs.c").read_text(encoding="utf-8")
    m = re.search(r"static int fatfs_vfs_read_stream\(.*?\n\}", src, re.S)
    if not m:
        return False
    body = m.group(0)
    # f_close は f_lseek の失敗の枝にも出るので、最後のものを見る。
    order = [body.find("f_open("), body.find("f_lseek("), body.find("f_read("),
             body.rfind("f_close(")]
    return all(x >= 0 for x in order) and order == sorted(order)


def prepare_font(tmp):
    """font_replay の材料を tmp に置いて env を返す。無ければ None (SKIP)。"""
    if not D88.exists():
        return None
    raw = d88_to_raw(D88.read_bytes())
    font = fat12_extract(raw, FONT_GUEST)
    img = pathlib.Path(tmp) / "fd.raw"
    fnt = pathlib.Path(tmp) / "font.kcg"
    img.write_bytes(raw)
    fnt.write_bytes(font)
    seek_max, multi_max, desc = font_bounds(raw)
    print(f"font_replay の上限: seek <= {seek_max}, multi <= {multi_max} ({desc})",
          flush=True)
    return {"FDC_TRACK_IMAGE": str(img), "FDC_TRACK_FONT": str(fnt),
            "FDC_TRACK_PATH": "0:/" + "/".join(FONT_GUEST).lower(),
            "FDC_TRACK_SEEK_MAX": str(seek_max), "FDC_TRACK_MULTI_MAX": str(multi_max)}

# fdc.c の fdc_motor_off() は元から未使用の static (test_fdc_seek.py と同じ)。
# tick_count の差し替えは「volatile u32 * を返す関数」の宣言になるので
# -Wignored-qualifiers は出ないが、念のため戻り値の修飾は許す。
FLAGS = ["-std=gnu11", "-Wall", "-Wextra", "-Werror",
         "-D__cdecl=",
         "-Wno-unused-function", "-Wno-unused-parameter",
         "-Dtick_count=(*fdc_fake_tick_ptr())"]


def includes(first=None):
    dirs = ([first] if first else []) + [SHIM] + [
        ROOT / p for p in ("include", "drivers", "lib", "sdk/include/os32",
                           "fs/fatfs")]
    return ["-I" + str(d) for d in dirs]


# 否定側。実装を 1 か所だけ壊して RED になることを見る。
# (ファイル, パターン, 置換, 説明)。最後の 1 本は対照 (何も変えない)。
MUTATIONS = [
    # --- 区切り・先読み・読み直し (drivers/fdc_track.c)
    ("drivers/fdc_track.c",
     r"in_track = \(count < left\) \? count : left;",
     "in_track = (count < left + 1) ? count : left + 1;",
     "トラックの境目を 1 つ越えて切る (EOT を越えたコマンドを出す)"),
    ("drivers/fdc_track.c",
     r"left = spt - \(lba % spt\);",
     "left = spt * heads - (lba % (spt * heads));",
     "ヘッドの境目で切らない (MT 無しでヘッド 1 まで読もうとする)"),
    ("drivers/fdc_track.c",
     r"if \(run->sect < t->first\) continue;",
     "",
     "持っていない手前のセクタを当てる"),
    ("drivers/fdc_track.c",
     r"if \(t->geom != g\) continue;",
     "",
     "ジオメトリが変わっても中身を当てる"),
    ("drivers/fdc_track.c",
     r"if \(t->drv != drv \|\| ",
     "if (",
     "別ドライブに中身を当てる"),
    ("drivers/fdc_track.c",
     r"if \(t->gen != gen\) continue;",
     "",
     "世代 (書き込み / Ready 変化) が進んでも中身を当てる"),
    ("drivers/fdc_track.c",
     r"            t->valid = 0;\n            mrc = \(eot > 0\)",
     "            mrc = (eot > 0)",
     "まとめ読みの前に捨てない (失敗しても古い中身が残る)"),
    ("drivers/fdc_track.c",
     r"if \(fdc_track_read_singly\(ops, drv, g, &run, dst\) != 0\) \{\n                    return -1;",
     "if (1) {\n                    return -1;",
     "まとめ読みが失敗したら読み直さずに失敗にする"),
    ("drivers/fdc_track.c",
     r"    return \(int\)g->spt;\n#else",
     "    return run->sect + run->count - 1;\n#else",
     "先読みしない (count=1 の連続が 1 セクタ 1 コマンドのまま)"),
    ("drivers/fdc_track.c",
     r"if \(eot > want_last && fdc_track_is_bad\(c, drv, g, &run\)\) \{",
     "if (0) {",
     "先読みが落ちたトラックでも先読みを続ける (毎回失敗 + 1 セクタずつ)"),
    # --- セクタキャッシュ (2026-09-24 夕: VFS の開き直しで 4〜5 本を巡回)
    ("drivers/fdc_track.c",
     r"int single = \(count == 1\);",
     "int single = 0;",
     "セクタキャッシュを使わない (6541ef1 の姿: 実物のフォントで multi 769)"),
    ("drivers/fdc_track.c",
     r"if \(v < 0 \|\| c->sec\[j\]\.used < c->sec\[v\]\.used\) v = j;",
     "if (v < 0) v = j;",
     "セクタキャッシュが LRU でない (毎回使うディレクトリを追い出す)"),
    ("drivers/fdc_track.c",
     r"if \(e->gen != gen \|\| e->geom != g\) continue;",
     "if (e->geom != g) continue;",
     "セクタキャッシュが世代を見ない (書いた後に古い中身)"),
    ("drivers/fdc_track.c",
     r"if \(e->drv != drv \|\| e->lba != lba\) continue;",
     "if (e->drv != drv) continue;",
     "セクタキャッシュが LBA を見ない (別のセクタを返す)"),
    # --- シークの省略・完了待ち (drivers/fdc.c)
    ("drivers/fdc.c",
     r"if \(drv >= 0 && drv < FDC_MAX_DRIVES && s_known_cyl\[drv\] == cyl\) \{\n        s_stats\.seek_skipped\+\+;\n        return 0;\n    \}",
     "",
     "同じシリンダでもシークする (直す前の姿)"),
    ("drivers/fdc.c",
     [(r"     \* 排水は pending 無しなら SIS 1 回 \(1 バイト応答\) で終わる。 \*/\n    \(void\)fdc_drain_interrupts\(\);",
       "     */"),
      (r"        s_stats\.seek_skipped\+\+;\n        return 0;\n    \}\n    fdc_forget_cyl\(drv\);",
       "        s_stats.seek_skipped++;\n        return 0;\n    }\n    (void)fdc_drain_interrupts();\n    fdc_forget_cyl(drv);")],
     None,
     "シークの省略を排水より先に置く (取り残しで INT 線が上がったまま)"),
    ("drivers/fdc.c",
     r"                s_stats\.sis_foreign\+\+;\n                continue;",
     "                s_stats.sis_foreign++;\n                break;",
     "1 本のエッジで 1 件しか読まない (別の通知の後ろの完了を期限まで待つ)"),
    ("drivers/fdc.c",
     r"    if \(\(u8\)\(st0 & FDC_ST0_IC_MASK\) == FDC_ST0_IC_RDYCHG\n        && \(st0 & FDC_ST0_SE\) == 0\) return 1;",
     "",
     "自ドライブの Ready 変化をシークの失敗として扱う"),
    ("drivers/fdc.c",
     r"        s_stats\.ready_change\+\+;\n        fdc_bump_gen\(d\);",
     "        s_stats.ready_change++;",
     "Ready 変化で世代を進めない (入れ替えた媒体に古い先読みを当てる)"),
    ("drivers/fdc.c",
     r"    fdc_bump_gen\(drv\);\n    s_stats\.writes\+\+;",
     "    s_stats.writes++;",
     "書き込みで世代を進めない (dev.c / KAPI の書き込みの後に古い先読み)"),
    # リセットの後は Ready 変化の通知も来て、SIS の側でも捨てる (三重)。
    ("drivers/fdc.c",
     [(r"    /\* ヘッドが動く。通るまでは知らないことにする。 \*/\n    fdc_note_drive\(drv\);\n    fdc_forget_cyl\(drv\);",
       "    fdc_note_drive(drv);"),
      (r"    /\* 2\. FDC をリセットして実行フェーズを畳む。覚えているシリンダも捨てる。 \*/\n    fdc_forget_all\(\);",
       "    /* 2. */"),
      (r"        if \(d < FDC_MAX_DRIVES\) s_known_cyl\[d\] = FDC_CYL_UNKNOWN;",
       "")],
     None,
     "リセットと RECALIBRATE で覚えた値を捨てない (RECALIBRATE が落ちても古い値を信じる)"),
    ("drivers/fdc.c",
     r"        fdc_abort_transfer\(\);\n        \(void\)fdc_recalibrate\(drv\);",
     "        fdc_abort_transfer();",
     "まとめ読みの失敗の後に RECALIBRATE しない"),
    ("drivers/fdc.c",
     r"    if \(drv != s_last_drv\) fdc_forget_all\(\);",
     "",
     "ドライブの切り替えで捨てない"),
    ("drivers/fdc.c",
     r"    s_geom\[drv\] = g;\n    /\* メディアが変わった。覚えているシリンダは信じず、先読みも捨てさせる。 \*/\n    fdc_forget_cyl\(drv\);",
     "    s_geom[drv] = g;",
     "メディアの変更で覚えたシリンダを捨てない"),
    ("drivers/fdc.c",
     r"if \(fdc_send_byte\(\(u8\)eot\) != 0\) goto fail;",
     "if (fdc_send_byte((u8)sect) != 0) goto fail;",
     "EOT を sect のまま出す (1 セクタしか読まない)"),
    ("drivers/fdc.c",
     r"if \(fdc_send_byte\(FDC_OPT_MF \| FDC_CMD_READ_DATA\) != 0\) goto fail;",
     "if (fdc_send_byte(FDC_OPT_MT | FDC_OPT_MF | FDC_CMD_READ_DATA) != 0) goto fail;",
     "MT を立てる"),
    ("drivers/fdc.c",
     r"if \(eot > \(int\)g->spt\) return -2;",
     "",
     "トラックをまたぐ引数を断らない"),
    ("drivers/fdc.c",
     r"        if \(seek_rc == FDC_RC_NOT_READY\) \{\n            fdc_forget_cyl\(drv\);\n            s_stats\.multi_nr\+\+;\n            return -3;\n        \}",
     "",
     "SEEK の NR をまとめ読みの失敗に数える (行も出す)"),
    ("drivers/fdc.c",
     r"        if \(\(results\[0\] & FDC_ST0_NR\) != 0\) \{\n            fdc_forget_cyl\(drv\);",
     "        if (0) {\n            fdc_forget_cyl(drv);",
     "READ の NR をまとめ読みの失敗に数える (行も出す)"),
    # --- ラリー 2 (Codex / Fable)
    ("drivers/fdc.c",
     r"            fdc_head_settle\(\);\n            fdc_note_cyl\(drv, 0\);",
     "            fdc_note_cyl(drv, 0);",
     "RECALIBRATE の後に整定しない (回復直後の C=0 の READ が整定前に出る)"),
    # 読み取り側 (READ) だけを消す。WRITE 側 (次の変異) と同じ形なので、count=1 と
    # ファイルの中の順番に頼らず、read 側にしか無い前置きの注釈で場所を決める。
    ("drivers/fdc.c",
     r"(        /\* NR \(媒体無し\) は\*\*回復 \(リセット・RECALIBRATE\) より前に\*\*打ち切る\n[^\n]*\n[^\n]*\n)        if \(\(results\[0\] & FDC_ST0_NR\) != 0\) \{\n            dma_chan_mask\(FDC_DMA_CHANNEL\);\n            dma_armed = 0;\n            phase = \"nr\";\n            break;\n        \}",
     r"\1",
     "単発の READ の NR で回復とリトライを踏む"),
    # 書き込みの NR は read 側と同じ形。2 つ目の一致 (WRITE 側) だけを消す。
    ("drivers/fdc.c",
     r"(            return 0;\n        \}\n\n        /\* NR \(媒体無し\) は回復より前に打ち切る \(read 側と同じ\)。[^\n]*\n[^\n]*\n)        if \(\(results\[0\] & FDC_ST0_NR\) != 0\) \{\n            dma_chan_mask\(FDC_DMA_CHANNEL\);\n            dma_armed = 0;\n            phase = \"nr\";\n            break;\n        \}",
     r"\1",
     "単発の WRITE の NR で回復とリトライを踏む"),
    ("drivers/fdc.c",
     r"        \(void\)fdc_recover\(drv\);\n    \} else \{\n        fdc_forget_cyl\(drv\);",
     "        fdc_forget_cyl(drv);\n    } else {\n        fdc_forget_cyl(drv);",
     "まとめ読みのシークの失敗で回復を通さない"),
    ("drivers/fdc.c",
     r"        if \(i == FDC_SIS_DRAIN_MAX\) more = 1;",
     "",
     "SIS を件数の上限で止めたら次のエッジを待つ (上限ちょうどの通知で期限切れ)"),
    ("drivers/fdc_track.c",
     r"            \} else if \(mrc == -3\) \{",
     "            } else if (0) {",
     "まとめ読みの NR で 1 セクタずつへ落ちる"),
    ("drivers/fdc_track.c",
     r"    if \(c->touched && \(u32\)\(now - c->last_tick\) > \(u32\)FDC_TRACK_IDLE_TICKS\) \{",
     "    if (c->touched && (u32)(now - c->last_tick) > (u32)FDC_TRACK_IDLE_TICKS * 1000000UL) {",
     "2 秒規則を当てない (差し替えた媒体に古い中身を返し続ける)"),
    ("drivers/fdc_track.c",
     r"\(u32\)\(now - c->last_tick\) > \(u32\)FDC_TRACK_IDLE_TICKS",
     "(u32)(now - c->last_tick) >= (u32)FDC_TRACK_IDLE_TICKS",
     "2 秒規則の境目を 1 tick 早める"),
    # --- diskio.c の結線
    ("fs/fatfs/diskio.c",
     r"        fdc_track_invalidate\(&fdd_track\);\n        fdd_status = 0;",
     "        fdd_status = 0;",
     "disk_initialize で先読みを捨てない (Ready 変化の無い入れ替え)"),
    # --- 純粋な判定 (drivers/fdc_decide.c)
    ("drivers/fdc_decide.c",
     r"xfer = \(\(u32\)count \* rot_ticks \+ \(u32\)spt - 1\) / \(u32\)spt;",
     "xfer = ((u32)count * rot_ticks) / (u32)spt;",
     "時間上限の回転数を切り捨てる"),
    ("drivers/fdc_decide.c",
     r"return \(t < floor_ticks\) \? floor_ticks : t;",
     "return t;",
     "時間上限が単発の値を下回る"),
    ("drivers/fdc_decide.c",
     r"    if \(!fdc_crosses_bank\(start, slot\)\) \{",
     "    if (1) {",
     "DMA の窓を常に先頭に置く (境界をまたぐ)"),
    # 対照: 何も変えない。SURVIVED でなければ試験が不安定 (偽の RED)。
    ("drivers/fdc_track.c", r"(#include \"fdc_track.h\")", r"\1",
     "対照 (何も変えない)"),
]
CONTROL = len(MUTATIONS)


def host_build(tmp, mutated=None):
    """ハーネスをコンパイルして実行ファイルのパスを返す。
    mutated = (相対パス, 本文) なら、変異させた写しの木から組む。"""
    tmpd = pathlib.Path(tmp)
    exe = tmpd / "fdc-track-host"
    if mutated is None:
        cmd = ["gcc", *FLAGS, *includes(), str(HARNESS), "-o", str(exe)]
    else:
        rel, text = mutated
        tree = tmpd / "tree"
        for sub in ("drivers", "tools/tests", "fs/fatfs"):
            (tree / sub).mkdir(parents=True, exist_ok=True)
        for rel_ in ("drivers/fdc_track.c", "drivers/fdc_track.h",
                     "drivers/fdc.c", "drivers/fdc.h",
                     "drivers/fdc_decide.c", "drivers/fdc_decide.h",
                     "drivers/dma8237_math.c",
                     "fs/fatfs/diskio.c"):
            (tree / rel_).write_text(
                (ROOT / rel_).read_text(encoding="utf-8"), encoding="utf-8")
        (tree / rel).write_text(text, encoding="utf-8")
        harness = tree / "tools/tests/fdc_track_host.c"
        harness.write_text(HARNESS.read_text(encoding="utf-8"), encoding="utf-8")
        cmd = ["gcc", *FLAGS, *includes(tree / "drivers"), str(harness),
               "-o", str(exe)]
    subprocess.run(cmd, cwd=ROOT, check=True)
    return exe


def run_cases(exe, cases, quiet=False):
    failed = 0
    for case in cases:
        env = dict(os.environ)
        env.update(_ENV)
        rc = subprocess.run([str(exe), case], cwd=ROOT, timeout=120, env=env,
                            stdout=subprocess.DEVNULL if quiet else None,
                            stderr=subprocess.DEVNULL if quiet else None).returncode
        if not quiet:
            print(f"EXIT {case}={rc}", flush=True)
        failed += rc != 0
    if not quiet:
        print(f"SUMMARY {len(cases) - failed}/{len(cases)} PASS", flush=True)
    return failed


def build_target(tmp):
    """カーネルと同じ i386-elf で通す。"""
    for rel in SOURCES:
        extra = ["-Wno-unused-function"] if rel == "drivers/fdc.c" else []
        cmd = ["i386-elf-gcc", "-std=gnu11", "-m32", "-march=i386",
               "-ffreestanding", "-fno-pie", "-fno-stack-protector", "-nostdlib",
               "-mno-red-zone", "-fcommon", "-fsigned-char", "-fno-short-enums",
               "-O2", "-Wall", "-Werror", 
               "-D__KERNEL_BUILD__",
               "-I" + str(ROOT), "-I" + str(ROOT / "include"),
               "-I" + str(ROOT / "arch/x86"), "-I" + str(ROOT / "platform/pc98"),
               "-I" + str(ROOT / "sdk/include/os32"), "-I" + str(ROOT / "drivers"),
               "-I" + str(ROOT / "lib"), "-I" + str(ROOT / "fs/fatfs"),
               "-I" + str(ROOT / "kernel"),
               *extra, "-c", str(ROOT / rel),
               "-o", str(pathlib.Path(tmp) / (rel.replace("/", "_") + ".o"))]
        subprocess.run(cmd, cwd=ROOT, check=True)
    print("TARGET i386-elf GNU11 -Werror PASS", flush=True)


def mutate(tmp):
    """実装を 1 か所ずつ壊して RED になることを見る。対照は SURVIVED。"""
    bad = 0
    tally = {"RED": 0, "ERROR": 0, "SURVIVED": 0}
    for i, (rel, pattern, repl, why) in enumerate(MUTATIONS, 1):
        original = (ROOT / rel).read_text(encoding="utf-8")
        pairs = pattern if isinstance(pattern, list) else [(pattern, repl)]
        mutated, n = original, 1
        for pat, rp in pairs:
            mutated, k = re.subn(pat, rp, mutated, count=1)
            n = n and k == 1
        if n != 1:
            print(f"MUTATION {i} NOT APPLICABLE: {why}", flush=True)
            bad += 1
            continue
        sub = pathlib.Path(tmp) / f"m{i}"
        sub.mkdir()
        try:
            exe = host_build(sub, (rel, mutated))
        except subprocess.CalledProcessError:
            status = "ERROR"
            hits = 0
        else:
            hits = run_cases(exe, CASES, quiet=True)
            status = "RED" if hits else "SURVIVED"
        tally[status] += 1
        if i == CONTROL:
            ok = status == "SURVIVED" and mutated == original
        else:
            ok = status == "RED"
        mark = "" if ok else "  **想定外**"
        print(f"MUTATION {i} {status} ({hits} 件): {why}{mark}", flush=True)
        bad += not ok
    print("MUTATION TALLY RED={RED} ERROR={ERROR} SURVIVED={SURVIVED}"
          .format(**tally), flush=True)
    return bad


if __name__ == "__main__":
    args = sys.argv[1:]
    tmp_root = os.environ.get("TMPDIR") or None
    with tempfile.TemporaryDirectory(prefix="os32-fdc-track-", dir=tmp_root) as tmp:
        exe = host_build(tmp)
        print("HOST GNU11 -Werror compile PASS (real fdc_track.c / fdc.c)",
              flush=True)
        if "--target" in args:
            build_target(tmp)
        rc = 0
        if not check_read_stream_shape():
            print("FAIL fs/fatfs_vfs.c の read_stream の形が変わった "
                  "(font_replay の写しを直す)", flush=True)
            rc += 1
        env = prepare_font(tmp)
        if env is None and "--require-image" in args:
            # make check-fdc-track-host はイメージに依存させてある。
            print(f"FAIL font_replay ({D88.relative_to(ROOT)} が無い)", flush=True)
            rc += 1
            CASES.remove("font_replay")
        elif env is None:
            # [V4] 飛ばしたことをそのまま言う。
            print(f"SKIP font_replay ({D88.relative_to(ROOT)} が無い — "
                  "make all の後で回す)", flush=True)
            CASES.remove("font_replay")
        else:
            _ENV.update(env)
        rc += run_cases(exe, [a for a in args if not a.startswith("--")] or CASES)
        if "--mutate" in args:
            rc += mutate(tmp)
        sys.exit(bool(rc))
