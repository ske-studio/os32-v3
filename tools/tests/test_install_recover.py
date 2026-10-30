"""S3-I: `install --recover-settings` / `--revert-settings` のホスト TDD。

実 `userland/system/install_recover.inc` + 実 SQLite + 実 os32 SQLite VFS +
実 FD 表 + 実 `kapi/kapi_db.c` を RAM のバックエンドに載せて回す。ホストの
ファイルシステム・sudo・mount・配備・エミュレータには一切触らない。
記録は tools/tests/s3_tdd.md 節 I。

  python3 -B tools/tests/test_install_recover.py [--target] [--sanitize] [case ...]
"""
CHECK = dict(name='install-recover-host', tier=1,
    covers=['fs/vfs_fd.c', 'kapi/kapi_db.c', 'kapi/kapi_db.h', 'kernel/sqlite_engine.h', 'lib/sqlite3/os32_sqlite_vfs.c', 'lib/sqlite3/sqlite3.c', 'tools/tests/install_recover_host.c', 'tools/tests/vfs_fd_sqlite_host.c', 'userland/system/install.c', 'userland/system/install_recover.inc', 'tools/tests/input_fixture_host.h', 'kernel/input_service.c', 'include/pointer_service.h', 'include/input_service.h', 'include/kbd.h'],
    needs=[], budget=8.130882)

import pathlib
import subprocess
import sys
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[2]

CASES = [
    "sqlite_only",  # FD root + actual public db_* gate + master/target SQL
    "media",        # 1a 起動媒体 / 対象デバイス (受入 I1)
    "scan",         # 1b 三値 / UNKNOWN / inode 共有 / .new 残骸
    "gate",         # 1b 印の phase による門と印の往復
    "master",       # 1c マスタの検査と close 失敗
    "master_copy",  # FAT -> ext2 copy/byte check/SQL + failure/cancel cleanup
    "approve",      # 1d 承認しない (受入 I5)
    "happy",        # 1e/1f 正常系 (受入 I2 / I3 / I4)
    "backup_fail",  # 1e 退避の各失敗 (元の対は不変)
    "newfail",      # 1f .new のコピー / 検証 / close 失敗
    "switch",       # 1f journal 除去と rename の 2 つの中途半端 (受入 I6)
    "record",       # 1f(7) sync / reopen / close の記録
    "revert",       # 1g revert の正常系 (受入 I7)
    "revert_fail",  # 1g revert の途中失敗 (phase は reverting のまま)
    "chain",        # 1h 連鎖
    "pure",         # 印の書式・コピー・バイト比較の純関数
]

INC = ["-I" + str(ROOT / p) for p in
       ("include", "fs", "drivers", "kapi", "sdk/include", "sdk/include/os32",
        "lib", "lib/sqlite3", "userland/lib")]
CONFIG = str(ROOT / "lib/sqlite3/os32_sqlite_config.h")

SHIMS = {
    "memmap.h": "extern unsigned char test_shm[];\n#define MEM_SHM_BASE test_shm\n#define MEM_SHM_DB_OFFSET 0UL\n#define MEM_SHM_DB_BASE (MEM_SHM_BASE + MEM_SHM_DB_OFFSET)\n",
    "exec.h": "int ring3_user_range_ok(u32 p, u32 len);\n",
}


def build(tmp, sanitize, recovery_source=None, kapi_source=None):
    for name, text in SHIMS.items():
        (tmp / name).write_text(text)
    obj = str(tmp / "sqlite.o")
    exe = str(tmp / "recoverhost")
    san = (["-fsanitize=address", "-fno-omit-frame-pointer"]
           if sanitize else [])
    subprocess.run(["gcc", "-std=gnu89", "-O0", *san, "-include", CONFIG,
                    "-c", str(ROOT / "lib/sqlite3/sqlite3.c"), "-o", obj],
                   check=True)
    subprocess.run(["gcc", "-std=gnu11", "-Wall", "-Wextra", "-Werror",
                    "-Wno-unused-parameter",
                    "-Wno-sign-compare", "-Wno-pointer-to-int-cast",
                    "-Wno-missing-field-initializers", "-D__cdecl=",
                    "-D__OS32_USERLAND__",
                    *(['-DINSTALL_RECOVER_SOURCE="' + str(recovery_source) + '"']
                      if recovery_source else []),
                    *(['-DINSTALL_KAPI_DB_SOURCE="' + str(kapi_source) + '"']
                      if kapi_source else []),
                    *san, "-I" + str(tmp), *INC,
                    str(ROOT / "tools/tests/install_recover_host.c"), obj,
                    "-o", exe], check=True)
    print("HOST GNU11 -Werror compile PASS "
          "(real install_recover.inc + real kapi_db.c + bundled SQLite)",
          flush=True)
    return exe


