"""票 TASK_VFS_FD_PATH: FD は inode で動き名前空間の変更で失効する / 長いパスは
切り詰めずに断る / パッケージの長さ / newlib の errno。

票:   docs/archive/kernel_v21/TASK_VFS_FD_PATH.md
記録: tools/tests/vfs_fd_path_tdd.md

tools/tests/vfs_fd_path_host.c が実物の fs/ext2_*.c / vfs.c / vfs_fd.c と
userland/lib/rt/pkg.c を取り込み、RAM 上の 8MB の ext2 で

  fd    … 欠陥 1 の反例・inode の再利用・親の rename・置き換え rename (注入)・
          ハードリンク・inode 取得の失敗注入・umount・ISDIR・O_EXCL
  busy  … 開いている SQLite DB / ジャーナル / 祖先の rename、loop イメージ、
          失効 (unlink) しても接続が閉じるまでは BUSY (レビュー ラリー 1 の B3)
  nocase … 大文字小文字を区別しない FS (name_fold) の BUSY / pinned (B4)
  cdinst … 実物の userland/system/cdinst.c の install_packages が、どの
          パッケージの失敗でも止まり完了を出さない
  dot   … 最終要素の "." / ".."
  path  … 255/256 バイト、32/33 要素、相対パス + 長い cwd、mount prefix
  pkg   … pkg_parse / pkg_first_overflow / pkg_extract (PKG はここで作る)

を回し、像を本物の `e2fsck -fn` に当てる (**0 以外は失敗**、無ければ SKIP と明示)。
ほかに

  mkpkg … 実物の tools/mkpkg.py が 123/124 バイト (UTF-8)・128/129 項目で断る
  errno … 実物の sdk/crt/syscalls.c を newlib のヘッダでホストに組み、
          OS32 の負値が -1 + errno になる (tools/tests/newlib_errno_host.c)
  ime   … 実物の kernel/ime_dict.c + SQLite + os32_sqlite_vfs.c + vfs_fd.c で、
          辞書の FD が失効したら接続ごと開き直す・1 回だけ・hot journal なら
          開かない・旧接続で SQL を流す前に失効を見る (B1)・管理 API も同じ
          (B5)・SQLite のファイルメソッドは失効で成功しない (B2)・長すぎる
          DB 名は開く時点で断る (tools/tests/ime_dict_host.c)
  fatfold … fs/fatfs_vfs.c の大文字化表が ff.c の TBL_CT437 と一致する
  namerule / fatname / hostname … FatFs・Win32 が意味を変える名前 ("disk.img "、
          "d\\img.dat"、"db." …) を入口で INVAL で断る (ラリー 2 の blocker)。
          fatname は実物の fs/fatfs_vfs.c + fs/fatfs/ff.c を RAM の FAT12 で回す
          (FAT を "/" にマウントした 253/254/255 バイトと rmdir の種別 — ラリー 3)
  utf8  … Win32 の名前の規則と lib/kutf16.c の変換が同じ集合を通し 1 対 1 (ラリー 3 B2)
  hostwire … fs/hostdrvfs.c の入口検査の配線 (本文の検査。ホストで組めないため)

  python3 -B tools/tests/test_vfs_fd_path.py [--target] [--mutants] [case]

--target  触った fs/*.c を i386-elf-gcc で -Werror 単体コンパイル ([C1])。
--mutants fs/ と userland/lib/rt/ の**写し**に変異を 1 つずつ当てて組み直し、
          どれも落ちることを見る。実物のソースは書き換えない。
--red=REV 修正前 (REV、既定 6de1fea) の fs/ と userland/lib/rt/ を git から
          取り出し、-DFDP_RED で組んで回す。**落ちること**を確かめて記録を出す
          (失効の印・inode の口を直接見る検査と注入は RED では組まない)。
"""
import os
import pathlib
import shutil
import struct
import subprocess
import sys
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))
import mkpkg  # noqa: E402  (lzss_encode と定数を借りる)
sys.path.insert(0, str(ROOT / "tools/tests"))
import mutpar  # noqa: E402  (tools/tests/mutpar.py)

HOST_FLAGS = ["-std=gnu89", "-m32", "-march=i386", "-ffreestanding", "-fno-pie",
              "-fno-stack-protector", "-nostdlib", "-static", "-O1",
              "-Wall", "-Wextra", "-Werror",
              "-Wno-unused-parameter", "-Wno-sign-compare",
              "-Wdeclaration-after-statement", "-D__cdecl="]
SRC = ROOT / "tools/tests/vfs_fd_path_host.c"
ERRNO_SRC = ROOT / "tools/tests/newlib_errno_host.c"
SYSCALLS_SRC = ROOT / "sdk/crt/syscalls.c"
TARGET_SRCS = ["fs/vfs.c", "fs/vfs_fd.c", "fs/ext2_vfs.c", "fs/ext2_file.c",
               "fs/fatfs_vfs.c", "fs/hostdrvfs.c", "fs/iso9660.c", "lib/kutf16.c"]
TARGET_FLAGS = ["-std=gnu89", "-m32", "-march=i386", "-ffreestanding",
                "-fno-pie", "-fno-stack-protector", "-nostdlib",
                "-mno-red-zone", "-fcommon", "-O2",
                "-Wall", "-Wextra", "-Werror",
                "-Wdeclaration-after-statement",
                "-Wno-sign-compare", "-Wno-unused-parameter",
                "-Wno-address-of-packed-member",
                "-D__KERNEL_BUILD__", "-I.", "-Iinclude",
                "-Iarch/x86", "-Iplatform/pc98", "-Isdk/include",
                "-Isdk/include/os32", "-Ikernel", "-Idrivers", "-Inet",
                "-Ifs", "-Iexec", "-Igfx", "-Ilib", "-Ikapi"]

