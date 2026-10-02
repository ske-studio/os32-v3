"""票 TASK_HDD_INSTALL 段 2 (インストーラ cdinst / install) のホスト試験。

記録: tools/tests/hdd_stage2_tdd.md
票  : docs/archive/realhw_v21/TASK_HDD_INSTALL.md 段 2 (9〜11) / §1-v3 (N4・N6・N8・R3-1)

実物を 1 行も写さずに回す:
  tools/tests/hdd_stage2_host.c   userland/system/inst_disk.c (+ pc98pt.c・
                                  hdprep_plan.c・ext2_layout.c) の判定
  tools/tests/cdinst_host.c       userland/system/cdinst.c を main から
                                  (+ inst_hdd.c・pkg.c、PKG は試験が組む)
  tools/tests/install_fresh_host.c userland/system/install.c を main から
                                  (段 2 の 6 段、test_install_fresh.py と共有)
  tools/tests/ext2_mini_host.c    boot/ext2_mini.c (mke2fs + debugfs の像)
見るもの:
  - 8/17 と 16/63 で区画表 (標準配置) と IPL の [8]/[9] の幾何が一致し、
    開始は 1632 / 2016、長さは 256MiB まで
  - 空・再作成 (標準 / 旧配置 8/17)・未知・2 項目・開始違い・55AA・壊れ・旧配置 16/63
  - 事前検査 (IPL / ローダ / vmkernel の大きさ・必須の中身・容量・前置の溢れ・
    幾何・マウント) の失敗で 1 セクタも書かない
  - format / 区画表の読み戻し / マウント / ローダ / IPL / 展開 / sync の失敗で
    INCOMPLETE と出し「完了」と言わない
  - 容量の見積もりの空きが実物の ext2_format_at の像 (dumpe2fs) と一致する
  - ext2_mini は 508KiB を超えるファイルを切り詰めずにエラーにする

  python3 -B tools/tests/test_hdd_stage2.py            # 全部
  python3 -B tools/tests/test_hdd_stage2.py --target   # + i386-elf -Werror で新しいソース
  python3 -B tools/tests/test_hdd_stage2.py --mutate   # 否定側 (変異が RED になるか)
"""
import host32
import importlib.util
import pathlib
import re
import shutil
import subprocess
import sys
import tempfile

import mutpar  # noqa: E402  (tools/tests/mutpar.py、同じディレクトリ)

ROOT = pathlib.Path(__file__).resolve().parents[2]
PURE = ROOT / "tools/tests/hdd_stage2_host.c"
CDI = ROOT / "tools/tests/cdinst_host.c"
INS = ROOT / "tools/tests/install_fresh_host.c"
MINI = ROOT / "tools/tests/ext2_mini_host.c"

PURE_CASES = ["classify817", "classify1663", "paths", "bootfiles", "blocks", "space", "iplpt"]
CDI_CASES = ["ok817", "ok1663", "modes", "preflight", "incomplete", "paths", "final",
             "erase", "erase_fail", "erase_mount", "keys"]
INS_CASES = ["nokernel", "precheck", "boot_fail", "sync_fail", "geom817", "geom1663",
             "modes", "preflight", "incomplete", "rerun", "erase", "erase_fail", "erase_mount"]
MAX_IMAGE = 508 * 1024
# (像の中の名前, 大きさ, 期待: 大きさ か -2 = EXT2M_ERR_TOO_BIG)
MINI_FILES = [("small", 1000, 1000), ("exact", MAX_IMAGE, MAX_IMAGE),
              ("over1", MAX_IMAGE + 1, -2), ("over", 600 * 1024, -2)]
# 空きを実物の format_at と突き合わせる大きさ (test_hdd_stage1.py の像と同じ)
ROOM_SIZES = [16652, 20160, 36000, 62496]
# 1 ケースの上限 (秒)。鍵を読まなくなる変異は贋物が 100000 回で落とすが、二重の守り
# (mutate は TimeoutExpired を RED と数える)
CASE_TIMEOUT = 120

SHARED = ["userland/system/inst_disk.c", "drivers/pc98pt.c",
          "userland/shell/hdprep_plan.c", "fs/ext2_layout.c"]
INS_SHARED = ["userland/system/inst_hdd.c"] + SHARED

HOST = ["gcc", "-std=gnu11", "-Wall", "-Wextra", "-Werror",
        "-D__cdecl="]
ILP32 = ["gcc", "-std=gnu11", "-m32", "-march=i386", "-ffreestanding", "-fno-pie",
         "-fno-stack-protector", "-fno-builtin", "-nostdlib", "-static", "-O1",
         "-Wall", "-Wextra", "-Werror", "-Wno-unused-parameter", "-Wno-sign-compare",
         "-Wno-unused-function", "-D__cdecl="]

MIRROR = ["userland/system/inst_disk.c", "userland/system/inst_disk.h",
          "userland/system/inst_hdd.c", "userland/system/inst_hdd.h",
          "userland/system/cdinst.c", "userland/system/install.c",
          "userland/system/install_recover.inc",
          "userland/shell/hdprep_plan.c", "userland/shell/hdprep_plan.h",
          "drivers/pc98pt.c", "drivers/pc98pt.h", "fs/ext2_layout.c", "fs/ext2_layout.h",
          "boot/ext2_mini.c", "boot/boot_defs.h",
          "userland/lib/rt/pkg.c", "userland/lib/rt/pkg.h", "userland/lib/rt/dbgserial.h"]


def run(cmd, **kw):
    return host32.run(cmd, cwd=ROOT, **kw)


def _harness(src, tmp, root):
    """root が ROOT でなければ、ハーネスの "../../x" を写しの root/x に向ける。"""
    if root == ROOT:
        return src
    text = src.read_text(encoding="utf-8").replace('"../../', '"' + str(root) + "/")
    out = pathlib.Path(tmp) / src.name
    out.write_text(text, encoding="utf-8")
    return out


def _cc(cmd, quiet):
    r = run(cmd, capture_output=True, text=True)
    if r.returncode != 0:
        if not quiet:
            sys.stderr.write(r.stderr)
        return False
    return True


HARNESS_KEYS = ("pure", "cdi", "ins", "mini")

# 変異を当てたファイル → 組み直して回すハーネス (票 TASK_CHECK_MUT_PARALLEL)。
# ほかのハーネスは実物で組んだもの (main の 1 回目、全部通ったもの) をそのまま使い、
# ケースも回さない (同じ実行ファイル・同じ入力なので結果も同じ)。表に無いファイルは
# 全部組む。表が gcc -MM の依存より狭ければ mutate() が落ちる (mutpar.check_rebuild_table)。
REBUILD = {
    "userland/system/inst_disk.c": ("pure", "cdi", "ins"),
    "userland/system/inst_hdd.c": ("cdi", "ins"),
    "userland/system/cdinst.c": ("cdi",),
    "userland/system/install.c": ("ins",),
    "boot/ext2_mini.c": ("mini",),
}


