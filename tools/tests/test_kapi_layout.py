"""KAPI データ欄の固定配置と OS32X ヘッダ v4 (票 TASK_KAPI_DATA_FIELDS)。

票:   docs/archive/kernel_v21/TASK_KAPI_DATA_FIELDS.md (方針 v2 / v3 / ユーザー決裁)

見るもの:
  1. exec / shlib ローダ / 常駐シェルが使う判定関数 (exec/os32x_hdr.c) —
     tools/tests/os32x_layout_host.c が実物を #include して踏む
     (v2 → 断る / v3 値違い → 断る / 一致 → 通す、長さの境界)。
  2. sdk/gen_kapi.py が関数表の容量 (func_capacity) を超えたら生成を拒否する。
  3. sdk/mkos32x.py がヘッダ v4 を焼き、kapi_data_off が ELF の
     .os32_kapi_layout と一致する。刻印が無い / 食い違う / .raw と .elf の
     世代が違う (大きさ、または同じ大きさで PT_LOAD の中身) / --elf が無い、
     は失敗する。min_api_ver は 71 に引き上がる。
     刻印は平らなバイナリに入らない (非ロード)。
  4. tools/mkshlib.py (ビルド済みの libos32gui.elf があれば) も v3 を焼き、
     刻印を剥がした ELF は断る。無ければ SKIP と表示する。
  5. crt の大域変数 kapi の改名 (os32_kapi_v63) で、改名前にコンパイルした
     オブジェクト (= `kapi` を参照する .o) がリンクで落ちる。

  python3 -B tools/tests/test_kapi_layout.py [--mutate]    (make check-kapi-layout-host)

--mutate は否定側 — 通常の試験が通った後、判定や拒否を崩した版で、この試験が
確かに落ちることを見る (変異は一時ディレクトリの写しの木に当てる — 実物は読むだけ)。
make・エミュレータ・実配備には触れない (i386-elf-gcc / ld / objcopy は使う)。
"""
import json
import os
import pathlib
import shutil
import struct
import subprocess
import sys
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "sdk"))
import os32x_hdr as H  # noqa: E402
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import mutpar  # noqa: E402

CROSS_DIR = pathlib.Path(os.environ.get("CROSS_DIR", str(pathlib.Path.home() / "opt/cross")))
TCC = "i386-elf-gcc"
TLD = "i386-elf-ld"
TOBJCOPY = "i386-elf-objcopy"
TFLAGS = ["-include", str(ROOT / "sdk/include/os32/os32_unit_stamp.h"),"-std=gnu11", "-m32", "-march=i386", "-ffreestanding", "-fno-pie",
          "-fno-stack-protector", "-nostdlib", "-fcommon", "-O2", "-Wall",
          "-Werror", "-D__OS32_USERLAND__",
          "-I" + str(ROOT / "include"), "-I" + str(ROOT / "sdk/include"),
          "-I" + str(ROOT / "sdk/include/os32")]
LDFLAGS = ["-m", "elf_i386", "-T", str(ROOT / "sdk/link/app.ld"), "-nostdlib",
           "--nmagic", "--gc-sections"]

failures = 0
checks = 0


def check(cond, name):
    global failures, checks
    checks += 1
    print("  %s %s" % ("ok  " if cond else "FAIL", name), flush=True)
    if not cond:
        failures += 1


def run(cmd, **kw):
    return subprocess.run([str(c) for c in cmd], cwd=str(ROOT),
                          capture_output=True, text=True, **kw)


# --------------------------------------------------------------------------
#  1. 判定関数 (exec/os32x_hdr.c)
# --------------------------------------------------------------------------

