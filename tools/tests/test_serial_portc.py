"""drivers/serial.c が 0035h (8255 ポート C) を丸ごと書かないことのホスト試験。

教訓: docs/POLICY_DEBUG.md §4-59 (実機 PC-9821Ra266 で rshell 中にビープが
鳴り続けた、2026-09-24)。資料: docs/hw/undocumented/io_syste.md の I/O 0035h /
0037h (BUZ は bit3 = 1 で停止、BSR 06h = 鳴動 / 07h = 停止)。

実物の drivers/serial.c を #include し、include/io.h の実装側 (arch_io.h /
platform_io.h) だけをここで作る偽物に差し替える。偽物は 8255 のポート C を
模型として持ち、0037h の BSR を 1 ビットずつ適用し、0035h への書き込みを
違反として数える。

**NP21/W の beepc.c も bit3 = 0 で鳴らすが、エミュレータでは鳴らなかった**
(理由は未確認)。だからホストで書き方そのものを押さえる。

  python3 -B tools/tests/test_serial_portc.py            # 全ケース
  python3 -B tools/tests/test_serial_portc.py --mutate   # 否定側 (変異が RED になるか)
"""
import pathlib
import re
import shutil
import subprocess
import sys
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[2]
HARNESS = ROOT / "tools/tests/serial_portc_host.c"
SRCS = {
    "drivers/serial.c": ROOT / "drivers/serial.c",
    "include/pc98.h": ROOT / "include/pc98.h",
    "kernel/sys.c": ROOT / "kernel/sys.c",
}
CASES = ["bsr_polarity", "init_bsr_only", "isr_edge_bsr", "real_hw_story",
         "init_from_all_ones", "isr_drain_limit_edge", "vfast_fifo_path",
         "sys_buz_values"]

# kernel/sys.c は pgalloc や rtc まで引くので丸ごとは載せない。buz_on /
# buz_off の関数本体を**そのまま**切り出して sys_buz.inc にする (写経しない)。
BUZ_RE = re.compile(r"^void buz_on\(void\)\n\{.*?^\}\n\s*^void buz_off\(void\)\n\{.*?^\}\n",
                    re.S | re.M)


def extract_buz(sys_c):
    m = BUZ_RE.search(sys_c)
    if not m:
        raise SystemExit("kernel/sys.c に buz_on / buz_off が見つからない")
    return m.group(0)
FLAGS = ["-std=gnu89", "-Wall", "-Wextra", "-Werror",
         "-Wdeclaration-after-statement", "-D__cdecl=",
         "-Wno-unused-function"]

# include/io.h の契約をホストで満たす偽物。ポート I/O はハーネスの
# fake_inp / fake_outp へ回し、割り込み禁止と hlt は何もしない。
FAKE_ARCH_IO = """#ifndef ARCH_IO_H
#define ARCH_IO_H
static inline void _enable(void) {}
static inline void _disable(void) {}
static inline unsigned int irq_save(void) { return 0; }
static inline void irq_restore(unsigned int flags) { (void)flags; }
static inline int _irq_enabled(void) { return 0; }
static inline void _lidt(void *ptr) { (void)ptr; }
static inline void _halt(void) {}
static inline void _idle(void) {}
static inline void _stop(void) {}
#endif
"""
FAKE_PLATFORM_IO = """#ifndef PLATFORM_IO_H
#define PLATFORM_IO_H
void fake_outp(unsigned port, unsigned val);
unsigned fake_inp(unsigned port);
static inline unsigned int inp(unsigned int port) { return fake_inp(port); }
static inline void outp(unsigned int port, unsigned int value)
{ fake_outp(port, value); }
static inline unsigned int inpw(unsigned int port) { return fake_inp(port); }
static inline void outpw(unsigned int port, unsigned int value)
{ fake_outp(port, value); }
static inline unsigned long inpd(unsigned int port) { return fake_inp(port); }
static inline void outpd(unsigned int port, unsigned long value)
{ fake_outp(port, (unsigned)value); }
static inline void insw_rep(unsigned int port, void *buf, unsigned int count)
{ (void)port; (void)buf; (void)count; }
static inline void io_wait(void) { outp(0x5F, 0); }
static inline void io_wait_n(int n) { while (n-- > 0) io_wait(); }
#endif
"""