# 変異: (名前, 写しの中のファイル, 置き換え前, 置き換え後)。どれも**落ちなければならない**。
MUTANTS = [
    ("write が失効の印を見ない", "fs/vfs_fd.c",
     "    if (f->stale) return VFS_ERR_STALE;   /* 書かない。fs_ctx より先に見る */\n",
     ""),
    ("read が失効の印を見ない", "fs/vfs_fd.c",
     "    if (f->stale) return VFS_ERR_STALE;   /* fs_ctx より先に見る */\n",
     ""),
    ("fstat が失効の印を見ない", "fs/vfs_fd.c",
     "    if (open_files[fd].stale) return VFS_ERR_STALE;\n",
     ""),
    ("seek が失効の印を見ない", "fs/vfs_fd.c",
     "    if (f->stale) return VFS_ERR_STALE;\n\n    if (whence == SEEK_SET) {",
     "\n    if (whence == SEEK_SET) {"),
    ("FD がパスで書く (inode を使わない)", "fs/vfs_fd.c",
     "    open_files[fd].has_ino = ops->ino ? 1 : 0;",
     "    open_files[fd].has_ino = 0;"),
    ("unlink が印を付けない", "fs/vfs.c",
     "    if (has_ino) vfs_fd_invalidate_ino(fs_ctx, ino);\n    return rc;",
     "    return rc;"),
    ("置き換え rename が印を付けない", "fs/vfs.c",
     "    if (has_new && has_old) vfs_fd_invalidate_ino(old_ctx, new_ino);",
     ""),
    ("rename の印を成功時だけにする", "fs/vfs.c",
     "    if (has_new && has_old) vfs_fd_invalidate_ino(old_ctx, new_ino);",
     "    if (rc == VFS_OK && has_new && has_old) vfs_fd_invalidate_ino(old_ctx, new_ino);"),
    ("rename(f,f) にも印を付ける", "fs/vfs.c",
     "    if (has_new && has_old && old_ino == new_ino) has_new = 0;\n",
     ""),
    ("umount が印を付けない", "fs/vfs.c",
     "            vfs_fd_invalidate_mount(mounts[i].fs_ctx);\n",
     ""),
    ("inode 取得の失敗で消してしまう", "fs/vfs.c",
     "    rc = vfs_target_ino(ops, fs_ctx, rel_path, &has_ino, &ino);\n    if (rc != VFS_OK) return rc;",
     "    rc = vfs_target_ino(ops, fs_ctx, rel_path, &has_ino, &ino);\n    has_ino = has_ino && rc == VFS_OK;"),
    ("open が作ったものを消さない", "fs/vfs_fd.c",
     "            if (created && ops->unlink) (void)ops->unlink(fs_ctx, rel_path);\n",
     ""),
    ("SQLite の rename を断らない", "fs/vfs.c",
     "    if (vfs_fd_rename_busy(old_ctx, old_rel, new_rel)) return VFS_ERR_BUSY;",
     ""),
    ("ジャーナル名を見ない", "fs/vfs_fd.c",
     "    return sfx[j] == '\\0' && rel[i + j] == '\\0';",
     "    return 0;"),
    ("loop イメージの unlink を断らない", "fs/vfs.c",
     "    if (vfs_fd_pinned_busy(fs_ctx, has_ino, ino, rel_path)) return VFS_ERR_BUSY;",
     ""),
    ("ext2_write_stream がディレクトリに書く", "fs/ext2_file.c",
     "    if ((inode.mode & EXT2_S_IFMT) != EXT2_S_IFREG) return EXT2_ERR_ISDIR;\n\n    now = ext2_current_time();\n    inode.mtime = now;\n",
     "\n    now = ext2_current_time();\n    inode.mtime = now;\n"),
    ("ext2_read_stream がディレクトリを読む", "fs/ext2_file.c",
     "    if ((inode.mode & EXT2_S_IFMT) != EXT2_S_IFREG) return EXT2_ERR_ISDIR;\n\n    if (offset >= inode.size) return 0;",
     "\n    if (offset >= inode.size) return 0;"),
    ("O_EXCL のマウント点を EXIST にしない", "fs/vfs_fd.c",
     "        if (rel_path[0] == '/' && rel_path[1] == '\\0') return VFS_ERR_EXIST;\n",
     ""),
    ("入力の長さを先に見ない", "fs/vfs.c",
     "    if (in_len >= VFS_MAX_PATH) return VFS_ERR_NAMETOOLONG;\n",
     ""),
    ("溢れた要素を黙って捨てる", "fs/vfs.c",
     "            if (num_parts >= VFS_MAX_PATH_DEPTH) return VFS_ERR_NAMETOOLONG;\n            parts[num_parts] = &tmp[start];\n            part_len[num_parts] = len;\n            num_parts++;",
     "            if (num_parts < VFS_MAX_PATH_DEPTH) { parts[num_parts] = &tmp[start];\n            part_len[num_parts] = len;\n            num_parts++; }"),
    ("結果の長さを見ない (切り詰める)", "fs/vfs.c",
     "    if (o + 1 > out_size) return VFS_ERR_NAMETOOLONG;\n",
     "    if (o + 1 > out_size) { output[0] = '/'; output[1] = '\\0'; return VFS_OK; }\n"),
    ("rmdir の末尾 . を見ない", "fs/vfs.c",
     "    /* `rmdir a/.` を正規化すると a 自体を消す。正規化の前に断る */\n    if (vfs_last_is_dot(path)) return VFS_ERR_INVAL;\n",
     ""),
    ("rename の宛先の末尾 .. を見ない", "fs/vfs.c",
     "    if (vfs_last_is_dot(oldpath) || vfs_last_is_dot(newpath)) return VFS_ERR_INVAL;",
     "    if (vfs_last_is_dot(oldpath)) return VFS_ERR_INVAL;"),
    ("mount prefix を切り詰めて登録する", "fs/vfs.c",
     "    if (i >= VFS_MAX_PATH) return VFS_ERR_NAMETOOLONG;\n\n    rc = vfs_dev_parse",
     "\n    rc = vfs_dev_parse"),
    ("pkg_parse が長いパスを切り詰める", "lib/rt/pkg.c",
     "        if (path_len >= PKG_MAX_PATH) {\n            api->sys_close(fd);\n            return PKG_ERR_TOOLONG;\n        }",
     "        if (path_len >= PKG_MAX_PATH) path_len = PKG_MAX_PATH - 1;"),
    ("pkg_parse が 129 項目目を捨てる", "lib/rt/pkg.c",
     "        if (info->entry_count >= PKG_MAX_ENTRIES) {\n            api->sys_close(fd);\n            return PKG_ERR_CORRUPT;\n        }",
     "        if (info->entry_count >= PKG_MAX_ENTRIES) break;"),
    ("pkg_parse が切れた表を成功にする", "lib/rt/pkg.c",
     "        if (rd != 1) { api->sys_close(fd); return PKG_ERR_CORRUPT; }",
     "        if (rd != 1) break;"),
    ("前置の検査が 1 バイト甘い", "lib/rt/pkg.c",
     "        if (prefix_len + n + 1 > PKG_MAX_PATH) return i;",
     "        if (prefix_len + n > PKG_MAX_PATH) return i;"),
    ("pkg_extract が開けない失敗を飲む", "lib/rt/pkg.c",
     "            if (wfd < 0) { result = PKG_ERR_IO; break; }",
     "            if (wfd < 0) continue;"),
    ("失効した SQLite FD を BUSY から外す", "fs/vfs_fd.c",
     "        if (!f->in_use || !f->sqlite_db || f->fs_ctx != fs_ctx) continue;",
     "        if (!f->in_use || !f->sqlite_db || f->stale || f->fs_ctx != fs_ctx) continue;"),
    ("名前比較が FS の規則を見ない", "fs/vfs_fd.c",
     "    return (ops && ops->name_fold) ? ops->name_fold((u8)c) : (u8)c;",
     "    return (u8)c;"),
    ("cdinst が NORMAL の失敗で止まらない", "system/cdinst.c",
     '        ret = install_series(PKG_BASE_NORMAL);\n        if (ret != PKG_OK) return ret;',
     '        ret = install_series(PKG_BASE_NORMAL);'),
    ("cdinst が GUI の失敗で止まらない", "system/cdinst.c",
     '        ret = install_series(PKG_BASE_GUI);\n        if (ret != PKG_OK) return ret;',
     '        ret = install_series(PKG_BASE_GUI);'),
    ("cdinst が分割の途中の失敗で止まらない", "system/cdinst.c",
     '        ret = install_step(path, label);\n        if (ret != PKG_OK) return ret;',
     '        ret = install_step(path, label);\n        (void)ret;'),
    ("cdinst が媒体に無いパッケージを黙って飛ばす", "system/cdinst.c",
     '        return PKG_ERR_IO;\n    }\n    for (n = 1;',
     '        return PKG_OK;\n    }\n    for (n = 1;'),
    ("cdinst が失敗しても完了を出す", "system/cdinst.c",
     "    ret = install_series(PKG_BASE_MINIMAL);\n    if (ret != PKG_OK) return ret;",
     "    ret = install_series(PKG_BASE_MINIMAL);\n    if (ret != PKG_OK) ret = PKG_OK;"),
    ("FAT の入口が名前を見ない", "fs/fatfs_vfs.c",
     "    rc = vfs_name_rule_check(path, VFS_NAME_RULE_FAT);\n",
     "    rc = VFS_OK; (void)path;\n"),
    ("名前の規則が '\\' を通す", "fs/vfs_name_rules.inc",
     "        if (c == '\\\\') return VFS_ERR_INVAL;\n", ""),
    ("名前の規則が制御文字を通す", "fs/vfs_name_rules.inc",
     "        if (c < 0x20) return VFS_ERR_INVAL;\n", ""),
    ("FAT が途中の空白を通す", "fs/vfs_name_rules.inc",
     "        if (c == ' ' && rule == VFS_NAME_RULE_FAT) return VFS_ERR_INVAL;\n", ""),
    ("名前の規則が末尾の空白を通す", "fs/vfs_name_rules.inc",
     "                if (last == ' ') return VFS_ERR_INVAL;\n", ""),
    ("名前の規則が末尾の '.' を通す", "fs/vfs_name_rules.inc",
     "if (last == '.' && !dot_entry)", "if (last == '.' && !dot_entry && 0)"),
    ("Win32 でも途中の空白を断る", "fs/vfs_name_rules.inc",
     "if (c == ' ' && rule == VFS_NAME_RULE_FAT)", "if (c == ' ')"),
    ("'.' / '..' の要素まで断る", "fs/vfs_name_rules.inc",
     "if (last == '.' && !dot_entry)", "if (last == '.' && (dot_entry || 1))"),
    # ---- 実装レビュー ラリー 3 (B1 FAT の長いパス / rmdir、B2 HostDrv の名前) ----
    ("FAT が \"0:\" 付きの長さを見ない (切り詰める)", "fs/fatfs_vfs.c",
     "    if (max < 0 || need > (u32)max) return VFS_ERR_NAMETOOLONG;",
     "    if (max < 0 || need > (u32)max + 16u) return VFS_ERR_NAMETOOLONG;"),
    ("FAT の rmdir が種別を見ない", "fs/fatfs_vfs.c",
     "    if (!(fno.fattrib & AM_DIR)) return VFS_ERR_NOTDIR;\n", ""),
    ("vfs_rmdir が pinned を見ない", "fs/vfs.c",
     "    if (!ops->ino && vfs_fd_pinned_busy(fs_ctx, 0, 0, rel_path))\n        return VFS_ERR_BUSY;\n",
     ""),
    ("Win32 の規則が ':' を通す", "fs/vfs_name_rules.inc",
     "if (c == ':' || c == '*'", "if (c == '*'"),
    ("Win32 の規則が UTF-8 を見ない", "fs/vfs_name_rules.inc",
     "            if (n == 0) return VFS_ERR_INVAL;\n", "            if (n == 0) n = 1;\n"),
    ("Win32 の規則が 2 バイトの冗長形を通す", "fs/vfs_name_rules.inc",
     "    if (c >= 0xC2 && c <= 0xDF)", "    if (c >= 0xC0 && c <= 0xDF)"),
    ("Win32 の規則が 3 バイトの冗長形を通す", "fs/vfs_name_rules.inc",
     "        if (c == 0xE0 && p[1] < 0xA0) return 0;", ""),
    ("Win32 の規則がサロゲートを通す", "fs/vfs_name_rules.inc",
     "        if (c == 0xED && p[1] >= 0xA0) return 0;", ""),
    ("Win32 の規則が途中で切れた列を通す", "fs/vfs_name_rules.inc",
     "        return ((p[1] & 0xC0) == 0x80) ? 2 : 0;", "        return p[1] ? 2 : 1;"),
    ("FAT にも UTF-8 の規則をかける", "fs/vfs_name_rules.inc",
     "        if (rule == VFS_NAME_RULE_WIN32) {", "        if (1) {"),
    ("変換が途中で切れた列で打ち切る", "lib/kutf16.c",
     "            if ((s[1] & 0xC0) != 0x80) return -1;\n            cp = ((u32)(c & 0x1F) << 6)",
     "            if ((s[1] & 0xC0) != 0x80) break;\n            cp = ((u32)(c & 0x1F) << 6)"),
    ("変換が 3 バイトの冗長形を受理する", "lib/kutf16.c",
     "            if (cp < 0x800) return -1;                      /* 冗長 */\n", ""),
    ("変換がサロゲートを受理する", "lib/kutf16.c",
     "            if (cp >= 0xD800 && cp <= 0xDFFF) return -1;    /* サロゲート */\n", ""),
    ("変換が BMP 外を U+FFFD にする", "lib/kutf16.c",
     "             * (BMP 外: UTF-16 ではサロゲートペアが要るが未対応) */\n            return -1;",
     "             * (BMP 外: UTF-16 ではサロゲートペアが要るが未対応) */\n            s++; while ((*s & 0xC0) == 0x80) s++; cp = 0xFFFD;"),
    ("変換が収まらない入力を切り詰める", "lib/kutf16.c",
     "        if (out >= max_words - 1) return -1;   /* 収まらない: 切り詰めない */",
     "        if (out >= max_words - 1) break;"),
    ("pkg_extract (LZSS) が開けない失敗を飲む", "lib/rt/pkg.c",
     "            if (wfd < 0) {\n                api->mem_free(data_buf);\n                return PKG_ERR_IO;\n            }",
     "            if (wfd < 0) continue;"),
]


