"""vmkernel.lz4 の高圧縮 (LZ4 HC) と展開側 3 実装の一致、生成時の上限検査。

記録: tools/tests/vmkernel_lz4_tdd.md
票  : docs/archive/realhw_v21/TASK_SERIAL_HOSTFS.md 部品 A-1 (N8 の生成側は TASK_HDD_INSTALL)

tools/mkvmkernel.py (実物) で VK32 イメージを作り、各エントリを
  mode 0  boot/lz4_mini.c        (HDD ローダ loader_hdd.bin が使う)
  mode 1  lib/lz4.c              (lz4 コマンド。境界チェックを lz4_mini と揃えている)
  mode 2  boot/loader_fat_new.asm の pm_lz4_decode (FD ローダ。**ASM をそのまま** 32bit で)
で展開して元の kernel.bin / sqlite.bin とバイト一致するかを見る。ハーネス
tools/tests/vmkernel_lz4_host.c は libc なしの 32bit 静的 ELF で、ASM を
ファイルから切り出して nasm -f elf32 で組んでリンクする。

合成データは HC が使う形 (リテラル長の延長 255 を跨ぐ・マッチ長の延長 255 を
跨ぐ・offset >= 32768・重なるマッチ) を**必ず含む**ことを流れを数えて確かめる
(当たるかどうかを実データの運に任せない)。

  python3 -B tools/tests/test_vmkernel_lz4.py            # 合成データ + 上限
  python3 -B tools/tests/test_vmkernel_lz4.py --real     # + build/out の実物 (無ければ FAIL)
  python3 -B tools/tests/test_vmkernel_lz4.py --target   # デコーダを i386-elf-gcc (ローダと同じ) で組む
  python3 -B tools/tests/test_vmkernel_lz4.py --mutate   # 否定側 (写しの上で変異 → RED)
"""
import host32
import os
import pathlib
import random
import re
import shutil
import struct
import subprocess
import sys
import tempfile

import lz4.block

ROOT = pathlib.Path(__file__).resolve().parents[2]
HARNESS = ROOT / "tools/tests/vmkernel_lz4_host.c"
SCRIPT = ROOT / "tools/mkvmkernel.py"
MINI_SRC = ROOT / "boot/lz4_mini.c"
LZ4C_SRC = ROOT / "lib/lz4.c"
ASM_SRC = ROOT / "boot/loader_fat_new.asm"
LIMIT_HEADER = ROOT / "boot/boot_defs.h"
REAL_KERNEL = ROOT / "build/out/kernel.bin"
REAL_SQLITE = ROOT / "build/out/sqlite.bin"

MODES = [(0, "lz4_mini.c"), (1, "lib/lz4.c"), (2, "pm_lz4_decode (asm)")]


class Fail(Exception):
    pass


def sh(cmd, **kw):
    r = subprocess.run(cmd, capture_output=True, **kw)
    if r.returncode != 0:
        raise Fail("{} -> {}\n{}{}".format(
            " ".join(str(c) for c in cmd), r.returncode,
            r.stdout.decode(errors="replace"), r.stderr.decode(errors="replace")))
    return r


# ---------------------------------------------------------------- ビルド

def extract_asm(text):
    """loader_fat_new.asm から pm_lz4_decode と LZ4_MINMATCH を切り出す。

    pm_lz4_decode は VK32_HOST_BEGIN〜END の中にある (test_vk32_crc.py と同じ
    区間)。ここは関数の頭から次の区切り (;; ===== か区間の終わり) まで。"""
    m = re.search(r"^LZ4_MINMATCH\s+EQU\s+\S+\s*$", text, re.MULTILINE)
    if not m:
        raise Fail("LZ4_MINMATCH EQU が見つからない")
    equ = m.group(0)
    lines = text.splitlines()
    try:
        start = next(i for i, ln in enumerate(lines) if ln.startswith("pm_lz4_decode:"))
    except StopIteration:
        raise Fail("pm_lz4_decode: が見つからない")
    end = start + 1
    while end < len(lines) and not (lines[end].startswith(";; =====")
                                    or lines[end].startswith(";; <<< VK32_HOST_END")
                                    or lines[end].startswith(";; CRC-32 nibble")):
        end += 1
    body = "\n".join(lines[start:end])
    return ("bits 32\n{}\nsection .text\nglobal pm_lz4_decode\n{}\n".format(equ, body))