def _commands(tmp, root):
    """ハーネスごとの gcc のコマンド。"""
    tmp = pathlib.Path(tmp)
    inc = ["-I" + str(root), "-I" + str(root / "include"), "-I" + str(ROOT / "include")]
    cinc = ["-I" + str(root), "-I" + str(root / "userland/lib"),
            "-I" + str(ROOT / "include"), "-I" + str(ROOT / "sdk/include/os32"),
            "-I" + str(ROOT / "tools/tests/mtar_freestanding")]
    iinc = ["-I" + str(root / "userland/system"), "-I" + str(root)] + \
           ["-I" + str(ROOT / p) for p in ("include", "sdk/include", "sdk/include/os32",
                                          "userland/lib")]
    return {
        "pure": [*HOST, *inc, str(_harness(PURE, tmp, root)),
                 *[str(root / f) for f in SHARED], "-o", str(tmp / "pure")],
        "cdi": [*ILP32, *cinc, str(_harness(CDI, tmp, root)), "-o", str(tmp / "cdi")],
        "ins": ["gcc", "-std=gnu11", "-Wall", "-Wextra", "-Werror",
                "-Wno-unused-function",
                "-Wno-pointer-to-int-cast", "-D__cdecl=", "-D__OS32_USERLAND__", "-O0",
                *iinc, str(_harness(INS, tmp, root)),
                *[str(root / f) for f in INS_SHARED], "-o", str(tmp / "ins")],
        "mini": [*ILP32, "-I" + str(root / "boot"), str(_harness(MINI, tmp, root)),
                 "-o", str(tmp / "mini")],
    }


def build_all(tmp, root=ROOT, quiet=False, keys=HARNESS_KEYS):
    """keys のハーネスを組む (既定は 4 本)。どれかが組めなければ None。"""
    cmds = _commands(tmp, root)
    exes = {}
    for k in keys:
        if not _cc(cmds[k], quiet):
            return None
        exes[k] = pathlib.Path(tmp) / k
    return exes


def make_mini_image(tmp):
    """mke2fs (1KiB ブロック、inode 128 B、機能なし) + debugfs で /boot に並べる。"""
    tmp = pathlib.Path(tmp)
    img = tmp / "mini.img"
    run(["mke2fs", "-q", "-F", "-t", "ext2", "-b", "1024", "-I", "128", "-O", "none",
         "-N", "64", str(img), "2048"], check=True, capture_output=True)
    cmds = ["mkdir boot"]
    for name, size, _ in MINI_FILES:
        f = tmp / ("f_" + name)
        f.write_bytes(bytes(((i * 7 + 3) & 0xFF) for i in range(size)))
        cmds.append(f"write {f} boot/{name}")
    script = tmp / "debugfs.cmd"
    script.write_text("\n".join(cmds) + "\n")
    run(["debugfs", "-w", "-f", str(script), str(img)], check=True, capture_output=True)
    return img


ROOM_EXPECT = {}   # {大きさ: (空きブロック, 空き inode)} — room_cross_check が実物の像から埋める


def run_cases(exes, img, quiet=False, keys=HARNESS_KEYS, first_fail=False):
    """keys のハーネスのケースを回し、落ちた数を返す。first_fail なら最初に
    落ちたところで打ち切る (変異は RED かどうかだけ分かればよい)。"""
    failed = 0
    if "pure" in keys:
        for size, want in ROOM_EXPECT.items():
            got = run([str(exes["pure"]), "room", str(size)], capture_output=True,
                      text=True).stdout.split()
            failed += len(got) != 2 or (int(got[0]), int(got[1])) != want
            if failed and first_fail:
                return failed
    for key, cases in (("pure", PURE_CASES), ("cdi", CDI_CASES), ("ins", INS_CASES)):
        if key not in keys:
            continue
        for c in cases:
            r = run([str(exes[key]), c], capture_output=True, text=True, timeout=CASE_TIMEOUT)
            if not quiet:
                print(f"EXIT {key}:{c}={r.returncode}", flush=True)
                if r.returncode != 0:
                    sys.stdout.write(r.stdout[-3000:] + r.stderr[-3000:])
            failed += r.returncode != 0
            if failed and first_fail:
                return failed
    if "mini" not in keys:
        return failed
    for name, _, want in MINI_FILES:
        r = run([str(exes["mini"]), str(img), f"/boot/{name}", str(want)],
                capture_output=True, text=True)
        if not quiet:
            print(f"EXIT mini:{name}={r.returncode} {r.stderr.strip()}", flush=True)
        failed += r.returncode != 0
        if failed and first_fail:
            return failed
    return failed


def room_cross_check(pure_exe, tmp):
    """容量の見積もりの空き (ブロック・inode) = 実物の ext2_format_at の像の dumpe2fs。"""
    spec = importlib.util.spec_from_file_location("hdd1", ROOT / "tools/tests/test_hdd_stage1.py")
    hdd1 = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(hdd1)
    pexe = hdd1.build_part(tmp)
    if pexe is None:
        print("  room: ext2_part_host のビルド失敗")
        return 1
    bad = 0
    for size in ROOM_SIZES:
        img = pathlib.Path(tmp) / f"room_{size}.img"
        with open(img, "wb") as f:
            r = host32.run([str(pexe), "dump", "2016", str(size)], stdout=f)
        if r.returncode != 0:
            bad += 1
            continue
        d = run(["dumpe2fs", "-h", str(img)], capture_output=True, text=True).stdout
        fb = int(re.search(r"^Free blocks:\s+(\d+)", d, re.M).group(1))
        fi = int(re.search(r"^Free inodes:\s+(\d+)", d, re.M).group(1))
        got = run([str(pure_exe), "room", str(size)], capture_output=True,
                  text=True).stdout.split()
        ok = [int(got[0]), int(got[1])] == [fb, fi]
        ROOM_EXPECT[size] = (fb, fi)
        print(f"  room {size}: estimate {got[0]}/{got[1]} image {fb}/{fi} "
              f"{'ok' if ok else 'MISMATCH'}", flush=True)
        bad += not ok
    return bad


def consts_cross_check(pure_exe):
    """試験の外の正典と定数が一致する。"""
    got = run([str(pure_exe), "consts"], capture_output=True, text=True).stdout.split()
    kmax, lmax, groups, off_h, off_s = (int(x) for x in got)
    bad = 0
    bd = (ROOT / "boot/boot_defs.h").read_text(encoding="utf-8")
    m = re.search(r"#define\s+MAX_IMAGE_SIZE\s+\((\d+)\s*\*\s*(\d+)\)", bd)
    bad += not (m and int(m.group(1)) * int(m.group(2)) == kmax)
    ctx = (ROOT / "fs/ext2_ctx.h").read_text(encoding="utf-8")
    m = re.search(r"#define\s+EXT2_MAX_GROUPS\s+(\d+)", ctx)
    bad += not (m and int(m.group(1)) == groups)
    nhd = (ROOT / "tools/nhd_deploy.py").read_text(encoding="utf-8")
    m = re.search(r"^LOADER_MAX_SECTORS\s*=\s*(\d+)", nhd, re.M)
    bad += not (m and int(m.group(1)) * 512 == lmax)
    # IPL がローダを読むセクタ数 (boot_hdd.asm の「LBA 2 から」の mov cx) × 512
    asm = (ROOT / "boot/boot_hdd.asm").read_text(encoding="utf-8")
    m = re.search(r"mov\s+ax,\s*2\s*;;[^\n]*\n\s*mov\s+cx,\s*(\d+)", asm)
    bad += not (m and int(m.group(1)) * 512 == lmax)
    ipl = ROOT / "boot/boot_hdd.bin"
    if ipl.is_file():
        b = ipl.read_bytes()
        # boot_hdd.asm の geo_heads / geo_spt / geo_da の既定 8 / 17 / 80h がその位置にある
        bad += not (b[off_h] == 8 and b[off_s] == 17 and b[off_s + 1] == 0x80)
    else:
        print("  consts: boot/boot_hdd.bin が無い (make all の前) — IPL の位置は見ない")
    print(f"  consts: kernel {kmax} loader {lmax} groups {groups} ipl [{off_h}]/[{off_s}] "
          f"{'ok' if bad == 0 else 'MISMATCH'}", flush=True)
    return bad


