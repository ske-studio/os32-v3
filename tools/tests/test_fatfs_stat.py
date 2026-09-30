"""S3-K: FAT の stat が FR_INVALID_NAME を NOTFOUND と言えるかの回帰試験。
S3I2-K の列挙エラー伝播と、起動ログ (2026-09-25) の「f_close の失敗を
fatfs_vfs_write が返す」も同じ贋物の FatFs で見る。

実物の fs/fatfs_vfs.c をそのまま取り込み、FatFs の f_* / Device / IDE /
kmalloc だけを差し替える。実デバイス・実イメージ・実 FatFs には触らない。

追う事象: FDD ブート (root = FAT、ffconf.h の FF_USE_LFN 0) で
db_open_existing("/etc/settings.db", 0) が SQLITE_IOERR。KAPI v50 の
hot journal 検査は vfs_stat("<path>-journal") の NOTFOUND 以外を IOERR と
するが、"settings.db-journal" は 8.3 に収まらず f_stat が FR_INVALID_NAME を
返していた。
"""
import pathlib
import subprocess
import sys
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[2]
CASES = ["stat_invalid_name_is_notfound",
         "stat_missing_is_notfound",
         "stat_disk_err_is_io",
         "stat_ok_fills_size_mode",
         "get_size_invalid_name_is_notfound",
         "open_paths_keep_inval",
         "v50_journal_probe_on_8_3",
         "list_ok_enumerates_all",
         "list_readdir_error_propagates",
         "list_opendir_error_propagates",
         "list_empty_is_ok",
         "write_close_fail_is_error",
         "write_fail_wins_and_closes_once",
         "bootlog_close_fail_keeps_logs"]
FLAGS = ["-std=gnu89", "-Wall", "-Wextra", "-Werror",
         "-Wno-unused-parameter", "-Wno-sign-compare",
         "-Wdeclaration-after-statement", "-D__cdecl=",
         "-DBOOTLOG_NO_IRQ_LOCK"]     # kernel/bootlog.c の錠をホストで空にする
INCLUDES = ["-I" + str(ROOT / p)
            for p in ("include", "fs", "lib", "kernel", "drivers",
                      "sdk/include/os32")]

if __name__ == "__main__":
    with tempfile.TemporaryDirectory(prefix="os32-fatfsstat-") as tmp:
        exe = pathlib.Path(tmp) / "fatfs-stat-host"
        subprocess.run(["gcc", *FLAGS, *INCLUDES,
                        str(ROOT / "tools/tests/fatfs_stat_host.c"),
                        "-o", str(exe)],
                       cwd=ROOT, check=True)
        print("COMPILE GNU89 -Werror PASS", flush=True)
        failed = 0
        cases = sys.argv[1:] or CASES
        for case in cases:
            rc = subprocess.run([str(exe), case], cwd=ROOT).returncode
            print(f"EXIT {case}={rc}", flush=True)
            failed += rc != 0
        print(f"SUMMARY {len(cases) - failed}/{len(cases)} PASS", flush=True)
        sys.exit(bool(failed))