def build(work, srcs, target):
    """srcs: {'mini': path, 'lz4c': path, 'asm': path}。exe の path を返す。"""
    work = pathlib.Path(work)
    common = ["-std=gnu11", "-m32", "-march=i386", "-ffreestanding",
              "-fno-stack-protector", "-fno-pie", "-Wall", "-Werror"]
    if target:
        cc = "i386-elf-gcc"
        if shutil.which(cc) is None:
            raise Fail("i386-elf-gcc が PATH に無い (--target)")
    else:
        cc = "gcc"
        common = common + ["-fno-pic"]

    mini = work / "lz4_mini.c"
    shutil.copy(srcs["mini"], mini)
    # ローダと同じ -Os (build/boot.mk CFLAGS_BOOT)
    sh([cc] + common + ["-Os", "-I" + str(ROOT / "boot"), "-c", str(mini),
                        "-o", str(work / "mini.o")])

    lz4c = work / "lz4.c"
    shutil.copy(srcs["lz4c"], lz4c)
    # lz4 コマンドと同じ -O0 (build/programs.mk lib/lz4_prog.o)
    sh([cc] + common + ["-O0", "-I" + str(ROOT / "lib"), "-I" + str(ROOT / "include"),
                        "-c", str(lz4c), "-o", str(work / "lz4c.o")])

    asm = work / "pm_lz4.asm"
    asm.write_text(extract_asm(pathlib.Path(srcs["asm"]).read_text(encoding="utf-8")),
                   encoding="utf-8")
    sh(["nasm", "-f", "elf32", str(asm), "-o", str(work / "asm.o")])

    sh(["gcc", "-std=gnu11", "-m32", "-ffreestanding", "-fno-pic", "-fno-pie",
        "-fno-stack-protector", "-fno-builtin", "-O2", "-Wall", "-Wextra",
        "-Werror", 
        "-c", str(HARNESS), "-o", str(work / "harness.o")])

    exe = work / "vmkernel_lz4_host"
    sh(["ld", "-m", "elf_i386", "-static", "-e", "_start", "-o", str(exe),
        str(work / "harness.o"), str(work / "mini.o"), str(work / "lz4c.o"),
        str(work / "asm.o")])
    return exe


def decode(exe, mode, comp, raw):
    data = struct.pack("<3I", mode, len(comp), raw) + comp
    r = host32.run([str(exe)], input=data, capture_output=True, timeout=60)
    if r.returncode == 3:
        return None, "展開先の後ろ (番兵) を書き越した"
    if r.returncode != 0:
        return None, "harness exit {}".format(r.returncode)
    if len(r.stdout) < 4:
        return None, "出力が短い"
    ret = struct.unpack("<i", r.stdout[:4])[0]
    return ret, r.stdout[4:]


# ---------------------------------------------------------------- 流れの数え上げ

def lz4_stats(comp):
    """LZ4 ブロックを走査して、HC が使う形を数える。"""
    st = {"seq": 0, "lit_ext": 0, "lit_ext255": 0, "match_ext": 0,
          "match_ext255": 0, "max_offset": 0, "overlap": 0}
    ip = 0
    n = len(comp)
    while ip < n:
        tok = comp[ip]
        ip += 1
        lit = tok >> 4
        if lit == 15:
            st["lit_ext"] += 1
            while True:
                b = comp[ip]
                ip += 1
                lit += b
                if b == 255:
                    st["lit_ext255"] += 1
                if b != 255:
                    break
        ip += lit
        if ip >= n:
            break
        off = comp[ip] | (comp[ip + 1] << 8)
        ip += 2
        ml = (tok & 15) + 4
        if (tok & 15) == 15:
            st["match_ext"] += 1
            while True:
                b = comp[ip]
                ip += 1
                ml += b
                if b == 255:
                    st["match_ext255"] += 1
                if b != 255:
                    break
        st["seq"] += 1
        st["max_offset"] = max(st["max_offset"], off)
        if off < ml:
            st["overlap"] += 1
    return st


# ---------------------------------------------------------------- 入力