def real_pkg_paths(pure_exe):
    """packages/*.PKG (make all の成果物) の全項目のパスが inst_check_path を通る。"""
    import struct
    paths = []
    for f in sorted((ROOT / "packages").glob("*.PKG")):
        b = f.read_bytes()
        off = 32
        while b[off]:
            n = b[off]
            paths.append(b[off + 1:off + 1 + n].decode("utf-8"))
            off += 1 + n + 5
    if not paths:
        print("  real PKG paths: packages/*.PKG が無い (make all の前) — 見ない")
        return 0
    r = run([str(pure_exe), "pathok", *paths], capture_output=True, text=True)
    print(f"  real PKG paths: {r.stdout.strip()}", flush=True)
    return r.returncode != 0


# CPL=3 が読んでよい文字列の返り先 (カーネル帯の static を返さない実体)。
# 2026-09-24、NP21/W で cdinst が vfs_devname の返り値 (カーネルのマウント表) を
# 読んで fault kill された — ホスト試験の贋物は利用者の文字列を返すので見えない。
# 往復 2 (Fable) で path_get_drive / path_get_cwd も同じ手で直した。
USER_SAFE_STR_TARGETS = {"vfs_cwd_user", "vfs_devname_user", "path_get_drive_user",
                         "path_get_cwd_user", "kapi_db_last_error", "kapi_db_column_text"}
# トランポリンの写しを返す実体 (exec/exec.c)
TRAMP_TARGETS = {"vfs_cwd_user", "vfs_devname_user", "path_get_drive_user", "path_get_cwd_user"}


def str_return_guard():
    """`const char *` を返す KAPI は**全部**、CPL=3 に読める場所を返す実体につながる
    (kapi.json の target → 生成物の wrap → exec.c の写し)。あわせて userland/ の
    全 C ソースのうち、それ以外の実体を呼ぶ箇所が無いことを見る。"""
    import json

    def walk(o):
        if isinstance(o, dict):
            if "name" in o and "args" in o:
                yield o
            for v in o.values():
                yield from walk(v)
        elif isinstance(o, list):
            for v in o:
                yield from walk(v)

    k = json.load(open(ROOT / "sdk/kapi.json", encoding="utf-8"))
    strfn = {f["name"]: f.get("target", f["name"]) for f in walk(k)
             if "char" in f["ret"] and "*" in f["ret"]}
    bad = []
    for name, tgt in sorted(strfn.items()):
        if tgt not in USER_SAFE_STR_TARGETS:
            bad.append(f"sdk/kapi.json: {name} -> {tgt} (カーネル帯を返し得る)")
    gen = (ROOT / "kapi/kapi_generated.c").read_text(encoding="utf-8")
    for name, tgt in strfn.items():
        m = re.search(r"wrap_%s\([^)]*\)\n\{(.*?)\n\}" % name, gen, re.S)
        if not m or f"return {tgt}(" not in m.group(1):
            bad.append(f"kapi/kapi_generated.c: wrap_{name} が {tgt} を通らない (make all の前?)")
    exe = (ROOT / "exec/exec.c").read_text(encoding="utf-8")
    for tgt in sorted(TRAMP_TARGETS):
        m = re.search(r"const char \*%s\([^)]*\)\s*\{(.*?)\}" % tgt, exe, re.S)
        if not m or ("ring3_user_str(ring3_in_syscall" not in m.group(1) and "tramp_copy(" not in m.group(1)):
            bad.append(f"exec/exec.c: {tgt} がトランポリンの写しを返さない")
    calls = 0
    for f in sorted((ROOT / "userland").rglob("*")):
        if f.suffix not in (".c", ".inc", ".h") or "target" in f.parts:
            continue
        src = f.read_text(encoding="utf-8", errors="replace")
        for name in re.findall(r"->\s*(\w+)\s*\(", src):
            if name in strfn:
                calls += 1
                if strfn[name] not in USER_SAFE_STR_TARGETS:
                    bad.append(f"{f.relative_to(ROOT)}: {name} -> {strfn[name]}")
    for b in bad:
        print("  str-return: " + b)
    print(f"  str-return guard: {'ok' if not bad else 'NG'} "
          f"({len(strfn)} string KAPIs, {calls} call sites under userland/)", flush=True)
    return 1 if bad else 0


def build_target(tmp):
    base = ["i386-elf-gcc", "-std=gnu11", "-m32", "-march=i386", "-ffreestanding",
            "-fno-pie", "-fno-stack-protector", "-nostdlib", "-mno-red-zone", "-fcommon",
            "-O2", "-Wall", "-Wextra", "-Werror", 
            "-D__OS32_USERLAND__", "-I.", "-Iinclude", "-Isdk/include", "-Isdk/include/os32",
            "-Iuserland/lib"]
    for rel in ("userland/system/inst_disk.c", "userland/system/inst_hdd.c",
                "userland/system/cdinst.c", "userland/system/install.c"):
        extra = ["-Wno-unused-function"] if rel.endswith("install.c") else []
        run([*base, *extra, "-c", rel, "-o", str(pathlib.Path(tmp) / (pathlib.Path(rel).stem + ".o"))],
            check=True)
    run(["i386-elf-gcc", "-std=gnu11", "-m32", "-march=i386", "-ffreestanding", "-fno-pie",
         "-fno-stack-protector", "-nostdlib", "-Os", "-Wall", "-Werror", "-Iboot", "-c",
         "boot/ext2_mini.c", "-o", str(pathlib.Path(tmp) / "em.o")], check=True)
    print("TARGET i386-elf GNU11 -Werror COMPILE PASS", flush=True)


