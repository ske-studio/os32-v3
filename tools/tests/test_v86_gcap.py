"""`v86 -g` の記録器と引数の判定 (kernel/v86_gcap_math.c) のホスト試験。

記録: tools/tests/v86_gcap_tdd.md
票: docs/tasks/realhw/TASK_PEGC480_REALHW.md §3 段 1 (実機の ROM の INT 18h
    AH=31h/30h を V86 で呼び、その間の OUT を記録する)

実物の kernel/v86_gcap_math.c を 1 行も写さずに #include して回す。ポートにも
V86 にも触らない部分 (記録の列・溢れ・IN の表・幅で ops の口を選ぶこと・
打ち切る命令・通すポート・AH=31h の値から AH=30h の引数を決めること) を見る。

**ここで見る分岐は NP21/W でも実機でも狙って踏めない**:
  - AH=31h の bit の並びの判定 — NP21/W の ROM は bit2 並びしか返さず、
    実機がどちらかは次の実機の回まで分からない。両方の並び・どちらでもない値・
    印のまま (未対応) を表で固定する。
  - 溢れ — 実機の ROM が 512 件を超えるかは分からない。

ゲストのバイト列 (kernel/v86_gcap.c の v86_gcap_code[]) が正本の
kernel/v86_test16_gcap.asm を組んだものと同じかも見る (nasm が無ければ SKIP)。
自己試験 (`v86 -g -t`) はそのゲストで記録器をゲスト内から確かめる。

  python3 -B tools/tests/test_v86_gcap.py            # ホストで全ケース
  python3 -B tools/tests/test_v86_gcap.py --target   # + i386-elf で v86_gcap*.c を通す
  python3 -B tools/tests/test_v86_gcap.py --mutate   # 否定側 (変異が RED になるか)
"""
import pathlib
import re
import shutil
import subprocess
import sys
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[2]
HARNESS = ROOT / "tools/tests/v86_gcap_host.c"
SRC = ROOT / "kernel/v86_gcap_math.c"
ASM = ROOT / "kernel/v86_test16_gcap.asm"
GCAP_C = ROOT / "kernel/v86_gcap.c"

TARGET_SRCS = ["kernel/v86_gcap_math.c", "kernel/v86_gcap.c"]

CASES = ["port_list", "insn", "record_order", "overflow", "in_table",
         "pass_ops", "decide", "mode_31k", "restore"]

# -Wno-attributes: __cdecl は x86-64 のホストでは無視される (警告だけ)。
FLAGS = ["-std=gnu89", "-Wall", "-Wextra", "-Werror", "-Wno-attributes",
         "-Wdeclaration-after-statement"]
INCLUDES = ["-I" + str(ROOT / p) for p in ("sdk/include/os32", "kernel")]

