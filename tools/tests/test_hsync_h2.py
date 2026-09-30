"""H2: hsync の置換安全化 (一時ファイル + 検証 + 置換)。

票:   docs/archive/shell/TASK_H2.md §2-3 / §2-4 / §4-1
記録: tools/tests/hsync_h2_tdd.md

tools/tests/hsync_h2_host.c が userland/system/hsync.c を 1 行も写さず
そのまま #include し、KernelAPI だけをオンメモリの贋ファイルシステムへ
差し替えて回す (模型ではない)。贋 FS は票 H2 が要求する故障を注入できる:

  - sys_open の **O_EXCL** (既存は EXIST / 非対応 FS は NOSYS)
  - n 回目の sys_write を任意の番号で落とす (空き不足 = NOSPC / FULL も)
  - sys_set_mtime の失敗と、それに伴う **ROFS への転落** (ext2 と同じ)
  - sys_rename の 4 通り (公開の前に失敗 / 公開の後に失敗 / 判定不能 / 成功)
  - st_nlink をノードごとに持つ (hardlink 判定と予約名の掃除)
  - api->version を 52 / 53 に切り替える (古いカーネルの門)

  python3 -B tools/tests/test_hsync_h2.py [--target] [--mutate]

--target は実機と同じ i386-elf クロスコンパイラでも userland/system/hsync.c が
-Werror で通ることの確認 ([C1] GNU11)。

--mutate は**否定側**。この票の中心規則 (公開の判定は st_ino、NOSYS で直接
上書きへ落ちない、mtime の失敗を copied に数えない、予約名は st_nlink を
見ない、保護は予約より優先、古いカーネルは既定で断る) を崩した版で試験が
確かに落ちることを確かめる。落ちなければ試験が規則を見ていない。

make・エミュレータ・実配備には一切触れない。
"""
import os
import pathlib
import re
import subprocess
import sys
import tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import mutpar                                                   # noqa: E402

ROOT = pathlib.Path(__file__).resolve().parents[2]

HOST_FLAGS = ["-std=gnu11", "-Wall", "-Wextra", "-Werror",
              "-D__cdecl=", "-D__OS32_USERLAND__"]
HOST_INC = ["-I" + str(ROOT / p) for p in
            (".", "include", "sdk/include", "sdk/include/os32",
             "userland/lib", "userland/system", "lib")]

CROSS_DIR = pathlib.Path(os.environ.get("CROSS_DIR", "/usr/local/cross"))
if not CROSS_DIR.exists():
    alt = pathlib.Path.home() / "opt/cross"
    if alt.exists():
        CROSS_DIR = alt

TARGET_USER = ["-std=gnu11", "-m32", "-march=i386", "-ffreestanding",
               "-fno-pie", "-fno-stack-protector", "-nostdlib",
               "-mno-red-zone", "-fcommon", "-O2",
               "-Wall", "-Wextra", "-Werror",
               "-D__OS32_USERLAND__", "-I.", "-Iinclude", "-Isdk/include",
               "-Isdk/include/os32", "-Iuserland/lib",
               "-I" + str(CROSS_DIR / "i386-elf/include")]