# ---- 否定側: (ファイル, 前, 後, 説明) -------------------------------------------
MUTATIONS = [
    ("userland/system/inst_disk.c",
     "        if (lba0[510] == 0x55 && lba0[511] == 0xAA) return HDPREP_E_MBR_SIG;",
     "        (void)0;", "空の表で LBA 0 の 55AA を見ない"),
    ("userland/system/inst_disk.c", "    if (used != 1) return INST_E_MULTI;",
     "    if (used < 1) return INST_E_MULTI;", "2 項目以上を作り直す"),
    ("userland/system/inst_disk.c",
     "    if (e.sys_id != PC98PT_SID_OS32 || !name_is_os32(raw)) return INST_E_FOREIGN;",
     "    if (e.sys_id != PC98PT_SID_OS32 || (!name_is_os32(raw) && 0)) return INST_E_FOREIGN;",
     "名前 OS32 を見ない"),
    ("userland/system/inst_disk.c",
     "    if (e.sys_id != PC98PT_SID_OS32 || !name_is_os32(raw)) return INST_E_FOREIGN;",
     "    if (!name_is_os32(raw)) return INST_E_FOREIGN;", "sid を見ない (未知の区画を消す)"),
    ("userland/system/inst_disk.c",
     "        if (st != expect_start) return INST_E_START;\n        *out_mode = INST_MODE_RECREATE;",
     "        *out_mode = INST_MODE_RECREATE;", "標準配置の開始を見ない"),
    ("userland/system/inst_disk.c",
     "        if (st != expect_start) return INST_E_START;\n        *out_mode = INST_MODE_RECREATE_OLD;",
     "        *out_mode = INST_MODE_RECREATE_OLD;", "旧配置の開始を見ない (16/63 のシリンダ 12 を消す)"),
    ("userland/system/inst_disk.c", "    if (pc98pt_os32_is_legacy(lba1, heads, spt, disk_total)) {",
     "    if (0) {", "旧配置の NHD を入れ直せない"),
    ("userland/system/inst_disk.c", "        if (c != ' ' && c != 0) return 0;", "        (void)c;",
     "名前の後ろ (OS32X) を見ない"),
    ("userland/system/inst_disk.c", "loader_len > INST_LOADER_MAX)", "loader_len > INST_LOADER_MAX + 1UL)",
     "ローダ 8193 B を通す"),
    ("userland/system/inst_disk.c", "kernel_len > INST_KERNEL_MAX)", "kernel_len > INST_KERNEL_MAX + 1UL)",
     "vmkernel 508KiB + 1 を通す"),
    ("userland/system/inst_disk.c", "ipl_len > INST_IPL_MAX)", "ipl_len > INST_IPL_MAX + 1UL)",
     "IPL 513 B を通す"),
    ("userland/system/inst_disk.c", "    if (data > 12UL) meta += 1UL;", "    (void)0;",
     "単一間接ブロックを数えない"),
    ("userland/system/inst_disk.c", "    if (need_blocks > free_blocks) return INST_E_SPACE;\n", "",
     "ブロックの不足を見ない"),
    ("userland/system/inst_disk.c", "    if (need_inodes > free_inodes) return INST_E_INODES;\n", "",
     "inode の不足を見ない"),
    ("userland/system/inst_disk.c", "    ipl[INST_IPL_OFF_HEADS] = (unsigned char)heads;",
     "    ipl[INST_IPL_OFF_HEADS] = (unsigned char)(heads * 0UL + 8UL);", "IPL のヘッド数を 8 に固定 (旧 cdinst)"),
    ("userland/system/inst_disk.c", "    for (i = 0; i < PC98PT_SECTOR_SIZE; i++) sect[i] = 0;",
     "    for (i = 0; i < 0; i++) sect[i] = 0;",
     "区画表の残りの項目を消さない"),
    ("userland/system/inst_disk.c", "    meta += EXT2L_FIRST_DATA_BLOCK + EXT2L_ROOT_DATA_BLOCKS;",
     "    meta += EXT2L_FIRST_DATA_BLOCK;", "空きの見積もりがルートのブロックを数えない"),
    ("userland/system/inst_hdd.c",
     "    if (rc != 0 || !ih_memeq(ih_sect, ih_back, IH_SECT)) {\n        inst_hdd_incomplete(api, \"partition table",
     "    if (rc != 0) {\n        inst_hdd_incomplete(api, \"partition table", "区画表を読み戻して比べない"),
    ("userland/system/inst_hdd.c", "    rc = api->sys_mount(INST_MOUNT, INST_DEV, \"ext2\");",
     "    rc = 0;", "通常のマウントで確かめない"),
    ("userland/system/inst_hdd.c", "    rc = api->sys_umount_checked(INST_MOUNT);", "    rc = 0;",
     "マウント中の hd0 を外さない"),
    ("userland/system/inst_hdd.c",
     "    if (api->dev_mount_count(INST_DRIVE) != 0) {\n        if (unmounted) {",
     "    if (api->dev_mount_count(INST_DRIVE) > 1) {\n        if (unmounted) {",
     "外した後にマウントが 1 つ残っていても書く"),
    ("userland/system/inst_hdd.c",
     "    root_hd0 = (rootdev && ih_streq(rootdev, INST_DEV)) ? 1 : 0;",
     "    root_hd0 = (rootdev && 0) ? 1 : 0;",
     "ルートの hd0 を書く"),
    ("userland/system/inst_hdd.c", "    inst_patch_ipl(ih_sect, t->plan.heads, t->plan.spt);",
     "    inst_patch_ipl(ih_sect, t->hg.ata_def_heads, t->hg.ata_def_spt);",
     "IPL に IDENTIFY の幾何を書く (旧 install、F15)"),
    ("userland/system/inst_hdd.c", "    return ih_memeq(buf, ih_back, IH_SECT) ? 0 : -1;", "    return 0;",
     "ローダ / IPL を読み戻して比べない"),
    ("userland/system/inst_hdd.c", "    rc = api->ext2_format_at(INST_DRIVE, t->plan.start, t->plan.len);",
     "    rc = api->ext2_format_at(INST_DRIVE, t->plan.start, t->g.ata_total - t->plan.start);",
     "ディスク全体を format する (旧動作)"),
    ("userland/system/inst_hdd.c",
     "    if (rc != 0) { ih_refuse(api, rc); return rc; }\n    return 0;\n}\n\nvoid inst_hdd_describe",
     "    (void)rc;\n    return 0;\n}\n\nvoid inst_hdd_describe", "容量の不足を断らない"),
    ("userland/system/inst_hdd.c", "    if (t->mode != INST_MODE_EMPTY)\n", "    if (0)\n",
     "作り直しでデータが消えることを表示しない"),
    ("userland/system/inst_hdd.c", "    t->mounts = api->dev_mount_count(INST_DRIVE);",
     "    t->mounts = 0;", "検査でマウントを数えない (外さずに書きに行く)"),
    ("userland/system/cdinst.c", "    if (g_final_kernel == 0 || g_final_shell == 0) {",
     "    if (0 && (g_final_kernel == 0 || g_final_shell == 0)) {",
     "MINIMAL の必須の中身を見ない"),
    ("userland/system/cdinst.c",
     "            i = pkg_first_overflow(&info, 4);   /* strlen(\"/hd0\") */\n            if (i >= 0) {",
     "            i = -1;\n            if (i >= 0) {", "前置の溢れを書く前に見ない"),
    ("userland/system/cdinst.c", "        if (choice < need_from[b]) continue;",
     "        if (0 && choice < need_from[b]) continue;",
     "選ばない型のパッケージも容量に数える"),
    ("userland/system/cdinst.c", "    if (info.header.flags & PKG_FLAG_LZSS) {\n        println(COL_RED, \"  BOOT.PKG must not",
     "    if (0) {\n        println(COL_RED, \"  BOOT.PKG must not", "LZSS の BOOT.PKG を通す"),
    ("userland/system/cdinst.c",
     "    if (ret != PKG_OK) inst_hdd_incomplete(api, \"package installation failed\", ret);",
     "    (void)ret;", "追加パッケージの失敗を INCOMPLETE にしない"),
    ("userland/system/cdinst.c", "    boot_img_free(&boot);\n    if (ret != 0) return;",
     "    boot_img_free(&boot);\n    (void)ret;", "IPL の失敗の後も展開する"),
    ("userland/system/cdinst.c", "    if (preflight(choice, &boot, &tgt) != 0) return;",
     "    (void)preflight(choice, &boot, &tgt);", "事前検査の失敗でも書く"),
    ("userland/system/install.c", "    if (measure_need(sizes[MEDIA_KERNEL], &need) != 0) {",
     "    if (measure_need(sizes[MEDIA_KERNEL], &need) != 0 && 0) {", "FD の列挙の失敗を書く前に見ない"),
    ("userland/system/install.c", "    if (inst_hdd_release(api, &tgt) != 0) goto end;", "",
     "install が hd0 を外さずに書く"),
    ("userland/system/install.c", "    if (inst_hdd_check_media(api, &tgt, sizes[MEDIA_IPL]",
     "    if (0 && inst_hdd_check_media(api, &tgt, sizes[MEDIA_IPL]", "install が大きさと容量を見ない"),
    ("userland/system/install.c", "        inst_hdd_incomplete(api, \"sync failed\", ret);\n", "",
     "install の sync の失敗を INCOMPLETE と出さない"),
    # ---- 実装レビュー往復 1 (Codex P1-1〜3・P2-4〜6、Fable minor) ----
    ("userland/system/cdinst.c", "    if (sum != info->header.orig_size) {", "    if (0) {",
     "P1-1 項目の和と orig_size を比べない (orig_size 0 のヘッダを通す)"),
    ("userland/system/cdinst.c",
     "        st.st_size != info->data_offset + info->header.comp_size) {",
     "        0) {", "P1-1 PKG の長さを見ない (データ部が切れた媒体を通す)"),
    ("userland/system/cdinst.c", "            if (!pkg_data_ok(path, &info)) return PKG_ERR_CORRUPT;\n", "",
     "P1-1 追加パッケージのデータ部を書く前に見ない"),
    ("userland/system/cdinst.c", "    if (!pkg_data_ok(PKG_BOOT, &info)) return PKG_ERR_CORRUPT;\n", "",
     "P1-1 BOOT.PKG のデータ部を見ない"),
    ("userland/system/cdinst.c", "    if (g_final_kernel == 0 || g_final_shell == 0) {",
     "    if (g_final_kernel == 0) {", "P1-1 空の shell.bin を必須として通す"),
    ("userland/system/cdinst.c",
     "            if (pkg_paths_ok(path, &info) != 0) return PKG_ERR_CORRUPT;\n            if (!pkg_data_ok",
     "            if (!pkg_data_ok", "P1-2 パスを書く前に見ない (展開の途中で気付く)"),
    ("userland/system/inst_disk.c",
     "        if (len == 2 && c[0] == '.' && c[1] == '.') return INST_E_PATH;\n", "",
     "P1-2 '..' を通す (/hd0 の外へ書く)"),
    ("userland/system/inst_disk.c", "        if (len == 1 && c[0] == '.') return INST_E_PATH;\n", "",
     "P1-2 '.' を通す"),
    ("userland/system/inst_disk.c", "        if (len == 0) return INST_E_PATH;", "        (void)0;",
     "P1-2 空の要素 ('//'・末尾 '/') を通す"),
    ("userland/system/inst_disk.c", "    if (!path || path[0] != '/') return INST_E_PATH;",
     "    if (!path || !path[0]) return INST_E_PATH;", "P1-2 絶対でないパスを通す"),
    ("userland/system/inst_disk.c", "        if (depth + INST_PREFIX_DEPTH > INST_VFS_MAX_DEPTH) return INST_E_DEPTH;",
     "        if (depth > INST_VFS_MAX_DEPTH) return INST_E_DEPTH;", "P2-5 前置 /hd0 の 1 要素を数えない"),
    ("userland/system/install.c", "    for (i = 0; i < MEDIA_COUNT; i++) {",
     "    for (i = 0; i < MEDIA_COUNT - 1; i++) {", "P1-3 FD の /sys/shell.bin を見ない"),
    ("userland/system/install.c", "        if ((st.st_mode & OS_S_IFMT) != OS_S_IFREG) {", "        if (0) {",
     "P1-3 FD の必須の種別 (通常のファイル) を見ない"),
    ("userland/system/inst_disk.c", "    if (size > INST_EXT2_MAX_FILE) n->too_big++;\n", "",
     "P2-4 1 ファイルの上限を数えない"),
    ("userland/system/inst_disk.c", "    if (n->too_big) return INST_E_FILE_SIZE;\n", "",
     "P2-4 1 ファイルの上限で断らない"),
    ("userland/system/inst_hdd.c", "REBOOT, then run the installer again", "Run the installer again",
     "P2-6 再起動を案内しない"),
    ("userland/system/inst_hdd.c", "            ih_host_hint(api);           /* OS32 の項目が中途半端",
     "            (void)0;           /* OS32 の項目が中途半端",
     "P2-6 直せない表にホスト側の手当てを案内しない"),
    ("userland/system/inst_hdd.c", "        if (t->mounts == 1 && dev && ih_streq(dev, INST_DEV)) {",
     "        if (dev && ih_streq(dev, INST_DEV)) {", "Fable 別の prefix にもマウントされた hd0 を承認後に外しに行く"),
    ("userland/system/inst_hdd.c", "        ih_refuse(api, INST_E_GEOM);", "        ih_refuse(api, INST_E_ARG);",
     "Fable hdd_geom_info の失敗を bad argument と出す"),
    ("userland/system/inst_disk.c", "    need_inodes = n->files + n->dirs + INST_SPACE_MARGIN_INODES;",
     "    need_inodes = n->files + n->dirs;", "Fable 自動で作る親ディレクトリの inode の余白を持たない"),
    ("userland/system/inst_disk.c", "        out->free_blocks = 0;\n", "",
     "Fable 失敗のとき room を埋めない"),
    # ---- 実装レビュー往復 2 (Codex P1-1・P1-2 / Fable minor) ----
    ("userland/system/cdinst.c",
     "                if (ent->type != PKG_TYPE_FILE) {\n                    api->kprintf(COL_RED, \"  %s: unknown entry type",
     "                if (0) {\n                    api->kprintf(COL_RED, \"  %s: unknown entry type",
     "R2 P1-1 未知の型の項目をファイルとして数える (型 2 の shell が検査を通る)"),
    ("userland/system/cdinst.c",
     "                if (path_eq(ent->path, PKG_NEED_SHELL)) g_final_shell = ent->size;",
     "                if (path_eq(ent->path, PKG_NEED_SHELL) && g_final_shell == 0) g_final_shell = ent->size;",
     "R2 P1-2 最初の shell で判定する (後の大きさ 0 の上書きを見ない)"),
    ("userland/system/cdinst.c",
     "                if (path_eq(ent->path, PKG_NEED_KERNEL)) g_final_kernel = ent->size;",
     "                if (path_eq(ent->path, PKG_NEED_KERNEL) && b == 0) g_final_kernel = ent->size;",
     "R2 P1-2 vmkernel を MINIMAL だけで判定する (後の PKG の上書きを見ない)"),
    ("userland/system/cdinst.c", "    if (g_final_kernel != 0 && !required_on_hd0()) {", "    if (0) {",
     "R2 P1-2 展開の後に必須の実物を見ない"),
    ("userland/system/install.c",
     "        if (api->sys_stat(DST_SHELL, &st) != 0 || (st.st_mode & OS_S_IFMT) != OS_S_IFREG ||\n            st.st_size != sizes[MEDIA_SHELL]) {",
     "        if (api->sys_stat(DST_SHELL, &st) != 0 && 0) {", "R2 P1-2 install が写した shell の実物を見ない"),
    ("userland/system/inst_hdd.c",
     "        if (rc == INST_E_FOREIGN || rc == INST_E_MULTI || rc == HDPREP_E_MBR_SIG)",
     "        if (0)", "R2 Fable 他の OS の区画にも「表を消せ」と案内する"),
    # ---- ERASE (N4 の例外、2026-09-25。順序は Codex レビュー後の PM 決定:
    #      全検査 → 確認と y/N → ERASE の打鍵 → 消去 → format) ----
    ("userland/system/inst_hdd.c",
     "    if (ih_read_line(api, line, INST_LINE_MAX, 1) != 0 || !ih_streq(line, INST_ERASE_WORD)) {",
     "    if (ih_read_line(api, line, INST_LINE_MAX, 1) != 0 && 0) {", "ERASE でない行でも消す"),
    ("userland/system/inst_hdd.c", "!ih_streq(line, INST_ERASE_WORD)) {",
     "!(ih_streq(line, INST_ERASE_WORD) || ih_streq(line, \"erase\"))) {", "小文字の erase でも消す"),
    ("userland/system/inst_hdd.c", "        if (ch < 0x20 || ch > 0x7E) { bad = 1; continue; }",
     "        if (ch == ' ') continue;\n        if (ch < 0x20 || ch > 0x7E) { bad = 1; continue; }",
     "空白を読み捨てる ('ERASE ' で消す)"),
    ("userland/system/inst_hdd.c", "        if (ch < 0x20 || ch > 0x7E) { bad = 1; continue; }",
     "        if (ch < 0x20 || ch > 0x7E) { continue; }", "制御文字 (BS) を読み捨てる ('ERA<BS>SE' で消す)"),
    ("userland/system/inst_hdd.c", "        if (ch == 0x1B) { bad = 1; break; }",
     "        if (ch == 0x1B) { break; }", "ESC を Enter と同じに扱う"),
    ("userland/system/inst_hdd.c", "        if (ch == '\\r' || ch == '\\n') break;",
     "        if (ch == '\\r') break;", "LF で行を終えない"),
    # P1 (Codex): NUL を「入力なし」と読み捨てると ERA<NUL>SE<CR> で消える
    ("userland/system/inst_hdd.c",
     "    ch = api->kbd_trygetchar();\n    if (ch < 0) ch = api->serial_trygetchar();\n    return ch;",
     "    ch = api->kbd_trygetchar();\n    if (ch <= 0) ch = api->serial_trygetchar();\n"
     "    if (ch == 0) ch = -1;\n    return ch;",
     "P1 NUL を「入力なし」として読み捨てる (ERA<NUL>SE で消す)"),
    ("userland/system/inst_hdd.c", "        if (ch < 0x20 || ch > 0x7E) { bad = 1; continue; }",
     "        if (ch == 0) continue;\n        if (ch < 0x20 || ch > 0x7E) { bad = 1; continue; }",
     "P1 行の中の NUL を読み捨てる"),
    ("userland/system/inst_hdd.c", "    if (ch < 0) ch = api->serial_trygetchar();\n    return ch;",
     "    return ch;", "serial からの鍵を読まない"),
    # P2 (Codex): CRLF は 1 つの行末
    ("userland/system/inst_hdd.c",
     "    if (ch == '\\n' && ih_last_cr) { ih_last_cr = 0; return -1; }",
     "    if (0) { ih_last_cr = 0; return -1; }", "P2 CR の直後の LF を捨てない (次の問いに持ち越す)"),
    ("userland/system/inst_hdd.c",
     "        ch = ih_poll_key(api);           /* 届いている LF なら捨てる */\n"
     "        if (ch >= 0) ih_pushed = ch;     /* 別の字なら戻す */\n", "",
     "P2 CR の後に届いている LF を同じ行末として読まない"),
    ("userland/system/inst_hdd.c", "        if (ch >= 0) ih_pushed = ch;     /* 別の字なら戻す */",
     "        (void)ch;", "CR の後に覗いた別の字を捨てる"),
    # 順序: 消すのは全検査・y・ERASE・マウントの検査の後
    ("userland/system/inst_hdd.c",
     "        t->erase_needed = 1;\n        t->erase_code = rc;\n        t->mode = INST_MODE_EMPTY;\n    }",
     "        t->erase_code = rc;\n        t->mode = INST_MODE_EMPTY;\n"
     "        if (ih_check_mounts(api, t) < 0) return rc;\n"
     "        if (t->umount_hd0) { (void)ih_umount_hd0(api); t->umount_hd0 = 0; }\n"
     "        if (ih_erase(api, t) != 0) return rc;\n    }",
     "検査の中で消す (y/N と ERASE の前、旧順序)"),
    ("userland/system/inst_hdd.c",
     "    if (t->umount_hd0) {\n        rc = ih_umount_hd0(api);\n        if (rc != 0) return rc;\n        t->umount_hd0 = 0;\n        unmounted = 1;\n    }",
     "    if (t->erase_needed && ih_erase(api, t) != 0) return -1;\n    t->erase_needed = 0;\n"
     "    if (t->umount_hd0) {\n        rc = ih_umount_hd0(api);\n        if (rc != 0) return rc;\n        t->umount_hd0 = 0;\n        unmounted = 1;\n    }",
     "マウントの検査より前に消す"),
    ("userland/system/inst_hdd.c",
     "        rc = ih_umount_hd0(api);\n        if (rc != 0) return rc;\n        t->umount_hd0 = 0;",
     "        rc = 0;\n        (void)rc;", "/hd0 の hd0 を外さずに消す・書く"),
    ("userland/system/inst_hdd.c",
     "    if (api->dev_mount_count(INST_DRIVE) != 0) {\n        if (unmounted) {",
     "    if (0) {\n        if (unmounted) {", "外したのにマウントが残っていても消す・書く"),
    ("userland/system/inst_hdd.c", "    if (t->erase_needed) return ih_erase(api, t);\n    return 0;",
     "    return 0;", "ERASE を受けても消さない"),
    ("userland/system/inst_hdd.c", "    return ih_check_mounts(api, t);\n}",
     "    t->erase_needed = 1;\n    return ih_check_mounts(api, t);\n}", "空のディスク・作り直しでも ERASE を聞く"),
    ("userland/system/inst_hdd.c",
     "    rc = ih_write_verify(api, 0, ih_sect);\n    if (rc == 0) rc = ih_write_verify(api, PC98PT_LBA, ih_sect);",
     "    rc = api->ide_write_sector(INST_DRIVE, 0, ih_sect);\n"
     "    if (rc == 0) rc = api->ide_write_sector(INST_DRIVE, PC98PT_LBA, ih_sect);",
     "消した LBA 0/1 を読み戻して比べない"),
    ("userland/system/inst_hdd.c",
     "    rc = ih_write_verify(api, 0, ih_sect);\n    if (rc == 0) rc = ih_write_verify(api, PC98PT_LBA",
     "    rc = 0;\n    if (rc == 0) rc = ih_write_verify(api, PC98PT_LBA", "LBA 0 (55AA) を消さない"),
    ("userland/system/inst_hdd.c", "    if (rc == 0) rc = ih_write_verify(api, PC98PT_LBA, ih_sect);\n", "",
     "LBA 1 (区画表) を消さない"),
    ("userland/system/inst_hdd.c", "    for (i = 0; i < IH_SECT; i++) ih_sect[i] = 0;", "    (void)i;",
     "0 で埋めずに前のセクタの中身を書く"),
    ("userland/system/inst_hdd.c", "    ih_erased = 1;\n    t->erased = 1;\n", "    t->erased = 1;\n",
     "消したことを記録しない (format の失敗で「空のディスク」と案内しない)"),
    ("userland/system/inst_hdd.c", "    return code == INST_E_FOREIGN || code == INST_E_MULTI",
     "    return code == INST_E_MULTI", "FOREIGN (実機の -40) で ERASE を聞かない (断る)"),
    ("userland/system/inst_hdd.c", "           code == INST_E_BROKEN || code == INST_E_START;",
     "           code == INST_E_START;", "壊れた項目で ERASE を聞かない"),
    ("userland/system/inst_hdd.c", "code == HDPREP_E_MBR_SIG ||\n", "\n", "55AA だけのディスクで ERASE を聞かない"),
    ("userland/system/inst_hdd.c", "    ih_erased = 0;\n    ih_pt_written = 0;\n", "",
     "前の実行の「消した」を持ち越す"),
    ("userland/system/inst_hdd.c", "    if (ih_erased && !ih_pt_written) ih_erased_hint(api);",
     "    if (ih_erased) ih_erased_hint(api);", "区画表を書いた後も「空のディスク」と案内する"),
    ("userland/system/inst_hdd.c", "    if (ih_erased && !ih_pt_written) ih_erased_hint(api);", "",
     "format の失敗で「空のディスクとして入れ直せる」と案内しない"),
    ("userland/system/inst_hdd.c", "                     i, (u32)e.bootable, (u32)e.sys_id, name);",
     "                     i, (u32)e.bootable, (u32)e.bootable, name);", "要約の sid が mid"),
    ("userland/system/inst_hdd.c", "                     (u32)e.start_cyl, (u32)e.end_cyl,\n",
     "                     (u32)e.start_cyl, (u32)e.start_cyl,\n", "要約の終了シリンダが開始"),
    ("userland/system/inst_hdd.c", "            name[k] = (c >= 0x20 && c <= 0x7E) ? (char)c : '.';",
     "            name[k] = (c >= 0x20 && c <= 0x7E) ? '.' : '.';", "要約に区画の名前を出さない"),
    ("userland/system/inst_hdd.c", "        ih_show_disk(api, t);\n", "", "確認の前に LBA 0/1 の要約を出さない"),
    ("userland/system/inst_hdd.c", "        t->mode = INST_MODE_EMPTY;\n", "",
     "消す予定でも元の断りのモードのまま (確認画面が空のディスクでない)"),
    ("userland/system/inst_hdd.c",
     "    if (t->erase_needed) {\n        api->kprintf(ATTR_RED,\n                     \"  hd0 holds another",
     "    if (0) {\n        api->kprintf(ATTR_RED,\n                     \"  hd0 holds another",
     "確認画面に「y の後に ERASE」を出さない"),
    ("userland/system/inst_hdd.c", "    if (!t->erase_needed) return 0;\n", "    return 0;\n",
     "ERASE を聞かずに消す (打鍵なしで消える)"),
    # 読み戻しの比較の長さ (Codex: 末尾の 511 バイト目)
    ("userland/system/inst_hdd.c", "    return ih_memeq(buf, ih_back, IH_SECT) ? 0 : -1;",
     "    return ih_memeq(buf, ih_back, IH_SECT - 1) ? 0 : -1;",
     "読み戻しの比較を 511 バイトに縮める (ローダ / IPL / 消去)"),
    ("userland/system/inst_hdd.c", "    if (rc != 0 || !ih_memeq(ih_sect, ih_back, IH_SECT)) {",
     "    if (rc != 0 || !ih_memeq(ih_sect, ih_back, IH_SECT - 1)) {",
     "区画表の読み戻しの比較を 511 バイトに縮める"),
    # LBA ごとの write / read の失敗で後続の書き込みが止まる (Codex)
    ("userland/system/inst_hdd.c",
     "            inst_hdd_incomplete(api, \"loader write/readback failed\", rc);\n            return rc < 0 ? rc : -1;",
     "            inst_hdd_incomplete(api, \"loader write/readback failed\", rc);\n            rc = 0;",
     "ローダの書き込みの失敗の後も書き続ける"),
    ("userland/system/inst_hdd.c",
     "        inst_hdd_incomplete(api, \"IPL write/readback failed\", rc);\n        return rc < 0 ? rc : -1;",
     "        inst_hdd_incomplete(api, \"IPL write/readback failed\", rc);\n        (void)0;",
     "IPL の書き込みの失敗の後も展開する"),
    ("userland/system/inst_hdd.c",
     "        ih_host_hint(api);\n        return rc < 0 ? rc : -1;\n    }\n    api->kprintf(ATTR_GREEN, \"%s\", \"  Partition table written and verified.\\n\");",
     "        ih_host_hint(api);\n        (void)0;\n    }\n    api->kprintf(ATTR_GREEN, \"%s\", \"  Partition table written and verified.\\n\");",
     "区画表の書き込みの失敗の後もマウントしに行く"),
    # 呼び手 (cdinst / install)
    ("userland/system/cdinst.c", "    if (inst_hdd_ask_erase(api, &tgt) != 0) {\n        boot_img_free(&boot);\n        return;\n    }",
     "    (void)inst_hdd_ask_erase(api, &tgt);", "cdinst が ERASE でない入力でも先へ進む (消す)"),
    ("userland/system/cdinst.c", "    if (inst_hdd_ask_erase(api, &tgt) != 0) {", "    if (0) {",
     "cdinst が ERASE を聞かずに消す"),
    ("userland/system/cdinst.c",
     "            println(COL_NORMAL, \"Installation cancelled. Nothing was written.\");\n            boot_img_free(&boot);\n            return;",
     "            println(COL_NORMAL, \"Installation cancelled. Nothing was written.\");",
     "cdinst が N でも書く"),
    ("userland/system/install.c", "    if (inst_hdd_ask_erase(api, &tgt) != 0) goto end;",
     "    (void)inst_hdd_ask_erase(api, &tgt);", "install が ERASE でない入力でも先へ進む (消す)"),
    ("userland/system/install.c", "    if (inst_hdd_ask_erase(api, &tgt) != 0) goto end;\n", "",
     "install が ERASE を聞かずに消す"),
    ("userland/system/install.c",
     "        api->kprintf(ATTR_WHITE, \"%s\", \"Installation aborted. Nothing was written.\\n\");\n        rc = 0;\n        goto end;",
     "        api->kprintf(ATTR_WHITE, \"%s\", \"Installation aborted. Nothing was written.\\n\");\n        rc = 0;",
     "install が N でも書く"),
    # y/N の読みは inst_hdd_getkey_after_key (選択 [0-3] の getkey は数字以外を読み飛ばすので、
    # 旧の鍵読みに替えても区別できない — y/N の側に当てる)
    ("userland/system/cdinst.c", "        int k = inst_hdd_getkey_after_key(api);",
     "        int k;\n        for (;;) {\n            k = api->kbd_trygetchar();\n            if (k > 0) break;\n"
     "            k = api->serial_trygetchar();\n            if (k > 0) break;\n        }",
     "cdinst の y/N が旧の鍵読み (serial の CRLF・NUL を共通部と別に扱う)"),
    # y の後の行末と ERASE の行 (Codex 2 回目 P2-1)
    ("userland/system/inst_hdd.c", "    if (ih_read_line(api, line, INST_LINE_MAX, 1) != 0",
     "    if (ih_read_line(api, line, INST_LINE_MAX, 0) != 0",
     "y の行末を読み捨てない (y + Enter → ERASE + Enter が取り消しになる)"),
    ("userland/system/inst_hdd.c", "            skip_eol = 0;                /* 最初の 1 字だけ */\n", "",
     "行末を何度でも読み捨てる (Enter だけで取り消せない)"),
    ("userland/system/inst_hdd.c", "            if (ch == '\\r' || ch == '\\n') continue;   /* CRLF",
     "            if (ch == '\\r' || ch == '\\n' || ch == 0) continue;   /* CRLF",
     "y の後の NUL も読み捨てる (NUL は入力の規則を壊す)"),
    ("userland/system/inst_hdd.c", "            skip_eol = 0;                /* 最初の 1 字だけ */\n"
     "            if (ch == '\\r' || ch == '\\n') continue;",
     "            skip_eol = 0;                /* 最初の 1 字だけ */\n"
     "            if (ch == '\\r' || ch == '\\n') { api->kprintf(ATTR_WHITE, \"%s\", \"\\n\"); continue; }",
     "読み捨てた行末を映す (y の後に余計な改行)"),
    # [0-3] の選択の後の行末と Continue (Codex 3 回目 P2)
    ("userland/system/cdinst.c", "        int k = inst_hdd_getkey_after_key(api);", "        int k = getkey();",
     "選択の行末を Continue の答え (取り消し) にする"),
    ("userland/system/inst_hdd.c", "    if (ch == '\\r' || ch == '\\n') ch = inst_hdd_getkey(api);",
     "    while (ch == '\\r' || ch == '\\n') ch = inst_hdd_getkey(api);",
     "選択の後の行末を何度でも捨てる (Enter で取り消せない)"),
    ("userland/system/inst_hdd.c", "    if (ch == '\\r' || ch == '\\n') ch = inst_hdd_getkey(api);",
     "    if (ch == '\\r' || ch == '\\n' || ch == 0) ch = inst_hdd_getkey(api);",
     "選択の後の NUL を捨てる (NUL は答え = 取り消し)"),
    ("userland/system/inst_hdd.c", "    if (ch == '\\r' || ch == '\\n') ch = inst_hdd_getkey(api);",
     "    if (ch == '\\r' || ch == '\\n' || ch == 0x1B) ch = inst_hdd_getkey(api);",
     "選択の後の ESC を捨てる (ESC は答え = 取り消し)"),
    # umount の後の断りの表示 (Codex 2 回目 P2-2)
    ("userland/system/inst_hdd.c",
     "                 \"  Nothing was erased or formatted. (Unmounting may have flushed\\n\"\n"
     "                 \"  hd0's file system data, as any umount does.)\\n\");",
     "                 \"  Nothing was written.\\n\");",
     "umount の後の断りでも Nothing was written と言う"),
    ("userland/system/inst_hdd.c",
     "        api->kprintf(ATTR_RED, \"Refused: umount /hd0 failed (rc=%d).\\n\", rc);\n"
     "        ih_refused_after_umount(api);",
     "        api->kprintf(ATTR_RED,\n"
     "                     \"Refused: umount /hd0 failed (rc=%d). Nothing was written.\\n\", rc);",
     "umount の失敗で Nothing was written と言う (旧)"),
    ("userland/system/inst_hdd.c", "        if (unmounted) {\n            api->kprintf(ATTR_RED, \"Refused: %s",
     "        if (unmounted && 0) {\n            api->kprintf(ATTR_RED, \"Refused: %s",
     "umount の後もマウントが残る断りで Nothing was written と言う"),
    ("userland/system/inst_hdd.c", "        if (unmounted) {\n            api->kprintf(ATTR_RED, \"Refused: %s",
     "        if (unmounted || 1) {\n            api->kprintf(ATTR_RED, \"Refused: %s",
     "umount を呼ぶ前の断りでも「sync はあり得る」と言う (何も書いていないのに)"),
    ("boot/ext2_mini.c", "    if (file_size > max_size) return EXT2M_ERR_TOO_BIG;",
     "    if (file_size > max_size) file_size = max_size;", "上限で切り詰める (旧動作)"),
    ("boot/ext2_mini.c", "    if (file_size > max_size) return EXT2M_ERR_TOO_BIG;\n", "",
     "上限を見ない"),
]


