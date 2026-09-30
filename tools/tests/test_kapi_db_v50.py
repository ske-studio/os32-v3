"""S0-K: KAPI v50 のホスト TDD。

実 SQLite + 実 os32 SQLite VFS + 実 FD 表 + 実 kapi_db.c を RAM の
バックエンドに載せて回す。ホストのファイルシステム・sudo・mount・配備・
エミュレータには一切触らない。記録は tools/tests/s0_tdd.md。

  python3 -B tools/tests/test_kapi_db_v50.py [--target] [--sanitize] [case ...]
"""
import pathlib
import re
import subprocess
import sys
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[2]
CASES = ["open_existing", "prepare_only", "binds", "error_code",
         # N2 (c): ブート自己診断 db_v50_selftest() を make check で踏む (F1 回帰)
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
INC = ["-I" + str(ROOT / p) for p in
       ("include", "fs", "drivers", "sdk/include/os32", "lib", "lib/sqlite3")]
CONFIG = str(ROOT / "lib/sqlite3/os32_sqlite_config.h")

SHIMS = {
    # kapi_db.c は SHM を MEM_SHM_BASE で引く。ホストでは試験側の配列へ向ける。
    "memmap.h": "extern unsigned char test_shm[];\n#define MEM_SHM_BASE test_shm\n",
    # exec/exec.c 側の口だけを模型にする (帯と PTE はカーネル番地に依存する)。
    "exec.h": "int ring3_user_range_ok(u32 p, u32 len);\n",
}


def check_reclaim_order():
    """exec_reclaim_owned が DB を FD より先に回収しているか (票 §1c)。

    並び自体は exec/exec.c の中の 1 か所にしかない。ホストでは exec.c を
    そのままリンクできないので、ここは**本文の並び**を読んで固定する。
    順序が入れ替わったら気付ける最小の番人。
    """
    src = (ROOT / "exec/exec.c").read_text(encoding="utf-8")
    m = re.search(r"static void exec_reclaim_owned\(int id\)\s*\{(.*?)\n\}",
                  src, re.S)
    assert m, "exec_reclaim_owned が見つからない"
    body = m.group(1)
    db = body.index("db_cleanup_owned(id);")
    fd = body.index("vfs_close_owned(id);")
    assert db < fd, "exec_reclaim_owned: db_cleanup_owned は vfs_close_owned より先"
    print("ORDER SOURCE: db_cleanup_owned before vfs_close_owned PASS", flush=True)


if __name__ == "__main__":
    check_reclaim_order()
    with tempfile.TemporaryDirectory(prefix="os32-s0k-") as tmp:
        tmp = pathlib.Path(tmp)
        for name, text in SHIMS.items():
            (tmp / name).write_text(text)
        obj = str(tmp / "sqlite.o")
        exe = str(tmp / "dbv50")
        san = (["-fsanitize=address", "-fno-omit-frame-pointer"]
               if "--sanitize" in sys.argv else [])
        subprocess.run(["gcc", "-std=gnu89", "-O0", *san, "-include", CONFIG,
                        "-c", str(ROOT / "lib/sqlite3/sqlite3.c"), "-o", obj],
                       check=True)
        subprocess.run(["gcc", "-std=gnu89", "-Wall", "-Wextra", "-Werror",
                        "-Wdeclaration-after-statement", "-Wno-unused-parameter",
                        "-Wno-sign-compare", "-Wno-pointer-to-int-cast",
                        "-Wno-missing-field-initializers", "-D__cdecl=",
                        *san, "-I" + str(tmp), *INC,
                        str(ROOT / "tools/tests/kapi_db_v50_host.c"), obj,
                        "-o", exe], check=True)
        print("HOST GNU89 -Werror compile PASS (real kapi_db.c + bundled SQLite)",
              flush=True)
        if "--target" in sys.argv:
            for src, extra in ((ROOT / "kapi/kapi_db.c",
                                ["-Ikapi", "-Ikernel", "-Iexec", "-Igfx"]),
                               (ROOT / "exec/exec.c",
                                ["-Iexec", "-Ikapi", "-Igfx", "-Ikernel"]),
                               (ROOT / "kernel/paging.c",
                                ["-Ikernel", "-Iexec", "-Ikapi", "-Igfx"]),
                               (ROOT / "kernel/kselftest.c",
                                ["-Ikernel", "-Iexec", "-Ikapi", "-Igfx"])):
                subprocess.run(["i386-elf-gcc", "-std=gnu89", "-m32", "-march=i386",
                                "-ffreestanding", "-fno-pie", "-fno-stack-protector",
                                "-nostdlib", "-msoft-float", "-Os", "-Wall",
                                "-Wdeclaration-after-statement", "-D__KERNEL_BUILD__",
                                "-I" + str(ROOT), "-I" + str(ROOT / "include"),
                                # include/io.h は契約だけで、実装は固定名
                                # arch_io.h / platform_io.h を引く (順序 3)。
                                "-I" + str(ROOT / "arch/x86"),
                                "-I" + str(ROOT / "platform/pc98"),
                                "-I" + str(ROOT / "sdk/include"),
                                "-I" + str(ROOT / "sdk/include/os32"),
                                "-I" + str(ROOT / "drivers"), "-I" + str(ROOT / "fs"),
                                "-I" + str(ROOT / "lib"),
                                "-I" + str(ROOT / "lib/sqlite3"),
                                *["-I" + str(ROOT / p[2:]) for p in extra],
                                "-c", str(src), "-o", str(tmp / (src.stem + ".o"))],
                               check=True, cwd=ROOT)
            print("TARGET i386-elf GNU89 compile PASS", flush=True)
        cases = [x for x in sys.argv[1:] if not x.startswith("--")] or CASES
        failed = 0
        for case in cases:
            rc = subprocess.run([exe, case], cwd=ROOT).returncode
            print(f"EXIT {case}={rc}", flush=True)
            failed += rc != 0
        print(f"SUMMARY {len(cases)-failed}/{len(cases)} PASS", flush=True)
        sys.exit(bool(failed))