# 否定側。実装を 1 か所だけ壊して RED になることを見る。
MUTATIONS = [
    (r"    if \(!set_ran\) return 1;\n    return \(\(set_ah & 0xFFU\) == 0x05U\) \? 1 : 0;",
     "    (void)set_ran; (void)set_ah;\n    return 1;",
     "③ が断られても (無変化) ④ を呼ぶ — ④ も断られると OS32 の表で同期を書き換え、"
     "並びの誤判定と重なると 24kHz 機に 31kHz を入れる (代行レビュー P2-2)"),
    (r"    if \(!set_ran\) return 1;\n",
     "    (void)set_ran;\n",
     "③ が途中で終わったとき ④ を呼ばない (何を変えたか分からないまま放置)"),
    (r"    if \(rst_ran && \(rst_ah & 0xFFU\) == 0x05U\) return V86G_RST_ROM;",
     "    if (rst_ran || (rst_ah & 0xFFU) == 0x05U) return V86G_RST_ROM;",
     "④ が途中で終わっても ROM で戻ったとみなす (FALLBACK を落とす)"),
    (r"    return \(\(set_ah & 0xFFU\) == 0x05U\) \? 1 : 0;",
     "    return (set_ah == 0x05U) ? 1 : 0;",
     "③ の戻り AH の上位を見てしまう (AX を渡す呼び手で 05h を取りこぼす)"),
    (r"if \(port == 0x09A8U \|\| port == 0x09A0U\)",
     "if (port == 0x09A0U)",
     "09A8h (水平周波数) を通さない — 票の表の筆頭。捕まえて捨てると ROM の "
     "31kHz への切り替えが記録にも実機にも残らない"),
    (r"port <= 0x007AU && \(port & 1U\) == 0\)",
     "port <= 0x007AU)",
     "偶数だけに絞らない (奇数の 71h〜77h は PIT — OS32 の 100Hz タイマを "
     "ROM に触らせる)"),
    (r"if \(!opsize16 && \(opcode == 0xE5U",
     "if (opsize16 && (opcode == 0xE5U",
     "66h の向きを取り違える (16 ビットの IN/OUT を打ち切り、32 ビットを "
     "幅 2 で通す)"),
    (r"opcode <= 0x6FU\)",
     "opcode < 0x6FU)",
     "OUTSW (6Fh) を打ち切らない (#GP ハンドラの未対応命令として黙って畳む)"),
    (r"    if \(g->n_out >= V86G_OUT_MAX\) \{",
     "    if (g->n_out > V86G_OUT_MAX) {",
     "溢れの境界が 1 つずれる (513 件目を配列の外へ書く)"),
    (r"    g->in_total\+\+;\n    g->seq_next\+\+;\n",
     "    g->in_total++;\n",
     "seq を IN で進めない (OUT の seq の飛びで「その間の IN の数」が読めない)"),
    (r"return \(size == 2\) \? 0xFFFFU : 0xFFU;",
     "return (size == 2) ? 0xFFFFU : 0xFFFFU;",
     "幅 1 の値を 8 ビットに切らない (AX の上位を OUT の値として記録する)"),
    (r"    if \(size == 2\) \{\n        ops->out16\(",
     "    if (0) {\n        ops->out16(",
     "16 ビットの OUT を 8 ビットの口で通す (既存の中継 v86_io.c と同じ誤り)"),
    (r"    if \(size == 2\) \{\n        v = ops->in16\(",
     "    if (0) {\n        v = ops->in16(",
     "16 ビットの IN を 8 ビットの口で読む"),
    (r"    \(void\)v86g_note_out\(g, port, size, value, cs, ip, 1, phase\);\n",
     "    if (v86g_note_out(g, port, size, value, cs, ip, 1, phase) != 0) return;\n",
     "溢れたら実機へ通すのをやめる (ROM のモード切り替えが途中で崩れ、戻しの "
     "AH=30h の前提が壊れる)"),
    (r"\*al480 = 0x08U \| 0x04U \| \(al & V86G_AL_RASTER\);",
     "*al480 = 0x08U | (al & V86G_AL_RASTER);",
     "bit2 並びの 480 で 31kHz の bit を立てない (NP21/W は 640x480 を断る)"),
    (r"    if \(\(al & ~\(0x04U \| V86G_AL_RASTER\) & 0xFFU\) != 0x08U\) return 0;",
     "    if ((al & 0xFBU) != 0x08U) return 0;",
     "bit2 並びで AL bit0 (ラスタスキャン) を弾く — 実機 Ra266 の AX=310Dh が "
     "V86G_ST_UNDECIDED になり 30h へ進まない (2026-09-29 の実機の v86 -g)"),
    (r"    if \(\(al & ~\(0x08U \| V86G_AL_RASTER\) & 0xFFU\) != 0\) return 0;",
     "    if ((al & 0xF7U) != 0) return 0;",
     "bit3 並びで AL bit0 を弾く (資料の bit0 は並びを区別しない)"),
    (r"    if \(\(al & ~\(0x04U \| V86G_AL_RASTER\) & 0xFFU\) != 0x08U\) return 0;",
     "    if ((al & 0xF9U) != 0x08U) return 0;",
     "bit2 並びで bit0 の代わりに予約の bit1 を許す"),
    (r"        \*al480 = 0x08U \| 0x04U \| \(al & V86G_AL_RASTER\);",
     "        *al480 = 0x08U | 0x04U;",
     "30h へ渡す AL で ROM の bit0 を落とす (ROM の設定したラスタスキャンを "
     "480 へ入るときに勝手に変える)"),
    (r"        \*al480 = 0x08U \| \(al & V86G_AL_RASTER\);",
     "        *al480 = 0x08U;",
     "bit3 並びで 30h へ渡す AL の bit0 を落とす"),
    (r"\*bh480 = \(3U << 4\) \| 2U;",
     "*bh480 = (3U << 4) | 1U;",
     "bit2 並びの 480 を 25 行にする (OS32 の PEGC は 30 行 = "
     "PEGC_GDC_MSYNC_480 と同じ ROM の表)"),
    (r"\*bh480 = \(2U << 3\) \| \(3U << 1\);",
     "*bh480 = (3U << 1);",
     "bit3 並びの 480 の行数を 20 行にする (票の Bible の値 16h と違う)"),
    (r"    if \(rows == 2U && res != 3U\) return 0;      /\* 30 行は 480 だけ \*/\n",
     "",
     "30 行を 480 以外でも受け入れる (400・30 行というありえない組で並びを決める)"),
    (r"    if \(res == 3U && !is31k\) return 0;          /\* 480 は 31kHz だけ \*/\n",
     "    (void)is31k;\n",
     "480 を 24kHz でも受け入れる (ありえない組で並びを決める)"),
    (r"    if \(rows == 3U\) return 0;\n",
     "",
     "行数 3 (未定義) を受け入れる"),
    (r"    return valid_mode\(\(bh >> 1\) & 3U, \(bh >> 3\) & 3U, \(al & 0x08U\) != 0\);",
     "    return valid_mode((bh >> 3) & 3U, (bh >> 1) & 3U, (al & 0x08U) != 0);",
     "bit3 並びの行数と解像度を取り違える (Bible の D4-D3 / D2-D1)"),
    (r"    if \(b2 && !b3\) \{",
     "    if (b2) {",
     "両方の並びで正しい値 (AL=08h BH=00h) で bit2 に決めてしまう (両方の候補を試さない約束を破る)"),
    (r"    if \(al == V86G_SENTINEL_AL && bh == V86G_SENTINEL_BH\) \{\n"
     r"        return V86G_DEC_NO31;\n    \}\n",
     "",
     "印のままを「答えが無い」と言わない (未対応の ROM を「並び不明」に混ぜる)"),
    (r"    if \(layout == V86G_LAYOUT_BIT3\) return \(al & 0x08U\) \? 1 : 0;",
     "    if (layout == V86G_LAYOUT_BIT3) return (al & 0x04U) ? 1 : 0;",
     "bit3 並びの 31kHz を bit2 で読む (ROM で戻れないときに 24kHz へ落とし、"
     "CUI の桁ずれを再発させる)"),
    (r" &&\n            \(\(e->flags & V86G_F_PASSED\) != 0\) == \(passed != 0\)\) \{",
     ") {",
     "実機から読んだ IN と仮想化した IN を同じ行に数える"),
    (r"        g->in_other\+\+;\n        return;",
     "        return;",
     "表に入りきらないポートの IN を数えない"),
]