def _tally(counts, status, why):
    counts[status] = counts.get(status, 0) + 1
    print(f"MUTATION {status}: {why}", flush=True)


def _mutate_one(item):
    """変異 1 本 (または対照) を自分専用の一時ディレクトリの写しで組んで回す。
    (状態, 説明, 対照か) を返す。並列に呼ばれる — 実物にも他の写しにも書かない。"""
    rel, before, after, why, base, img = item
    with tempfile.TemporaryDirectory(prefix="os32-hdd2-mut-") as tmp:
        troot = pathlib.Path(tmp) / "root"
        for m in MIRROR:
            dst = troot / m
            dst.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(ROOT / m, dst)
        path = troot / rel
        text = path.read_text(encoding="utf-8")
        control = before is None
        if control:
            text += "\n/* identity mutation (control) */\n"
        elif text.count(before) != 1:
            return "NOT_APPLIED", why + f" (置き換え元 {text.count(before)} 件)", False
        else:
            text = text.replace(before, after, 1)
        path.write_text(text, encoding="utf-8")
        # 組み直すハーネスを 1 本ずつ組んでは回し、落ちたらそこで打ち切る (後のハーネスは
        # 組まない)。組めなければ ERROR。全部通れば SURVIVED。
        exes = dict(base)
        for k in REBUILD.get(rel, HARNESS_KEYS):
            built = build_all(tmp, troot, quiet=True, keys=(k,))
            if built is None:
                return "ERROR", why, control
            exes.update(built)
            try:
                if run_cases(exes, img, quiet=True, keys=(k,), first_fail=True):
                    return "RED", why, control
            except subprocess.TimeoutExpired:
                return "RED", why, control
        return "SURVIVED", why, control