def case_check_fn(tmp):
    print("== 1: exec / shlib / 常駐シェルの判定関数 ==", flush=True)
    exe = tmp / "os32x-layout"
    r = run(["gcc", "-std=gnu11", "-Wall", "-Wextra", "-Werror",
             "-D__cdecl=",
             "-I" + str(ROOT / "exec"), "-I" + str(ROOT / "sdk/include/os32"),
             str(ROOT / "tools/tests/os32x_layout_host.c"), "-o", str(exe)])
    check(r.returncode == 0, "ホスト GNU11 -Werror でコンパイルできる")
    if r.returncode != 0:
        print(r.stderr)
        return
    r = run([exe])
    sys.stdout.write(r.stdout)
    check(r.returncode == 0, "os32x_layout_host が全部通る")
    old_exe = tmp / "os32x-layout-old-shlib"
    r = run(["gcc", "-std=gnu11", "-Wall", "-Wextra", "-Werror", "-D__cdecl=",
             "-DOS32_TEST_OLD_SHLIB", "-I" + str(ROOT / "exec"),
             "-I" + str(ROOT / "sdk/include/os32"),
             str(ROOT / "tools/tests/os32x_layout_host.c"), "-o", str(old_exe)])
    check(r.returncode == 0, "旧 shlib protocol 1 の同じ loader 判定を組める")
    if r.returncode == 0:
        r = run([old_exe])
        sys.stdout.write(r.stdout)
        check(r.returncode == 0, "app 2 × shlib 1 も拒否 (app 1 × shlib 2 と両方向)")
    r = run([TCC, "-std=gnu11", "-m32", "-march=i386", "-ffreestanding", "-fno-pie",
             "-fno-stack-protector", "-O2", "-Wall", "-Wextra", "-Werror",
             "-D__KERNEL_BUILD__",
             "-I" + str(ROOT / "include"), "-I" + str(ROOT / "sdk/include/os32"),
             "-I" + str(ROOT / "exec"),
             "-c", str(ROOT / "exec/os32x_hdr.c"), "-o", str(tmp / "os32x_hdr.o")])
    check(r.returncode == 0, "カーネルと同じ i386-elf-gcc -Werror でも通る")
    if r.returncode != 0:
        print(r.stderr)


# --------------------------------------------------------------------------
#  2. gen_kapi の容量拒否
# --------------------------------------------------------------------------

def case_capacity(tmp):
    print("== 2: gen_kapi.py は関数表の容量を超えたら生成を拒否する ==", flush=True)
    kj = json.loads((ROOT / "sdk/kapi.json").read_text(encoding="utf-8"))
    n = len(kj["api"])

    def try_cap(cap, drop=False):
        d = dict(kj)
        if drop:
            d.pop("func_capacity", None)
        else:
            d["func_capacity"] = cap
        p = tmp / "kapi_cap.json"
        p.write_text(json.dumps(d), encoding="utf-8")
        return run([sys.executable, "-B", "sdk/gen_kapi.py", "--check-only", str(p)])

    r = try_cap(n)
    check(r.returncode == 0, "容量 = 関数数 (%d) なら通る" % n)
    r = try_cap(n - 1)
    check(r.returncode != 0, "容量 = 関数数 - 1 なら拒否する")
    check("func_capacity" in r.stderr, "拒否の理由に func_capacity を出す")
    r = try_cap(0, drop=True)
    check(r.returncode != 0, "func_capacity が無ければ拒否する")
    case_capacity_full(tmp, kj)
    check(kj["func_capacity"] == 300, "いまの容量は R = 300 (票の決定)")
    check(12 * kj["func_capacity"] + 272 <= 4096,
          "容量 R はトランポリン 1 ページに収まる (12R + 272 <= 4096)")


KERNEL_CFLAGS = ["-std=gnu11", "-m32", "-march=i386", "-ffreestanding", "-fno-pie",
                 "-fno-stack-protector", "-nostdlib", "-mno-red-zone", "-fcommon",
                 "-fsigned-char", "-fno-short-enums", "-O2", "-Wall",
                 "-D__KERNEL_BUILD__"]


def case_capacity_full(tmp, kj):
    """関数数 = 容量 (予約 0 本) で生成し、exec/exec.c がコンパイルできるか。

    生成器は予約 0 本なら kapi_reserved を作らない。exec_kapi_layout_selftest が
    それを無条件に参照していると 300 本目の追加でカーネルが作れなくなる
    (実装レビュー R1)。本物の木は書き換えない — 生成器と exec.c を一時の木へ
    写し、そこで生成して、その生成物を先に探す -I で exec.c を構文検査する。"""
    n = len(kj["api"])
    gk = tmp / "gk_full"
    for d in ("sdk/include", "kapi", "exec"):
        (gk / d).mkdir(parents=True, exist_ok=True)
    shutil.copytree(ROOT / "sdk/include/os32", gk / "sdk/include/os32")
    shutil.copy(ROOT / "sdk/gen_kapi.py", gk / "sdk/gen_kapi.py")
    shutil.copy(ROOT / "exec/exec.c", gk / "exec/exec.c")
    d = dict(kj)
    d["func_capacity"] = n
    (gk / "sdk/kapi.json").write_text(json.dumps(d), encoding="utf-8")
    r = subprocess.run([sys.executable, "-B", "sdk/gen_kapi.py"], cwd=str(gk),
                       capture_output=True, text=True)
    check(r.returncode == 0, "容量 = 関数数 (%d) で生成できる" % n)
    if r.returncode != 0:
        print(r.stderr)
        return
    hdr = (gk / "sdk/include/os32/os32_kapi_generated.h").read_text(encoding="utf-8")
    check("kapi_reserved[" not in hdr and "#define KAPI_FUNC_RESERVED 0\n" in hdr,
          "予約 0 本なら kapi_reserved を作らず KAPI_FUNC_RESERVED は 0")
    inc = ["-I" + str(gk / "sdk/include"), "-I" + str(gk / "sdk/include/os32"),
           "-I" + str(gk / "exec")]
    for rel in (".", "include", "arch/x86", "platform/pc98", "exec", "kapi", "fs",
                "gfx", "drivers", "lib", "kernel"):
        inc.append("-I" + str(ROOT / rel))
    r = run([TCC, *KERNEL_CFLAGS, *inc, "-fsyntax-only", gk / "exec/exec.c"])
    check(r.returncode == 0,
          "予約 0 本の生成物で exec/exec.c がコンパイルできる "
          "(kapi_reserved の参照は #if KAPI_FUNC_RESERVED > 0 の中)")
    if r.returncode != 0:
        print("\n".join(r.stderr.splitlines()[:8]))