# sdk/crt/syscalls.c の変異 (errno の段で落ちること)
ERRNO_MUTANTS = [
    ("STALE を ESTALE にしない", "    case OS32_ERR_STALE:       return ESTALE;\n", ""),
    ("NAMETOOLONG を ENAMETOOLONG にしない",
     "    case OS32_ERR_NAMETOOLONG: return ENAMETOOLONG;\n", ""),
    ("_write が負値をそのまま返す",
     "    return os32_ret(kapi->sys_write(fd, ptr, len));",
     "    return kapi->sys_write(fd, ptr, len);"),
    ("_unlink が負値をそのまま返す",
     "    return os32_ret(kapi->sys_unlink(name));",
     "    return kapi->sys_unlink(name);"),
]


# kernel/ime_dict.c の変異 (ime の段で落ちること)
IME_MUTANTS = [
    ("hot journal を見ずに開き直す",
     "    if (rc != OS32_ERR_NOTFOUND && !(rc == 0 && st.st_size == 0)) {",
     "    if (0) {"),
    ("I/O エラーで何度でも開き直す",
     "    if (!stale && dict->io_retried) return 0;\n", ""),
    ("失効しても開き直さない",
     "    if (io_error && dict_recover(dict)) {",
     "    if (0 && io_error && dict_recover(dict)) {"),
    ("旧接続に SQL を流す前に失効を見ない",
     "    if (fd >= 0 && vfs_fd_is_stale(fd)) (void)dict_recover(dict);",
     "    if (fd >= 0 && 0) (void)dict_recover(dict);"),
    ("export が step の異常終了を EOF にする",
     "    if (result == 0 && step_rc != SQLITE_DONE) {", "    if (0) {"),
    ("clear が旧接続で先に SQL を流す",
     "    if (!dict_ready(dict)) return -1;\n    rc = sqlite3_exec",
     "    if (!dict->db) return -1;\n    rc = sqlite3_exec"),
    ("list が I/O エラーで開き直さない",
     "    if (io_error && dict_recover(dict))\n        n = user_list_once",
     "    if (0)\n        n = user_list_once"),
    ("export が I/O エラーで開き直さない",
     "    if (io_error && dict_recover(dict))\n        rc = user_export_once",
     "    if (0)\n        rc = user_export_once"),
    ("delete が I/O エラーで開き直さない",
     "    if (io_error && dict_recover(dict))\n        rc = user_delete_once",
     "    if (0)\n        rc = user_delete_once"),
    ("clear が I/O エラーで開き直さない",
     "        dict_recover(dict)) {\n        rc = sqlite3_exec",
     "        0) {\n        rc = sqlite3_exec"),
    ("list の prepare の失敗を 0 件にする",
     '"IME: user_list prepare failed (rc=%d)\\r\\n", rc);\n        return -4;',
     '"IME: user_list prepare failed (rc=%d)\\r\\n", rc);\n        return 0;'),
    ("学習が旧接続で先に SQL を流す",
     "    if (!dict_ready(dict) || !dict->learn_stmt) return;",
     "    if (!dict->db || !dict->learn_stmt) return;"),
]