def check_manifest():
    """build/app.conf の hsync の要求 API 版は **52 のまま**であること。

    票 H2 §1 の 7 / §2-3 末尾: カーネルを入れ替える途中で **v52 のカーネルの
    上で新しい hsync を動かす場面が現に起こる**。app.conf を 53 にすると
    exec が min_api_ver > KAPI_VERSION で弾き、hsync 自身の kernel_too_old も
    --unsafe-overwrite も届かない。版の判定は hsync が api->version で行う。
    """
    conf = (ROOT / "build/app.conf").read_text(encoding="utf-8")
    m = re.search(r"^userland/system/hsync\s+(\d+)", conf, re.M)
    if not m:
        raise SystemExit("build/app.conf に userland/system/hsync が無い")
    if int(m.group(1)) != 52:
        raise SystemExit(
            "build/app.conf の hsync の要求 API 版は 52 のままにすること "
            "(票 H2 §2-3 末尾: v52 のカーネル上で kernel_too_old を出すため)。"
            "いまは %s" % m.group(1))

    src = (ROOT / "userland/system/hsync.c").read_text(encoding="utf-8")
    if "#define HS_MIN_KAPI_H2  53" not in src:
        raise SystemExit("hsync.c の HS_MIN_KAPI_H2 が 53 でない")
    if '#define HS_TEMP_PREFIX     ".hs~"' not in src:
        raise SystemExit("hsync.c の予約接頭辞が \".hs~\" でない")

    # --root の ext2 判定は fs/ext2.h の EXT2_ROOT_INO の写し ([C4])
    m = re.search(r"^#define EXT2_ROOT_INO\s+(\d+)",
                  (ROOT / "fs/ext2.h").read_text(encoding="utf-8"), re.M)
    if not m or ("#define HS_EXT2_ROOT_INO %sUL" % m.group(1)) not in src:
        raise SystemExit("hsync.c の HS_EXT2_ROOT_INO が fs/ext2.h の "
                         "EXT2_ROOT_INO と合わない")

    man = (ROOT / "docs/manpages/hsync.1").read_text(encoding="utf-8")
    for word in (".hs~", "--unsafe-overwrite", "replace_partial", "--root",
                 "root_not_ext2", "root_not_mount", "root_is_source",
                 "root_stat_failed"):
        if word not in man:
            raise SystemExit("docs/manpages/hsync.1 に %s の記載が無い "
                             "(予約名の明記が決裁 D3 (a') の前提)" % word)

    print("MANIFEST PASS (app.conf hsync=52, HS_MIN_KAPI_H2=53, "
          "予約名 .hs~ は man ページに明記)", flush=True)


def build_host(tmp, name, src="tools/tests/hsync_h2_host.c"):
    exe = tmp / name
    subprocess.run(["gcc", *HOST_FLAGS, *HOST_INC, str(ROOT / src),
                    "-o", str(exe)], cwd=ROOT, check=True)
    print("HOST GNU11 -Werror COMPILE PASS (%s)" % src, flush=True)
    return exe