def synthetic():
    """HC の延長バイト・遠い offset・重なるマッチを必ず含むデータ。"""
    rnd = random.Random(20260924)

    def rb(n):
        return bytes(rnd.getrandbits(8) for _ in range(n))

    blk = rb(4000)
    kernel = (rb(3000)                      # リテラル長の延長が 255 を跨ぐ
              + bytes(70000)                # offset 1 の重なるマッチ、延長 255 が多数
              + blk + rb(60000) + blk       # offset 64000 (上位バイトが要る)
              + b"OS32 " * 3000             # 周期 5 の重なり
              + bytes(range(256)) * 40)
    sqlite = rb(2000) + (b"\x00\x01\x02\x03" * 5000) + rb(300) + bytes(20000) + rb(17)
    return kernel, sqlite


def run_mk(script, kernel_path, sqlite_path, out_path):
    return subprocess.run(
        [sys.executable, "-B", str(script),
         "--kernel", str(kernel_path), "--kernel-addr", "0x100000",
         "--sqlite", str(sqlite_path), "--sqlite-addr", "0x200000",
         "--limit-header", str(LIMIT_HEADER), "-o", str(out_path)],
        capture_output=True)


def parse_vk32(img):
    # VK32 v2 (エントリ表の後ろに CRC 表 + image_size + image_crc)。CRC の中身は
    # tools/tests/test_vk32_crc.py が見る。ここは展開側の一致だけ。
    magic, hsz, ver, cnt = struct.unpack_from("<4I", img, 0)
    if magic != 0x32334B56 or ver != 2 or hsz != 16 + cnt * 20 + 8:
        raise Fail("VK32 ヘッダ不正 magic={:#x} ver={} hsz={} cnt={}".format(
            magic, ver, hsz, cnt))
    ents = []
    for i in range(cnt):
        addr, raw, off, csz = struct.unpack_from("<4I", img, 16 + i * 16)
        ents.append((addr, raw, img[off:off + csz]))
    return ents


def max_image_size():
    text = LIMIT_HEADER.read_text(encoding="utf-8")
    m = re.search(r"^#define\s+MAX_IMAGE_SIZE\s+\((\d+)\s*\*\s*(\d+)\)", text, re.MULTILINE)
    if not m:
        raise Fail("boot_defs.h の MAX_IMAGE_SIZE の形が想定外")
    return int(m.group(1)) * int(m.group(2))


# ---------------------------------------------------------------- ケース

def check_image(exe, script, work, kernel, sqlite, label, need_features):
    work = pathlib.Path(work)
    kp, sp, op = work / (label + "_k.bin"), work / (label + "_s.bin"), work / (label + ".lz4")
    kp.write_bytes(kernel)
    sp.write_bytes(sqlite)
    r = run_mk(script, kp, sp, op)
    if r.returncode != 0:
        raise Fail("[{}] mkvmkernel が失敗\n{}".format(label, r.stderr.decode(errors="replace")))
    img = op.read_bytes()
    ents = parse_vk32(img)
    if [e[0] for e in ents] != [0x100000, 0x200000]:
        raise Fail("[{}] load_addr がずれた".format(label))
    fast_total = 64 + sum(len(lz4.block.compress(x, store_size=False)) for x in (kernel, sqlite))
    if not len(img) < fast_total:
        raise Fail("[{}] 高圧縮になっていない ({} >= 既定 {})".format(label, len(img), fast_total))
    lines = ["[{}] image {} B (既定の圧縮なら {} B)".format(label, len(img), fast_total)]
    for (addr, raw, comp), orig, name in zip(ents, (kernel, sqlite), ("kernel", "sqlite")):
        if raw != len(orig):
            raise Fail("[{}] {} raw_size {} != {}".format(label, name, raw, len(orig)))
        st = lz4_stats(comp)
        lines.append("  {}: raw {} comp {} {}".format(name, raw, len(comp), st))
        if need_features and name == "kernel":
            for key, cond in (("lit_ext255", st["lit_ext255"] > 0),
                              ("match_ext255", st["match_ext255"] > 0),
                              ("max_offset>=32768", st["max_offset"] >= 32768),
                              ("overlap", st["overlap"] > 0)):
                if not cond:
                    raise Fail("[{}] 合成データが {} を含まない (試験が弱い)".format(label, key))
        for mode, mname in MODES:
            ret, out = decode(exe, mode, comp, raw)
            if ret is None:
                raise Fail("[{}] {} {}: {}".format(label, name, mname, out))
            if ret != raw or out != orig:
                pos = next((i for i in range(min(len(out), len(orig))) if out[i] != orig[i]),
                           min(len(out), len(orig)))
                raise Fail("[{}] {} {}: 不一致 ret={} raw={} 最初の差 {}".format(
                    label, name, mname, ret, raw, pos))
        lines.append("    3 実装とも {} バイト一致".format(raw))
    return len(img), lines