# --------------------------------------------------------------------------
#  3. mkos32x.py のヘッダ v4
# --------------------------------------------------------------------------

START_C = r'''
#include "os32_kapi_slots.h"
void _start(void) __attribute__((section(".text.startup"), used, noreturn));
void _start(void) { for (;;) { } }
%s
'''


def build_elf(tmp, name, stamp=True, extra_asm=None, pad=0, fill=1):
    src = tmp / (name + ".c")
    body = "OS32_KAPI_LAYOUT_STAMP();" if stamp else ""
    if pad:
        body += "\nconst unsigned char pad_%s[%d] = {%d};\n" % (name, pad, fill)
        body += "const unsigned char *keep_%s(void) { return pad_%s; }\n" % (name, name)
    src.write_text(START_C % body, encoding="utf-8")
    objs = [tmp / (name + ".o")]
    r = run([TCC, *TFLAGS, "-c", src, "-o", objs[0]])
    if r.returncode != 0:
        raise SystemExit("compile failed: " + r.stderr)
    if extra_asm:
        a = tmp / (name + "_x.s")
        a.write_text(extra_asm + "\n.section .os32_generations\n.long " + ",".join(str(x) for x in (H.OS32X_HDR_VERSION, H.OS32_KAPI_ABI_GENERATION, H.OS32_MEMORY_LAYOUT_GENERATION, H.OS32_SHLIB_PROTOCOL)) + "\n", encoding="utf-8")
        objs.append(tmp / (name + "_x.o"))
        r = run([TCC, "-m32", "-c", a, "-o", objs[-1]])
        if r.returncode != 0:
            raise SystemExit("asm failed: " + r.stderr)
    elf = tmp / (name + ".elf")
    r = run([sys.executable, "sdk/link_guard.py", TLD, *LDFLAGS, "-u", "keep_" + name if pad else "_start",
             "-o", elf, *objs])
    if r.returncode != 0:
        raise SystemExit("link failed: " + r.stderr)
    raw = tmp / (name + ".raw")
    run([TOBJCOPY, "-O", "binary", elf, raw])
    return elf, raw


def mkos32x(raw, out, elf=None, api=39):
    cmd = [sys.executable, "-B", "sdk/mkos32x.py", raw, out, "--api", str(api)]
    if elf is not None:
        cmd += ["--elf", elf]
    return run(cmd)