MUTATIONS = [
    # ---- --root (2026-09-26) の否定側 ----
    # 変異: 根が ext2 かを見ない版 (/hd1 = FAT、/cd0 = ISO9660 へ書き出す)
    ("root_ext2_unchecked",
     "            else if ((st.st_mode & OS_S_IFMT) != OS_S_IFDIR ||\n"
     "                     st.st_ino != HS_EXT2_ROOT_INO)",
     "            else if ((st.st_mode & OS_S_IFMT) != OS_S_IFDIR)"),
    # ---- 着地後の P3 (2026-09-26) の否定側 ----
    # 変異: 根の stat の失敗を「ext2 でない」と言う版 (直す前の姿、err も出ない)
    ("root_stat_fail_as_ext2",
     "                why = HR_ROOT_STAT_FAIL;",
     "                why = HR_ROOT_NOT_EXT2;"),
    # 変異: --root /sys の案内を「シェル再起動」のままにする版
    ("root_sys_note_shell",
     "        if (g_touched_sys && g_root_len > 0)",
     "        if (g_touched_sys && 0)"),
    # 変異: --root /boot の案内を「再起動」のままにする版
    ("root_boot_note_reset",
     "        if (g_touched_boot && g_root_len > 0)",
     "        if (g_touched_boot && 0)"),
    # 変異: --root を正規化しない版 (/hd0/ や //hd0/./ がマウントの根に見えない)
    ("root_not_normalized",
     "!hsp_normalize(root_arg, rnorm, (int)sizeof(rnorm))",
     "!str_ncpy(rnorm, root_arg, (int)sizeof(rnorm))"),
    # 変異: -n では vmkernel.old の門を通さない版 (-n が「通る」と言って本番で断る)
    ("old_gate_skipped_on_dry_run",
     "    if (!g_no_backup && api->version >= 65 &&\n"
     "        api->boot_image_info(&bi) == 0 && bi.crc_valid)",
     "    if (g_dry_run) return 0;\n"
     "    if (!g_no_backup && api->version >= 65 &&\n"
     "        api->boot_image_info(&bi) == 0 && bi.crc_valid)"),
    # 変異: 根がマウントの根かを見ない版 (/hd0/bin や HDD 起動の /hd0)
    ("root_mount_unchecked",
     "        if (!dev[0]) {\n            why = HR_ROOT_NOT_MOUNT;",
     "        if (0) {\n            why = HR_ROOT_NOT_MOUNT;"),
    # 変異: 根の FD 判定を外した版 (理由が dest_on_fd でなくなる)
    ("root_fd_unchecked",
     "        } else if (dev[0] == HS_FD_DEV_PREFIX0 && dev[1] == HS_FD_DEV_PREFIX1) {\n"
     "            why = HR_DST_ON_FD;",
     "        } else if (0) {\n"
     "            why = HR_DST_ON_FD;"),
    # 変異: 同期元 (/host) を根に受ける版
    ("root_source_accepted",
     "    if (str_cmp(root, \"/host\") == 0 || str_has_prefix(root, \"/host/\")) {",
     "    if (0) {"),
    # 変異: 名前規則を完全パスで見る版 (/hd0/etc/settings.db-wal を作る)
    ("root_protect_by_full_path",
     "    int cls = hsp_path_classify(hs_rel(dst_path));",
     "    int cls = hsp_path_classify(dst_path);"),
    # 変異: 実体規則の走査を / の /etc のままにする版 (/hd0 の別名を上書き)
    ("root_protect_scan_slash_etc",
     "    if (!str_ncpy(etc, g_root, (int)sizeof(etc)) ||",
     "    if (!str_ncpy(etc, \"\", (int)sizeof(etc)) ||"),
    # 変異: 名札の照合で根を除かない版 (manifest_extra が増える)
    ("root_manifest_full_path",
     "    const char *rel = hs_rel(dst_path);\n    int i;\n",
     "    const char *rel = dst_path;\n    int i;\n"),
    # 変異: vmkernel.old を / の /boot に作る版
    ("root_old_on_slash",
     "    if (!str_ncpy(old, g_root, (int)sizeof(old)) ||",
     "    if (!str_ncpy(old, \"\", (int)sizeof(old)) ||"),
    # 変異: vmkernel の判定を完全パスで見る版 (.old を作らずに置き換える)
    ("root_kernel_by_full_path",
     "    if (dst_exists && str_cmp(hs_rel(dst_path), HS_KERNEL_PATH) == 0) {",
     "    if (dst_exists && str_cmp(dst_path, HS_KERNEL_PATH) == 0) {"),
    # 変異: 再起動の案内を完全パスで見る版 (/boot を更新した が消える)
    ("root_note_by_full_path",
     "    const char *rel = hs_rel(dst);\n    if (str_has_prefix(rel, \"/sys/\"))  g_touched_sys = 1;\n"
     "    if (str_has_prefix(rel, \"/boot/\")) g_touched_boot = 1;",
     "    const char *rel = hs_rel(dst);\n    if (str_has_prefix(dst, \"/sys/\"))  g_touched_sys = 1;\n"
     "    if (str_has_prefix(dst, \"/boot/\")) g_touched_boot = 1;\n    (void)rel;"),
    # 変異: 絞り込みの宛先に根を前置しない版 (FD の /bin へ書く)
    ("root_subdir_not_prefixed",
     "            !str_ncpy(dst, g_root, (int)sizeof(dst)) ||\n"
     "            !str_ncat(dst, norm, (int)sizeof(dst))) {",
     "            !str_ncpy(dst, \"\", (int)sizeof(dst)) ||\n"
     "            !str_ncat(dst, norm, (int)sizeof(dst))) {"),
    # 変異: 同期先がフロッピーでも断らない版 (Codex 2026-09-25 P2 の否定側)
    ("fd_dest_not_refused",
     "    return dev[0] == HS_FD_DEV_PREFIX0 && dev[1] == HS_FD_DEV_PREFIX1;",
     "    return 0;"),
    # 変異: 再帰でマウントをまたぐ版 (Codex 2026-09-25 2 回目 P2 の否定側)
    ("cross_mount",
     "            if (mdev && mdev[0]) {\n                g_excluded++;",
     "            if (mdev && mdev[0] && 0) {\n                g_excluded++;"),
    # 変異: 既定の sys 除外をマウント判定より先に見る版 (Codex 3 回目 P3 の否定側)。
    # /sys が別マウントでも理由が default_sys_exclusion に隠れて行が出ない
    ("sys_exclusion_hides_mount",
     "        {\n            const char *mdev = api->vfs_devname(dst_path);",
     "        if (depth == 0 && g_root_sync && str_cmp(fl.names[i], \"sys\") == 0) {\n"
     "            g_excluded++;\n"
     "            continue;\n"
     "        }\n"
     "        {\n            const char *mdev = api->vfs_devname(dst_path);"),
    # 変異: 宛先のマウントを遡らずルートだけ見る版 (/fd0 のサブマウントを見逃す)
    ("fd_dest_root_only",
     "        dev = api->vfs_devname(buf);\n        if (dev && dev[0]) break;",
     "        dev = api->vfs_devname(\"/\");\n        if (dev && dev[0]) break;"),
    # 変異 1: 公開の判定を **サイズ** に戻した版 (往復 2 所見 3 の否定側)。
    # -f で新旧が同じ内容だと、未公開を「公開された」と誤る。
    ("publish_by_size",
     "            if (now.st_ino == tmp_ino) {",
     "            if (now.st_size == size || now.st_ino == tmp_ino) {"),
    # 変異 2: NOSYS で**直接上書きへ黙って落ちる**版 (§2-3 手順 2 の否定側)。
    ("fallback_direct_on_nosys",
     "        if (fd == OS32_ERR_NOSYS)\n"
     "            fail_file(dst_path, HR_REPLACE_UNSUPPORTED, fd);",
     "        if (fd == OS32_ERR_NOSYS) {\n"
     "            const char *vr = HR_IO;\n"
     "            if (copy_verify(src_path, dst_path, size, &vr) == 0) return 0;\n"
     "            fail_file(dst_path, HR_REPLACE_UNSUPPORTED, fd);\n"
     "        }"),
    # 変異 3: mtime の失敗を握り潰して置換へ進む版 (Codex 往復 1 所見 4 の否定側)。
    ("mtime_failure_ignored",
     "    mrc = apply_mtime(tmp, dst_path, ss1.st_mtime);\n"
     "    if (mrc < 0) {",
     "    mrc = apply_mtime(tmp, dst_path, ss1.st_mtime);\n"
     "    if (mrc < -9999) {"),
    # 変異 4: 予約名の所有を **st_nlink** で判断する版 (往復 1 所見 2 の否定側)。
    # 手順 2 の EXIST 経路と、§2-4 の掃除の両方に当てる。
    ("temp_owned_by_nlink",
     "    if (dst_protected(tmp) != 0) return -1;                 "
     "/* 保護 > 予約 (R5) */",
     "    if (dst_protected(tmp) != 0) return -1;\n"
     "    if (st.st_nlink > 1) return -1;"),
    ("clean_owned_by_nlink",
     "        if ((st.st_mode & OS_S_IFMT) != OS_S_IFREG) continue;\n",
     "        if ((st.st_mode & OS_S_IFMT) != OS_S_IFREG) continue;\n"
     "        if (st.st_nlink > 1) continue;\n"),
    # 変異 5: 古いカーネルの門を外した版 (R1 の否定側)。
    ("no_kernel_gate",
     "    if (api->version < HS_MIN_KAPI_H2) {",
     "    if (0) {"),
    # 変異 6: 掃除が保護対象を無視する版 (R5 の否定側)。
    ("clean_ignores_protection",
     "            if (prot > 0) {\n"
     "                /* 予約名だが保護対象の実体を指している。**消さない** (R5) */",
     "            if (0) {\n"
     "                /* mutated: 保護を無視する */"),
    # 変異 7: 予約名をコピー元の列挙で弾かない版 (R4 の否定側)。
    ("temp_names_synced",
     "    if (hs_is_temp_name(entry->name)) {\n"
     "        g_excluded++;\n",
     "    if (0) {\n"
     "        g_excluded++;\n"),
    # 変異 8: コピー元の予約名を**黙って**落とす版 (非 blocker 2 の否定側)。
    #         弾くこと自体は変えず、数えるのと -v の行だけを消す。
    ("temp_names_dropped_silently",
     "        g_excluded++;\n"
     "        if (g_verbose) {\n"
     "            const char *dir = fl->src_dir ? fl->src_dir : \"\";\n",
     "        if (0) {\n"
     "            const char *dir = fl->src_dir ? fl->src_dir : \"\";\n"),
    # 変異 9: 手順 9 の sync 失敗で再起動の案内を消す版 (非 blocker 1 の否定側)。
    ("published_note_dropped",
     "        if (published) note_target(dst_path);\n",
     "        if (0) note_target(dst_path);\n"),
    # 変異 10: 手順 8 の replace_partial で案内を消す版
    #          (PM 決裁 2026-09-16 の否定側。公開済みなのに案内が出ない)。
    ("partial_note_dropped",
     '                note = " (公開済み: 宛先は検証済みの新しい内容。'
     '後始末が落ちた)";\n',
     '                note = " (公開済み: 宛先は検証済みの新しい内容。'
     '後始末が落ちた)";\n'
     "                if (0)\n"),
    # 変異 11: 後始末の STALE を独立した 1 件として数え直す版
    #          (非 blocker 4 の否定側 = 1 つの失敗を 2 と数える)。
    ("stale_counted_twice",
     "        (void)drop_temp(tmp);\n",
     "        if (drop_temp(tmp) != 0) g_errors++;\n"),
    # 票 TASK_SERIAL_HOSTFS (レビュー往復 1、Codex 2): vmkernel.old の門
    ("old_no_tmp_selfcheck",
     "        if (hbo_decide(0, 1, bi.image_crc, tok, tcrc, tstored) != HBO_MAKE) {",
     "        if (tok < 0) {"),
    ("old_ignores_stored_field",
     "                     disk_ok, disk_crc, stored);",
     "                     disk_ok, disk_crc, disk_crc);"),
    ("old_backup_failure_ignored",
     "        if (kernel_backup(dst_path, &ds) != 0) return;",
     "        (void)kernel_backup(dst_path, &ds);"),
]


