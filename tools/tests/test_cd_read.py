"""CD の読み (drivers/atapi.c + fs/iso9660.c + userland/lib/rt/pkg.c) のホスト試験。

記録: tools/tests/cd_read_tdd.md
症状: 実機 PC-9821Ra266 の cdinst (Full) が 10 分を超えても NORMAL.PKG の途中
      (実効 20KB/s 未満)。READ(10) を 1 セクタずつ出し、VFS の区切り (4KB) ごとに
      根のディレクトリを読み直していた。

実物を 1 行も写さずに #include して回す。
  cd_read_host.c  atapi.c + iso9660.c。ポートは tools/tests/atapi_hostshim/io.h が
                  ATAPI デバイスの模型へ回す (READ(10) の数・セクタ数を数える)
  cd_pkg_host.c   pkg.c の pkg_extract (cdinst の無圧縮の展開) の区切り方。
                  記録した読みの列を cd_read_host の replay で再生する

  python3 -B tools/tests/test_cd_read.py            # ホストで全ケース
  python3 -B tools/tests/test_cd_read.py --target   # + i386-elf で通す
  python3 -B tools/tests/test_cd_read.py --mutate   # 否定側 (変異が RED になるか)

ホストは -fsanitize=address,undefined で組む (窓からはみ出す変異を確実に落とす)。
ATAPI_READ_MAX_SECTORS を 32 にした版 (1 回の転送が 64KB = byte count limit の
16 ビットを超える形) も同じケースで回す。

変異の判定: RED = どれかのケースが落ちた / ERROR = コンパイルできなかった
(**RED に数えない**) / SURVIVED = 全ケースが通った (見逃し)。対照 (何も変え
ない変異) は SURVIVED でなければならない。
"""
import os
import pathlib
import re
import subprocess
import sys
import tempfile

import mutpar  # noqa: E402  (tools/tests/mutpar.py、同じディレクトリ)

ROOT = pathlib.Path(__file__).resolve().parents[2]
READ_HARNESS = "tools/tests/cd_read_host.c"
PKG_HARNESS = "tools/tests/cd_pkg_host.c"
SHIM = "tools/tests/atapi_hostshim"
TREE_FILES = ["drivers/atapi.c", "drivers/atapi.h", "fs/iso9660.c", "fs/iso9660.h",
              "userland/lib/rt/pkg.c", "userland/lib/rt/pkg.h",
              READ_HARNESS, PKG_HARNESS, SHIM + "/io.h"]

READ_CASES = ["stream_4k", "stream_odd", "stream_32k", "read_file", "multi_fallback",
              "short_transfer", "bad_outside", "bad_inside", "lru_order", "ua_mount", "multi_drq",
              "path_cache", "dir_lru", "list_reentrant", "unit_attention",
              "idle_rule", "no_window", "stat_swap", "list_swap",
              "np2_read", "np2_async", "np2_empty", "cap_nodata", "np2_slave",
              "np2_slave_strict", "cap_len", "np2_sel_lag", "np2_absent", "np2_sel_busy",
              "np2_stuck", "np2_ua_init", "np2_becoming_ready", "np2_no_medium",
              "np2_ready_total", "np2_srst", "ua_multi",
              "np2_spinup", "np2_srst_long", "np2_boot_worst", "np2_start_unit",
              "np2_bus_dead"]
# N = 32 の版で回すもの (1 回 64KB)
WIDE_CASES = ["stream_4k", "stream_32k", "read_file", "multi_drq", "multi_fallback",
              "np2_read", "np2_async", "np2_sel_lag", "np2_stuck"]
PKG_CASES = ["aligned_chunks", "nomem", "open_fail_frees"]

COMMON = ["-std=gnu89", "-Wall", "-Wextra", "-Werror",
          "-Wdeclaration-after-statement", "-D__cdecl=",
          "-Wno-unused-function", "-Wno-unused-parameter"]
SAN = ["-fsanitize=address,undefined", "-fno-sanitize-recover=all", "-g"]


def read_includes(tree):
    return ["-I" + str(tree / SHIM), "-I" + str(tree / "drivers"), "-I" + str(tree / "fs"),
            "-I" + str(tree)] + ["-I" + str(ROOT / p) for p in
                                 ("include", "drivers", "fs", "lib", "kernel",
                                  "sdk/include/os32")] + ["-I" + str(ROOT)]


def pkg_includes(tree):
    return ["-I" + str(tree), "-I" + str(tree / "userland/lib/rt")] + [
        "-I" + str(ROOT / p) for p in ("sdk/include", "sdk/include/os32", "userland/lib")]


EXE_KEYS = ("read", "wide", "pkg")

# 変異を当てたファイル → 組み直す実行ファイル (票 TASK_CHECK_MUT_PARALLEL)。
# ほかは実物で組んだもの (main の 1 回目、全部通ったもの) を使う。表に無いファイル
# (ヘッダなど) は 3 本とも組む。表が gcc -MM の依存より狭ければ mutate() が落ちる。
REBUILD = {
    "drivers/atapi.c": ("read", "wide"),
    "fs/iso9660.c": ("read", "wide"),
    "userland/lib/rt/pkg.c": ("pkg",),
}