def case_mkos32x(tmp):
    print("== 3: mkos32x.py はヘッダ v4 を ELF の刻印から焼く ==", flush=True)
    elf, raw = build_elf(tmp, "stamped")
    out = tmp / "stamped.bin"
    r = mkos32x(raw, out, elf)
    check(r.returncode == 0, "刻印のある ELF なら成功する")
    if r.returncode != 0:
        print(r.stdout, r.stderr)
        return
    blob = out.read_bytes()
    h = H.parse_header(blob)
    check(h["version"] == H.OS32X_HDR_VERSION and h["header_size"] == H.OS32X_HDR_SIZE, "ヘッダ v4 / 60 バイト")
    sec = H.Elf32(str(elf)).section(".os32_kapi_layout")
    val = struct.unpack_from("<I", H.Elf32(str(elf)).section_bytes(sec), 0)[0]
    check(h.get("kapi_data_off") == val == 0x4B8,
          "kapi_data_off (0x%X) = ELF の .os32_kapi_layout (0x%X) = 0x4B8"
          % (h.get("kapi_data_off", 0), val))
    check(not (sec["flags"] & H.SHF_ALLOC), "刻印のセクションは非ロード (alloc でない)")
    check(h["min_api_ver"] == H.OS32X_MIN_API, f"--api 39 は {H.OS32X_MIN_API} に引き上がる (旧カーネルが受け入れない)")
    check(h["text_size"] == len(raw.read_bytes()) == len(blob) - H.OS32X_HDR_SIZE,
          "本文は .raw そのまま (刻印は平らなバイナリに入らない)")
    check(h.get("load_addr") == 0x80100000, "load_addr は ELF の .text")

    for api in (H.OS32X_MIN_API - 1, H.OS32X_MIN_API):
        api_out = tmp / f"api{api}.bin"
        r = mkos32x(raw, api_out, elf, api=api)
        check(r.returncode == 0 and H.parse_header(api_out.read_bytes())
              ["min_api_ver"] == H.OS32X_MIN_API,
              f"--api {api}: 現行版未満は引き上げ、現行版はそのまま")

    check(h['kapi_abi_generation'] == H.OS32_KAPI_ABI_GENERATION and
          h['memory_layout_generation'] == H.OS32_MEMORY_LAYOUT_GENERATION and
          h['shlib_protocol'] == 0, "独立の世代欄と依存なし")
    r = run([sys.executable, 'sdk/mkos32x.py', raw, tmp / 'stack.bin', '--elf', elf, '--stack', '524288'])
    check(r.returncode == 0 and H.parse_header((tmp / 'stack.bin').read_bytes())['stack_size'] == 524288,
          "明示 512KiB stack を保存")
    # A new CRT must not make an old selected object acceptable, even with gc-sections.
    old = tmp / 'old.o'
    run([TOBJCOPY, '--remove-section', '.os32_generations', tmp / 'stamped.o', old])
    fresh = tmp / 'fresh.c'; fresh.write_text('void fresh(void) {}')
    run([TCC, *TFLAGS, '-c', fresh, '-o', tmp / 'fresh.o'])
    def guarded(inputs, name):
        return run([sys.executable, 'sdk/link_guard.py', TLD, *LDFLAGS,
                    '-o', tmp / (name + '.elf'), *inputs])
    check(guarded([old, tmp / 'fresh.o'], 'old_direct').returncode != 0,
          "新SDK + 旧.o の欠落刻印をリンク入力で拒否")
    archive = tmp / 'old.a'; run(['i386-elf-ar', 'rcs', archive, old])
    check(guarded([tmp / 'fresh.o', '-u', '_start', archive], 'old_archive').returncode != 0,
          "取り込んだ旧 archive member も拒否")
    check(guarded([tmp / 'stamped.o', archive], 'unused_archive').returncode == 0,
          "未使用の旧 member を取り込んだものと区別")
    # Copies of actual compiler archives outside CROSS_DIR, without its environment.
    vendor = tmp / 'alternate-toolchain'; vendor.mkdir()
    shutil.copyfile(CROSS_DIR / 'i386-elf/lib/libc.a', vendor / 'libc.a')
    gcc_archive = next((CROSS_DIR / 'lib/gcc/i386-elf').glob('*/libgcc.a'))
    shutil.copyfile(gcc_archive, vendor / 'libgcc.a')
    env = dict(os.environ); env.pop('CROSS_DIR', None)
    r = subprocess.run([sys.executable, 'sdk/link_guard.py', TLD, *LDFLAGS,
                       '-L', str(vendor), '-o', str(tmp / 'vendor.elf'),
                       str(tmp / 'stamped.o'), '-u', 'strlen', '-u', '__udivdi3', '-lc', '-lgcc'],
                       cwd=ROOT, env=env, capture_output=True, text=True)
    check(r.returncode == 0, 'CROSS_DIRなし・別位置のnewlib/libgcc実memberはリンク可能: ' + r.stderr)
    if r.returncode == 0:
        evidence = json.loads((tmp / 'vendor.inputs.json').read_text())
        check(set(evidence['vendor']) == {str((vendor / 'libc.a').resolve()),
                                       str((vendor / 'libgcc.a').resolve())},
              'vendor免除はldが選択した正確なarchive pathを記録')
    note = tmp / 'old.note'
    note.write_bytes(struct.pack('<4I', H.OS32X_HDR_VERSION - 1, H.OS32_KAPI_ABI_GENERATION,
                                 H.OS32_MEMORY_LAYOUT_GENERATION, H.OS32_SHLIB_PROTOCOL))
    stale = tmp / 'stale.o'
    run([TOBJCOPY, '--update-section', '.os32_generations=' + str(note), tmp / 'stamped.o', stale])
    check(guarded([stale, tmp / 'fresh.o'], 'mixed_generation').returncode != 0,
          "形式世代の旧値と新単位の混在を拒否")
    # rustc wrapper must reject legacy metadata before launching the compiler/LTO.
    dep = tmp / 'old.rlib'; dep.write_bytes(b'old bitcode')
    r = run([sys.executable, 'sdk/rustc_stamp.py', '/bin/true', '--crate-name', 'test',
             '--out-dir', tmp, '--emit=link', '--extern', 'old=' + str(dep)])
    check(r.returncode != 0, "旧 Rust crate は LTO 前に拒否")

    # 刻印の無い ELF (crt0 を付けずにリンクした) → 失敗
    elf2, raw2 = build_elf(tmp, "nostamp", stamp=False)
    check(len(raw2.read_bytes()) == len(raw.read_bytes()),
          "刻印の有無で平らなバイナリの大きさは変わらない")
    r = mkos32x(raw2, tmp / "nostamp.bin", elf2)
    check(r.returncode != 0, "刻印が無ければ失敗する")
    check(".os32_kapi_layout" in r.stderr, "理由に .os32_kapi_layout を出す")
    check(not (tmp / "nostamp.bin").exists(), "失敗したら出力を作らない")

    # 刻印が 2 つで値が違う (古いオブジェクトの混入) → 失敗
    elf3, raw3 = build_elf(tmp, "mixed", extra_asm=(
        '.pushsection .os32_kapi_layout,"",@progbits\n.p2align 2\n.long 0x4B4\n.popsection\n'))
    r = mkos32x(raw3, tmp / "mixed.bin", elf3)
    check(r.returncode != 0, "刻印の値が食い違えば失敗する")

    # 刻印が 2 つで同じ値 (crt0 + os32api) → 通る
    elf4, raw4 = build_elf(tmp, "twin", extra_asm=(
        '.pushsection .os32_kapi_layout,"",@progbits\n.p2align 2\n.long 0x4B8\n.popsection\n'))
    r = mkos32x(raw4, tmp / "twin.bin", elf4)
    check(r.returncode == 0, "刻印が 2 つでも同じ値なら通る (crt0 + os32api)")

    # .raw と .elf の世代違い → 失敗
    elf5, raw5 = build_elf(tmp, "bigger", pad=256)
    r = mkos32x(raw5, tmp / "swap.bin", elf)
    check(r.returncode != 0, "別の ELF の .raw を渡したら失敗する")

    # 同じ大きさで中身だけ違う .raw (旧配置を読む disp32 と新配置を読む
    # disp32 は同じ長さ — 実装レビュー R1、Codex blocker 1) → 失敗
    elf6, raw6 = build_elf(tmp, "same1", pad=256, fill=1)
    elf7, raw7 = build_elf(tmp, "same2", pad=256, fill=2)
    b6, b7 = raw6.read_bytes(), raw7.read_bytes()
    check(len(b6) == len(b7) and b6 != b7,
          "試験の前提: 2 つの .raw は同じ大きさで中身が違う")
    r = mkos32x(raw6, tmp / "same_ok.bin", elf6)
    check(r.returncode == 0, "同じ世代の .raw と .elf は通る")
    r = mkos32x(raw7, tmp / "same_swap.bin", elf6)
    check(r.returncode != 0, "同じ大きさで中身の違う .raw を渡したら失敗する")
    check("PT_LOAD" in r.stderr, "理由に PT_LOAD の不一致を出す")
    check(not (tmp / "same_swap.bin").exists(), "失敗したら出力を作らない")
    # 末尾の 1 バイトだけ違う (範囲の終端まで比べているか)
    flip = bytearray(b6)
    flip[-1] ^= 0xFF
    (tmp / "flip_last.raw").write_bytes(bytes(flip))
    r = mkos32x(tmp / "flip_last.raw", tmp / "flip_last.bin", elf6)
    check(r.returncode != 0, ".raw の最後の 1 バイトが違えば失敗する")
    flip = bytearray(b6)
    flip[0] ^= 0xFF
    (tmp / "flip_first.raw").write_bytes(bytes(flip))
    r = mkos32x(tmp / "flip_first.raw", tmp / "flip_first.bin", elf6)
    check(r.returncode != 0, ".raw の最初の 1 バイトが違えば失敗する")

    # --elf が無い → 失敗
    r = mkos32x(raw, tmp / "noelf.bin", None)
    check(r.returncode != 0, "--elf が無ければ失敗する")


