"""票 TASK_DB_ERRSTR: `db_last_error()` / `db_column_text()` の返り先のホスト TDD。

実 SQLite + 実 os32 SQLite VFS + 実 FD 表 + 実 `kapi/kapi_db.c` を RAM の
バックエンドに載せて回す。DB は `:memory:` で開くので、ホストのファイル
システム・sudo・mount・配備・エミュレータには一切触らない。
記録は tools/tests/db_errstr_tdd.md。

  python3 -B tools/tests/test_db_errstr.py [--target] [--sanitize] [--mutate] [case ...]

見るのは 2 つ (票 §5)。

  E6  `const char *` を返す KAPI の**全経路**で、返り先が共有メモリの
      範囲内であること。ホストでは番地の区別が付かないので、「読めた」では
      なく `test_shm` の範囲の検査として書く。
  E7  結果データを上限いっぱいまで書いた直後に診断文が壊れていないこと。

--mutate は**否定側**。上限の引き算を 1 か所だけ元に戻す / 経路を
カーネル番地のまま返す / 切り詰めで NUL を置き忘れる、をそれぞれ作り、
この試験が RED になることを見る。どれもコンパイルは通る。

**上限の引き算は 5 か所あって、うち 2 か所は実行時には見えない** —
事前検査 (`shm_row_fits_n`) が先に断るので、writer 側 (`shm_write_row` の
`remaining`) と中間の溢れ検査 (`shm_row_check`) を戻しても行は書かれない。
そこは静的な番人 (`check_no_block_size`) が受け持つ: 票 §4 の
「`DB_SHM_BLOCK_SIZE` を直接引き算している箇所を残さない」をそのまま
規則にして、`kapi/kapi_db.c` がこの名前を 1 回も出さないことを見る。
"""
import pathlib
import re
import subprocess
import sys
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[2]
CASES = ["errstr_range", "coltext_range", "errstr_truncate", "result_bound",
         "diag_layout"]
INC = ["-I" + str(ROOT / p) for p in
       ("include", "fs", "drivers", "sdk/include/os32", "lib", "lib/sqlite3")]
CONFIG = str(ROOT / "lib/sqlite3/os32_sqlite_config.h")

SHIMS = {
    # kapi_db.c は SHM を MEM_SHM_BASE で引く。ホストでは試験側の配列へ向ける。
    "memmap.h": "extern unsigned char test_shm[];\n#define MEM_SHM_BASE test_shm\n",
    # exec/exec.c 側の口だけを模型にする (帯と PTE はカーネル番地に依存する)。
    "exec.h": "int ring3_user_range_ok(u32 p, u32 len);\n",
}

HOST_FLAGS = ["-std=gnu89", "-Wall", "-Wextra", "-Werror",
              "-Wdeclaration-after-statement", "-Wno-unused-parameter",
              "-Wno-sign-compare", "-Wno-pointer-to-int-cast",
              "-Wno-missing-field-initializers", "-D__cdecl="]


def check_no_block_size():
    """kapi/kapi_db.c が `DB_SHM_BLOCK_SIZE` を名指ししていないこと (票 §4 の 1)。

    結果データの上限は 5 か所で見ている。1 か所でも 16KB から直接引くと
    結果が診断文を踏み潰すが、**事前検査が先に断つ経路は実行時に見えない**。
    だから「その名前を使わない」を規則そのものとして固定する。上限は
    `DB_SHM_RESULT_LIMIT` (= 診断領域を除いた分) からだけ導く。
    """
    src = (ROOT / "kapi/kapi_db.c").read_text(encoding="utf-8")
    hits = [n for n, line in enumerate(src.splitlines(), 1)
            if "DB_SHM_BLOCK_SIZE" in line]
    if hits:
        print("FAIL SOURCE: kapi/kapi_db.c は DB_SHM_BLOCK_SIZE から直接引いている "
              "(行 %s) — DB_SHM_RESULT_LIMIT を使う" % ", ".join(map(str, hits)),
              flush=True)
        return 1
    limits = len(re.findall(r"DB_SHM_RESULT_LIMIT", src))
    if limits < 5:
        print("FAIL SOURCE: 上限を見ている箇所が %d 件しかない (5 件以上のはず)"
              % limits, flush=True)
        return 1
    print("SOURCE: kapi_db.c は上限を DB_SHM_RESULT_LIMIT からだけ引く "
          "(%d 箇所) PASS" % limits, flush=True)
    return 0