def _commands(tmp, tree):
    tmp = pathlib.Path(tmp)
    return {
        "read": ["gcc", *COMMON, *SAN, *read_includes(tree),
                 str(tree / READ_HARNESS), "-o", str(tmp / "cd-read")],
        "wide": ["gcc", *COMMON, *SAN, "-DATAPI_READ_MAX_SECTORS=32",
                 *read_includes(tree), str(tree / READ_HARNESS),
                 "-o", str(tmp / "cd-read-wide")],
        "pkg": ["gcc", *COMMON, *SAN, *pkg_includes(tree),
                str(tree / PKG_HARNESS), "-o", str(tmp / "cd-pkg")],
    }


def build(tmp, tree, keys=EXE_KEYS):
    """keys の実行ファイル (既定は 3 本) を組む。コンパイルできなければ CalledProcessError。"""
    common = dict(cwd=ROOT, check=True, stdout=subprocess.DEVNULL, stderr=subprocess.PIPE)
    cmds = _commands(tmp, tree)
    exes = {}
    for k in keys:
        subprocess.run(cmds[k], **common)
        exes[k] = pathlib.Path(cmds[k][-1])
    return exes


def run1(exe, case, env=None, quiet=False):
    e = dict(os.environ)
    e["ASAN_OPTIONS"] = "detect_leaks=0"
    if env:
        e.update(env)
    try:
        r = subprocess.run([str(exe), case], cwd=ROOT, timeout=60, env=e,
                           capture_output=True, text=True)
    except subprocess.TimeoutExpired:
        return 124, ""
    if not quiet:
        sys.stdout.write(r.stdout)
        sys.stderr.write(r.stderr)
    return r.returncode, r.stdout


class _FirstFail(Exception):
    pass