# 否定側。実装を 1 か所だけ壊して RED になることを見る。
# (ソース, パターン, 置換, 説明)
MUTATIONS = [
    ("drivers/serial.c",
     r"        u8 cur = \(u8\)\(inp\(SER_MASK\) & \(IEN_RX \| IEN_TXEMP \| IEN_TX\)\);\n"
     r"        ser_ien_bsr\(cur, 0\);\n",
     "        ser_ien_bsr((u8)(IEN_RX | IEN_TXEMP | IEN_TX), 0);\n",
     "初期化で 0 のビットにも落とす BSR を書く (TXRE への 04h が NP21/W で"
     "余分な IRQ4 を起こしうる)"),
    ("kernel/sys.c",
     r"(void buz_off\(void\)\n\{\n    outp\(SYSPORT_C_BSR, )BSR_BUZ_OFF",
     r"\1BSR_BUZ_ON",
     "kernel/sys.c の buz_off() が 06h (鳴動) を書く (rshell が応答のたびに鳴らす)"),
    ("drivers/serial.c",
     r"    ser_ien_bsr\(s_mask_ien, 0\);\n    ser_ien_bsr\(s_mask_ien, s_mask_ien\);\n",
     "    outp(SER_MASK, 0x00);\n    outp(SER_MASK, s_mask_ien);\n",
     "ISR のエッジ作りを直す前の 0035h 全体書き (0x00 → 許可) に戻す"
     " — 受信のたびに BUZ = 0 = 鳴動"),
    ("drivers/serial.c",
     r"        u8 cur = \(u8\)\(inp\(SER_MASK\) & \(IEN_RX \| IEN_TXEMP \| IEN_TX\)\);\n"
     r"        ser_ien_bsr\(cur, 0\);\n",
     "        outp(SER_MASK, 0x00);\n",
     "初期化の全マスクを 0035h 全体書きに戻す (SHUT0/SHUT1/BUZ も 0 になる)"),
    ("drivers/serial.c",
     r"    ser_ien_bsr\(s_mask_ien, 0\);\n    ser_ien_bsr\(s_mask_ien, s_mask_ien\);\n",
     "    ser_ien_bsr(s_mask_ien, s_mask_ien);\n",
     "ISR で許可を一度落とさない (Bible §2-10 のエッジ作りが消える)"),
    ("drivers/serial.c",
     r"    if \(which & IEN_TX\)\n        outp",
     "    if (1)\n        outp",
     "許可していない TXRE も毎回 BSR で書く (NP21/W では IRQ4 を自作しうる)"),
    ("drivers/serial.c",
     r"    outp\(SYSPORT_C_BSR, BSR_BUZ_OFF\);\n",
     "    outp(SYSPORT_C_BSR, BSR_BUZ_ON);\n",
     "初期化のブザー停止を 06h に戻す (Bible の向き = 実機では鳴動)"),
    ("include/pc98.h",
     r"#define BSR_BUZ_ON          0x06(.*)\n#define BSR_BUZ_OFF         0x07",
     r"#define BSR_BUZ_ON          0x07\1\n#define BSR_BUZ_OFF         0x06",
     "pc98.h の BUZ の名前を Bible の向きに戻す (buz_off() が鳴らす)"),
    ("drivers/serial.c",
     r"(ien & IEN_RX\)    \? )BSR_RXRE_ON : BSR_RXRE_OFF",
     r"\1BSR_TXEE_ON : BSR_RXRE_OFF",
     "RXRE を立てるつもりで TXEE を立てる (BSR の番号の取り違え)"),
]


