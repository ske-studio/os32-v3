"""S2-C: libos32cfg / cfg コマンドのホスト TDD。

実 SQLite + 実 os32 SQLite VFS + 実 FD 表 + 実 kapi_db.c を RAM の
バックエンドに載せ、その上に **実 userland/lib/cfg/*.c と実
userland/cmds/cfg.c** をそのまま乗せて回す。ホストのファイルシステム・
sudo・mount・配備・エミュレータには一切触らない (tsv fixture も stdin)。
記録は tools/tests/s2_tdd.md。

  python3 -B tools/tests/test_cfg.py [--target] [--sanitize] [case ...]
"""
import pathlib
import subprocess
import sys
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[2]
MK = ROOT / "tools/mk_settings_db.py"

CASES = [
    # 票 §4 の (1)〜(23)
    "missing",      # (1)
    "corrupt",      # (2)
    "version",      # (3)(12)
    "types",        # (4)(19)
    "roundtrip",    # (5)
    "txn",          # (6)(7)(15)
    "limits",       # (8)
    "enum",         # (9)(14)
    "meta",         # (13)(18)
    "shm_copy",     # (17)
    "init",         # (11)(22)
    "rename",       # (16)(20)
    "args",         # cfg.c の引数解釈・整形 (純関数)
    "cmd",          # (1)(23) と受入 C1〜C4 のホスト版
    "list_big",     # list が溜め場を越えたら閉じて吐いて続きから (票 §2)
    # 実装レビュー 往復 1 の blocker (s2_tdd.md §C)
    "stat_fail",       # ② stat の失敗を「不存在」に丸めない
    "export_guard",    # ③ export の出力先に設定 DB 自身を指定できない
    "txn_poison",      # ④ 検証で断られた set も txn を failed にする
    "fetch_fail",      # ⑤ list / export の値取得の障害を成功にしない
    "nullval",         # ⑥ NULL を空値 / 0 に化けさせない
    "corrupt_schema",  # ⑦ schema 検査中の NOTADB / CORRUPT は CFG_CORRUPT
    "commit_diag",     # ⑧ COMMIT の失敗を rollback の失敗で上書きしない
    "open_close_fail", # ⑨ open 内部の close 失敗を公開 close へ伝える
    "bind_fail",       # ⑩ get の bind 失敗も CFG_ERROR
    "get_many",        # ⑪ 単一 key の get が列挙の上限に引きずられない
    "enum_reenter_get",# ⑫ enum callback 内の get は INVAL
    "out_fail",        # ⑬ 出力の溢れ / short write / 失敗を終了コードへ
    "long_scope",      # ⑭ list の scope を切り詰めない
    "intmin",          # ⑰ INT_MIN の解析で signed overflow を踏まない
    "tsv_many",        # ⑯ 63B の名前が並んでも受理、重複は PRIMARY KEY
    # 実装レビュー 往復 2 の blocker (s2_tdd.md §C2)
    "r2_int_fail",     # 1 整数取得だけの失敗を 0 として出さない
    "r2_txn_error",    # 2 ERROR 状態での set 拒否も txn を failed に
    "r2_close_diag",   # 3 close の失敗が操作診断を上書きしない
    "r2_null_type",    # 4 NULL の export が宣言型を保つ
    "r2_wide",         # 5 64bit 整数 / 未知 type を正当値にしない
    "r2_badval",       # 6 保存済み text/blob の境界と埋込み NUL の key
    "r2_alias",        # 7 export 先の stat 障害を「別ファイル」にしない
    # 実装レビュー 往復 3 の blocker (s2_tdd.md §C3)
    "r3_fat",          # B1 inode を供給しない FS / 大小を区別しない別名
    "r3_enum_write",   # B2 列挙 callback からの書き込み拒否も txn を failed に
    "r3_enum_type",    # B3 列挙も縮小前に type の値域を見る
    "r3_enum_err",     # B4 ERROR 状態の列挙を 0 件にしない
    "r3_tsv_long",     # B5 過長フィールドでカウンタを飽和させる
    # 票 S5-C (記録は tools/tests/s5_tdd.md)
    "s5_pure",         # cfg_bench の引数 / 巡回 / 失敗の数え方 / 集計 / 整形
    "s5_bench",        # cfg_bench の通し (実 DB、yield なし、pool の戻り)
    "s5_pool",         # cfg status の ` pool <n> B`
    # 票 S3-C §2 (記録は tools/tests/s3_tdd.md §C)
    "s3_json",         # JSON reader 単体の受理 / 拒否 (エスケープ / 境界 / base64)
    "s3_roundtrip",    # 実 writer の出力を import に通す往復 (CR / 制御 / blob / null)
    "s3_scope",        # --scope の削除範囲と scope 外の不変、--merge
    "s3_args",         # import の引数解釈と usage
    "s3_reject",       # MISSING / CORRUPT / 版 / 壊れた行 / 重複 / 長すぎ
    "s3_fail",         # 途中失敗の rollback、hash 衝突、commit 後の close 失敗
    "s3_gen",          # 8192 受理 / 8193 拒否、巡回の間の差し替え
    "s3_bulk",         # 8192 件を実 SQLite に 1 transaction で書き切る
]

