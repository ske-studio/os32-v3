"""H4 (読む側): 配備マニフェスト `.deploy/manifest.txt` を読む規則。

票:   docs/archive/shell/TASK_H4.md §2-1 / §2-3 / §2-3-1 / §4-1
記録: tools/tests/h4_manifest_tdd.md

tools/tests/h4_manifest_host.c が userland/system/hsync.c を 1 行も写さず
そのまま #include し、KernelAPI だけをオンメモリの贋ファイルシステムへ
差し替えて回す (模型ではない)。`/host/.deploy/manifest.txt` に任意の本文を
置けるので、票が挙げた壊し方をそのまま注入できる。

  python3 -B tools/tests/test_h4_manifest.py [--target] [--mutate]

--target は実機と同じ i386-elf クロスコンパイラでも userland/system/hsync.c が
-Werror で通ることの確認 ([C1] C89/GNU89)。

--mutate は**否定側**。この票の中心規則 (読めない名札を「一致」と扱わない /
断るのは全体同期のときだけ / 名札を信じて内容比較を省かない / 大きすぎる
名札は切り詰めず捨てる / build は完全一致) を崩した版で試験が確かに落ちる
ことを確かめる。落ちなければ試験が規則を見ていない。

書く側 (tools/hostdrv_deploy.py) は tools/tests/test_hostdrv_manifest.py。
ここでは**両層が同じ名札を指していること**だけを静的に突き合わせる ([C4])。

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

HOST_FLAGS = ["-std=gnu89", "-Wall", "-Wextra", "-Werror",
              "-Wdeclaration-after-statement",
              "-D__cdecl=", "-D__OS32_USERLAND__"]
HOST_INC = ["-I" + str(ROOT / p) for p in
            (".", "include", "sdk/include", "sdk/include/os32",
             "userland/lib", "userland/system", "lib")]

CROSS_DIR = pathlib.Path(os.environ.get("CROSS_DIR", "/usr/local/cross"))
if not CROSS_DIR.exists():
    alt = pathlib.Path.home() / "opt/cross"
    if alt.exists():
        CROSS_DIR = alt

TARGET_USER = ["-std=gnu89", "-m32", "-march=i386", "-ffreestanding",
               "-fno-pie", "-fno-stack-protector", "-nostdlib",
               "-mno-red-zone", "-fcommon", "-O2",
               "-Wall", "-Wextra", "-Werror",
               "-Wdeclaration-after-statement",
               "-D__OS32_USERLAND__", "-I.", "-Iinclude", "-Isdk/include",
               "-Isdk/include/os32", "-Iuserland/lib",
               "-I" + str(CROSS_DIR / "i386-elf/include")]


def check_manifest():
    """**書く側と読む側が同じ名札を指している**ことを突き合わせる ([C4])。

    ここがずれると、ホストが書いた名札をゲストが一生見つけられないのに
    「名札が無い配備元」として**今までどおり緑になる** — 一番気づきにくい
    壊れ方なので、機械で固定する。
    """
    src = (ROOT / "userland/system/hsync.c").read_text(encoding="utf-8")
    dep = (ROOT / "tools/hostdrv_deploy.py").read_text(encoding="utf-8")

    def cdef(name):
        m = re.search(r"^#define\s+%s\s+(.+?)\s*$" % name, src, re.M)
        if not m:
            raise SystemExit("hsync.c に #define %s が無い" % name)
        return m.group(1).strip()

    def pydef(name):
        m = re.search(r"^%s\s*=\s*'([^']*)'" % name, dep, re.M)
        if not m:
            raise SystemExit("hostdrv_deploy.py に %s が無い" % name)
        return m.group(1)

    rel = cdef("HS_MANIFEST_REL").strip('"')
    host_rel = pydef("MANIFEST_DIR") + "/" + pydef("MANIFEST_NAME")
    if rel != host_rel:
        raise SystemExit(
            "名札の位置がずれている: hsync.c=%r / hostdrv_deploy.py=%r" %
            (rel, host_rel))

    if cdef("HS_MAN_FORMAT").strip('"') != pydef("MANIFEST_FORMAT"):
        raise SystemExit("形式版が両層でずれている")
    if cdef("HS_MAN_SEP").strip('"') != pydef("MANIFEST_SEP"):
        raise SystemExit("区切り行が両層でずれている")

    # 読む側のパス上限は NAME_CAP に揃える (票 §2-3)
    if cdef("HS_MAN_PATH_CAP") != "NAME_CAP":
        raise SystemExit("HS_MAN_PATH_CAP は NAME_CAP に揃えること")
    name_cap = int(cdef("NAME_CAP"))
    man_max = int(cdef("HS_MAN_MAX"))
    # 実配備は約 200 件 (2026-09-16 実測)。上限がそこを割ると、正しい名札を
    # 「大きすぎる」と捨てて --expect-build が常に断る形になる。
    if man_max < 256:
        raise SystemExit(
            "HS_MAN_MAX=%d は実配備 (約 200 件) に対して余裕が無い" % man_max)

    # 書く側の build の上限が読む側に収まること
    m = re.search(r"^MANIFEST_BUILD_CAP\s*=\s*(\d+)", dep, re.M)
    if not m:
        raise SystemExit("hostdrv_deploy.py に MANIFEST_BUILD_CAP が無い")
    if int(m.group(1)) >= name_cap:
        raise SystemExit(
            "MANIFEST_BUILD_CAP=%s が読む側の上限 %d に収まらない"
            % (m.group(1), name_cap))

    # 票が「実装しない」と決めたものを勝手に足していないこと (§2-3 / §2-4)
    # 文字列として出していないこと (コメントでの言及は残してよい)
    if re.search(r'"[^"\n]*manifest_missing', src):
        raise SystemExit(
            "manifest_missing は初期実装では出さない (票 §2-3 / §2-4: 別票)")

    man = (ROOT / "docs/manpages/hsync.1").read_text(encoding="utf-8")
    for word in ("--expect-build", ".deploy/manifest.txt", "manifest_extra",
                 "build_mismatch"):
        if word not in man:
            raise SystemExit("docs/manpages/hsync.1 に %s の記載が無い" % word)

    print("MANIFEST PASS (名札 %s, format=%s, HS_MAN_MAX=%d, "
          "HS_MAN_PATH_CAP=NAME_CAP=%d, man ページに記載)"
          % (rel, cdef("HS_MAN_FORMAT").strip('"'), man_max, name_cap),
          flush=True)


def build_host(tmp, name, src="tools/tests/h4_manifest_host.c"):
    exe = tmp / name
    subprocess.run(["gcc", *HOST_FLAGS, *HOST_INC, str(ROOT / src),
                    "-o", str(exe)], cwd=ROOT, check=True)
    print("HOST GNU89 -Werror COMPILE PASS (%s)" % src, flush=True)
    return exe


MUTATIONS = [
    # 変異 1: --expect-build の門を素通りさせる版 (§2-3 の否定側)。
    ("expect_build_ignored",
     "    if (!g_expect_build) return 0;      /* 既定は表示だけして同期を続ける */",
     "    if (1) return 0;"),
    # 変異 2: 門を main に繋いでいない版 (「1 件も書かない」の否定側)。
    ("gate_not_wired",
     "    man_load();\n    if (man_gate() != 0) {",
     "    man_load();\n    (void)man_gate();\n    if (0) {"),
    # 変異 3: **壊れた名札を「一致」と扱う**版 (往復 1 所見 2 の否定側)。
    #         これが素通りすると H4 が防ぐはずの事故がそのまま起きる。
    ("invalid_manifest_is_match",
     "    api->kprintf(ATTR_RED,\n"
     "                 \"  --expect-build を指定した以上、世代を確かめられないなら\\n\"\n"
     "                 \"  進まない。**1 件も書かない**\\n\");\n"
     "    return 1;",
     "    return 0;"),
    # 変異 4: 名札が**無い**のを「一致」と扱う版 (M6c の否定側)。
    #         全体同期でも素通りしてしまう。
    ("missing_manifest_is_match",
     "    if (!g_root_sync) {\n",
     "    if (!g_root_sync || !g_man_present) {\n"),
    # 変異 5: 絞り込みでも断る版 (往復 2 所見 1 / M12 / M6c2 の否定側)。
    ("narrowed_sync_refused",
     "    if (!g_root_sync) {\n",
     "    if (0) {\n"),
    # 変異 5b: **名札が無いときだけ**絞り込みでも断る版。
    #          PM 決裁 2026-09-16 の否定側 = 直す前の非対称そのもの。
    #          「確かめられない」の扱いを、壊れている場合と無い場合で変える。
    ("absent_refused_when_narrowed",
     "    if (!g_root_sync) {\n",
     "    if (g_man_present && !g_root_sync) {\n"),
    # 変異 5c: 不一致を絞り込みで見逃す版。
    #          **「確かめた結果おかしい」と「確かめられない」を混ぜる**版で、
    #          protection の本体が絞り込みで抜ける。
    ("mismatch_excused_when_narrowed",
     "        if (str_cmp(g_man_build, g_expect_build) == 0) return 0;",
     "        if (str_cmp(g_man_build, g_expect_build) == 0 || !g_root_sync)\n"
     "            return 0;"),
    # 変異 6: **名札を信じて内容比較を省く**版 (M10 の否定側)。
    ("trust_manifest_skip_compare",
     "        int mtime_known = (ss.st_mtime != 0 && ds.st_mtime != 0);\n"
     "        int mtime_same  = mtime_known && (ss.st_mtime == ds.st_mtime);\n",
     "        int mtime_known = (ss.st_mtime != 0 && ds.st_mtime != 0);\n"
     "        int mtime_same  = mtime_known && (ss.st_mtime == ds.st_mtime);\n"
     "        {\n"
     "            int mi = man_find(dst_path);\n"
     "            if (mi >= 0 && g_man[mi].size == size) {\n"
     "                g_unchanged++;\n"
     "                return;\n"
     "            }\n"
     "        }\n"),
    # 変異 7: 壊れた名札を捨てずに半端な表のまま使う版 (§2-3 の否定側)。
    ("broken_manifest_used",
     "    if (!g_man_valid) g_man_count = 0;      /* 半端な表を残さない */",
     "    if (!g_man_valid) g_man_valid = 1;"),
    # 変異 8: 大きすぎる名札を**切り詰めて**使う版 (M9b の否定側)。
    ("too_many_entries_truncated",
     "    if (want > (u32)HS_MAN_MAX) { g_man_bad = \"too many entries\"; return 0; }",
     "    if (want > (u32)HS_MAN_MAX) want = (u32)HS_MAN_MAX;"),
    # 変異 9: count より行が多いのを見逃す版 (§2-3 の否定側)。
    ("count_mismatch_ignored",
     "    if (man_next_line(&p)) { g_man_bad = \"count mismatch\"; return 0; }",
     "    if (0) { g_man_bad = \"count mismatch\"; return 0; }"),
    # 変異 10: build を**前方一致**で見る版 (完全一致の否定側)。
    ("build_compare_prefix",
     "        if (str_cmp(g_man_build, g_expect_build) == 0) return 0;",
     "        if (str_has_prefix(g_man_build, g_expect_build)) return 0;"),
    # 変異 11: build の**大小を無視する**版 (§2-3「大小を比べない」の否定側)。
    ("build_compare_case_insensitive",
     "        if (str_cmp(g_man_build, g_expect_build) == 0) return 0;",
     "        {\n"
     "            int ci;\n"
     "            for (ci = 0; ; ci++) {\n"
     "                char ca = g_man_build[ci];\n"
     "                char cb = g_expect_build[ci];\n"
     "                if (ca >= 'A' && ca <= 'Z') ca = (char)(ca + 32);\n"
     "                if (cb >= 'A' && cb <= 'Z') cb = (char)(cb + 32);\n"
     "                if (ca != cb) break;\n"
     "                if (!ca) return 0;\n"
     "            }\n"
     "        }"),
    # 変異 12: manifest_extra を数えない版 (M9 の否定側)。
    ("extra_not_counted",
     "    g_man_extra++;\n    if (g_verbose)",
     "    if (g_verbose)"),
    # 変異 13: 名札自身の写しを extra に数える版 (まっさらなゲストで必ず
    #          manifest_extra=1 になり、本当の食い違いが埋もれる)。
    ("manifest_self_counted",
     "    if (str_cmp(rel, HS_MANIFEST_REL) == 0) return;",
     "    if (0) return;"),
    # 変異 14: 世代の表示を落とす版 (受入 4-2 の 1 行目の否定側)。
    ("deploy_line_dropped",
     "        api->kprintf(ATTR_CYAN, \"DEPLOY build=%s count=%d generated=%s kapi=%d/v%d\\n\",",
     "        if (0) api->kprintf(ATTR_CYAN, \"DEPLOY build=%s count=%d generated=%s kapi=%d/v%d\\n\","),
    # 変異 15: 壊れた名札を黙って捨てる版 (M6 の「表示する」の否定側)。
    ("invalid_line_dropped",
     "        api->kprintf(ATTR_YELLOW, \"DEPLOY manifest invalid: %s\\n\",",
     "        if (0) api->kprintf(ATTR_YELLOW, \"DEPLOY manifest invalid: %s\\n\","),
    # ---- 票 TASK_KAPI_DATA_FIELDS (KAPI の門、case_kapi) ----
    # 変異 K1: KAPI の門を main に繋いでいない版。
    ("kapi_gate_not_wired",
     "    if (man_kapi_gate(subdir ? norm : 0) != 0) {",
     "    if (0 && man_kapi_gate(subdir ? norm : 0) != 0) {"),
    # 変異 K2: 配置違いを見逃す版 (この票の中心)。
    ("kapi_layout_ignored",
     "    if (g_man_kapi != kernel_off) { *why = HR_KAPI_LAYOUT; return 1; }",
     "    if (0 && g_man_kapi != kernel_off) { *why = HR_KAPI_LAYOUT; return 1; }"),
    # 変異 K3: 「host の版 > カーネルの版」を見逃す版 (決裁 2026-09-24)。
    ("kapi_newer_ignored",
     "    if (!boot_only && g_man_kapi_ver > kernel_ver) {",
     "    if (0 && boot_only && g_man_kapi_ver > kernel_ver) {"),
    # 変異 K3b: /boot だけの同期も「版が新しい」で断る版 (カーネルを先に
    #   運べなくなる — Codex 実装レビュー R1 blocker 2)。
    ("kapi_boot_not_exempt",
     "    if (!boot_only && g_man_kapi_ver > kernel_ver) {",
     "    if ((boot_only || 1) && g_man_kapi_ver > kernel_ver) {"),
    # 変異 K3c: /boot の除外が配置違いまで開けてしまう版。
    ("kapi_boot_skips_layout",
     "    if (g_man_kapi != kernel_off) { *why = HR_KAPI_LAYOUT; return 1; }",
     "    if (!boot_only && g_man_kapi != kernel_off) { *why = HR_KAPI_LAYOUT; return 1; }"),
    # 変異 K3d: `/boot` の接頭辞判定が緩い版 (`/bootx` まで除外する)。
    ("kapi_boot_prefix_loose",
     "                      str_has_prefix(target, \"/boot/\")));",
     "                      str_has_prefix(target, \"/boot\")));"),
    # 変異 K4: 名札が無いのを一致と扱う版。
    ("kapi_absent_is_match",
     "    if (!g_man_present) { *why = HR_MANIFEST_ABSENT;  return 1; }",
     "    if (!g_man_present) { *why = 0; return 0; }"),
    # 変異 K5: 壊れた名札を一致と扱う版。
    ("kapi_invalid_is_match",
     "    if (!g_man_valid)   { *why = HR_MANIFEST_INVALID; return 1; }",
     "    if (!g_man_valid)   { *why = 0; return 0; }"),
    # 変異 K6: -f が KAPI の門まで開けてしまう版。
    ("force_opens_kapi_gate",
     "                g_force = 1;\n",
     "                g_force = 1;\n                g_force_kapi = 1;\n"),
    # 変異 K7: kapi= を任意の鍵にする版 (欠落を一致と扱う)。
    ("kapi_key_optional",
     "    if (!have_format || !have_build || !have_gen || !have_count ||\n"
     "        !have_kapi || !have_kapi_ver) {",
     "    if (!have_format || !have_build || !have_gen || !have_count) {"),
    # 変異 16: CRC の大文字を通す版 (§2-1「8 桁 16 進、小文字」の否定側)。
    ("crc_uppercase_accepted",
     "        else if (c >= 'a' && c <= 'f') d = c - 'a' + 10;",
     "        else if (c >= 'a' && c <= 'f') d = c - 'a' + 10;\n"
     "        else if (c >= 'A' && c <= 'F') d = c - 'A' + 10;"),
    # 変異 17: 絶対パスを読む版 (§2-1「絶対パスと `..` は読まない」の否定側)。
    ("absolute_path_accepted",
     "    if (!p[0] || p[0] == '/') return 0;\n"
     "    for (;;) {\n"
     "        char c = p[i];\n"
     "        if (c == '\\0' || c == '/') {\n"
     "            int n = i - s;\n"
     "            if (n == 0) return 0;",
     "    if (!p[0]) return 0;\n"
     "    for (;;) {\n"
     "        char c = p[i];\n"
     "        if (c == '\\0' || c == '/') {\n"
     "            int n = i - s;\n"
     "            if (n == 0 && i > 0) return 0;"),
]


MUT_TARGET = "userland/system/hsync.c"


def host_cmd(exe, src="tools/tests/h4_manifest_host.c"):
    return ["gcc", *HOST_FLAGS, *HOST_INC, str(ROOT / src), "-o", str(exe)]


def one_mutation(item):
    """変異 1 本を一時ディレクトリの写しで組んで回す。(印字, 見逃し) を返す。"""
    name, old, new = item
    original = (ROOT / MUT_TARGET).read_text(encoding="utf-8")
    if old not in original:
        return "MUTATE %-30s SKIP (目印が見つからない)" % name, 1
    with tempfile.TemporaryDirectory(prefix="os32-h4-mut-") as td:
        exe = pathlib.Path(td) / ("mut-" + name)
        try:
            tree = mutpar.build_in_tree(
                ROOT, td, {MUT_TARGET: original.replace(old, new, 1)},
                [host_cmd(exe)])
        except subprocess.CalledProcessError:
            return "MUTATE %-30s RED (コンパイルが通らない)" % name, 0
        head = "HOST GNU89 -Werror COMPILE PASS (tools/tests/h4_manifest_host.c)\n"
        rc = subprocess.run([str(exe)], cwd=str(tree), timeout=300,
                            capture_output=True).returncode
    if rc == 0:
        return (head + "MUTATE %-30s **GREEN のまま = 試験が規則を見ていない**"
                % name, 1)
    return head + "MUTATE %-30s RED (期待どおり落ちた)" % name, 0


def run_mutations(tmp):
    """否定側。変異は一時ディレクトリの写しにだけ当てる (mutpar で並列、
    実物のソースは読むだけ — check-par で回せる)。"""
    return mutpar.run_with_control(one_mutation, MUTATIONS, ("control", "", ""))


if __name__ == "__main__":
    with tempfile.TemporaryDirectory(prefix="os32-h4-") as tmp:
        tmp = pathlib.Path(tmp)
        failed = 0

        check_manifest()

        exe = build_host(tmp, "h4-manifest")
        rc = subprocess.run([str(exe)], cwd=ROOT, timeout=300).returncode
        print("EXIT h4_manifest_host=%d" % rc, flush=True)
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