def build(tmp, tag, sqlite_obj, san):
    exe = str(tmp / ("dberrstr-" + tag))
    rc = subprocess.run(["gcc", *HOST_FLAGS, *san, "-I" + str(tmp), *INC,
                         str(ROOT / "tools/tests/db_errstr_host.c"), sqlite_obj,
                         "-o", exe])
    return exe if rc.returncode == 0 else None


def run_cases(exe, cases):
    failed = 0
    for case in cases:
        rc = subprocess.run([exe, case], cwd=ROOT).returncode
        print("EXIT %s=%d" % (case, rc), flush=True)
        failed += rc != 0
    return failed


# --------------------------------------------------------------------------
#  否定側 (--mutate)
# --------------------------------------------------------------------------

# (相対パス, 目印, 置き換え, 期待する落ち方) — どれもコンパイルは通る。
#   "run"    実行時に落ちる (ケースが RED)
#   "source" 静的な番人が捕まえる (事前検査が先に断つので実行時には見えない)
MUTATIONS = [
    # 1. 上限の引き算を 1 か所だけ元に戻す (事前検査の payload 側)。
    #    → 上限を超える行が ROW として通り、診断領域を勘定に入れなくなる。
    ("kapi/kapi_db.c",
     "    if (payload > (u32)DB_SHM_RESULT_LIMIT - need) return 0;",
     "    if (payload > (u32)DB_SHM_BLOCK_SIZE - need) return 0;",
     "run"),
    # 2. 1 回の db_exec のエラー欄の上限を元に戻す。
    #    → 長い診断文がそのまま診断領域へ流れ込む。
    ("kapi/kapi_db.c",
     "    max_len = DB_SHM_RESULT_LIMIT - data_start - 1;",
     "    max_len = DB_SHM_BLOCK_SIZE - data_start - 1;",
     "run"),
    # 3. writer 側の残り容量を元に戻す。事前検査が先に断つので**実行時には
    #    見えない** — 静的な番人だけが捕まえる (この試験の要点の 1 つ)。
    ("kapi/kapi_db.c",
     "        i32 remaining = DB_SHM_RESULT_LIMIT - data_offset;",
     "        i32 remaining = DB_SHM_BLOCK_SIZE - data_offset;",
     "source"),
    # 4. db_last_error の「範囲外の handle」経路をカーネル番地のまま返す。
    ("kapi/kapi_db.c",
     "    if (handle < 0 || handle >= DB_MAX_CONNECTIONS)\n"
     "        return db_shm_diag(\"invalid handle\");",
     "    if (handle < 0 || handle >= DB_MAX_CONNECTIONS)\n"
     "        return \"invalid handle\";",
     "run"),
    # 5. db_last_error の sqlite3_errmsg 経路を SQLite の帯のまま返す。
    ("kapi/kapi_db.c",
     "    return db_shm_diag(sqlite3_errmsg(slot->db));",
     "    return sqlite3_errmsg(slot->db);",
     "run"),
    # 6. db_column_text のエラー経路を `return "";` に戻す。
    ("kapi/kapi_db.c",
     "    if (info->data_offset == 0) return db_shm_empty();",
     "    if (info->data_offset == 0) return \"\";",
     "run"),
    # 7. 切り詰めで NUL を置き忘れる (切った側)。
    ("kapi/kapi_db.c",
     "    dst[len + mark] = '\\0';",
     "    if (len == 0u) dst[len + mark] = '\\0';",
     "run"),
    # 8. 切り詰めの上限を診断領域いっぱいに広げる (空文字列の 1 バイトを潰す)。
    ("kapi/kapi_db.c",
     "    u32 cap = (u32)DB_SHM_ERRSTR_MAX - 1u;",
     "    u32 cap = (u32)DB_SHM_DIAG_SIZE;",
     "run"),
]


