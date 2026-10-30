"""票 TASK_DB_ERRSTR: `db_last_error()` / `db_column_text()` の返り先のホスト TDD。

実 SQLite + 実 os32 SQLite VFS + 実 FD 表 + 実 `kapi/kapi_db.c` を RAM の
バックエンドに載せて回す。DB は `:memory:` で開くので、ホストのファイル
システム・sudo・mount・配備・エミュレータには一切触らない。
記録は tools/tests/db_errstr_tdd.md、tools/tests/kapi_db_owned_tdd.md。

  python3 -B tools/tests/test_db_errstr.py [--target] [--sanitize] [--mutate] [case ...]

TT2: 旧 db-v50 の API 25 ケース・回収順の番人・変異 3 本と、
旧 db-owned の寿命/障害注入 8 ケース・所有者ガード変異もここから実行する。

診断文字列で見るのは 2 つ (票 §5)。

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
from kernel_includes import include_flags
import pathlib
import re
import subprocess
import sys
import tempfile

import mutpar

ROOT = pathlib.Path(__file__).resolve().parents[2]
CASES = ["errstr_range", "coltext_range", "errstr_truncate", "result_bound",
         "diag_layout", "module_absent"]
API_CASES = ["group_callers", "open_existing", "prepare_only", "binds", "error_code",
         # 起動時から移した v50 の契約と、実 appslot.h の ID 池の容量を検査
         "v50_selftest",
         # 票 H3 の是正 (2026-09-15): slot 配置の検査が **末尾追記で落ちない**
         # こと。KAPI をもう 1 本足した場合を引数で模して踏む
         "slot_layout_append",
         "shm_bound",
         "user_range", "owner_isolation", "order_new", "order_old",
         # 実装レビュー 往復 1 の blocker 6 件 + TRANSIENT (s0_tdd.md §K)
         "shm_exact", "stat_faults", "journal_mode", "step_no_stmt",
         "path_len", "transient",
         # 実装レビュー 往復 2 の blocker 4 件 (s0_tdd.md §K)
         "sql_tail_sqlite", "prepare_replaces", "materialize_fail",
         "resolve_len",
         # 実装レビュー 往復 3 の blocker (s0_tdd.md §K 2d)
         "resolve_truncate",
         # ユーザー承認の最終往復 (s0_tdd.md §K 2e)
         "resolve_depth",
         # 実機 K2 の回帰 (s0_tdd.md §K 2f)
         "cpl3_paths"]
INC = include_flags(ROOT)
CONFIG = str(ROOT / "lib/sqlite3/os32_sqlite_config.h")

SHIMS = {
    # kapi_db.c は SHM を MEM_SHM_BASE で引く。ホストでは試験側の配列へ向ける。
    "memmap.h": "extern unsigned char test_shm[];\n#define MEM_SHM_BASE test_shm\n#define MEM_SHM_DB_OFFSET 0UL\n#define MEM_SHM_DB_BASE (MEM_SHM_BASE + MEM_SHM_DB_OFFSET)\n",
    # exec/exec.c 側の口だけを模型にする (帯と PTE はカーネル番地に依存する)。
    "exec.h": "int ring3_user_range_ok(u32 p, u32 len);\n",
}

HOST_FLAGS = ["-std=gnu11", "-Wall", "-Wextra", "-Werror",
              "-Wno-unused-parameter",
              "-Wno-sign-compare", "-Wno-pointer-to-int-cast",
              "-Wno-missing-field-initializers", "-D__cdecl="]


def check_reclaim_order():
    """exec_reclaim_resources が DB を FD より先に回収しているか (票 §1c)。

    並び自体は exec/exec.c の中の 1 か所にしかない。ホストでは exec.c を
    そのままリンクできないので、ここは**本文の並び**を読んで固定する。
    順序が入れ替わったら気付ける最小の番人。
    """
    src = (ROOT / "exec/exec.c").read_text(encoding="utf-8")
    m = re.search(r"static void exec_reclaim_resources\(int id\)\s*\{(.*?)\n\}",
                  src, re.S)
    assert m, "exec_reclaim_resources が見つからない"
    body = m.group(1)
    db = body.index("db_cleanup_owned(id);")
    fd = body.index("vfs_close_owned(id);")
    assert db < fd, "exec_reclaim_resources: db_cleanup_owned は vfs_close_owned より先"
    m = re.search(r"static void exec_reclaim_owned\(int id, int kind\)\s*\{(.*?)\n\}", src, re.S)
    assert m, "exec_reclaim_owned が見つからない"
    assert m.group(1).index("exec_reclaim_resources(id);") < m.group(1).index("exec_notify_owned(id, kind);")
    print("ORDER SOURCE: db_cleanup_owned before vfs_close_owned PASS", flush=True)


def run_owned_cases(mutate):
    """Eight lifetime/fault cases need a deterministic SQLite fault boundary."""
    with tempfile.TemporaryDirectory(prefix="os32-db-owned-") as tmp:
        tmp = pathlib.Path(tmp)
        (tmp / "memmap.h").write_text(
            "extern unsigned char test_shm[];\n#define MEM_SHM_BASE test_shm\n#define MEM_SHM_DB_OFFSET 0UL\n#define MEM_SHM_DB_BASE (MEM_SHM_BASE + MEM_SHM_DB_OFFSET)\n")
        (tmp / "kstring.h").write_text(
            "#include <string.h>\n#define kstrncpy(d,s,n) strncpy(d,s,n)\n"
            "char *host_strlcat(char *d, const char *s, unsigned long n);\n"
            "#define kstrncat(d,s,n) host_strlcat(d,s,n)\n"
            "#define kstrlen(s) ((u32)strlen(s))\n"
            "#define kstrcmp(a,b) strcmp(a,b)\n"
            "#define kstrncmp(a,b,n) strncmp(a,b,n)\n"
            "#define kmemcpy(d,s,n) memcpy(d,s,n)\n")
        # v50: kapi_db.c は exec 側のポインタ検証と vfs_stat を使う。
        # どちらもこの票 (F1) の範囲外なので最小の模型で置く。
        (tmp / "exec.h").write_text(
            "int ring3_user_range_ok(u32 p, u32 len);\n")
        (tmp / "kprintf.h").write_text("int kprintf(int color, const char *fmt, ...);\n")
        exe = tmp / "db-owned"
        command = [
            "cc", "-std=gnu11", "-Wall", "-Wextra", "-Werror",
            "-Wno-unused-parameter", "-D__cdecl=", "-I" + str(ROOT / "tools/tests"),
            "-I" + str(tmp), "-I" + str(ROOT / "sdk/include/os32"),
            "-I" + str(ROOT / "include"), "-I" + str(ROOT / "fs"),
            "-I" + str(ROOT / "lib"), "-I" + str(ROOT / "lib/sqlite3"), "-I" + str(ROOT / "kapi"),
            str(ROOT / "tools/tests/kapi_db_owned_host.c"), "-o", str(exe)
        ]
        subprocess.run(command, check=True, cwd=ROOT)
        subprocess.run([str(exe)], check=True, cwd=ROOT)
        if mutate:
            fixture_text = (ROOT / "tools/tests/kapi_db_owned_host.c").read_text()
            paths = ['kapi/kapi_db.c']
            for path in paths:
                source = (ROOT / path).read_text()
                # Mutate each public guard independently: omitted, inverted,
                # or incorrectly applied to TRUSTED calls.
                guards = []
                offset = 0
                for line in source.splitlines(keepends=True):
                    if "ring3_call_from_user() &&" in line:
                        guards.append((offset, line.rstrip("\n")))
                    offset += len(line)
                for index, (begin, guard) in enumerate(guards):
                    for label, new in (
                        ("removed", guard.replace("ring3_call_from_user() &&", "0 &&")),
                        ("inverted", guard.replace("!= res_owner_get()", "== res_owner_get()")),
                        ("trusted-denied", guard.replace("ring3_call_from_user() &&", "")),
                    ):
                        mutated = tmp / pathlib.Path(path).name
                        # Replace only this occurrence (SHM/DB guards repeat).
                        offset = begin + len(guard)
                        mutated.write_text(source[:begin] + new + source[offset:])
                        host = fixture_text
                        for included in paths:
                            target = mutated if included == path else ROOT / included
                            host = host.replace("../../" + included, str(target))
                        host = host.replace('"../../', '"' + str(ROOT) + '/')
                        host_path = tmp / "mutant_host.c"
                        host_path.write_text(host)
                        mutant_command = [str(host_path) if arg.endswith("/kapi_db_owned_host.c") else arg
                                          for arg in command]
                        compiled = subprocess.run(mutant_command, cwd=ROOT,
                                                  capture_output=True, text=True)
                        assert compiled.returncode == 0, compiled.stderr
                        result = subprocess.run([str(exe)], cwd=ROOT,
                                                capture_output=True, text=True)
                        assert result.returncode != 0, f"surviving {path}:{index} {label}"
                        assert "FAIL" in result.stdout or "Assertion" in result.stderr, result.stdout + result.stderr
                        print(f"RED {path}:{index} {label}")


def check_no_block_size(root=ROOT):
    """kapi/kapi_db.c が `DB_SHM_BLOCK_SIZE` を名指ししていないこと (票 §4 の 1)。

    結果データの上限は 5 か所で見ている。1 か所でも 16KB から直接引くと
    結果が診断文を踏み潰すが、**事前検査が先に断つ経路は実行時に見えない**。
    だから「その名前を使わない」を規則そのものとして固定する。上限は
    `DB_SHM_RESULT_LIMIT` (= 診断領域を除いた分) からだけ導く。
    """
    src = (root / "kapi/kapi_db.c").read_text(encoding="utf-8")
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


def build(tmp, tag, sqlite_obj, san, root=ROOT):
    exe = str(tmp / ("dberrstr-" + tag))
    rc = subprocess.run(["gcc", *HOST_FLAGS, *san, "-I" + str(tmp), *INC,
                         str(root / "tools/tests/db_errstr_host.c"), sqlite_obj,
                         "-o", exe])
    return exe if rc.returncode == 0 else None


def build_api(tmp, sqlite_obj, san):
    exe = str(tmp / "db-api")
    subprocess.run(["gcc", *HOST_FLAGS, "-Wno-type-limits", *san,
                    "-I" + str(tmp), *INC,
                    str(ROOT / "tools/tests/kapi_db_v50_host.c"), sqlite_obj,
                    "-o", exe], check=True)
    return exe


def run_api_mutations(tmp, sqlite_obj, exe):
    original = (ROOT / "kapi/kapi_db.c").read_text()
    guard = ("    if (!db_copy_input(sql, sql_copy_buf, SQL_COPY_BUF_SIZE, 1) ||\n"
             "        !sql_copy_buf[0]) return -1;")
    assert original.count(guard) == 3
    vfs_original = (ROOT / "lib/sqlite3/os32_sqlite_vfs.c").read_text()
    appslot = (ROOT / "exec/appslot.h").read_text()
    pool = "#define APP_MAX_APPS     4"
    assert appslot.count(pool) == 1
    mutations = [
        ("DB diagnostics must cover app ID pool", "exec/appslot.h",
         appslot.replace(pool, "#define APP_MAX_APPS     5"), "v50_selftest"),
        ("finalize before copy refusal", "kapi/kapi_db.c", original.replace(guard, "    if (slot->active_stmt) { sqlite3_finalize(slot->active_stmt); slot->active_stmt = 0; }\n" + guard), "prepare_replaces"),
        ("SQL truncation", "kapi/kapi_db.c", original.replace(guard, "    if (!sql) return -1;\n    kstrncpy(sql_copy_buf, sql, SQL_COPY_BUF_SIZE);\n    if (!sql_copy_buf[0]) return -1;"), "prepare_replaces"),
        ("late journal current owner", "lib/sqlite3/os32_sqlite_vfs.c", vfs_original.replace("vfs_open_sqlite(zName, oflags, g->owner,", "vfs_open_sqlite(zName, oflags, res_owner_get(),"), "group_callers"),
    ]
    for name, path, changed, case in mutations:
        mutant = tmp / pathlib.Path(path).name
        mutant.write_text(changed)
        fixture = (ROOT / "tools/tests/kapi_db_v50_host.c").read_text()
        fixture = fixture.replace('../../' + path, str(mutant))
        fixture = fixture.replace('"../../', '"' + str(ROOT) + '/')
        host = tmp / "mutant.c"
        host.write_text(fixture)
        subprocess.run(["gcc", "-std=gnu11", "-D__cdecl=",
                        "-Wno-pointer-to-int-cast", "-I" + str(tmp), *INC,
                        "-I" + str(ROOT / "tools/tests"), "-I" + str(ROOT / "kapi"),
                        str(host), sqlite_obj, "-o", exe], check=True)
        result = subprocess.run([exe, case], capture_output=True, text=True)
        assert result.returncode != 0 and "FAIL" in result.stdout + result.stderr, result
        print("RED (runtime): " + name, flush=True)


def run_cases(exe, cases, root=ROOT):
    failed = 0
    for case in cases:
        rc = subprocess.run([exe, case], cwd=root).returncode
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
    # T4c-1: an unavailable module must return a readable SHM diagnostic.
    ("kapi/kapi_db.c",
     '    if (!DB_MODULE_READY()) return db_shm_diag("SQLite module unavailable");',
     '    if (!DB_MODULE_READY()) return 0;',
     "run"),
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
     "    if (info->data_offset <= 0 || (u32)info->data_offset >= DB_SHM_RESULT_LIMIT)\n"
     "        return db_shm_empty();",
     "    if (info->data_offset <= 0 || (u32)info->data_offset >= DB_SHM_RESULT_LIMIT)\n"
     '        return "";',
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
    """Each mutation lives in an isolated tree, including the static guard input."""
    real = {"tools/tests/db_errstr_host.c", "kapi/kapi_db.c"}
    control = mutpar.mutant_tree(ROOT, tmp / "control-tree", {}, real=real)
    exe = build(tmp, "control", sqlite_obj, san, root=control)
    if exe is None or run_cases(exe, CASES, root=control) or check_no_block_size_quiet(control):
        print("MUTATE CONTROL FAILED", flush=True)
        return 1
    print("MUTATE CONTROL GREEN (isolated tree)", flush=True)
    bad = 0
    for i, (relpath, old, new, kind) in enumerate(MUTATIONS, 1):
        original = (ROOT / relpath).read_text(encoding="utf-8")
        label = "%d %s" % (i, kind)
        if original.count(old) != 1:
            print("MUTATE %-12s SKIP (目印が一意でない)" % label, flush=True)
            bad += 1
            continue
        tree = mutpar.mutant_tree(ROOT, tmp / ("mutant-tree-%d" % i),
                                 {relpath: original.replace(old, new, 1)}, real=real)
        exe = build(tmp, "mut%d" % i, sqlite_obj, san, root=tree)
        if exe is None:
            print("MUTATE %-12s **コンパイルが通らない = 目が働いていない**"
                  % label, flush=True)
            bad += 1
            continue
        fails = run_cases(exe, CASES, root=tree) if kind == "run" else 0
        fails += check_no_block_size_quiet(tree)
        if fails == 0:
            print("MUTATE %-12s **GREEN のまま = 試験が規則を見ていない**"
                  % label, flush=True)
            bad += 1
        else:
            print("MUTATE %-12s RED (期待どおり落ちた: %d 件)"
                  % (label, fails), flush=True)
    return bad


def check_no_block_size_quiet(root=ROOT):
    import io
    import contextlib
    buf = io.StringIO()
    with contextlib.redirect_stdout(buf):
        rc = check_no_block_size(root)
    return rc


if __name__ == "__main__":
    selected = [x for x in sys.argv[1:] if not x.startswith("--")]
    unknown = set(selected) - set(CASES) - set(API_CASES)
    if unknown:
        sys.exit("unknown cases: " + ", ".join(sorted(unknown)))
    api_cases = [x for x in selected if x in API_CASES] if selected else API_CASES
    cases = [x for x in selected if x in CASES] if selected else CASES
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
            sys.exit("HOST GNU11 -Werror compile FAILED")
        print("HOST GNU11 -Werror compile PASS (real kapi_db.c + bundled SQLite)",
              flush=True)

        if "--target" in sys.argv:
            subprocess.run(["i386-elf-gcc", "-std=gnu11", "-m32", "-march=i386",
                            "-ffreestanding", "-fno-pie", "-fno-stack-protector",
                            "-nostdlib", "-msoft-float", "-Os", "-Wall",
                            "-D__KERNEL_BUILD__",
                             *include_flags(ROOT),
                            "-c", str(ROOT / "kapi/kapi_db.c"),
                            "-o", str(tmp / "kapi_db.o")], check=True, cwd=ROOT)
            print("TARGET i386-elf GNU11 compile PASS", flush=True)

        check_reclaim_order()
        api_exe = build_api(tmp, sqlite_obj, san)
        api_failed = run_cases(api_exe, api_cases)
        print("API SUMMARY %d/%d PASS" % (len(api_cases) - api_failed, len(api_cases)), flush=True)
        run_owned_cases("--mutate" in sys.argv)
        failed = run_cases(exe, cases)
        print("SUMMARY %d/%d PASS" % (len(cases) - failed, len(cases)), flush=True)
        failed += api_failed
        failed += check_no_block_size()
        if "--mutate" in sys.argv:
            failed += run_mutations(tmp, sqlite_obj, san)
            run_api_mutations(tmp, sqlite_obj, api_exe)
        sys.exit(bool(failed))