def host_build(tmp, source_text=None):
    src_dir = pathlib.Path(tmp)
    exe = src_dir / "v86-gcap-host"
    if source_text is None:
        cmd = ["gcc", *FLAGS, *INCLUDES, str(HARNESS), "-o", str(exe)]
    else:
        mut = src_dir / "kernel"
        mut.mkdir(exist_ok=True)
        (mut / "v86_gcap_math.c").write_text(source_text, encoding="utf-8")
        (mut / "v86_gcap_math.h").write_text(
            (ROOT / "kernel/v86_gcap_math.h").read_text(encoding="utf-8"),
            encoding="utf-8")
        shim = src_dir / "harness.c"
        shim.write_text(
            HARNESS.read_text(encoding="utf-8").replace(
                '"../../kernel/v86_gcap_math.c"', '"kernel/v86_gcap_math.c"'),
            encoding="utf-8")
        cmd = ["gcc", *FLAGS, "-I" + str(ROOT / "sdk/include/os32"),
               "-I" + str(src_dir), "-I" + str(mut), str(shim), "-o", str(exe)]
    subprocess.run(cmd, cwd=ROOT, check=True)
    return exe


def run_cases(exe, cases):
    failed = 0
    for case in cases:
        rc = subprocess.run([str(exe), case], cwd=ROOT).returncode
        print(f"EXIT {case}={rc}", flush=True)
        failed += rc != 0
    print(f"SUMMARY {len(cases) - failed}/{len(cases)} PASS", flush=True)
    return failed


def c_bytes():
    text = GCAP_C.read_text(encoding="utf-8")
    m = re.search(r"static const u8 v86_gcap_code\[\] = \{(.*?)\};", text, re.S)
    if not m:
        raise SystemExit("v86_gcap_code[] not found in kernel/v86_gcap.c")
    return bytes(int(x, 16) for x in re.findall(r"0x([0-9A-Fa-f]{2})", m.group(1)))