def mutate(img, counts, base):
    """ビルドが通って試験が落ちたものだけ RED。ビルドが通らない変異は ERROR。
    変異させるファイルごとに恒等変異 (対照) を当て、SURVIVED を確かめる。
    base は実物で組んだ 4 本 (作り直さないハーネスに使う)。変異は並列に回し
    (mutpar、OS32_MUT_JOBS)、結果は変異の順に出す。"""
    files = []
    for m in MUTATIONS:
        if m[0] not in files:
            files.append(m[0])
    with tempfile.TemporaryDirectory(prefix="os32-hdd2-deps-") as dtmp:
        deps = {k: mutpar.gcc_deps(c, ROOT) for k, c in _commands(dtmp, ROOT).items()}
    stale = mutpar.check_rebuild_table(REBUILD, deps, files)
    for s in stale:
        print("REBUILD TABLE STALE: " + s, flush=True)
    plan = [(f, None, None, "対照 (恒等): " + f) for f in files] + list(MUTATIONS)
    for status, why, control in mutpar.run_ordered(
            _mutate_one, [(*p, base, img) for p in plan]):
        if control:
            _tally(counts, "CONTROL_OK" if status == "SURVIVED" else "CONTROL_BAD",
                   why + " → " + status)
        else:
            _tally(counts, status, why + (" (ビルドが通らない)" if status == "ERROR" else ""))
    if stale:
        counts["STALE"] = counts.get("STALE", 0) + 1
    return len(files)