# --------------------------------------------------------------------------
#  4. mkshlib.py (ビルド済みの libos32gui があれば)
# --------------------------------------------------------------------------

def case_mkshlib(tmp):
    print("== 4: mkshlib.py もヘッダ v4 ==", flush=True)
    elf = ROOT / "userland/libos32gui.elf"
    raw = ROOT / "userland/libos32gui.raw"
    if not elf.is_file():
        print("  SKIP userland/libos32gui.elf が未ビルド (make all の後に回すと見る)",
              flush=True)
        return
    rawtmp = tmp / "lib.raw"
    if raw.is_file():
        shutil.copy(raw, rawtmp)
    else:
        run([TOBJCOPY, "-O", "binary", elf, rawtmp])
    out = tmp / "lib.shlib"
    r = run([sys.executable, "-B", "tools/mkshlib.py", rawtmp, out, "--elf", elf,
             "--api", "51"])
    check(r.returncode == 0, "ビルド済み libos32gui から shlib を作れる")
    if r.returncode != 0:
        print(r.stdout, r.stderr)
        return
    h = H.parse_header(out.read_bytes())
    check(h["version"] == H.OS32X_HDR_VERSION and h["header_size"] == H.OS32X_HDR_SIZE, "shlib もヘッダ v4")
    check(h.get("kapi_data_off") == H.read_kapi_layout(H.Elf32(str(elf))) == 0x4B8,
          "shlib の kapi_data_off = os32api の刻印 = 0x4B8")
    check(h["flags"] & H.OS32X_FLAG_SHLIB, "OS32X_FLAG_SHLIB が立つ")
    check(h["min_api_ver"] == H.OS32X_MIN_API, "shlib の --api 51 も 71 に引き上がる")
    stripped = tmp / "lib_nostamp.elf"
    run([TOBJCOPY, "--remove-section", ".os32_kapi_layout", elf, stripped])
    r = run([sys.executable, "-B", "tools/mkshlib.py", rawtmp, tmp / "x.shlib",
             "--elf", stripped])
    check(r.returncode != 0, "刻印を剥がした ELF は断る")

    # 同じ大きさで中身だけ違う .raw (ジャンプ表の番地・大きさが同じでも
    # 本文が違う — 実装レビュー R1、Codex blocker 1) → 断る
    body = bytearray(rawtmp.read_bytes())
    body[-1] ^= 0xFF
    body[len(body) // 2] ^= 0x01
    swapped = tmp / "lib_swapped.raw"
    swapped.write_bytes(bytes(body))
    r = run([sys.executable, "-B", "tools/mkshlib.py", swapped, tmp / "sw.shlib",
             "--elf", elf, "--api", "51"])
    check(r.returncode != 0, "同じ大きさで中身の違う .raw は断る (shlib)")
    check("PT_LOAD" in r.stderr, "理由に PT_LOAD の不一致を出す (shlib)")
    check(not (tmp / "sw.shlib").exists(), "失敗したら出力を作らない (shlib)")


# --------------------------------------------------------------------------
#  5. kapi の改名で作り直し忘れの .o がリンクで落ちる
# --------------------------------------------------------------------------

STUB_C = r'''
int main(int argc, char **argv, void *api) { (void)argc; (void)argv; (void)api; return 0; }
void _init(void) { }
int os32_help_show(const char *n) { (void)n; return -1; }
void _start(void) __attribute__((section(".text.startup"), used, noreturn));
void _start(void) { for (;;) { } }
'''

STALE_C = r'''
/* v62 以前の SDK でコンパイルしたオブジェクトの代わり: ヘッダの
 * `#define kapi os32_kapi_v63` を通っていないので `kapi` を参照する。 */
extern void *kapi;
void *use_kapi(void) { return kapi; }
'''

FRESH_C = r'''
#include "os32api.h"
extern KernelAPI *kapi;   /* 作り直した .o: ヘッダを通るので実名は os32_kapi_v63 */
void *use_kapi(void) { return (void *)kapi; }
'''


def case_rename(tmp):
    print("== 5: crt の kapi の改名 (os32_kapi_v63) ==", flush=True)
    crt = tmp / "crt0_c.o"
    r = run([TCC, *TFLAGS, "-I" + str(ROOT / "sdk/include/os32"), "-c",
             ROOT / "sdk/crt/crt0_c.c", "-o", crt])
    check(r.returncode == 0, "crt0_c.c をコンパイルできる")
    if r.returncode != 0:
        print(r.stderr)
        return
    nm = run(["i386-elf-nm", crt]).stdout
    kj = json.loads((ROOT / "sdk/kapi.json").read_text(encoding="utf-8"))
    sym = kj["crt_kapi_symbol"]
    check((" " + sym) in nm and " kapi\n" not in nm,
          "crt0_c.o が定義するのは %s (kapi ではない)" % sym)

    objs = {}
    for name, text in (("stub", STUB_C), ("stale", STALE_C), ("fresh", FRESH_C)):
        p = tmp / (name + ".c")
        p.write_text(text, encoding="utf-8")
        o = tmp / (name + ".o")
        r = run([TCC, *TFLAGS, "-c", p, "-o", o])
        if r.returncode != 0:
            raise SystemExit("compile %s failed: %s" % (name, r.stderr))
        objs[name] = o

    r = run([sys.executable, "sdk/link_guard.py", TLD, *LDFLAGS, "-u", "use_kapi", "-o", tmp / "fresh.elf",
             objs["stub"], crt, objs["fresh"]])
    check(r.returncode == 0, "作り直した .o は新しい crt とリンクできる")
    if r.returncode != 0:
        print(r.stderr)
    r = run([sys.executable, "sdk/link_guard.py", TLD, *LDFLAGS, "-u", "use_kapi", "-o", tmp / "stale.elf",
             objs["stub"], crt, objs["stale"]])
    check(r.returncode != 0, "作り直し忘れの .o (kapi を参照) はリンクで落ちる")
    check("kapi" in r.stderr and "undefined" in r.stderr,
          "理由は kapi の未定義参照")


# --------------------------------------------------------------------------
#  変異 (否定側)
# --------------------------------------------------------------------------

MUTATIONS = [
    ("exec/os32x_hdr.c", "mismatch_accepted",
     "        return OS32X_LAYOUT_MISMATCH;", "        return OS32X_LAYOUT_OK;"),
    ("exec/os32x_hdr.c", "short_read_trusted",
     "    if (!hdr || read_len < OS32X_HDR_SIZE)", "    if (!hdr)"),
    ("exec/os32x_hdr.c", "unknown_format_accepted",
     "hdr->version != OS32X_HDR_VERSION", "hdr->version < OS32X_HDR_VERSION"),
    ("sdk/gen_kapi.py", "capacity_not_enforced",
     "    if n > cap:",
     "    if False:"),
    ("sdk/os32x_hdr.py", "missing_stamp_defaulted",
     "    if s is None or s['size'] == 0:\n        raise HeaderError(",
     "    if s is None or s['size'] == 0:\n        return 0x4B8\n        raise HeaderError("),
    ("sdk/os32x_hdr.py", "mixed_stamps_allowed",
     "    if len(uniq) != 1:",
     "    if False:"),
    ("sdk/os32x_hdr.py", "min_api_not_raised",
     "    return max(int(min_api), OS32X_MIN_API)",
     "    return int(min_api)"),
    ("sdk/os32x_hdr.py", "raw_elf_mismatch_ignored",
     "    if len(raw) != want:",
     "    if False:"),
    ("sdk/os32x_hdr.py", "raw_elf_content_ignored",
     "        if a != b:",
     "        if False:"),
    ("sdk/os32x_hdr.py", "raw_elf_segments_skipped",
     "        if seg['type'] != PT_LOAD or seg['filesz'] == 0:",
     "        if True:"),
    ("exec/os32x_hdr.c", "abi_generation_unchecked",
     "hdr->kapi_abi_generation != OS32_KAPI_ABI_GENERATION", "0"),
    ("sdk/kapi.json", "generation-held",
     '"shlib_protocol": 2', '"shlib_protocol": 1'),
    ("sdk/kapi.json", "memory_generation_rolled_back",
     '"memory_layout": 3', '"memory_layout": 2'),
    ("exec/os32x_hdr.c", "memory_generation_unchecked",
     "hdr->memory_layout_generation != OS32_MEMORY_LAYOUT_GENERATION", "0"),
    ("exec/os32x_hdr.c", "shlib_protocol_unchecked",
     "(hdr->shlib_protocol && hdr->shlib_protocol != OS32_SHLIB_PROTOCOL)", "0"),
    ("sdk/link_guard.py", "per_unit_note_unchecked",
     "def check_note(elf):", "def check_note(elf):\n    return"),
    ("sdk/gen_kapi.py", "crt_symbol_not_renamed",
     "#define kapi {CRT_KAPI_SYMBOL}",
     "#define os32_kapi_unused {CRT_KAPI_SYMBOL}"),
]


SELF = "tools/tests/test_kapi_layout.py"
# 写しの木で実体にする場所。変異を当てるファイル (exec/ sdk/) と、生成器が
# cwd 相対で書き出す先 (sdk/include/os32 kapi/ exec/)、子の試験が "..." で
# 引く先 (include/) は丸ごと実体にする — symlink を通して書くと実物を壊す。
MUT_REAL = ("exec", "sdk", "include", "kapi", "build/os32.ld",
            "tools/tests/os32x_layout_host.c")


def case_generation_isolation(tmp):
    """Generators in an overlay must preserve the owning tree, including mtimes."""
    seed = tmp / 'generation-seed'
    for directory in ('sdk/include/os32', 'sdk/rust/os32api/src', 'sdk/crt',
                      'sdk/link', 'kapi', 'exec', 'include', 'build', 'tools/tests'):
        (seed / directory).mkdir(parents=True, exist_ok=True)
    links = ('sdk/link/app.ld', 'sdk/link/app_sys.ld', 'sdk/link/shlib.ld', 'build/os32.ld')
    for rel in ('sdk/gen_kapi.py', 'sdk/kapi.json', 'tools/tests/os32x_layout_host.c', *links):
        shutil.copy2(ROOT / rel, seed / rel)
    def snapshot():
        return {rel: ((seed / rel).read_bytes(), (seed / rel).stat().st_mtime_ns)
                for rel in links}
    before = snapshot()
    tree = mutpar.overlay(seed, tmp / 'generation-overlay', real=MUT_REAL)
    result = subprocess.run([sys.executable, '-B', 'sdk/gen_kapi.py'], cwd=tree,
                            capture_output=True, timeout=60)
    check(result.returncode == 0, 'overlay generator completes')
    check(snapshot() == before, 'overlay preserves source link script bytes and mtimes')


def one_mutation(item):
    """変異 1 本: 写しの木を壊し (gen_kapi.py の変異なら写しの中で生成し直し)、
    写しの中のこの試験を流し直す。実物は読むだけ。(印字, 見逃し) を返す。"""
    rel, name, old, new = item
    original = (ROOT / rel).read_text(encoding="utf-8")
    if old not in original:
        return "MUTATE %-26s SKIP (目印が見つからない)" % name, 1
    regen = rel == "sdk/kapi.json" or (rel == "sdk/gen_kapi.py" and "CRT_KAPI_SYMBOL" in old)
    with tempfile.TemporaryDirectory(prefix="os32-kapi-layout-mut-") as td:
        tree = mutpar.mutant_tree(ROOT, pathlib.Path(td) / "tree",
                                  {rel: original.replace(old, new, 1)},
                                  real=set(MUT_REAL) | {SELF})
        if regen:
            subprocess.run([sys.executable, "-B", "sdk/gen_kapi.py"],
                           cwd=str(tree), check=True, capture_output=True)
        result = subprocess.run(
            [sys.executable, "-B", str(tree / SELF)],
            cwd=str(tree), capture_output=True, timeout=600)
        rc = result.returncode
        if name == "generation-held":
            expected = b"FAIL: OS32_SHLIB_PROTOCOL == 2"
            if rc == 0 or expected not in result.stdout:
                return "MUTATE generation-held ERROR (runtime predicate missing)", 1
            return f"MUTATE generation-held runtime RED rc={rc} -> " + expected.decode(), 0
    if rc == 0:
        return ("MUTATE %-26s **GREEN のまま = 試験が規則を見ていない**" % name,
                1)
    lines = result.stdout.decode(errors='replace').splitlines()
    failures = [line for line in lines if 'FAIL' in line]
    # Keep compile/link failures separate from executed semantic assertions.
    build_failures = [line for line in failures if any(word in line for word in
                      ('コンパイル', 'リンクできる', '組める'))]
    classification = 'compile/link error' if build_failures else 'runtime RED'
    if not failures:
        return "MUTATE %-26s ERROR (no assertion failure)" % name, 1
    return "MUTATE %-26s %s rc=%d -> %s" % (name, classification, rc, failures[0].strip()), 0


def run_mutations():
    """否定側。変異は一時ディレクトリの写しにだけ当てる (mutpar で並列、
    check-par で回せる)。"""
    return mutpar.run_with_control(one_mutation, MUTATIONS,
                                   ("exec/os32x_hdr.c", "control", "", ""))


if __name__ == "__main__":
    os.environ["PATH"] = str(CROSS_DIR / "bin") + os.pathsep + os.environ.get("PATH", "")
    with tempfile.TemporaryDirectory(prefix="os32-kapi-layout-") as td:
        td = pathlib.Path(td)
        case_check_fn(td)
        case_capacity(td)
        case_mkos32x(td)
        case_mkshlib(td)
        case_rename(td)
        case_generation_isolation(td)
    print("\n%d checks, %d failures" % (checks, failures))
    if failures:
        sys.exit(1)
    if "--mutate" in sys.argv:
        sys.exit(1 if run_mutations() else 0)
    sys.exit(0)