def check_guest_bytes(tmp):
    """C のバイト列 = 正本の asm を組んだもの。入口と期待 IP が命令を指すこと。"""
    if not shutil.which("nasm"):
        print("GUEST BYTES SKIP (nasm が無い)", flush=True)
        return 0
    out = pathlib.Path(tmp) / "g.bin"
    subprocess.run(["nasm", "-f", "bin", str(ASM), "-o", str(out)], check=True)
    built = out.read_bytes()
    have = c_bytes()
    bad = 0
    if built != have:
        print(f"FAIL guest bytes differ: asm {len(built)} B / C {len(have)} B",
              flush=True)
        bad += 1
    text = GCAP_C.read_text(encoding="utf-8")
    # 入口 (GCAP_ENTRY_*) は各ルーチンの先頭 = mov ax, 8C00h (B8 00 8C)
    for name, off in re.findall(r"#define GCAP_ENTRY_(\w+)\s+0x([0-9A-Fa-f]+)U", text):
        o = int(off, 16)
        if have[o:o + 3] != b"\xB8\x00\x8C":
            print(f"FAIL entry {name} @{o:#x} is not the routine head", flush=True)
            bad += 1
    # 期待する OUT 列の IP は OUT 命令を指す (imm8 形ならポートも一致)
    for port, _w, _p, _v, ip in re.findall(
            r"\{ 0x([0-9A-F]{4}), (\d), (\d), 0x([0-9A-F]{4}), 0x([0-9A-F]{4}) \}",
            text):
        p, i = int(port, 16), int(ip, 16)
        op = have[i]
        if op not in (0xE6, 0xE7, 0xEE, 0xEF) or \
           (op in (0xE6, 0xE7) and have[i + 1] != p):
            print(f"FAIL expect ip {i:#x} is not OUT to {p:#x}", flush=True)
            bad += 1
    m1 = re.search(r"#define GCAP_OUTS_IP\s+0x([0-9A-Fa-f]+)U", text)
    m2 = re.search(r"#define GCAP_IO32_IP\s+0x([0-9A-Fa-f]+)U", text)
    if not m1 or have[int(m1.group(1), 16)] != 0x6E:
        print("FAIL GCAP_OUTS_IP does not point at OUTSB", flush=True)
        bad += 1
    if not m2 or have[int(m2.group(1), 16):int(m2.group(1), 16) + 2] != b"\x66\xEF":
        print("FAIL GCAP_IO32_IP does not point at 66 EF", flush=True)
        bad += 1
    print(f"GUEST BYTES {'PASS' if not bad else 'FAIL'} ({len(have)} B)", flush=True)
    return bad


def build_target(tmp):
    for rel in TARGET_SRCS:
        cmd = ["i386-elf-gcc", "-std=gnu89", "-m32", "-march=i386",
               "-ffreestanding", "-fno-pie", "-fno-stack-protector", "-nostdlib",
               "-mno-red-zone", "-fcommon", "-fsigned-char", "-fno-short-enums",
               "-O2", "-Wall", "-Werror", "-Wdeclaration-after-statement",
               "-D__KERNEL_BUILD__",
               *["-I" + str(ROOT / p) for p in (
                   ".", "include", "arch/x86", "platform/pc98", "sdk/include",
                   "sdk/include/os32", "kernel", "drivers", "net", "fs", "exec",
                   "gfx", "lib", "kapi")],
               "-c", str(ROOT / rel),
               "-o", str(pathlib.Path(tmp) / (rel.replace("/", "_") + ".o"))]
        subprocess.run(cmd, cwd=ROOT, check=True)
    print("TARGET i386-elf GNU89 -Werror PASS", flush=True)


def mutate(tmp):
    original = SRC.read_text(encoding="utf-8")
    bad = 0
    for i, (pattern, repl, why) in enumerate(MUTATIONS, 1):
        mutated, n = re.subn(pattern, repl, original, count=1)
        if n != 1:
            print(f"MUTATION {i} NOT APPLICABLE: {why}", flush=True)
            bad += 1
            continue
        try:
            exe = host_build(tmp, mutated)
        except subprocess.CalledProcessError:
            print(f"MUTATION {i} RED (compile): {why}", flush=True)
            continue
        hits = sum(subprocess.run([str(exe), c], cwd=ROOT,
                                  stdout=subprocess.DEVNULL,
                                  stderr=subprocess.DEVNULL).returncode != 0
                   for c in CASES)
        status = "RED" if hits else "**GREEN (見逃し)**"
        print(f"MUTATION {i} {status} ({hits} 件): {why}", flush=True)
        bad += not hits
    print(f"MUTATIONS {len(MUTATIONS) - bad}/{len(MUTATIONS)} RED", flush=True)
    return bad


if __name__ == "__main__":
    args = sys.argv[1:]
    with tempfile.TemporaryDirectory(prefix="os32-v86-gcap-") as tmp:
        exe = host_build(tmp)
        print("HOST GNU89 -Werror compile PASS (real kernel/v86_gcap_math.c)",
              flush=True)
        rc = check_guest_bytes(tmp)
        if "--target" in args:
            build_target(tmp)
        rc += run_cases(exe, [a for a in args if not a.startswith("--")] or CASES)
        if "--mutate" in args:
            rc += mutate(tmp)
        sys.exit(bool(rc))