INC = ["-I" + str(ROOT / p) for p in
       ("include", "fs", "drivers", "sdk/include", "sdk/include/os32",
        "lib", "lib/sqlite3", "userland/lib", "userland/lib/cfg")]
CONFIG = str(ROOT / "lib/sqlite3/os32_sqlite_config.h")

SHIMS = {
    "memmap.h": "extern unsigned char test_shm[];\n#define MEM_SHM_BASE test_shm\n",
    "exec.h": "int ring3_user_range_ok(u32 p, u32 len);\n",
}

# ---------------------------------------------------------------------------
#  tsv fixture — ゲスト側の `cfg init` と tools/mk_settings_db.py の **判定**
#  を揃える。(票 §4 の (10)(21)。fixture は S0-T の test_mk_settings_db.py と
#  同じ形。) 重複 (scope, key) の検出は控えを持たず `settings` の PRIMARY KEY
#  に任せた (レビュー往復 1 の ⑯) ので、突き合わせる単位は reader 単体ではなく
#  「その tsv で DB が作れるか」。各要素は (名前, bytes, 期待 = True なら受理)。
# ---------------------------------------------------------------------------
ZEROS = b"0" * 9000

TSV_FIXTURES = [
    ("defaults", (ROOT / "assets/settings/defaults.tsv").read_bytes(), True),
    ("empty", b"", True),
    ("comments_only", b"# only comments\n\n", True),
    ("trailing_empty_field", b"gshell\ta\ttext\t\n", True),
    ("no_final_newline", b"gshell\ta\tint\t1", True),
    ("leading_zeros", b"gshell\ta\tint\t" + ZEROS + b"\n", True),
    ("leading_zeros_neg", b"gshell\ta\tint\t-" + ZEROS + b"\n", True),
    ("leading_zeros_max", b"gshell\ta\tint\t" + ZEROS + b"2147483647\n", True),
    ("leading_zeros_min", b"gshell\ta\tint\t-" + ZEROS + b"2147483648\n", True),
    ("leading_zeros_over", b"gshell\ta\tint\t" + ZEROS + b"2147483648\n", False),
    ("int32_bounds", b"gshell\ta\tint\t2147483647\ngshell\tb\tint\t-2147483648\n", True),
    ("int32_over", b"gshell\ta\tint\t2147483648\n", False),
    ("int32_under", b"gshell\ta\tint\t-2147483649\n", False),
    ("int_hex", b"gshell\ta\tint\t0x10\n", False),
    ("int_space", b"gshell\ta\tint\t 1\n", False),
    ("int_plus", b"gshell\ta\tint\t+1\n", False),
    ("int_empty", b"gshell\ta\tint\t\n", False),
    ("invalid_utf8", b"gshell\ta\ttext\t\xff\xfe\n", False),
    ("nul_in_text", b"gshell\ta\ttext\tx\x00y\n", False),
    ("nul_in_scope", b"gsh\x00ell\ta\ttext\tx\n", False),
    ("nul_in_comment", b"# c\x00mment\ngshell\ta\tint\t1\n", True),
    ("key_upper", b"gshell\tDesktop\tint\t1\n", False),
    ("key_trailing_slash", b"gshell\tdesktop/\tint\t1\n", False),
    ("key_leading_slash", b"gshell\t/desktop\tint\t1\n", False),
    ("key_space", b"gshell\tdesk top\tint\t1\n", False),
    ("key_empty", b"gshell\t\tint\t1\n", False),
    ("key_64", b"gshell\t" + b"a" * 64 + b"\tint\t1\n", False),
    ("key_63", b"gshell\t" + b"a" * 63 + b"\tint\t1\n", True),
    ("scope_upper", b"SYSTEM\ta\tint\t1\n", False),
    ("scope_app_empty", b"app:\ta\tint\t1\n", False),
    ("scope_app_space", b"app:My App\ta\tint\t1\n", False),
    ("scope_empty", b"\ta\tint\t1\n", False),
    ("scope_app_64", b"app:" + b"n" * 64 + b"\ta\tint\t1\n", False),
    ("type_upper", b"gshell\ta\tINT\t1\n", False),
    ("type_string", b"gshell\ta\tstring\tx\n", False),
    ("dup_key", b"gshell\ta\tint\t1\ngshell\ta\tint\t2\n", False),
    ("cols_3", b"gshell\ta\tint\n", False),
    ("cols_5", b"gshell\ta\tint\t1\textra\n", False),
    ("cols_space", b"gshell a int 1\n", False),
    ("cr_row", b"gshell\ta\tint\t1\r\n", False),
    ("cr_text", b"gshell\ta\ttext\tx\r\n", False),
    ("cr_comment", b"# comment\r\ngshell\ta\ttext\tx\n", False),
    ("text_255", b"gshell\ta\ttext\t" + b"x" * 255 + b"\n", True),
    ("text_256", b"gshell\ta\ttext\t" + b"x" * 256 + b"\n", False),
    ("text_kanji_258", "gshell\ta\ttext\t{}\n".format("あ" * 86).encode(), False),
    ("text_kanji_255", "gshell\ta\ttext\t{}\n".format("あ" * 85).encode(), True),
    ("blob_odd", b"gshell\ta\tblob\t0f0\n", False),
    ("blob_space", b"gshell\ta\tblob\t00 ff\n", False),
    ("blob_bad", b"gshell\ta\tblob\t00zz\n", False),
    ("blob_over", b"gshell\ta\tblob\t" + b"ab" * 4097 + b"\n", False),
    # 最大 blob 行 (8192 文字 + scope/key/type/タブ/改行 = 8328B)
    ("blob_4096", b"gshell\twindow/main\tblob\t" + b"ab" * 4096 + b"\n", True),
    # 長いコメント + その中の不正 UTF-8 / CR
    ("long_comment", b"#" + b"c" * 9000 + b"\ngshell\ta\tint\t1\n", True),
    ("long_comment_utf8", b"#" + b"c" * 9000 + b"\xff\ngshell\ta\tint\t1\n", False),
    ("comment_overlong_nul", b"# \xc0\x80\ngshell\ta\tint\t1\n", False),
    ("comment_surrogate", b"# \xed\xa0\x80\ngshell\ta\tint\t1\n", False),
    ("text_surrogate", b"gshell\ta\ttext\t\xed\xa0\x80\n", False),
    ("text_4byte", b"gshell\ta\ttext\t\xf0\x9f\x98\x80\n", True),
    ("text_truncated", b"gshell\ta\ttext\t\xe3\x81\n", False),
]