def case_oversize(script, work):
    """上限を超えたら失敗し、古い出力も残さない。"""
    work = pathlib.Path(work)
    rnd = random.Random(1)
    kp, sp, op = work / "big_k.bin", work / "big_s.bin", work / "big.lz4"
    kp.write_bytes(bytes(rnd.getrandbits(8) for _ in range(max_image_size() + 1000)))
    sp.write_bytes(b"\0" * 64)
    op.write_bytes(b"stale")
    r = run_mk(script, kp, sp, op)
    if r.returncode == 0:
        raise Fail("[oversize] 上限超えで成功した")
    if op.exists():
        raise Fail("[oversize] 古い出力が残った")
    return ["[oversize] 失敗 + 出力なし: {}".format(
        r.stderr.decode(errors="replace").strip().splitlines()[-1][:120])]


def case_boundary(script, work):
    """ちょうど上限は通り、上限 + 1 は落ちる (> と >= の取り違え)。"""
    work = pathlib.Path(work)
    limit = max_image_size()
    rnd = random.Random(7)
    pool = bytes(rnd.getrandbits(8) for _ in range(limit + 4096))
    sq = b"\0" * 64
    sq_c = len(lz4.block.compress(sq, store_size=False, mode="high_compression",
                                  compression=12))

    def total(n):
        return 64 + sq_c + len(lz4.block.compress(pool[:n], store_size=False,
                                                  mode="high_compression", compression=12))

    found = {}
    lo, hi = 0, len(pool)
    while lo < hi:          # total(n) >= limit となる最小の n
        mid = (lo + hi) // 2
        if total(mid) < limit:
            lo = mid + 1
        else:
            hi = mid
    for n in range(max(0, lo - 8), lo + 16):
        t = total(n)
        if t in (limit, limit + 1) and t not in found:
            found[t] = n
    if set(found) != {limit, limit + 1}:
        raise Fail("[boundary] ちょうどの大きさを作れない: {}".format(found))
    out = []
    sp = work / "bd_s.bin"
    sp.write_bytes(sq)
    for t, want_ok in ((limit, True), (limit + 1, False)):
        kp, op = work / "bd_k.bin", work / "bd.lz4"
        kp.write_bytes(pool[:found[t]])
        if op.exists():
            op.unlink()
        r = run_mk(script, kp, sp, op)
        ok = r.returncode == 0
        if ok != want_ok:
            raise Fail("[boundary] total {} (上限 {}) で {}".format(
                t, limit, "成功した" if ok else "失敗した"))
        if ok and op.stat().st_size != t:
            raise Fail("[boundary] 出力 {} != 期待 {}".format(op.stat().st_size, t))
        out.append("[boundary] total {} -> {}".format(t, "通る" if ok else "落ちる"))
    return out


def run_all(srcs, script, target, real):
    with tempfile.TemporaryDirectory(prefix="vmk_lz4_") as work:
        exe = build(work, srcs, target)
        lines = []
        k, s = synthetic()
        lines += check_image(exe, script, work, k, s, "synthetic", True)[1]
        if real:
            if not (REAL_KERNEL.is_file() and REAL_SQLITE.is_file()):
                raise Fail("[real] build/out/kernel.bin / sqlite.bin が無い (make kernel が先)")
            size, ln = check_image(exe, script, work, REAL_KERNEL.read_bytes(),
                                   REAL_SQLITE.read_bytes(), "real", False)
            lines += ln
            lines.append("[real] 上限 {} まで残り {} B".format(max_image_size(),
                                                           max_image_size() - size))
        lines += case_oversize(script, work)
        lines += case_boundary(script, work)
        return lines


