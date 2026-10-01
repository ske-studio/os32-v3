"""H3: mtime の取得・保存と、日時を前置フィルタにした同一判定。

票:   docs/archive/shell/TASK_H3.md §6 / §8
      docs/tasks/shell/HSYNC_IMPROVEMENT_PLAN.md §5 / §7.2 / §9 の A03 A04 A16
記録: tools/tests/h3_tdd.md

tools/tests/hsync_h3_host.c が userland/system/hsync.c を 1 行も写さず
そのまま #include し、KernelAPI だけをオンメモリの贋ファイルシステムへ
差し替えて回す (模型ではない)。贋 FS は**ノードごとに mtime を持ち**、
sys_set_mtime の成功 / NOSYS / I/O 失敗を注入できる。sys_read の呼び出し
回数も数えるので「サイズも日時も同じなら 1 バイトも読まない」を直に見られる。

fs/hostdrv_stat_rules.inc の FILETIME 変換 (hdrv_filetime_to_unix /
hdrv_stat_mtime) も同じ翻訳単位で直接叩く (A16)。

  python3 -B tools/tests/test_hsync_h3.py [--target] [--mutate]

--target を付けると、実機と同じ i386-elf クロスコンパイラでも
hsync.c / fs/hostdrvfs.c / fs/ext2_vfs.c / kapi/kapi_sys.c が -Werror で
通ることを確かめる ([C1] GNU11)。

--mutate は**否定側**。この票の中心規則は
「証拠が無いことを同一の根拠にしない」なので、日時が不明 (0) のときに
スキップしてしまう版へ差し替え、試験が確かに落ちることを確かめる。
落ちなければ試験が規則を見ていない。

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

TARGET_COMMON = ["-std=gnu11", "-m32", "-march=i386", "-ffreestanding",
                 "-fno-pie", "-fno-stack-protector", "-nostdlib",
                 "-mno-red-zone", "-fcommon", "-O2",
                 "-Wall", "-Wextra", "-Werror"]
TARGET_USER = TARGET_COMMON + [
    "-D__OS32_USERLAND__", "-I.", "-Iinclude", "-Isdk/include",
    "-Isdk/include/os32", "-Iuserland/lib",
    "-I" + str(CROSS_DIR / "i386-elf/include")]
TARGET_KERNEL = TARGET_COMMON + [
    # arch/x86 + platform/pc98: include/io.h は契約だけで、実装は固定名
    # arch_io.h / platform_io.h を引く (順序 3)。
    "-D__KERNEL_BUILD__", "-I.", "-Iinclude",
    "-Iarch/x86", "-Iplatform/pc98", "-Isdk/include",
    "-Isdk/include/os32", "-Ikernel", "-Idrivers", "-Inet", "-Ifs",
    "-Iexec", "-Igfx", "-Ilib", "-Ikapi"]


# ---------------------------------------------------------------------------
#  [ABI2] の主張のしかた (2026-09-16 に直した)
#
#  ここは以前 `api[-1]["name"] != "sys_set_mtime"` — つまり「sys_set_mtime が
#  **永遠に kapi.json の末尾**であること」を要求していた。それは [ABI2] の本旨
#  ではない。[ABI2] が禁じるのは **既存スロットを動かす / 消す** ことであって、
#  末尾への追記はむしろ [ABI2] が正当と定めた唯一の増やし方なので、この主張は
#  **KAPI が 1 本増えるたびに必ず落ちる**。実際 2026-09-16 に kbd_peekkey (v54)
#  の末尾追記で `make check` が落ちた。
#
#  `kapi/kapi_db.c` の `db_slot_layout_ok` が同じ形 (`KAPI_SLOT_COUNT ==
#  KAPI_SLOT_HOST_CLOSE + 1` の等号) を踏んで**下限比較**に直した前例がある
#  (同ファイルの見出しコメント)。ここも同じ考え方で **位置**だけを見る:
#
#    1. sys_set_mtime が**存在する** (消されていない)
#    2. その slot 番号が票 H3 で決まった 213 から**動いていない**
#    3. その**前**の並びが 1 つも変わっていない (名前列の SHA-256)
#    4. 表の長さは**下限だけ** — 後ろに何本足されていてもよい
#
#  3 の digest は「213 番目までの名前」なので、末尾追記では値が動かない。
#  KAPI を足すたびに更新する必要は無い (更新が要るなら、それは [ABI2] 違反)。
# ---------------------------------------------------------------------------
SYS_SET_MTIME_SLOT = 213        # 票 H3 (v52) で決まった位置

#  api[0..213] の名前を "," で繋いだ文字列の SHA-256。
SYS_SET_MTIME_PREFIX_SHA256 = (
    "158a679886fb53f2b4824f69742f5378f03ee9a0d66ab9ae757657c8d40458b6")


def check_sys_set_mtime_slot(api):
    """sys_set_mtime が H3 で決まった位置から動いていないこと ([ABI2])。

    末尾かどうかは**見ない**。見るのは位置と、その前の並び。
    """
    import hashlib

    names = [e["name"] for e in api]
    if "sys_set_mtime" not in names:
        raise SystemExit("sys_set_mtime が kapi.json から消えている ([ABI2])")

    slot = names.index("sys_set_mtime")
    if slot != SYS_SET_MTIME_SLOT:
        raise SystemExit(
            "sys_set_mtime の slot が %d から %d へ動いた ([ABI2] — 既存スロットは"
            " 並べ替えても消してもいけない)" % (SYS_SET_MTIME_SLOT, slot))

    # 長さは下限だけ (末尾追記は正当なので上限は見ない)
    if len(api) <= SYS_SET_MTIME_SLOT:
        raise SystemExit("kapi.json の api が slot %d に届いていない ([ABI2])"
                         % SYS_SET_MTIME_SLOT)

    digest = hashlib.sha256(
        ",".join(names[:SYS_SET_MTIME_SLOT + 1]).encode("utf-8")).hexdigest()
    if digest != SYS_SET_MTIME_PREFIX_SHA256:
        raise SystemExit(
            "slot 0..%d の並びが変わった ([ABI2])。末尾追記なら digest は動かない"
            " — 動いたということは既存スロットを並べ替えた / 消した / 改名した"
            % SYS_SET_MTIME_SLOT)

    # 生成物の slot 定数も同じ位置を指していること (再生成の取りこぼし検出)
    slots_h = (ROOT / "sdk/include/os32/os32_kapi_slots.h").read_text(
        encoding="utf-8")
    m = re.search(r"#define\s+KAPI_SLOT_SYS_SET_MTIME\s+(\d+)", slots_h)
    if not m or int(m.group(1)) != SYS_SET_MTIME_SLOT:
        raise SystemExit("os32_kapi_slots.h の KAPI_SLOT_SYS_SET_MTIME が %d でない"
                         " (再生成していない / 手編集した)" % SYS_SET_MTIME_SLOT)
    m = re.search(r"#define\s+KAPI_SLOT_COUNT\s+(\d+)", slots_h)
    if not m or int(m.group(1)) <= SYS_SET_MTIME_SLOT:
        raise SystemExit("os32_kapi_slots.h の KAPI_SLOT_COUNT が slot %d に届かない"
                         % SYS_SET_MTIME_SLOT)
    return slot


def check_kapi_slot():
    """[ABI1]/[ABI2]: sys_set_mtime が H3 で決まった slot に居て、版が揃う。

    **末尾かどうかは見ない** (理由は check_sys_set_mtime_slot の見出し)。
    既存スロットが動いた瞬間に配備済みバイナリは「別の関数を呼ぶ」という
    最も静かな壊れ方をする。生成物を手で触っていないことも、生成ヘッダの
    KAPI_FUNC_COUNT と slot 定数で見る。
    """
    import json

    kapi = json.loads((ROOT / "sdk/kapi.json").read_text(encoding="utf-8"))
    api = kapi["api"]
    slot = check_sys_set_mtime_slot(api)

    shared = (ROOT / "sdk/include/os32/os32_kapi_shared.h").read_text(
        encoding="utf-8")
    m = re.search(r"#define\s+KAPI_VERSION\s+(\d+)", shared)
    if not m or int(m.group(1)) != int(kapi["version"]):
        raise SystemExit("KAPI_VERSION が kapi.json の version と違う")

    gen = (ROOT / "sdk/include/os32/os32_kapi_generated.h").read_text(
        encoding="utf-8")
    m = re.search(r"#define\s+KAPI_FUNC_COUNT\s+(\d+)", gen)
    if not m or int(m.group(1)) != len(api):
        raise SystemExit("生成ヘッダの KAPI_FUNC_COUNT が kapi.json と違う "
                         "(再生成していない / 手編集した)")

    # hsync は sys_set_mtime (v52) を呼ぶので、要求版はそれ以上であること。
    #
    # **「最新の KAPI と同じ版」ではない** (票 H2 §1 の 7 / §2-3 末尾で変更)。
    # 票 H2 の hsync は v53 の O_EXCL を使うが、カーネルを入れ替える途中で
    # **v52 のカーネルの上で動かす場面が現に起こる**。要求版を 53 にすると
    # exec/exec.c の `min_api_ver > KAPI_VERSION` が「invalid OS32X binary」と
    # して起動そのものを拒否し、hsync 自身の kernel_too_old も
    # --unsafe-overwrite も届かない。版の判定は hsync が api->version で行う。
    # 上限 (kapi.json の version 以下) は据え置き — 存在しない版を要求しない。
    HSYNC_MIN_API = 52          # sys_set_mtime が入った版 (票 H3)
    conf = (ROOT / "build/app.conf").read_text(encoding="utf-8")
    m = re.search(r"^userland/system/hsync\s+(\d+)", conf, re.M)
    if not m:
        raise SystemExit("build/app.conf に userland/system/hsync が無い")
    if int(m.group(1)) < HSYNC_MIN_API:
        raise SystemExit("build/app.conf の hsync の要求 API 版が v%d 未満 "
                         "(sys_set_mtime が無い)" % HSYNC_MIN_API)
    if int(m.group(1)) > int(kapi["version"]):
        raise SystemExit("build/app.conf の hsync の要求 API 版が kapi.json の "
                         "v%s を越えている" % kapi["version"])

    print("KAPI SLOT PASS (sys_set_mtime = slot %d / 表は %d 本, v%s, "
          "app.conf hsync=%s)" % (slot, len(api), kapi["version"],
                                  m.group(1)), flush=True)


def build_host(tmp, src, name, extra=()):
    exe = tmp / name
    cmd = ["gcc", *HOST_FLAGS, *HOST_INC, *extra,
           str(ROOT / src), "-o", str(exe)]
    subprocess.run(cmd, cwd=ROOT, check=True)
    print("HOST GNU11 -Werror COMPILE PASS (%s)" % src, flush=True)
    return exe


def build_vfs(tmp, name, extra=()):
    exe = tmp / name
    inc = ["-I" + str(ROOT / p)
           for p in ("include", "fs", "lib", "kernel", "drivers",
                     "sdk/include/os32")]
    cmd = ["gcc", "-std=gnu11", "-Wall", "-Wextra", "-Werror",
           "-Wno-unused-parameter", "-Wno-sign-compare",
           "-D__cdecl=", *inc, *extra,
           str(ROOT / "tools/tests/vfs_set_mtime_host.c"), "-o", str(exe)]
    subprocess.run(cmd, cwd=ROOT, check=True)
    print("HOST GNU11 -Werror COMPILE PASS (real fs/vfs.c)", flush=True)
    return exe


def target_compile(tmp):
    jobs = [
        (TARGET_USER, "userland/system/hsync.c", []),
        (TARGET_KERNEL, "fs/hostdrvfs.c", ["-Wno-address-of-packed-member"]),
        (TARGET_KERNEL, "fs/ext2_vfs.c", []),
        (TARGET_KERNEL, "fs/fatfs_vfs.c", []),
        (TARGET_KERNEL, "fs/iso9660.c", []),
        (TARGET_KERNEL, "fs/vfs.c", ["-Wno-sign-compare",
                                     "-Wno-unused-parameter"]),
        (TARGET_KERNEL, "kapi/kapi_sys.c", []),
    ]
    for flags, src, extra in jobs:
        out = tmp / (pathlib.Path(src).stem + ".o")
        subprocess.run(["i386-elf-gcc", *flags, *extra, "-c", src,
                        "-o", str(out)], cwd=ROOT, check=True)
        print("TARGET i386-elf -Werror COMPILE PASS (%s)" % src, flush=True)


# 変異: 「日時が不明 (0) でも、両側が同じなら読まずに省略する」版。
# これは票 §8 が禁じた実装そのもの。試験が落ちなければ規則を見ていない。
MUTATIONS = [
    ("mtime_unknown_is_same",
     "int mtime_known = (ss.st_mtime != 0 && ds.st_mtime != 0);",
     "int mtime_known = 1;"),
    # 変異 2: 内容が同じでも「日時が違う」だけで本体をコピーしてしまう版。
    # コピーするかどうかを決めるのは**内容比較**であって日時ではない
    # (A03 が copied=0 / metadata_updated=1 を期待するので落ちる)。
    ("copy_on_mtime_diff",
     "        cmp = compare_files(src_path, dst_path, size);",
     "        cmp = compare_files(src_path, dst_path, size);\n"
     "        if (cmp == 0 && !mtime_same) cmp = 1;"),
    # 変異 6: --verify を無視して日時でスキップする版。
    ("verify_ignored",
     "        if (!g_verify && mtime_same) {",
     "        if (mtime_same) {"),
    # 変異 7: 設定失敗を握り潰して成功と言う版 (metadata_failed の否定側)。
    # 目印は票 H2 で apply_mtime が (target, label) の 2 経路になったのに
    # 合わせた。見るもの (設定失敗を握り潰さない) は変えていない。
    ("swallow_set_failure",
     "    fail_file(label, HR_META_FAILED, rc);\n    return -1;",
     "    return 0;"),
    # 変異 3: 時刻の保存を省く版。A03 / A04 が落ちなければ保存を見ていない。
    ("no_set_mtime",
     "    rc = api->sys_set_mtime(target, src_mtime);",
     "    (void)target; rc = 0;"),
    # 変異 4: FILETIME の起点差を足し忘れる (1601 起点のまま返す) 版。
    ("filetime_no_epoch_shift",
     "    secs = (ft - HDRV_FT_EPOCH_DIFF_100NS) / HDRV_FT_PER_SEC;",
     "    secs = ft / HDRV_FT_PER_SEC;"),
    # 変異 5: os_time_t の範囲外を切り詰めてしまう (wrap する) 版。
    ("filetime_wrap",
     "    if (secs > HDRV_STAT_MAX_TIME) return 0;         /* (c) 範囲外 */",
     "    /* mutated: 範囲検査を外す */"),
]


MUT_TARGETS = {
    "control": "userland/system/hsync.c",
    "mtime_unknown_is_same": "userland/system/hsync.c",
    "copy_on_mtime_diff": "userland/system/hsync.c",
    "no_set_mtime": "userland/system/hsync.c",
    "filetime_no_epoch_shift": "fs/hostdrv_stat_rules.inc",
    "filetime_wrap": "fs/hostdrv_stat_rules.inc",
    "verify_ignored": "userland/system/hsync.c",
    "swallow_set_failure": "userland/system/hsync.c",
}


def one_mutation(item):
    """変異 1 本を一時ディレクトリの写しで組んで回す。(印字, 見逃し) を返す。"""
    name, old, new = item
    rel = MUT_TARGETS[name]
    original = (ROOT / rel).read_text(encoding="utf-8")
    if old not in original:
        return "MUTATE %-26s SKIP (目印が見つからない)" % name, 1
    src = "tools/tests/hsync_h3_host.c"
    with tempfile.TemporaryDirectory(prefix="os32-hsync-h3-mut-") as td:
        exe = pathlib.Path(td) / ("mut-" + name)
        cmd = ["gcc", *HOST_FLAGS, *HOST_INC, str(ROOT / src), "-o", str(exe)]
        try:
            tree = mutpar.build_in_tree(
                ROOT, td, {rel: original.replace(old, new, 1)}, [cmd])
        except subprocess.CalledProcessError:
            return "MUTATE %-26s NOT COUNTED (コンパイルが通らない)" % name, 1
        head = "HOST GNU11 -Werror COMPILE PASS (%s)\n" % src
        rc = subprocess.run([str(exe)], cwd=str(tree), timeout=120,
                            capture_output=True).returncode
    if rc == 0:
        return (head + "MUTATE %-26s **GREEN のまま = 試験が規則を見ていない**"
                % name, 1)
    return head + "MUTATE %-26s RED (期待どおり落ちた)" % name, 0


def run_mutations(tmp):
    """変異させたソースで試験が **落ちる** ことを確かめる。変異は一時
    ディレクトリの写しにだけ当てる (mutpar で並列、check-par で回せる)。"""
    return mutpar.run_with_control(one_mutation, MUTATIONS,
                                   ("control", "", ""))


if __name__ == "__main__":
    with tempfile.TemporaryDirectory(prefix="os32-hsync-h3-") as tmp:
        tmp = pathlib.Path(tmp)
        failed = 0

        check_kapi_slot()

        exe = build_host(tmp, "tools/tests/hsync_h3_host.c", "hsync-h3")
        rc = subprocess.run([str(exe)], cwd=ROOT, timeout=120).returncode
        print("EXIT hsync_h3_host=%d" % rc, flush=True)
        failed += rc != 0

        exe = build_vfs(tmp, "vfs-set-mtime")
        rc = subprocess.run([str(exe)], cwd=ROOT, timeout=60).returncode
        print("EXIT vfs_set_mtime_host=%d" % rc, flush=True)
        failed += rc != 0

        if "--target" in sys.argv:
            target_compile(tmp)

        if "--mutate" in sys.argv:
            failed += run_mutations(tmp)

        sys.exit(1 if failed else 0)