def host_build(tmp, mutated=None):
    """ハーネスをコンパイルして実行ファイルのパスを返す。

    mutated = {相対パス: 中身}。**実物のソースは 1 バイトも触らず**、写しの
    上で変異させる (make check-par で並列に回せる)。serial.c と pc98.h は
    常に写しを使い、写しのディレクトリを -I の先頭に置く。
    """
    tmp = pathlib.Path(tmp)
    work = tmp / ("mut" if mutated else "real")
    if work.exists():
        shutil.rmtree(work)
    work.mkdir(parents=True)
    (work / "arch_io.h").write_text(FAKE_ARCH_IO, encoding="utf-8")
    (work / "platform_io.h").write_text(FAKE_PLATFORM_IO, encoding="utf-8")
    for rel, path in SRCS.items():
        text = (mutated or {}).get(rel, path.read_text(encoding="utf-8"))
        if rel == "kernel/sys.c":
            (work / "sys_buz.inc").write_text(extract_buz(text),
                                              encoding="utf-8")
            continue
        (work / pathlib.Path(rel).name).write_text(text, encoding="utf-8")
    exe = work / "serial-portc-host"
    cmd = ["gcc", *FLAGS, "-I" + str(work),
           "-I" + str(ROOT / "include"), "-I" + str(ROOT / "drivers"),
           "-I" + str(ROOT / "sdk/include/os32"),
           str(HARNESS), "-o", str(exe)]
    subprocess.run(cmd, cwd=ROOT, check=True,
                   stderr=subprocess.DEVNULL if mutated else None)
    return exe


def run_cases(exe, cases):
    failed = 0
    for case in cases:
        rc = subprocess.run([str(exe), case], cwd=ROOT).returncode
        print(f"EXIT {case}={rc}", flush=True)
        failed += rc != 0
    print(f"SUMMARY {len(cases) - failed}/{len(cases)} PASS", flush=True)
    return failed


def mutate(tmp):
    """実装を 1 か所ずつ壊して、どれも RED になることを見る。

    **コンパイルが通らない変異は数えない** (RED にも GREEN にも入れない)。
    数えた変異が 1 本でも GREEN なら失敗。数えられた本数が足りなくても失敗。
    """
    bad = 0
    counted = 0
    for i, (rel, pattern, repl, why) in enumerate(MUTATIONS, 1):
        original = SRCS[rel].read_text(encoding="utf-8")
        text, n = re.subn(pattern, repl, original, count=1)
        if n != 1:
            print(f"MUTATION {i} NOT APPLICABLE: {why}", flush=True)
            bad += 1
            continue
        try:
            exe = host_build(tmp, {rel: text})
        except subprocess.CalledProcessError:
            print(f"MUTATION {i} COMPILE FAIL (数えない): {why}", flush=True)
            continue
        counted += 1
        hits = sum(subprocess.run([str(exe), c], cwd=ROOT,
                                  stdout=subprocess.DEVNULL,
                                  stderr=subprocess.DEVNULL).returncode != 0
                   for c in CASES)
        status = "RED" if hits else "**GREEN (見逃し)**"
        print(f"MUTATION {i} {status} ({hits} 件): {why}", flush=True)
        bad += not hits
    print(f"MUTATION SUMMARY counted {counted}/{len(MUTATIONS)}", flush=True)
    if counted < len(MUTATIONS) - 1:
        print("MUTATION: 数えられた変異が少なすぎる", flush=True)
        bad += 1
    return bad


if __name__ == "__main__":
    args = sys.argv[1:]
    with tempfile.TemporaryDirectory(prefix="os32-serial-portc-") as tmp:
        exe = host_build(tmp)
        print("HOST GNU89 -Werror compile PASS (real drivers/serial.c)",
              flush=True)
        rc = run_cases(exe, [a for a in args if not a.startswith("--")] or CASES)
        if "--mutate" in args:
            rc += mutate(tmp)
        sys.exit(bool(rc))