# lib/sqlite3/os32_sqlite_vfs.c と fs/vfs_fd.c の変異 (ime の段で落ちること)
SQLITE_MUTANTS = [
    ("SQLite の入出力が失効を見ない", "lib/sqlite3/os32_sqlite_vfs.c",
     "    return p->fd >= 0 && !vfs_fd_is_stale(p->fd);", "    return p->fd >= 0;"),
    ("xTruncate が失効を見ない", "lib/sqlite3/os32_sqlite_vfs.c",
     "    if (!file_live(pFile)) return SQLITE_IOERR_TRUNCATE;",
     "    if (!file_valid(pFile)) return SQLITE_IOERR_TRUNCATE;"),
    ("xFileSize が失効を見ない", "lib/sqlite3/os32_sqlite_vfs.c",
     "    if (!file_live(pFile)) return SQLITE_IOERR_FSTAT;",
     "    if (!file_valid(pFile)) return SQLITE_IOERR_FSTAT;"),
    ("xCheckReservedLock が失効を見ない", "lib/sqlite3/os32_sqlite_vfs.c",
     "    if (!file_live(f)) return SQLITE_IOERR_CHECKRESERVEDLOCK;",
     "    if (!file_valid(f)) return SQLITE_IOERR_CHECKRESERVEDLOCK;"),
    ("close も失効で断る (FD を解放できない)", "fs/vfs_fd.c",
     "    if (vfs_validate_sqlite(lease) != VFS_OK) return VFS_ERR_INVAL;\n    f = &open_files[lease->fd];",
     "    if (vfs_validate_sqlite(lease) != VFS_OK) return VFS_ERR_INVAL;\n    f = &open_files[lease->fd];\n    if (f->stale) return VFS_ERR_STALE;"),
    ("journal 名の長さを見ない", "lib/sqlite3/os32_sqlite_vfs.c",
     "    if ((flags & SQLITE_OPEN_MAIN_DB) && !journal_name_fits(zName))",
     "    if ((flags & 0) && !journal_name_fits(zName))"),
    ("失効した SQLite FD を BUSY から外す (ime)", "fs/vfs_fd.c",
     "        if (!f->in_use || !f->sqlite_db || f->fs_ctx != fs_ctx) continue;",
     "        if (!f->in_use || !f->sqlite_db || f->stale || f->fs_ctx != fs_ctx) continue;"),
]
IME_SRC = ROOT / "tools/tests/ime_dict_host.c"