# ---------------------------------------------------------------- 変異

MUTATIONS = [
    # (対象キー, パターン, 置換, 何回目 (0 始まり), 説明)
    ("mini", "} while (s == 255);", "} while (0);", 0, "lz4_mini: リテラル長の延長を 1 バイトで打ち切る"),
    ("mini", "} while (s == 255);", "} while (0);", 1, "lz4_mini: マッチ長の延長を 1 バイトで打ち切る"),
    ("mini", "((int)ip[1] << 8)", "0", 0, "lz4_mini: offset の上位バイトを捨てる"),
    ("lz4c", "} while (s == 255);", "} while (0);", 0, "lib/lz4.c: リテラル長の延長を打ち切る"),
    ("lz4c", "} while (s == 255);", "} while (0);", 1, "lib/lz4.c: マッチ長の延長を打ち切る"),
    ("lz4c", "((int)ip[1] << 8)", "0", 0, "lib/lz4.c: offset の上位バイトを捨てる"),
    ("asm", "je      .lz4_lit_ext", "nop", 0, "asm: リテラル長の延長を打ち切る"),
    ("asm", "je      .lz4_match_ext", "nop", 0, "asm: マッチ長の延長を打ち切る"),
    ("asm", "movzx   eax, word [esi]", "movzx   eax, byte [esi]", 0, "asm: offset を 1 バイトで読む"),
    ("script", "if offset > max_image_size:", "if False:", 0, "mkvmkernel: 上限を見ない"),
    ("script", "if offset > max_image_size:", "if offset > max_image_size + 1:", 0,
     "mkvmkernel: 上限を 1 バイト緩める"),
    ("script", "mode='high_compression',", "mode='default',", 0, "mkvmkernel: 既定 (fast) 圧縮に戻す"),
    ("script", "os.remove(args.output)", "pass", 0, "mkvmkernel: 超えたときに古い出力を残す"),
]


def replace_nth(text, pat, rep, nth):
    idx = -1
    for _ in range(nth + 1):
        idx = text.find(pat, idx + 1)
        if idx < 0:
            return None
    return text[:idx] + rep + text[idx + len(pat):]


def run_mutations(target):
    base = {"mini": MINI_SRC, "lz4c": LZ4C_SRC, "asm": ASM_SRC, "script": SCRIPT}
    bad = 0
    for key, pat, rep, nth, desc in MUTATIONS:
        with tempfile.TemporaryDirectory(prefix="vmk_mut_") as mw:
            mw = pathlib.Path(mw)
            src = base[key]
            text = src.read_text(encoding="utf-8")
            new = replace_nth(text, pat, rep, nth)
            if new is None:
                print("MUTATION 当たらない: {}".format(desc))
                bad += 1
                continue
            mpath = mw / src.name
            mpath.write_text(new, encoding="utf-8")
            srcs = dict(base)
            srcs[key] = mpath
            script = mpath if key == "script" else SCRIPT
            try:
                run_all(srcs, script, target, False)
            except Fail as e:
                print("MUTATION RED (期待どおり): {} -- {}".format(
                    desc, str(e).splitlines()[0][:100]))
                continue
            print("MUTATION 生き残り: {}".format(desc))
            bad += 1
    return bad


def main(args):
    target = "--target" in args
    real = "--real" in args
    if real:
        sys.path.insert(0, str(ROOT / 'tools'))
        from check_artifacts import require_fresh
        require_fresh(ROOT, ['build/out/kernel.bin', 'build/out/sqlite.bin'])
    srcs = {"mini": MINI_SRC, "lz4c": LZ4C_SRC, "asm": ASM_SRC}
    try:
        for ln in run_all(srcs, SCRIPT, target, real):
            print(ln)
    except Fail as e:
        print("FAIL:", e)
        return 1
    print("vmkernel_lz4 PASS ({}{})".format("i386-elf" if target else "host gcc -m32",
                                           ", real" if real else ""), flush=True)
    if "--mutate" in args:
        bad = run_mutations(target)
        if bad:
            print("MUTATE FAIL ({} 件)".format(bad))
            return 1
        print("MUTATE PASS ({} 件すべて RED)".format(len(MUTATIONS)))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