def main(argv):
    failed = 0
    with tempfile.TemporaryDirectory(prefix="os32-hdd2-") as tmp:
        exes = build_all(tmp)
        if exes is None:
            print("BUILD FAIL")
            return 1
        print("HOST COMPILE PASS (hdd_stage2_host / cdinst_host ILP32 / install_fresh_host / "
              "ext2_mini_host ILP32, -Werror)", flush=True)
        img = make_mini_image(tmp)
        failed += run_cases(exes, img)
        failed += room_cross_check(exes["pure"], tmp)
        failed += consts_cross_check(exes["pure"])
        failed += real_pkg_paths(exes["pure"])
        failed += str_return_guard()
        total = len(PURE_CASES) + len(CDI_CASES) + len(INS_CASES) + len(MINI_FILES) + \
            len(ROOM_SIZES) + 3
        print(f"SUMMARY {total - failed}/{total} PASS", flush=True)

        if "--target" in argv:
            build_target(tmp)

        if "--mutate" in argv and failed == 0:
            counts = {}
            nctl = mutate(img, counts, exes)
            n = len(MUTATIONS)
            red = counts.get("RED", 0)
            print("MUTATIONS {}/{} RED (ERROR {}, SURVIVED {}, NOT_APPLIED {}); "
                  "CONTROLS {}/{} SURVIVED (期待どおり)".format(
                      red, n, counts.get("ERROR", 0), counts.get("SURVIVED", 0),
                      counts.get("NOT_APPLIED", 0), counts.get("CONTROL_OK", 0), nctl),
                  flush=True)
            if red != n or counts.get("CONTROL_OK", 0) != nctl or counts.get("STALE", 0):
                failed += 1
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