def mutations(tmp, sanitize):
    source = (ROOT / "userland/system/install_recover.inc").read_text()
    for label, old, new in (
            ("nosys-open-result", "out->err = h;", "out->err = 0;"),
            ("nosys-master-diagnostic", "if (master.err == OS32_ERR_NOSYS)", "if (0)")):
        assert source.count(old) == 1, label
        path = tmp / "install_recover_mutant.inc"
        path.write_text(source.replace(old, new))
        exe = build(tmp, sanitize, recovery_source=path)
        result = subprocess.run([exe, "master"], cwd=ROOT, capture_output=True)
        assert result.returncode == 1 and b"message missing: SQLite unavailable (MINIMAL)" in result.stderr, (label, result)
        print("MUTATE " + label + " RED (compiled runtime): " +
              result.stderr.decode().splitlines()[0], flush=True)

    for label, old, new, case, failure in (
            ("master-fat-sql", "rc_db_check(ops, rc_path[RC_N_MASTER], &master)",
             "rc_db_check(ops, RC_MASTER, &master)", "sqlite_only", b"run_recover() == 0"),
            ("master-byte-compare", "rc_clone(ops, RC_MASTER, rc_path[RC_N_MASTER])",
             "rc_copy_file(ops, RC_MASTER, rc_path[RC_N_MASTER])", "master_copy",
             b"host_db_open_calls == (fault == 2)"),
            ("master-sql-check", "if (rc_db_check(ops, rc_path[RC_N_MASTER], &master) != 0)",
             "if (rc_db_check(ops, rc_path[RC_N_MASTER], &master) == 12345)",
             "master_copy", b"message missing: master unreadable"),
            ("master-copy-cleanup", "drop_rc = ops->unlink(rc_path[RC_N_MASTER]);",
             "drop_rc = 0;", "master_copy", b"!fx_exists(rc_path[RC_N_MASTER])"),
            ("master-stale-copy", "rc_state[RC_N_MASTER] == RC_PRESENT ||",
             "0 ||", "master_copy", b"run_recover() == 1")):
        assert source.count(old) == 1, label
        path = tmp / "install_recover_mutant.inc"
        path.write_text(source.replace(old, new))
        exe = build(tmp, sanitize, recovery_source=path)
        result = subprocess.run([exe, case], cwd=ROOT, capture_output=True)
        assert result.returncode == 1 and failure in result.stderr, (label, result)
        print("MUTATE " + label + " RED (compiled runtime): " +
              result.stderr.decode().splitlines()[0], flush=True)

    source = (ROOT / "kapi/kapi_db.c").read_text()
    old = "#define DB_MODULE_READY() services_public"
    assert source.count(old) == 1
    path = tmp / "kapi_db_mutant.c"
    path.write_text(source.replace(old, "#define DB_MODULE_READY() (services_public && services_stage == SERVICES_READY)"))
    exe = build(tmp, sanitize, kapi_source=path)
    result = subprocess.run([exe, "sqlite_only"], cwd=ROOT, capture_output=True)
    assert result.returncode == 1 and b"run_recover() == 0" in result.stderr, result
    print("MUTATE sqlite-only-db-gate RED (compiled runtime): " +
          result.stderr.decode().splitlines()[0], flush=True)


def target_compile(tmp):
    """i386-elf でも同じソースが通ること (票 §3)。

    `-Wno-unused-function` は install.c に元からある死んだ静的関数
    (`str_endswith_ci`) の分。回復モードのコード自体はホスト側のビルドが
    `-Wall -Wextra -Werror` (抑制なし) で見ている。
    """
    flags = ["-std=gnu11", "-m32", "-march=i386", "-ffreestanding", "-fno-pie",
             "-fno-stack-protector", "-nostdlib", "-mno-red-zone", "-fcommon",
             "-O2", "-Wall", "-Wextra", "-Werror", "-Wno-unused-function",
             "-D__OS32_USERLAND__",
             "-I.", "-Iinclude", "-Isdk/include", "-Isdk/include/os32",
             "-Iuserland/lib", "-I/usr/local/cross/i386-elf/include"]
    subprocess.run(["i386-elf-gcc", *flags, "-c", "userland/system/install.c",
                    "-o", str(tmp / "install.o")], check=True, cwd=ROOT)
    print("TARGET i386-elf GNU11 compile PASS (install.c + install_recover.inc)",
          flush=True)


if __name__ == "__main__":
    with tempfile.TemporaryDirectory(prefix="os32-s3i-") as tmp:
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
        print(f"SUMMARY {len(cases)-failed}/{len(cases)} PASS", flush=True)
        if "--mutate" in sys.argv and not failed:
            mutations(tmp, "--sanitize" in sys.argv)
        sys.exit(bool(failed))


def run(ctx) -> int:
    import os
    import shlex
    import subprocess
    base_env = dict(os.environ, HOST32_RUNNERS=' '.join(ctx.runners))
    env = dict(base_env)
    argv = ['python3', '-B', 'tools/tests/test_install_recover.py', *shlex.split(('--mutate' if ctx.mutate else ''))]
    rc = subprocess.call(argv, cwd=ctx.root, env=env, close_fds=False)
    if rc:
        return rc
    return 0