def run_mutations(tmp, sqlite_obj, san):
    bad = 0
    for i, (relpath, old, new, kind) in enumerate(MUTATIONS, 1):
        target = ROOT / relpath
        original = target.read_text(encoding="utf-8")
        label = "%d %s" % (i, kind)
        if old not in original:
            print("MUTATE %-12s SKIP (目印が見つからない)" % label, flush=True)
            bad += 1
            continue
        try:
            target.write_text(original.replace(old, new, 1), encoding="utf-8")
            exe = build(tmp, "mut%d" % i, sqlite_obj, san)
            if exe is None:
                print("MUTATE %-12s **コンパイルが通らない = 目が働いていない**"
                      % label, flush=True)
                bad += 1
                continue
            fails = run_cases(exe, CASES) if kind == "run" else 0
            fails += check_no_block_size_quiet()
            if fails == 0:
                print("MUTATE %-12s **GREEN のまま = 試験が規則を見ていない**"
                      % label, flush=True)
                bad += 1
            else:
                print("MUTATE %-12s RED (期待どおり落ちた: %d 件)"
                      % (label, fails), flush=True)
        finally:
            target.write_text(original, encoding="utf-8")
    return bad


def check_no_block_size_quiet():
    import io
    import contextlib
    buf = io.StringIO()
    with contextlib.redirect_stdout(buf):
        rc = check_no_block_size()
    return rc


if __name__ == "__main__":
    with tempfile.TemporaryDirectory(prefix="os32-db-errstr-") as tmp:
        tmp = pathlib.Path(tmp)
        for name, text in SHIMS.items():
            (tmp / name).write_text(text)
        sqlite_obj = str(tmp / "sqlite.o")
        san = (["-fsanitize=address", "-fno-omit-frame-pointer"]
               if "--sanitize" in sys.argv else [])
        subprocess.run(["gcc", "-std=gnu89", "-O0", *san, "-include", CONFIG,
                        "-c", str(ROOT / "lib/sqlite3/sqlite3.c"),
                        "-o", sqlite_obj], check=True)
        exe = build(tmp, "main", sqlite_obj, san)
        if exe is None:
            sys.exit("HOST GNU89 -Werror compile FAILED")
        print("HOST GNU89 -Werror compile PASS (real kapi_db.c + bundled SQLite)",
              flush=True)

        if "--target" in sys.argv:
            subprocess.run(["i386-elf-gcc", "-std=gnu89", "-m32", "-march=i386",
                            "-ffreestanding", "-fno-pie", "-fno-stack-protector",
                            "-nostdlib", "-msoft-float", "-Os", "-Wall",
                            "-Wdeclaration-after-statement", "-D__KERNEL_BUILD__",
                            "-I" + str(ROOT), "-I" + str(ROOT / "include"),
                            "-I" + str(ROOT / "arch/x86"),
                            "-I" + str(ROOT / "platform/pc98"),
                            "-I" + str(ROOT / "sdk/include"),
                            "-I" + str(ROOT / "sdk/include/os32"),
                            "-I" + str(ROOT / "drivers"), "-I" + str(ROOT / "fs"),
                            "-I" + str(ROOT / "lib"),
                            "-I" + str(ROOT / "lib/sqlite3"),
                            "-I" + str(ROOT / "kapi"), "-I" + str(ROOT / "kernel"),
                            "-I" + str(ROOT / "exec"), "-I" + str(ROOT / "gfx"),
                            "-c", str(ROOT / "kapi/kapi_db.c"),
                            "-o", str(tmp / "kapi_db.o")], check=True, cwd=ROOT)
            print("TARGET i386-elf GNU89 compile PASS", flush=True)

        cases = [x for x in sys.argv[1:] if not x.startswith("--")] or CASES
        failed = run_cases(exe, cases)
        print("SUMMARY %d/%d PASS" % (len(cases) - failed, len(cases)), flush=True)
        failed += check_no_block_size()
        if "--mutate" in sys.argv:
            failed += run_mutations(tmp, sqlite_obj, san)
        sys.exit(bool(failed))