def build(tmp, sanitize):
    for name, text in SHIMS.items():
        (tmp / name).write_text(text)
    obj = str(tmp / "sqlite.o")
    exe = str(tmp / "cfghost")
    # 符号付き桁あふれの検査も付ける: S5 の cfg_bench は設定値 (INT_MAX まで
    # 合法) を畳むので、`+=` の未定義動作は ASan では見えない (票 S5 レビュー
    # 往復 1 の B1)。`undefined` を丸ごと付けると kapi_db.c の SHM レイアウト
    # (ゲストでは詰めたバイト列) が alignment 検査に引っかかるので、この 1 種
    # だけを有効にする。
    san = (["-fsanitize=address,signed-integer-overflow",
            "-fno-sanitize-recover=signed-integer-overflow",
            "-fno-omit-frame-pointer"]
           if sanitize else [])
    subprocess.run(["gcc", "-std=gnu89", "-O0", *san, "-include", CONFIG,
                    "-c", str(ROOT / "lib/sqlite3/sqlite3.c"), "-o", obj],
                   check=True)
    subprocess.run(["gcc", "-std=gnu89", "-Wall", "-Wextra", "-Werror",
                    "-Wdeclaration-after-statement", "-Wno-unused-parameter",
                    "-Wno-sign-compare", "-Wno-pointer-to-int-cast",
                    "-Wno-missing-field-initializers", "-D__cdecl=",
                    "-D__OS32_USERLAND__",
                    *san, "-I" + str(tmp), *INC,
                    str(ROOT / "tools/tests/cfg_host.c"), obj,
                    "-o", exe], check=True)
    print("HOST GNU89 -Werror compile PASS "
          "(real libos32cfg + real cfg.c + real kapi_db.c + bundled SQLite)",
          flush=True)
    return exe