MUT_TARGET = "userland/system/hsync.c"


def host_cmd(exe, src="tools/tests/hsync_h2_host.c"):
    return ["gcc", *HOST_FLAGS, *HOST_INC, str(ROOT / src), "-o", str(exe)]


def one_mutation(item):
    """変異 1 本を一時ディレクトリの写しで組んで回す。(印字, 見逃し) を返す。"""
    name, old, new = item
    original = (ROOT / MUT_TARGET).read_text(encoding="utf-8")
    if old not in original:
        return "MUTATE %-26s SKIP (目印が見つからない)" % name, 1
    with tempfile.TemporaryDirectory(prefix="os32-hsync-h2-mut-") as td:
        exe = pathlib.Path(td) / ("mut-" + name)
        try:
            tree = mutpar.build_in_tree(
                ROOT, td, {MUT_TARGET: original.replace(old, new, 1)},
                [host_cmd(exe)])
        except subprocess.CalledProcessError:
            return "MUTATE %-26s RED (コンパイルが通らない)" % name, 0
        head = "HOST GNU11 -Werror COMPILE PASS (tools/tests/hsync_h2_host.c)\n"
        rc = subprocess.run([str(exe)], cwd=str(tree), timeout=180,
                            capture_output=True).returncode
    if rc == 0:
        return (head + "MUTATE %-26s **GREEN のまま = 試験が規則を見ていない**"
                % name, 1)
    return head + "MUTATE %-26s RED (期待どおり落ちた)" % name, 0


def run_mutations(tmp):
    """否定側。変異は一時ディレクトリの写しにだけ当てる (mutpar で並列、
    実物のソースは読むだけ — check-par で回せる)。"""
    return mutpar.run_with_control(one_mutation, MUTATIONS, ("control", "", ""))


if __name__ == "__main__":
    with tempfile.TemporaryDirectory(prefix="os32-hsync-h2-") as tmp:
        tmp = pathlib.Path(tmp)
        failed = 0

        check_manifest()

        exe = build_host(tmp, "hsync-h2")
        rc = subprocess.run([str(exe)], cwd=ROOT, timeout=180).returncode
        print("EXIT hsync_h2_host=%d" % rc, flush=True)
        failed += rc != 0

        if "--target" in sys.argv:
            subprocess.run(["i386-elf-gcc", *TARGET_USER, "-c",
                            "userland/system/hsync.c",
                            "-o", str(tmp / "hsync.o")], cwd=ROOT, check=True)
            print("TARGET i386-elf -Werror COMPILE PASS "
                  "(userland/system/hsync.c)", flush=True)

        if "--mutate" in sys.argv:
            failed += run_mutations(tmp)

        sys.exit(1 if failed else 0)