def build_ime(tmp, ime_c=None, exe_name="ime", overrides=None):
    """sqlite.o は 1 回だけ作る。ime_c を渡すとその kernel/ime_dict.c を使う。
    overrides = {"lib/sqlite3/os32_sqlite_vfs.c" / "fs/vfs_fd.c": 写しのパス}"""
    shim = tmp / "ime_shim"
    shim.mkdir(exist_ok=True)
    memmap = "extern unsigned char test_shm[];\n#define MEM_SHM_BASE test_shm\n"
    # 変異は並列に組む (mutpar) — 同じ中身なら書き直さない (読んでいる gcc と競らない)
    if not (shim / "memmap.h").exists() or (shim / "memmap.h").read_text() != memmap:
        (shim / "memmap.h").write_text(memmap)
    obj = tmp / "sqlite.o"
    if not obj.exists():
        subprocess.run(["gcc", "-std=gnu89", "-O0", "-w", "-include",
                        str(ROOT / "lib/sqlite3/os32_sqlite_config.h"), "-c",
                        str(ROOT / "lib/sqlite3/sqlite3.c"), "-o", str(obj)],
                       check=True)
    src = IME_SRC
    overrides = overrides or {}
    if ime_c is not None or overrides:
        src = tmp / f"{exe_name}_host.c"
        text = IME_SRC.read_text(encoding="utf-8")
        if ime_c is not None:
            text = text.replace('#include "../../kernel/ime_dict.c"',
                                f'#include "{ime_c}"')
        else:
            text = text.replace('#include "../../kernel/ime_dict.c"',
                                f'#include "{ROOT}/kernel/ime_dict.c"')
        fdhost = f"{ROOT}/tools/tests/vfs_fd_sqlite_host.c"
        if "fs/vfs_fd.c" in overrides:
            fdhost = tmp / f"{exe_name}_fdhost.c"
            fdhost.write_text((ROOT / "tools/tests/vfs_fd_sqlite_host.c").read_text(
                encoding="utf-8").replace('#include "../../fs/vfs_fd.c"',
                                          f'#include "{overrides["fs/vfs_fd.c"]}"'),
                encoding="utf-8")
        text = text.replace('#include "vfs_fd_sqlite_host.c"',
                            f'#include "{fdhost}"')
        if "lib/sqlite3/os32_sqlite_vfs.c" in overrides:
            text = text.replace('#include "../../lib/sqlite3/os32_sqlite_vfs.c"',
                                f'#include "{overrides["lib/sqlite3/os32_sqlite_vfs.c"]}"')
        text = text.replace('#include "../../lib/sqlite3/', f'#include "{ROOT}/lib/sqlite3/')
        text = text.replace('#include "sqlite_groups_backend.h"',
                            f'#include "{ROOT}/tools/tests/sqlite_groups_backend.h"')
        src.write_text(text, encoding="utf-8")
    exe = tmp / exe_name
    res = subprocess.run(
        ["gcc", "-std=gnu89", "-Wall", "-Wextra", "-Werror", "-Wno-type-limits",
         "-Wdeclaration-after-statement", "-Wno-unused-parameter",
         "-Wno-sign-compare", "-Wno-pointer-to-int-cast",
         "-Wno-missing-field-initializers", "-D__cdecl=", "-I" + str(shim),
         *["-I" + str(ROOT / p) for p in ("include", "fs", "drivers",
                                          "sdk/include/os32", "lib",
                                          "lib/sqlite3", "kernel")],
         str(src), str(obj), "-o", str(exe)],
        stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    if res.returncode != 0:
        return None, res.stdout.decode("utf-8", "replace")
    return exe, ""


def run_ime(tmp, quiet=False):
    exe, err = build_ime(tmp)
    if exe is None:
        print(err)
        print("IME BUILD FAIL")
        return False
    r = subprocess.run([str(exe)], stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                       timeout=120)
    if not quiet:
        print("case ime (real kernel/ime_dict.c + SQLite + vfs_fd.c)")
        print(r.stdout.decode("utf-8", "replace").rstrip())
    return r.returncode == 0


def _run_ime_exe(exe):
    try:
        r = subprocess.run([str(exe)], stdout=subprocess.PIPE,
                           stderr=subprocess.STDOUT, timeout=120)
        return r.returncode == 0
    except subprocess.TimeoutExpired:
        return False


def _ime_mutant_one(item):
    tmp, src, i, (name, before, after) = item
    if src.count(before) != 1:
        return name, f"IME MUTANT {i} ({name}): 置き換え元が {src.count(before)} 件"
    mp = tmp / f"ime_dict_m{i}.c"
    mp.write_text(src.replace(before, after), encoding="utf-8")
    exe, err = build_ime(tmp, mp, f"ime_m{i}")
    if exe is None:
        return name, f"IME MUTANT {i} ({name}): build failed\n{err}"
    ok = _run_ime_exe(exe)
    return (name if ok else None), f"IME MUTANT {i} ({name}): {'SURVIVED' if ok else 'killed'}"


def ime_mutants(tmp):
    """並列に回し (mutpar)、結果は番号順に出す。sqlite.o と shim は run_ime が先に作る。"""
    surv = []
    build_ime(tmp)
    src = (ROOT / "kernel/ime_dict.c").read_text(encoding="utf-8")
    items = [(tmp, src, i, m) for i, m in enumerate(IME_MUTANTS)]
    for s, line in mutpar.run_ordered(_ime_mutant_one, items):
        print(line, flush=True)
        if s:
            surv.append(s)
    return surv


def _sqlite_mutant_one(item):
    tmp, i, (name, fname, before, after) = item
    src = (ROOT / fname).read_text(encoding="utf-8")
    if src.count(before) != 1:
        return name, f"SQLITE MUTANT {i} ({name}): 置き換え元が {src.count(before)} 件"
    mp = tmp / f"sqm{i}_{pathlib.Path(fname).name}"
    mp.write_text(src.replace(before, after), encoding="utf-8")
    exe, err = build_ime(tmp, None, f"ime_sqm{i}", {fname: mp})
    if exe is None:
        return name, f"SQLITE MUTANT {i} ({name}): build failed\n{err}"
    ok = _run_ime_exe(exe)
    return (name if ok else None), \
        f"SQLITE MUTANT {i} ({name}): {'SURVIVED' if ok else 'killed'}"


def sqlite_mutants(tmp):
    """並列に回し (mutpar)、結果は番号順に出す。"""
    surv = []
    build_ime(tmp)
    items = [(tmp, i, m) for i, m in enumerate(SQLITE_MUTANTS)]
    for s, line in mutpar.run_ordered(_sqlite_mutant_one, items):
        print(line, flush=True)
        if s:
            surv.append(s)
    return surv


def check_fatfold():
    """fs/fatfs_vfs.c の fat_upper_ext が ff.c の TBL_CT437 と一致するか
    (FatFs が名前を畳む規則と VFS の BUSY / pinned の比較を揃える、B4)"""
    import re
    ff = (ROOT / "fs/fatfs/ff.c").read_text(encoding="utf-8", errors="replace")
    fv = (ROOT / "fs/fatfs_vfs.c").read_text(encoding="utf-8")
    conf = (ROOT / "fs/fatfs/ffconf.h").read_text(encoding="utf-8", errors="replace")
    cp = re.search(r"#define\s+FF_CODE_PAGE\s+(\d+)", conf).group(1)
    m = re.search(r"#define\s+TBL_CT%s\s*\{(.*?)\}" % cp, ff, re.S)
    want = [int(v.strip(), 16) for v in m.group(1).replace("\\", "").split(",")]
    m = re.search(r"fat_upper_ext\[128\]\s*=\s*\{(.*?)\}", fv, re.S)
    got = [int(v.strip(), 16) for v in m.group(1).split(",") if v.strip()]
    ok = len(want) == 128 and want == got
    print(f"case fatfold (fat_upper_ext == ff.c TBL_CT{cp}): {'PASS' if ok else 'FAIL'}")
    return ok


def check_hostwire(text=None):
    """fs/hostdrvfs.c の名前の入口検査の配線 (実物はハイパーコールを叩くので
    ホストで組めない。規則そのものは段 namerule / hostname が回す)。
      - 名前をホストへ渡す口は setup_create → session_set_path だけで、
        setup_create を呼ぶのは hostdrv_create だけ
      - hostdrv_create は setup_create の**前に** Win32 の規則で断る
      - rename の宛先 (hostdrv_create を通らない) は元を開く**前に**断る
      - パスを受け取る VfsOps の口は全部 hostdrv_create(path, …) を通る"""
    import re
    src = text if text is not None else (ROOT / "fs/hostdrvfs.c").read_text(encoding="utf-8")
    bad = []

    def body(name):
        m = re.search(r"^static int %s\(.*?\n\{(.*?)^\}" % name, src, re.S | re.M)
        return m.group(1) if m else None

    def calls(fn):
        return len(re.findall(r"(?<![\w])%s\(" % fn, src))

    if calls("session_set_path") != 2:          # 定義 + setup_create の中の 1 回
        bad.append("session_set_path の呼び手が setup_create だけでない")
    if calls("setup_create") != 2:              # 定義 + hostdrv_create の中の 1 回
        bad.append("setup_create の呼び手が hostdrv_create だけでない")
    b = body("hostdrv_create") or ""
    i = b.find("vfs_name_rule_check(path, VFS_NAME_RULE_WIN32)")
    if i < 0 or i > b.find("setup_create("):
        bad.append("hostdrv_create が setup_create の前に名前を見ない")
    # 変換の失敗でハイパーコールしない (ラリー 3 B2): setup_create の戻り値を見て
    # から hostdrv_hypercall
    j = b.find("rc = setup_create(")
    k = b.find("if (rc != VFS_OK) return rc;", j)
    if j < 0 or k < 0 or k > b.find("hostdrv_hypercall();"):
        bad.append("hostdrv_create が setup_create の失敗を見ずにハイパーコールする")
    b = body("hdrv_rename") or ""
    i = b.find("vfs_name_rule_check(new_path, VFS_NAME_RULE_WIN32)")
    if i < 0 or i > b.find("session_begin();"):
        bad.append("hdrv_rename が宛先を元を開く前に見ない")
    i = b.find("if (words < 1) return VFS_ERR_INVAL;")
    if i < 0 or i > b.find("session_begin();"):
        bad.append("hdrv_rename が宛先の変換の失敗を元を開く前に見ない")
    ops = re.findall(r"^static int (hdrv_\w+)\(void \*ctx, const char \*path", src, re.M)
    if len(ops) < 10:
        bad.append("パスを受け取る口が %d 本しか見つからない" % len(ops))
    for name in ops:
        if "hostdrv_create(path," not in (body(name) or ""):
            bad.append(name + " が hostdrv_create(path, …) を通らない")
    if "hostdrv_create(old_path," not in (body("hdrv_rename") or ""):
        bad.append("hdrv_rename が hostdrv_create(old_path, …) を通らない")
    for m in bad:
        print("  FAIL hostwire: " + m)
    print(f"case hostwire (fs/hostdrvfs.c の入口検査の配線, {len(ops)} 口): "
          f"{'PASS' if not bad else 'FAIL'}")
    return not bad


def find_e2fsck():
    for cand in ("/usr/sbin/e2fsck", "/sbin/e2fsck"):
        if os.access(cand, os.X_OK):
            return cand
    return shutil.which("e2fsck")


# ---------------------------------------------------------------------------
#  PKG (消費側の試験用)。mkpkg は長すぎるものを作らないので生で組む
# ---------------------------------------------------------------------------

def raw_pkg(entries, data=b"", lzss=False, count=None, terminate=True):
    """entries: [(path_bytes, size, type)]。count はヘッダの項目数 (既定は実数)"""
    table = bytearray()
    for path, size, ftype in entries:
        table.append(len(path))
        table += path
        table += struct.pack("<I", size)
        table.append(ftype)
    if terminate:
        table.append(0)
    if lzss and data:
        part = mkpkg.lzss_encode(data)
        flags = 1
    else:
        part = bytes(data)
        flags = 0
    hdr = bytearray(32)
    hdr[0:4] = b"PKG1"
    hdr[4:12] = b"test\0\0\0\0"
    hdr[12] = 1
    hdr[13] = flags
    struct.pack_into("<H", hdr, 16, len(entries) if count is None else count)
    struct.pack_into("<I", hdr, 18, len(data))
    struct.pack_into("<I", hdr, 22, len(part))
    return bytes(hdr) + bytes(table) + part


def make_pkgs(d):
    F, D = 0, 1
    w = lambda n, b: (d / n).write_bytes(b)
    long127 = b"/q/" + b"p" * 124
    w("P127.PKG", raw_pkg([(b"/q", 0, D), (long127, 5, F)], b"hello"))
    w("P128.PKG", raw_pkg([(b"/q", 0, D), (b"/q/" + b"p" * 125, 5, F)], b"hello"))
    w("E128.PKG", raw_pkg([(b"/e/%03d" % i, 0, F) for i in range(128)]))
    # ヘッダは 128 と偽る — 表そのもので超過を見つけること
    w("E129.PKG", raw_pkg([(b"/e/%03d" % i, 0, F) for i in range(129)], count=128))
    # 1 項目の後に終端が無い (ヘッダの項目数とは合っている)
    w("TRUNC.PKG", raw_pkg([(b"/t", 0, F)], terminate=False))
    w("COUNT.PKG", raw_pkg([(b"/c1", 0, F), (b"/c2", 0, F)], count=5))
    for n in range(123, 128):
        p = b"/l/" + b"x" * (n - 3)
        assert len(p) == n
        w("L%d.PKG" % n, raw_pkg([(b"/l", 0, D), (p, 1, F)], b"Z"))
    w("CLASH.PKG", raw_pkg([(b"/clash", 3, F)], b"abc"))
    w("CLASHZ.PKG", raw_pkg([(b"/clash", 3, F)], b"abc", lzss=True))
    w("GOOD.PKG", raw_pkg([(b"/good", 0, D), (b"/good/a.txt", 5, F)],
                          b"hello", lzss=True))
    # 段 cdinst: cdinst.c の固定名 (/cd0/NORMAL.PKG 等) を組の接頭辞で差し替える。
    # NORMAL2 は分割の 2 本目 (cdinst は連番を欠けるまで展開する)
    good = raw_pkg([(b"/good", 0, D), (b"/good/a.txt", 5, F)], b"hello", lzss=True)
    norm = raw_pkg([(b"/n", 0, D), (b"/n/a.txt", 1, F)], b"N")
    norm2 = raw_pkg([(b"/n2", 0, D), (b"/n2/b.txt", 1, F)], b"M")
    full = raw_pkg([(b"/full", 0, D), (b"/full/f.txt", 1, F)], b"F")
    gui = raw_pkg([(b"/g", 0, D), (b"/g/a.txt", 1, F)], b"G")
    too_long = raw_pkg([(b"/l", 0, D), (b"/l/" + b"x" * 121, 1, F)], b"Z")  # 124
    for setname, files in (
            ("A_", {"MINIMAL": good, "GUI": gui, "NORMAL": too_long, "DEBUG": full}),
            ("B_", {"MINIMAL": good, "GUI": gui, "NORMAL": norm,
                    "NORMAL2": raw_pkg([(b"/clash", 3, F)], b"abc"), "DEBUG": full}),
            ("C_", {"MINIMAL": good, "GUI": gui, "NORMAL": norm, "NORMAL2": norm2,
                    "DEBUG": full}),
            ("D_", {"MINIMAL": too_long, "GUI": gui, "NORMAL": norm}),
            ("E_", {"MINIMAL": good, "GUI": gui, "NORMAL": norm}),
            # GUI (MINIMAL の次、NORMAL の前) の失敗で止まる / GUI が媒体に無い
            ("F_", {"MINIMAL": good, "GUI": too_long, "NORMAL": norm}),
            ("G_", {"MINIMAL": good, "NORMAL": norm})):
        for n, b in files.items():
            w(f"{setname}{n}.PKG", b)


# ---------------------------------------------------------------------------
#  mkpkg (実物) の生産側の検査
# ---------------------------------------------------------------------------

def check_mkpkg(tmp):
    ok = True
    blob = tmp / "blob"
    blob.write_bytes(b"x")
    mk = [sys.executable, "-B", str(ROOT / "tools/mkpkg.py"), "--base", str(ROOT),
          "--name", "t", "-o", str(tmp / "out.PKG")]

    def run(files):
        r = subprocess.run(mk + [f"{g}={blob}" for g in files],
                           stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        return r.returncode, r.stdout.decode("utf-8", "replace")

    def expect(label, files, want_ok):
        nonlocal ok
        rc, out = run(files)
        good = (rc == 0) == want_ok
        print(f"  {'ok  ' if good else 'FAIL'} mkpkg {label}: rc={rc}")
        if not good:
            ok = False
            print("    | " + out.strip().replace("\n", "\n    | "))
        return rc

    expect("格納 123 バイト", ["/m/" + "a" * 120], True)
    expect("格納 124 バイト", ["/m/" + "a" * 121], False)
    # UTF-8 のバイト長で数える ("あ" = 3 バイト)
    expect("UTF-8 123 バイト (/m/ + あ x 40)", ["/m/" + "あ" * 40], True)
    expect("UTF-8 126 バイト", ["/m/" + "あ" * 41], False)
    # 255 バイトの黙った切り詰めは無い (長いものは断る)
    expect("格納 300 バイト", ["/m/" + "a" * 297], False)
    # 項目数 (ディレクトリ項目を含む): /d + 127 本 = 128 は通り、129 は断る
    expect("128 項目", ["/d/f%03d" % i for i in range(127)], True)
    expect("129 項目", ["/d/f%03d" % i for i in range(128)], False)
    if ok:
        data = (tmp / "out.PKG").read_bytes()
        # 通った方の最後の PKG (128 項目) の項目数
        cnt = struct.unpack_from("<H", data, 16)[0]
        if cnt != 128:
            print(f"  FAIL mkpkg 128 項目のヘッダ entry_count={cnt}")
            ok = False
    return ok


# ---------------------------------------------------------------------------
#  組み立てと実行
# ---------------------------------------------------------------------------

def _ff_cmd(fsdir, ffo):
    return ["gcc", "-std=gnu89", "-m32", "-march=i386", "-ffreestanding",
            "-fno-pie", "-fno-stack-protector", "-O1", "-w",
            "-I" + str(fsdir / "fatfs"), "-I" + str(ROOT / "lib"),
            "-I" + str(ROOT / "include"), "-c",
            str(fsdir / "fatfs/ff.c"), "-o", str(ffo)]


def build(tmp, fsdir, libdir, exe_name="fdpath", extra=(), ffo=None):
    """ffo を渡すとその ff.o を使い、FatFs を組み直さない (変異が FatFs に触らないとき)。"""
    inc = ["-I" + str(ROOT / "tools/tests/mtar_freestanding"), "-I" + str(fsdir),
           "-I" + str(libdir)]
    inc += ["-I" + str(ROOT / p) for p in
            ("include", "lib", "kernel", "drivers", "sdk/include/os32",
             "userland/lib")]
    inc += ["-I" + str(ROOT)]   # cdinst の共有部 (userland/shell/hdprep_plan.c 等)
    exe = tmp / exe_name
    # 段 fatname は実物の FatFs (fs/fatfs/ff.c) と組む。ff.c は別の翻訳単位で、
    # <string.h> を fs/fatfs/string.h (= kstring.h) に向ける (カーネルと同じ)。
    # FatFs 本体は第三者のソースなので警告では落とさない (-w)。
    if ffo is None:
        ffo = tmp / f"{exe_name}_ff.o"
        res = subprocess.run(_ff_cmd(fsdir, ffo),
                             stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        if res.returncode != 0:
            return None, res.stdout.decode("utf-8", "replace")
    res = subprocess.run(["gcc", *HOST_FLAGS, *extra, *inc, str(SRC), str(ffo),
                          "-o", str(exe)],
                         stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    if res.returncode != 0:
        return None, res.stdout.decode("utf-8", "replace")
    return exe, ""


def run_exe(exe, imgdir, pkgdir, case=None, quiet=False, e2fsck=None, first_fail=False):
    """first_fail なら段の途中で落ちた時点で打ち切り (ハーネスの "+first-fail")、
    e2fsck も最初に汚れた像で打ち切る (変異用、RED かどうかだけ分かればよい)。"""
    for p in imgdir.glob("*.img"):
        p.unlink()
    argv = [str(exe), str(imgdir), str(pkgdir)]
    if case:
        argv.append(case)
    elif first_fail:
        argv.append("+first-fail")
    res = subprocess.run(argv, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                         timeout=120)
    out = res.stdout.decode("utf-8", "replace")
    ok = res.returncode == 0
    lines = []
    for line in out.splitlines():
        if line.startswith("@@IMG "):
            path = line[6:].strip()
            if first_fail and not ok:
                continue
            if e2fsck is None:
                lines.append(f"  E2FSCK SKIP {os.path.basename(path)}")
                continue
            fr = subprocess.run([e2fsck, "-fn", path], stdin=subprocess.DEVNULL,
                                stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                                timeout=120)
            name = os.path.basename(path)
            if fr.returncode == 0:
                lines.append(f"  e2fsck -fn {name}: clean")
            else:
                ok = False
                lines.append(f"  e2fsck -fn {name}: rc={fr.returncode} FAIL")
                for l in fr.stdout.decode("utf-8", "replace").splitlines():
                    if l.startswith("e2fsck ") or l.startswith("Pass "):
                        continue
                    lines.append("    | " + l)
        else:
            lines.append(line)
    text = "\n".join(lines)
    if not quiet:
        print(text)
    return ok, text


def newlib_include():
    for base in (os.environ.get("CROSS_DIR"), str(pathlib.Path.home() / "opt/cross")):
        if base and (pathlib.Path(base) / "i386-elf/include/errno.h").is_file():
            return pathlib.Path(base) / "i386-elf/include"
    return None


def run_errno(tmp, syscalls=SYSCALLS_SRC, quiet=False):
    inc = newlib_include()
    if inc is None:
        print("ERRNO SKIP (newlib headers not found: $CROSS_DIR/i386-elf/include)")
        return True
    gccinc = subprocess.run(["gcc", "-m32", "-print-file-name=include"],
                            stdout=subprocess.PIPE).stdout.decode().strip()
    flags = ["-std=gnu89", "-m32", "-ffreestanding", "-fno-pie",
             "-fno-stack-protector", "-nostdinc", "-isystem", gccinc,
             "-isystem", str(inc), "-I" + str(ROOT / "sdk/include/os32"),
             "-O1", "-Wall", "-Werror", "-Wno-unused-parameter",
             "-Wno-unused-but-set-variable"]
    exe = tmp / "errno"
    res = subprocess.run(["gcc", *flags, "-nostdlib", "-static", "-no-pie",
                          str(ERRNO_SRC), str(syscalls), "-o", str(exe)],
                         stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    if res.returncode != 0:
        print(res.stdout.decode("utf-8", "replace"))
        print("ERRNO BUILD FAIL")
        return False
    r = subprocess.run([str(exe)], stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                       timeout=60)
    if not quiet:
        print("case errno (real sdk/crt/syscalls.c + newlib headers)")
        print(r.stdout.decode("utf-8", "replace").rstrip())
    return r.returncode == 0


def target_compile(tmp):
    cc = shutil.which("i386-elf-gcc")
    if not cc:
        print("TARGET SKIP (i386-elf-gcc not in PATH)")
        return True
    for src in TARGET_SRCS:
        subprocess.run([cc, *TARGET_FLAGS, "-c", src,
                        "-o", str(tmp / (pathlib.Path(src).stem + ".o"))],
                       cwd=ROOT, check=True)
    print("TARGET i386-elf -Werror compile PASS (%s)" % ", ".join(TARGET_SRCS))
    return True


# 変異を当てたファイル (写しの中の名前) → 組み直すもの (票 TASK_CHECK_MUT_PARALLEL)。
# "harness" だけのファイルは FatFs (ff.o) を組み直さず、実物で 1 回組んだ ff.o を使う。
# 表に無いファイルは両方組む。表が gcc -MM の依存より狭ければ mutants() が落ちる。
REBUILD = {
    "fs/vfs.c": ("harness",),
    "fs/vfs_fd.c": ("harness",),
    "fs/vfs_name_rules.inc": ("harness",),
    "fs/ext2_file.c": ("harness",),
    "fs/fatfs_vfs.c": ("harness",),
    "lib/kutf16.c": ("harness",),
    "lib/rt/pkg.c": ("harness",),
    "system/cdinst.c": ("harness",),
}


def _mutant_one(item):
    """変異 1 本。tmp/mut<i> (自分専用の写し) で組み、最初に落ちた段で打ち切る。
    並列に呼ばれる。(生き残りなら名前 / None, 行) を返す。"""
    tmp, pkgdir, e2fsck, ffo, i, (name, fname, before, after) = item
    mdir = tmp / f"mut{i}"
    shutil.copytree(ROOT / "fs", mdir / "fs")
    shutil.copytree(ROOT / "userland/lib/rt", mdir / "lib/rt")
    (mdir / "system").mkdir()
    shutil.copy(ROOT / "userland/system/cdinst.c", mdir / "system/cdinst.c")
    for f in ("inst_hdd.c", "inst_hdd.h", "inst_disk.c", "inst_disk.h"):
        shutil.copy(ROOT / "userland/system" / f, mdir / "system" / f)
    # lib/kutf16.c (段 utf8 が #include する) は -I の順で写しが先に当たる
    shutil.copy(ROOT / "lib/kutf16.c", mdir / "lib/kutf16.c")
    try:
        path = mdir / fname
        text = path.read_text(encoding="utf-8")
        if text.count(before) != 1:
            return name, (f"MUTANT {i} ({name}): 置き換え元が {text.count(before)} 件"
                          " — 変異表が古い")
        path.write_text(text.replace(before, after), encoding="utf-8")
        keys = REBUILD.get(fname, ("ff", "harness"))
        exe, err = build(tmp, mdir / "fs", mdir / "lib", f"fdpath_m{i}",
                         ffo=None if "ff" in keys else ffo)
        if exe is None:
            return name, f"MUTANT {i} ({name}): build failed\n{err}"
        imgdir = tmp / f"img_m{i}"
        imgdir.mkdir()
        try:
            ok, _ = run_exe(exe, imgdir, pkgdir, quiet=True, e2fsck=e2fsck,
                            first_fail=True)
        except subprocess.TimeoutExpired:
            ok = False
        return (name if ok else None), \
            f"MUTANT {i} ({name}): {'SURVIVED' if ok else 'killed'}"
    finally:
        shutil.rmtree(mdir, ignore_errors=True)
        shutil.rmtree(tmp / f"img_m{i}", ignore_errors=True)


def mutants(tmp, pkgdir, e2fsck):
    """並列に回し (mutpar、OS32_MUT_JOBS)、結果は変異の番号順に出す。"""
    survivors = []
    ffo = tmp / "mut_base_ff.o"
    cmd = _ff_cmd(ROOT / "fs", ffo)
    stale = mutpar.check_rebuild_table(
        {f: k for f, k in REBUILD.items()},
        {"ff": mutpar.gcc_deps(cmd, ROOT)}, [m[1] for m in MUTANTS])
    for s in stale:
        print("REBUILD TABLE STALE: " + s)
        survivors.append("REBUILD TABLE STALE")
    subprocess.run(cmd, check=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    items = [(tmp, pkgdir, e2fsck, ffo, i, m) for i, m in enumerate(MUTANTS)]
    for surv, line in mutpar.run_ordered(_mutant_one, items):
        print(line, flush=True)
        if surv:
            survivors.append(surv)
    return survivors


def run_red(tmp, pkgdir, rev):
    """修正前の fs/ と rt/ を取り出して回す。落ちれば RED 成立 (True)"""
    rdir = tmp / "red"
    for sub, dst in (("fs", rdir / "fs"), ("userland/lib/rt", rdir / "lib/rt")):
        dst.mkdir(parents=True)
        arc = subprocess.run(["git", "-C", str(ROOT), "archive", rev, sub],
                             stdout=subprocess.PIPE, check=True).stdout
        subprocess.run(["tar", "-x", "-C", str(dst), "--strip-components",
                        str(sub.count("/") + 1)], input=arc, check=True)
    exe, err = build(tmp, rdir / "fs", rdir / "lib", "fdpath_red", ("-DFDP_RED",))
    if exe is None:
        print(err)
        print("RED BUILD FAIL")
        return False
    imgdir = tmp / "img_red"
    imgdir.mkdir()
    print(f"RED ({rev} の fs/ + userland/lib/rt/)")
    ok, _ = run_exe(exe, imgdir, pkgdir, e2fsck=find_e2fsck())
    print("RED " + ("NOT REPRODUCED (修正前でも通った)" if ok else "reproduced"))
    old_sc = rdir / "syscalls.c"
    old_sc.write_bytes(subprocess.run(
        ["git", "-C", str(ROOT), "show", f"{rev}:sdk/crt/syscalls.c"],
        stdout=subprocess.PIPE, check=True).stdout)
    eok = run_errno(tmp, old_sc)
    print("RED errno " + ("NOT REPRODUCED" if eok else "reproduced"))
    return (not ok) and (not eok)


def main():
    red = [a.split("=", 1)[1] if "=" in a else "6de1fea"
           for a in sys.argv[1:] if a.startswith("--red")]
    if red:
        with tempfile.TemporaryDirectory(prefix="os32-vfs-fdpath-red-") as t:
            tmp = pathlib.Path(t)
            pkgdir = tmp / "pkg"
            pkgdir.mkdir()
            make_pkgs(pkgdir)
            return 0 if run_red(tmp, pkgdir, red[0]) else 1
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    case = args[0] if args else None
    e2fsck = find_e2fsck()
    with tempfile.TemporaryDirectory(prefix="os32-vfs-fdpath-") as t:
        tmp = pathlib.Path(t)
        pkgdir = tmp / "pkg"
        pkgdir.mkdir()
        make_pkgs(pkgdir)
        ok = True
        if case in (None, "errno"):
            ok = run_errno(tmp) and ok
        if case in (None, "mkpkg"):
            print("case mkpkg (real tools/mkpkg.py)")
            ok = check_mkpkg(tmp) and ok
        if case in (None, "ime"):
            ok = run_ime(tmp) and ok
        if case in (None, "fatfold"):
            ok = check_fatfold() and ok
        if case in (None, "hostwire"):
            ok = check_hostwire() and ok
        if case not in ("errno", "mkpkg", "ime", "fatfold", "hostwire"):
            exe, err = build(tmp, ROOT / "fs", ROOT / "userland/lib")
            if exe is None:
                print(err)
                print("BUILD FAIL")
                return 1
            print("HOST GNU89 -m32 -Werror compile PASS (real ext2 + vfs + vfs_fd + rt/pkg.c)")
            imgdir = tmp / "img"
            imgdir.mkdir()
            r, _ = run_exe(exe, imgdir, pkgdir, case=case, e2fsck=e2fsck)
            ok = r and ok
            if e2fsck is None:
                print("E2FSCK SKIP (e2fsck not found; self checks only)")
        if not ok:
            print("vfs_fd_path: FAIL")
            return 1
        if "--target" in sys.argv:
            target_compile(tmp)
        if "--mutants" in sys.argv:
            surv = mutants(tmp, pkgdir, e2fsck)
            if surv:
                print("MUTANTS SURVIVED: " + "; ".join(surv))
                return 1
            print(f"MUTANTS all {len(MUTANTS)} killed")
            if newlib_include() is not None:
                esurv = []
                src = SYSCALLS_SRC.read_text(encoding="utf-8")
                for i, (name, before, after) in enumerate(ERRNO_MUTANTS):
                    if src.count(before) != 1:
                        print(f"ERRNO MUTANT {i} ({name}): 置き換え元が {src.count(before)} 件")
                        esurv.append(name)
                        continue
                    mp = tmp / f"syscalls_m{i}.c"
                    mp.write_text(src.replace(before, after), encoding="utf-8")
                    ok = run_errno(tmp, mp, quiet=True)
                    print(f"ERRNO MUTANT {i} ({name}): {'SURVIVED' if ok else 'killed'}")
                    if ok:
                        esurv.append(name)
                if esurv:
                    print("ERRNO MUTANTS SURVIVED: " + "; ".join(esurv))
                    return 1
                print(f"ERRNO MUTANTS all {len(ERRNO_MUTANTS)} killed")
            isurv = ime_mutants(tmp)
            if isurv:
                print("IME MUTANTS SURVIVED: " + "; ".join(isurv))
                return 1
            print(f"IME MUTANTS all {len(IME_MUTANTS)} killed")
            ssurv = sqlite_mutants(tmp)
            if ssurv:
                print("SQLITE MUTANTS SURVIVED: " + "; ".join(ssurv))
                return 1
            print(f"SQLITE MUTANTS all {len(SQLITE_MUTANTS)} killed")
        print("vfs_fd_path: PASS")
        return 0


if __name__ == "__main__":
    sys.exit(main())