def target_compile(tmp):
    """i386-elf でも同じソースが -Werror で通ること (票 §3)。"""
    flags = ["-std=gnu89", "-m32", "-march=i386", "-ffreestanding", "-fno-pie",
             "-fno-stack-protector", "-nostdlib", "-mno-red-zone", "-fcommon",
             "-O2", "-Wall", "-Wextra", "-Werror",
             "-Wdeclaration-after-statement", "-D__OS32_USERLAND__",
             "-I.", "-Iinclude", "-Isdk/include", "-Isdk/include/os32",
             "-Iuserland/lib", "-I/usr/local/cross/i386-elf/include"]
    srcs = ["userland/lib/cfg/libos32cfg.c", "userland/lib/cfg/cfg_enum.c",
            "userland/lib/cfg/cfg_tsv.c", "userland/lib/cfg/cfg_init.c",
            "userland/lib/cfg/cfg_json.c", "userland/lib/cfg/cfg_import.c",
            "userland/lib/cfg/cfg_backend.c", "userland/cmds/cfg.c",
            "userland/tests/cfg_bench.c"]
    for src in srcs:
        extra = ["-Iuserland/lib/cfg"] if src.startswith("userland/lib") else []
        subprocess.run(["i386-elf-gcc", *flags, *extra, "-c", src,
                        "-o", str(tmp / (pathlib.Path(src).stem + ".target.o"))],
                       check=True, cwd=ROOT)
    print("TARGET i386-elf GNU89 -Werror compile PASS", flush=True)


def tsv_parity(exe):
    """同じ fixture を `cfg init` と mk_settings_db.py に通し、判定を揃える。"""
    bad = 0
    with tempfile.TemporaryDirectory(prefix="os32-s2-tsv-") as td:
        td = pathlib.Path(td)
        for name, data, want_ok in TSV_FIXTURES:
            c = subprocess.run([exe, "tsv"], input=data,
                               stdout=subprocess.PIPE, cwd=ROOT)
            c_out = c.stdout.decode().strip()
            c_ok = c.returncode == 0 and c_out.startswith("TSV ACCEPT")

            src = td / (name + ".tsv")
            src.write_bytes(data)
            out = td / (name + ".db")
            py = subprocess.run([sys.executable, "-B", str(MK),
                                 "--tsv", str(src), "--out", str(out),
                                 "--epoch", "0"],
                                stdout=subprocess.PIPE, stderr=subprocess.PIPE)
            py_ok = py.returncode == 0

            if c_ok != py_ok or c_ok != want_ok:
                print("FAIL tsv %-22s C=%-6s py=%-6s want=%-6s  %s | %s"
                      % (name, c_ok, py_ok, want_ok, c_out,
                         py.stderr.decode().strip()[:90]), flush=True)
                bad += 1
    print("TSV PARITY %d/%d PASS (cfg init == mk_settings_db.py)"
          % (len(TSV_FIXTURES) - bad, len(TSV_FIXTURES)), flush=True)
    return bad


if __name__ == "__main__":
    with tempfile.TemporaryDirectory(prefix="os32-s2c-") as tmp:
        tmp = pathlib.Path(tmp)
        exe = build(tmp, "--sanitize" in sys.argv)
        if "--target" in sys.argv:
            target_compile(tmp)
        cases = [x for x in sys.argv[1:] if not x.startswith("--")] or CASES
        failed = 0
        for case in cases:
            rc = subprocess.run([exe, case], cwd=ROOT).returncode
            print(f"EXIT {case}={rc}", flush=True)
            failed += rc != 0
        failed += tsv_parity(exe)
        print(f"SUMMARY {len(cases)-failed}/{len(cases)} PASS", flush=True)
        sys.exit(bool(failed))