def run_all(exes, tmp, quiet=False, keys=EXE_KEYS, first_fail=False, replay=None):
    """keys の実行ファイルに関わるケース (既定は全部)。落ちた数を返す。
    first_fail なら最初に落ちたところで打ち切る (変異用、RED かどうかだけ分かればよい)。
    replay (pkg のケースで記録を取り、read で再生する) は既定で read か pkg を
    含むとき回す。"""
    failed = 0
    total = 0

    def one(label, exe, case, env=None):
        nonlocal failed, total
        rc, out = run1(exe, case, env, quiet)
        total += 1
        if not quiet:
            print(f"EXIT {label}={rc}", flush=True)
        failed += rc != 0
        if rc != 0 and first_fail:
            raise _FirstFail()
        return rc, out

    # replay は pkg の記録 (pkg のケース) を read で再生する — どちらかを組み直したら回す
    if replay is None:
        replay = "read" in keys or "pkg" in keys
    try:
        if "read" in keys:
            for c in READ_CASES:
                one(c, exes["read"], c)
        if "wide" in keys:
            for c in WIDE_CASES:
                one("wide:" + c, exes["wide"], c)
        if replay:
            trace = pathlib.Path(tmp) / "pkg.trace"
            pkg_len = None
            for c in PKG_CASES:
                rc, out = one("pkg:" + c, exes["pkg"], c, {"CD_PKG_TRACE": str(trace)})
                m = re.search(r"pkg_len=(\d+)", out)
                if m:
                    pkg_len = int(m.group(1))
            # pkg.c の読み方をそのまま実物の iso9660 + atapi で再生する
            if pkg_len is None or not trace.exists():
                total += 1
                failed += 1
                if not quiet:
                    print("EXIT replay=FAIL (pkg の記録が無い)")
            else:
                secs = (pkg_len + 2047) // 2048
                mx = -(-secs // 16) + 3
                one("replay", exes["read"], "replay",
                    {"CD_READ_TRACE": str(trace), "CD_READ_BIGSIZE": str(pkg_len),
                     "CD_READ_TRACE_MAX": str(mx)})
    except _FirstFail:
        return failed
    if not quiet:
        print(f"SUMMARY {total - failed}/{total} PASS", flush=True)
    return failed


# 否定側。実装を 1 か所だけ壊して RED になることを見る。
# (ファイル, パターン, 置換, 説明)。最後の 1 本は対照 (何も変えない)。
MUTATIONS = [
    # --- drivers/atapi.c
    ("drivers/atapi.c",
     r"u32 n = \(count < ATAPI_READ_MAX_SECTORS\) \? count : ATAPI_READ_MAX_SECTORS;",
     "u32 n = 1;",
     "READ(10) を 1 セクタずつ出す (直す前の姿)"),
    ("drivers/atapi.c",
     r"if \(ret != ATAPI_OK && n > 1\) \{",
     "if (0) {",
     "複数セクタが落ちても 1 セクタずつ読み直さない"),
    ("drivers/atapi.c",
     r"                ret = atapi_read10\(lba \+ i, 1, p \+ i \* ATAPI_SECTOR_SIZE\);\n                if \(ret != ATAPI_OK\) break;",
     "                ret = atapi_read10(lba + i, 1, p + i * ATAPI_SECTOR_SIZE);",
     "1 セクタずつの読み直しが落ちても先へ進む (最後の結果だけ見る)"),
    ("drivers/atapi.c",
     r"        if \(ret == ATAPI_OK && got != n \* ATAPI_SECTOR_SIZE\) ret = ATAPI_ERR_IO;\n",
     "",
     "渡されたバイト数を確かめない (足りない転送を成功にする)"),
    ("drivers/atapi.c",
     r"for \(i = 0; i < IDE_SEL_SETTLE; i\+\+\) \(void\)inp\(IDE_ALT_STATUS\);\n}\n\n/\* 12",
     "for (i = 0; i < 0; i++) (void)inp(IDE_ALT_STATUS);\n}\n\n/* 12",
     "DRQ のブロックの後に 400ns 置かない (古い DRQ を次のブロックと取り違える)"),
    ("drivers/atapi.c",
     r"bcl = \(buf_size < ATAPI_PIO_BCL_MAX\) \? buf_size : ATAPI_PIO_BCL_MAX;",
     "bcl = buf_size;",
     "byte count limit を上限で抑えない (64KB で 16 ビットに入らない)"),
    ("drivers/atapi.c",
     r"        s_media_gen\+\+;\n",
     "",
     "UNIT ATTENTION で媒体の世代を進めない"),
    ("drivers/atapi.c",
     r"if \(s_last_sense != ATAPI_SK_UNIT_ATTENTION\) break;",
     "break;",
     "UNIT ATTENTION の後に出し直さない (入れ替え直後の mount が落ちる)"),
    ("drivers/atapi.c",
     r"cdb\[7\] = \(u8\)\(n >> 8\);\n        cdb\[8\] = \(u8\)\(n & 0xFF\);",
     "cdb[7] = 0;\n        cdb[8] = 1;",
     "CDB の転送長を 1 のまま (旧 CDB)"),
    # --- fs/iso9660.c: 覚えたパス
    ("fs/iso9660.c",
     r"if \(ctx->pc_valid && kstrcmp\(ctx->pc_path, path\) == 0\) \{",
     "if (0) {",
     "覚えたパスを使わない (区切りごとに根からたどる)"),
    ("fs/iso9660.c",
     r"if \(ctx->pc_valid && kstrcmp\(ctx->pc_path, path\) == 0\) \{",
     "if (ctx->pc_valid) {",
     "覚えたパスの名前を比べない (別のファイルを返す)"),
    # --- 捨てる合図
    ("fs/iso9660.c",
     r"    ctx->pc_valid = 0;\n    ctx->ra_valid = 0;\n",
     "    ctx->ra_valid = 0;\n",
     "捨てるときに覚えたパスを残す"),
    ("fs/iso9660.c",
     r"    ctx->pc_valid = 0;\n    ctx->ra_valid = 0;\n",
     "    ctx->pc_valid = 0;\n",
     "捨てるときに先読みの窓を残す"),
    ("fs/iso9660.c",
     r"    for \(i = 0; i < ISO_SCACHE_SLOTS; i\+\+\) ctx->scache\[i\]\.valid = 0;\n    ctx->stats\.drops\+\+;",
     "    (void)i;\n    ctx->stats.drops++;",
     "捨てるときにセクタの LRU を残す"),
    ("fs/iso9660.c",
     r"if \(gen != ctx->media_gen\n        \|\| ",
     "if (",
     "媒体の世代を見ない"),
    ("fs/iso9660.c",
     r"\(u32\)\(now - ctx->last_tick\) > \(u32\)ISO_IDLE_TICKS",
     "(u32)(now - ctx->last_tick) >= (u32)ISO_IDLE_TICKS",
     "2 秒規則の境目を 1 tick 早める"),
    ("fs/iso9660.c",
     r"\|\| \(ctx->touched && \(u32\)\(now - ctx->last_tick\) > \(u32\)ISO_IDLE_TICKS\)",
     "|| ((void)now, 0)",
     "2 秒規則を当てない"),
    ("fs/iso9660.c",
     r"    ctx->media_gen = gen;\n}",
     "    ctx->media_gen = gen;\n    ctx->last_tick = now;\n}",
     "キャッシュの当たりでも時刻を進める (読み続ける限り捨てない)"),
    ("fs/iso9660.c",
     r"if \(atapi_media_gen\(\) == gen0\) \{\n            return \(ret != 0\) \? ret : \(int\)to_read;",
     "if (atapi_media_gen() == gen0 || 1) {\n            return (ret != 0) ? ret : (int)to_read;",
     "読みの途中で世代が進んでも読み直さない (iso_read_range)"),
    # --- セクタの LRU
    ("fs/iso9660.c",
     r"if \(victim < 0 \|\| s->used < ctx->scache\[victim\]\.used\) victim = i;",
     "if (victim < 0) victim = i;",
     "LRU でない (いつも最初のスロットを追い出す)"),
    ("fs/iso9660.c",
     r"if \(s->valid && s->lba == lba\) \{",
     "if (s->valid) {",
     "LRU が LBA を見ない"),
    ("fs/iso9660.c",
     r"            kmemcpy\(sector, sec, ISO_SECTOR_SIZE\);\n            cur = sector;",
     "            cur = sec;",
     "list_dir がキャッシュを指したまま cb を呼ぶ (cb の読みで入れ替わる)"),
    ("fs/iso9660.c",
     r"        if \(sect_off == 0 \|\| !sector\) \{\n            sector = iso_get_sector\(ctx, sect_lba\);",
     "        if (!sector) {\n            sector = iso_get_sector(ctx, sect_lba);",
     "ディレクトリの 2 セクタ目を読まない"),
    # --- 先読みの窓・まとめ読み
    ("fs/iso9660.c",
     r"            if \(n > ISO_RA_SECTORS\) n = ISO_RA_SECTORS;\n",
     "",
     "窓の大きさで抑えない (窓からはみ出して書く)"),
    ("fs/iso9660.c",
     r"            n = file_secs - sect_idx;\n",
     "            n = ISO_RA_SECTORS;\n",
     "窓をファイルの外 (媒体の終わりの先) まで広げる"),
    ("fs/iso9660.c",
     r"&& left >= \(use_ra \? ISO_RA_BYTES : \(u32\)ISO_SECTOR_SIZE\)\) \{",
     "&& left >= ISO_SECTOR_SIZE) {",
     "窓より小さい揃った読みも直接読む (4KB ごとに READ(10))"),
    ("fs/iso9660.c",
     r"            ctx->ra_valid = 0;\n            if \(iso_dev_read\(ctx, lba, n, ctx->ra_buf\) != 0\)",
     "            if (iso_dev_read(ctx, lba, n, ctx->ra_buf) != 0)",
     "窓を読む前に捨てない (失敗の後に上書きされかけの窓を当てる)"),
    ("fs/iso9660.c",
     r"            u32 chunk = ctx->ra_count \* ISO_SECTOR_SIZE - pos;",
     "            u32 chunk = ctx->ra_count * ISO_SECTOR_SIZE;",
     "窓の中の位置を引かずに写す長さを決める"),
    ("fs/iso9660.c",
     r"                u32 secs = \(u32\)\(\(\(file_size - 1\) / ISO_SECTOR_SIZE\) \+ 1\);",
     "                u32 secs = (u32)(file_size / ISO_SECTOR_SIZE);",
     "ファイルのセクタ数を切り捨てる (末尾の半端なセクタで窓が 0 本)"),
    # --- userland/lib/rt/pkg.c
    ("userland/lib/rt/pkg.c",
     r"u32 room = cap - \(pos % PKG_STREAM_ALIGN\);",
     "u32 room = cap;",
     "区切りをセクタ境界に揃えない"),
    ("userland/lib/rt/pkg.c",
     r"                pos \+= \(u32\)r;\n",
     "",
     "読み位置を進めない"),
    ("userland/lib/rt/pkg.c",
     r"u8 \*tbuf = \(u8 \*\)api->mem_alloc\(PKG_STREAM_CHUNK\);",
     "u8 *tbuf = (u8 *)0;",
     "大きなバッファを取らない (4KB のまま)"),
    ("userland/lib/rt/pkg.c",
     r"        if \(tbuf != small\) api->mem_free\(tbuf\);\n",
     "",
     "取ったバッファを返さない"),
    # --- Codex レビュー 1 (P2 × 2)
    ("fs/iso9660.c",
     r"                use_ra = 0;\n                continue;",
     "                return VFS_ERR_IO;",
     "窓の先読みが要求の外の不良で落ちたら失敗にする (レビュー 1 の P2-1 の姿)"),
    ("fs/iso9660.c",
     r"        if \(atapi_media_gen\(\) == gen0\) \{\n            if \(gen_out\) \*gen_out = gen0;",
     "        if (atapi_media_gen() == gen0 || 1) {\n            if (gen_out) *gen_out = gen0;",
     "stat / get_file_size / list_dir の解決の途中で世代が進んでも引き直さない"),
    ("fs/iso9660.c",
     r"            if \(atapi_media_gen\(\) != gen0\) return VFS_ERR_IO;\n            kmemcpy",
     "            kmemcpy",
     "list_dir がセクタを読んだ後に世代を見ない (続きを別の媒体から一覧する)"),
    ("fs/iso9660.c",
     r"            cb\(&ent, user_ctx\);\n            /\* cb がこの FS を読んで媒体の交換を踏んだかもしれない \*/\n            if \(atapi_media_gen\(\) != gen0\) return VFS_ERR_IO;",
     "            cb(&ent, user_ctx);",
     "list_dir が cb の後に世代を見ない"),
    # --- 2026-09-26 NP21/W で 1 セクタも読めない (セカンダリのスレーブの CD)
    ("drivers/atapi.c",
     r"    for \(i = 0; i < 2; i\+\+\) \{\n        if \(atapi_probe_sig\(sels\[i\]\)\) \{\n            found\[n\+\+\] = sels\[i\];\n            s_present_mask \|= \(u8\)\(1 << i\);\n        \}\n    \}\n\n    if \(n == 0 && atapi_srst\(\) == ATAPI_OK\)",
     "    for (i = 0; i < 1; i++) {\n        if (atapi_probe_sig(sels[i])) {\n            found[n++] = sels[i];\n            s_present_mask |= (u8)(1 << i);\n        }\n    }\n\n    if (n == 0 && atapi_srst() == ATAPI_OK)",
     "マスターしか見ない (直す前の姿: スレーブの ISO を読めない)"),
    ("drivers/atapi.c",
     r"                if \(atapi_has_media\(\)\) break;",
     "                break;",
     "2 台あるとき媒体を見ずにマスターを選ぶ (マスターが空の CD だと 1 セクタも読めない)"),
    ("drivers/atapi.c",
     r"    outp\(IDE_DRV_HEAD, s_drvsel\);\n    s_cursel = s_drvsel;\n    atapi_settle\(\);\n    if \(atapi_wait_idle\(\) != ATAPI_OK\) \{\n        if \(atapi_recover\(\) != ATAPI_OK\) return ATAPI_ERR_TIMEOUT;\n        if \(atapi_wait_idle\(\) != ATAPI_OK\) return ATAPI_ERR_TIMEOUT;\n    \}\n    return ATAPI_OK;",
     "    outp(IDE_DRV_HEAD, s_drvsel);\n    s_cursel = s_drvsel;\n    atapi_settle();\n    return ATAPI_OK;",
     "選んだ後に BSY/DRQ クリアを待たない (選んだ直後の BSY の装置へ PACKET を書いて無視される)"),
    ("drivers/atapi.c",
     r"    if \(ret == ATAPI_OK && got != 8\) ret = ATAPI_ERR_IO;\n",
     "",
     "READ CAPACITY の 8 バイトを確かめない (受け皿の残りを容量にする)"),
    ("drivers/atapi.c",
     r"    if \(s_diag_lines >= ATAPI_DIAG_MAX\) return;",
     "    return;",
     "読みの失敗の診断の行を出さない"),
    ("drivers/atapi.c",
     r"        /\* BSY=0になるまで待つ \*/\n        ret = atapi_wait_bsy\(\);\n        if \(ret != ATAPI_OK\) \{\n            s_diag_got = total_read;\n            return ret;\n        \}\n",
     "",
     "DRQ の周の頭で BSY を待たない (NP21/W の CD_ASYNC で次のセクタを取りこぼす)"),
    # --- Codex レビュー 2 (2dc3c7f) の P1 / P2 × 3 / P3
    ("drivers/atapi.c",
     r"    ret = atapi_select_device\(\);\n    if \(ret != ATAPI_OK\) return ret;\n\n    outp\(IDE_FEATURES, 0x00\);\n    outp\(IDE_CYL_LO, \(unsigned\)\(bcl & 0xFF\)\);\n    outp\(IDE_CYL_HI, \(unsigned\)\(\(bcl >> 8\) & 0xFF\)\);\n    outp\(IDE_COMMAND, ATAPI_CMD_PACKET\);",
     "    ret = atapi_wait_bsy();\n    if (ret != ATAPI_OK) return ret;\n\n    outp(IDE_FEATURES, 0x00);\n    outp(IDE_CYL_LO, (unsigned)(bcl & 0xFF));\n    outp(IDE_CYL_HI, (unsigned)((bcl >> 8) & 0xFF));\n    outp(IDE_DRV_HEAD, s_drvsel);\n    outp(IDE_COMMAND, ATAPI_CMD_PACKET);",
     "直す前の順序: 旧装置の BSY を待って Byte Count を書き、選んだ直後に PACKET (レビュー 2 の P1)"),
    ("drivers/atapi.c",
     r"        if \(atapi_wait_idle\(\) != ATAPI_OK\n            && atapi_recover\(\) != ATAPI_OK\) return ATAPI_ERR_TIMEOUT;\n",
     "",
     "選ばれている (居る) 装置が BSY でも待たずに DRV_HEAD を書く (レビュー 2 の P2)"),
    ("drivers/atapi.c",
     r"static int atapi_recover\(void\)\n\{\n",
     "static int atapi_recover(void)\n{\n    return ATAPI_OK;\n",
     "コマンドが終わらない装置をリセットで戻さない"),
    ("drivers/atapi.c",
     r"    if \(atapi_srst\(\) != ATAPI_OK\) return ATAPI_ERR_TIMEOUT;\n    outp\(IDE_DRV_HEAD, s_drvsel\);\n    s_cursel = s_drvsel;\n    atapi_settle\(\);\n    return atapi_wait_idle\(\);",
     "    if (atapi_srst() != ATAPI_OK) return ATAPI_ERR_TIMEOUT;\n    return atapi_wait_idle();",
     "SRST の後に使う装置を選び直さない (マスターへ PACKET が行く)"),
    ("drivers/atapi.c",
     r"    if \(st != ATAPI_ST_FLOAT && \(st & \(IDE_ST_BSY \| IDE_ST_DRQ\)\)\n        && atapi_sel_present\(s_cursel\)\) \{",
     "    if (st != ATAPI_ST_FLOAT && (st & (IDE_ST_BSY | IDE_ST_DRQ))) {",
     "居ない装置の BSY らしい値も「コマンド未完了」と見て待ち・リセットする"),
    ("drivers/atapi.c",
     r"        if \(s_last_sense == ATAPI_SK_UNIT_ATTENTION\) \{\n            \(void\)atapi_request_sense\(&sk, &asc, &ascq\);",
     "        if (0) {\n            (void)atapi_request_sense(&sk, &asc, &ascq);",
     "UNIT ATTENTION を媒体なしと扱う (空のマスターに固定する。レビュー 2 の P2)"),
    ("drivers/atapi.c",
     r"            \(void\)atapi_request_sense\(&sk, &asc, &ascq\);\n            s_stats\.ready_retries\+\+;\n            continue;",
     "            s_stats.ready_retries++;\n            continue;",
     "UNIT ATTENTION を REQUEST SENSE で消さずに出し直す"),
    ("drivers/atapi.c",
     r"            atapi_delay_us\(ATAPI_READY_WAIT_US\);\n            continue;\n        \}\n        return ret;",
     "            return ATAPI_ERR_NO_MEDIA;\n        }\n        return ret;",
     "NOT READY (準備中) を待たずに媒体なしと確定する"),
    ("drivers/atapi.c",
     r"&& asc == ATAPI_ASC_MEDIUM_NOT_PRESENT\) \{\n                return ATAPI_ERR_NO_MEDIA;",
     "&& 0) {\n                return ATAPI_ERR_NO_MEDIA;",
     "媒体なし (3Ah) も待って出し直す"),
    ("drivers/atapi.c",
     r"        total_read = end;\n",
     "        total_read += words * 2;\n",
     "受け取ったバイト数をワードで数える (7 バイトを 8 と数える。レビュー 2 の P2)"),
    ("drivers/atapi.c",
     r"atapi_drive_index\(\), \(unsigned\)s_diag_lba, \(unsigned\)s_diag_n, ret,",
     "atapi_drive_index(), (unsigned)req_lba, (unsigned)req_n, ret,",
     "診断の行が呼び手の範囲を出す (落ちた 1 セクタのコマンドでなく。レビュー 2 の P3)"),
    ("drivers/atapi.c",
     r"        if \(ret != ATAPI_OK\) \{\n            s_diag_got = total_read;\n            return ret;\n        \}",
     "        if (ret != ATAPI_OK) return ret;",
     "待ちの期限切れで受け取った数を記録しない (got=0 になる)"),
    ("drivers/atapi.c",
     r"    u8 st = \(u8\)inp\(IDE_ALT_STATUS\);\n    s_diag_st = st;\n    return st;",
     "    u8 st = (u8)inp(IDE_ALT_STATUS);\n    return st;",
     "待ちで読んだステータスを記録しない (期限切れの st が前の値になる)"),
    # --- 代行レビュー (Fable 5.1) 2 回目の P2 / P3
    ("drivers/atapi.c",
     r"            atapi_delay_us\(ATAPI_READY_WAIT_US\);",
     "            cpu_delay_us(ATAPI_READY_WAIT_US);",
     "準備中の待ちを 250ms 1 回で頼む (cpu_delay_us が 100ms に丸めて合計 1.6 秒。レビュー 2 の P2)"),
    ("drivers/atapi.h",
     r"#define ATAPI_READY_RETRIES     20",
     "#define ATAPI_READY_RETRIES     16",
     "準備中の出し直しを 16 回 (4 秒) で諦める (4.5 秒準備中のスレーブを取り逃がす)"),
    ("drivers/atapi.c",
     r"    atapi_delay_us\(ATAPI_SRST_SETTLE_US\);\n",
     "",
     "SRST を解いた直後に 2ms 置かずステータスを読む (レビュー 2 の P3)"),
    ("drivers/atapi.c",
     r"    if \(atapi_status\(\) == ATAPI_ST_FLOAT\) return ATAPI_OK;\n    if \(atapi_wait_clear\(IDE_ST_BSY, ATAPI_SRST_TIMEOUT_US\) != ATAPI_OK\) \{\n(?:.*\n)*?    \}\n",
     "",
     "SRST の後にマスターの BSY=0 を待たずに DRV_HEAD を書く (レビュー 2 の P3)"),
    ("drivers/atapi.c",
     r"attempt <= ATAPI_UA_RETRIES;",
     "attempt < 2;",
     "READ(10) の UNIT ATTENTION を 1 回しか出し直さない (レビュー 2 の P3)"),
    # --- TASK_ATAPI_TIMEOUT (待ちの上限を秒で、SRST の 31 秒、START UNIT)
    ("drivers/atapi.c",
     r"    u32 waited = 0;\n    for \(;;\) \{\n        if \(!\(atapi_status\(\) & mask\)\) return ATAPI_OK;\n        if \(waited >= limit_us\) return ATAPI_ERR_TIMEOUT;\n        cpu_delay_us\(ATAPI_POLL_US\);\n        waited \+= ATAPI_POLL_US;\n    \}",
     "    int timeout = IDE_TIMEOUT_LOOP;\n    (void)limit_us;\n    while (timeout-- > 0) {\n        if (!(atapi_status() & mask)) return ATAPI_OK;\n    }\n    return ATAPI_ERR_TIMEOUT;",
     "BSY / DRQ の待ちの上限を回数 (IDE_TIMEOUT_LOOP 回の inp) に戻す (直す前の姿)"),
    ("drivers/atapi.c",
     r"        if \(waited >= s_wait_limit_us\) return ATAPI_ERR_TIMEOUT;\n        cpu_delay_us\(ATAPI_POLL_US\);\n        waited \+= ATAPI_POLL_US;\n    \}\n\}",
     "        if (++waited >= IDE_TIMEOUT_LOOP) return ATAPI_ERR_TIMEOUT;\n    }\n}",
     "CDB の DRQ の待ちの上限を回数に戻す"),
    ("drivers/atapi.h",
     r"#define ATAPI_CMD_TIMEOUT_US    10000000UL",
     "#define ATAPI_CMD_TIMEOUT_US    1000000UL",
     "PACKET の上限を 1 秒に (スピンアップ 3 秒の健全な装置を DEVICE RESET で捨てる)"),
    ("drivers/atapi.c",
     r"    if \(atapi_srst\(\) != ATAPI_OK\) return ATAPI_ERR_TIMEOUT;\n",
     "    (void)atapi_srst();\n",
     "SRST の戻り値を見ない (BSY のマスターが選ばれたまま DRV_HEAD を書く)"),
    ("drivers/atapi.c",
     r"atapi_wait_clear\(IDE_ST_BSY, ATAPI_SRST_TIMEOUT_US\)",
     "atapi_wait_clear(IDE_ST_BSY, s_wait_limit_us)",
     "SRST の後の BSY を 31 秒でなく PACKET の上限で諦める"),
    ("drivers/atapi.c",
     r"    if \(n == 0 && atapi_srst\(\) == ATAPI_OK\) \{",
     "    if (n == 0) {\n        (void)atapi_srst();",
     "atapi_init が SRST の戻り値を見ない (BSY のマスターのまま選び直す、起動が 10 秒延びる)"),
    ("drivers/atapi.c",
     r"    s_wait_limit_us = ATAPI_INIT_TIMEOUT_US;\n",
     "",
     "atapi_init も PACKET の上限で待つ (起動の最悪時間が延びる)"),
    ("drivers/atapi.c",
     r"    atapi_select_bank\(0\);\n    s_wait_limit_us = ATAPI_CMD_TIMEOUT_US;\n",
     "    atapi_select_bank(0);\n",
     "atapi_init の後に上限を戻さない (以後も 5 秒で諦める)"),
    ("drivers/atapi.c",
     r"                && ascq == ATAPI_ASCQ_INIT_CMD_REQUIRED && !started\) \{",
     "                && ascq == ATAPI_ASCQ_INIT_CMD_REQUIRED && !started && 0) {",
     "04h/02h に START UNIT を出さない (待っても変わらず媒体なし)"),
    ("drivers/atapi.c",
     r"                && ascq == ATAPI_ASCQ_INIT_CMD_REQUIRED && !started\) \{",
     "                && ascq == ATAPI_ASCQ_INIT_CMD_REQUIRED && (started || 1)) {",
     "START UNIT を 1 回に限らない"),
    ("drivers/atapi.c",
     r"    \*ascq = buf\[13\];",
     "    *ascq = 0;",
     "REQUEST SENSE の ASCQ を読まない (04h/02h を 04h/00h と取り違える)"),
    ("drivers/atapi.c",
     r"    if \(ret == ATAPI_ERR_NO_MEDIA\) atapi_note\(\"NOT READY, gave up\", 1, last_asc, last_ascq\);\n",
     "    (void)last_asc; (void)last_ascq;\n",
     "準備中のまま諦めたときに ASC/ASCQ の行を出さない"),
    # --- 着地後の P3: バスが死んだ印 (SRST の 31 秒切れの後は即失敗)
    ("drivers/atapi.c",
     r"        s_bus_dead = 1;\n",
     "",
     "SRST が 31 秒で切れても印を立てない (読みのたびに約 61 秒待つ)"),
    ("drivers/atapi.c",
     r"    if \(atapi_bus_dead_fail\(\)\) return ATAPI_ERR_TIMEOUT;\n\n    atapi_select_bank\(1\);\n\n    /\* 連続",
     "\n    atapi_select_bank(1);\n\n    /* 連続",
     "atapi_read_sectors が印を見ない"),
    ("drivers/atapi.c",
     r"    if \(!cap\) return ATAPI_ERR_IO;\n    if \(atapi_bus_dead_fail\(\)\) return ATAPI_ERR_TIMEOUT;\n",
     "    if (!cap) return ATAPI_ERR_IO;\n",
     "atapi_read_capacity が印を見ない"),
    ("drivers/atapi.c",
     r"    cdrom_present = 0;\n    s_bus_dead = 0;\n",
     "    cdrom_present = 0;\n",
     "atapi_init が印を解かない (装置が戻っても読めない)"),
    ("drivers/atapi.c",
     r"    if \(!s_dead_noted\) \{",
     "    if (1) {",
     "即失敗の行を毎回出す (ATAPI_DIAG_MAX 行を食い潰す)"),
    ("drivers/atapi.c",
     r"\"SRST: BSY did not clear, bus marked dead\", 0, 0, 0,\n                      ATAPI_SRST_TIMEOUT_US\);",
     "\"SRST: BSY did not clear, bus marked dead\", 0, 0, 0,\n                      s_wait_limit_us);",
     "SRST の行の limit= に PACKET の上限を出す (31 秒を待ったのに 10s と出る)"),
    # 対照: 何も変えない。SURVIVED でなければ試験が不安定 (偽の RED)。
    ("fs/iso9660.c", r"(#include \"iso9660\.h\")", r"\1", "対照 (何も変えない)"),
]
CONTROL = len(MUTATIONS)


def make_tree(tmp, rel=None, text=None):
    tree = pathlib.Path(tmp) / "tree"
    for f in TREE_FILES:
        dst = tree / f
        dst.parent.mkdir(parents=True, exist_ok=True)
        dst.write_text((ROOT / f).read_text(encoding="utf-8"), encoding="utf-8")
    if rel is not None:
        (tree / rel).write_text(text, encoding="utf-8")
    return tree


def _mutate_one(item):
    """変異 1 本を自分専用の一時ディレクトリの写しで組んで回す。並列に呼ばれる。
    (種別, 行) を返す。種別は RED / SURVIVED / ERROR / CONTROL / CTRL-RED。"""
    idx, (rel, pat, rep, desc), base = item
    src = (ROOT / rel).read_text(encoding="utf-8")
    new, n = re.subn(pat, rep, src, count=1)
    if n != 1:
        return "ERROR", f"MUT {idx:2d} NOMATCH  {desc}"
    keys = REBUILD.get(rel, EXE_KEYS)
    with tempfile.TemporaryDirectory(prefix="os32-cdmut-") as tmp:
        tree = make_tree(tmp, rel, new)
        # 組み直す実行ファイルを 1 本ずつ組んでは回し、落ちたらそこで打ち切る (後の
        # 実行ファイルは組まない)。組めなければ ERROR。最後に replay (pkg の記録を
        # read で再生) を、どちらかを組み直したときだけ回す。
        exes = dict(base)
        failed = 0
        for k in keys:
            try:
                exes.update(build(tmp, tree, (k,)))
            except subprocess.CalledProcessError:
                return "ERROR", f"MUT {idx:2d} ERROR    {desc} (コンパイル不可、数えない)"
            failed = run_all(exes, tmp, quiet=True, keys=(k,) if k != "pkg" else (),
                             first_fail=True, replay=k == "pkg")
            if failed:
                break
        if not failed and "read" in keys and "pkg" not in keys:
            failed = run_all(exes, tmp, quiet=True, keys=(), first_fail=True, replay=True)
    if idx == CONTROL:
        return ("CTRL-RED", "CONTROL")[failed == 0], \
            f"MUT {idx:2d} {'CONTROL' if failed == 0 else 'CTRL-RED'} {desc}"
    if failed:
        return "RED", f"MUT {idx:2d} RED      {desc}"
    return "SURVIVED", f"MUT {idx:2d} SURVIVED {desc}"


def mutate(base):
    """base は実物で組んだ 3 本 (作り直さない実行ファイルに使う)。変異は並列に
    回し (mutpar、OS32_MUT_JOBS)、結果は変異の番号順に出す。"""
    red = survived = error = 0
    control_ok = False
    only = {int(x) for x in os.environ.get("CD_MUT_ONLY", "").split(",") if x}
    with tempfile.TemporaryDirectory(prefix="os32-cddeps-") as tmp:
        deps = {k: mutpar.gcc_deps(c, ROOT) for k, c in _commands(tmp, ROOT).items()}
    stale = mutpar.check_rebuild_table(REBUILD, deps, [m[0] for m in MUTATIONS])
    for s in stale:
        print("REBUILD TABLE STALE: " + s, flush=True)
    items = [(idx, m, base) for idx, m in enumerate(MUTATIONS, 1)
             if not only or idx in only]
    for kind, line in mutpar.run_ordered(_mutate_one, items):
        print(line, flush=True)
        if kind == "ERROR":
            error += 1
        elif kind in ("CONTROL", "CTRL-RED"):
            control_ok = kind == "CONTROL"
        elif kind == "RED":
            red += 1
        else:
            survived += 1
    real = CONTROL - 1
    print(f"MUTATION {red}/{real} RED, {survived} SURVIVED, {error} ERROR/NOMATCH, "
          f"control={'OK' if control_ok else 'BROKEN'}", flush=True)
    return survived == 0 and error == 0 and control_ok and not stale


def build_target(tmp):
    """カーネル / ユーザーランドと同じ i386-elf で通す。"""
    base = ["i386-elf-gcc", "-std=gnu89", "-m32", "-march=i386", "-ffreestanding",
            "-fno-pie", "-fno-stack-protector", "-nostdlib", "-mno-red-zone", "-fcommon",
            "-fsigned-char", "-fno-short-enums", "-O2", "-Wall", "-Werror",
            "-Wdeclaration-after-statement"]
    kinc = ["-D__KERNEL_BUILD__"] + ["-I" + str(ROOT / p) for p in
                                     (".", "include", "arch/x86", "platform/pc98",
                                      "sdk/include", "sdk/include/os32", "drivers", "fs",
                                      "kernel", "lib")]
    for rel in ("drivers/atapi.c", "fs/iso9660.c"):
        subprocess.run([*base, *kinc, "-c", str(ROOT / rel), "-o",
                        str(pathlib.Path(tmp) / (rel.replace("/", "_") + ".o"))],
                       cwd=ROOT, check=True)
    uinc = ["-I" + str(ROOT / p) for p in ("sdk/include", "sdk/include/os32",
                                           "userland/lib", "userland/lib/rt")]
    subprocess.run([*base, *uinc, "-c", str(ROOT / "userland/lib/rt/pkg.c"), "-o",
                    str(pathlib.Path(tmp) / "pkg.o")], cwd=ROOT, check=True)
    print("TARGET i386-elf GNU89 -Werror PASS (atapi.c / iso9660.c / pkg.c)", flush=True)


def main():
    args = sys.argv[1:]
    do_target = "--target" in args
    do_mutate = "--mutate" in args
    cases = [a for a in args if not a.startswith("--")]
    ok = True
    with tempfile.TemporaryDirectory(prefix="os32-cdread-") as tmp:
        tree = make_tree(tmp)
        try:
            exes = build(tmp, tree)
        except subprocess.CalledProcessError as e:
            sys.stderr.write(e.stderr.decode(errors="replace"))
            print("HOST COMPILE FAIL")
            sys.exit(1)
        print("HOST COMPILE GNU89 -Werror (asan/ubsan) PASS", flush=True)
        if cases:
            failed = 0
            for c in cases:
                label, _, name = c.rpartition(":")
                exe = exes["wide"] if label == "wide" else exes["pkg"] if label == "pkg" \
                    else exes["read"]
                rc, _ = run1(exe, name)
                print(f"EXIT {c}={rc}", flush=True)
                failed += rc != 0
            ok = failed == 0
        else:
            ok = run_all(exes, tmp) == 0
        if do_target:
            build_target(tmp)
        if do_mutate and not cases:
            ok = mutate(exes) and ok
    sys.exit(0 if ok else 1)


if __name__ == "__main__":
    main()
