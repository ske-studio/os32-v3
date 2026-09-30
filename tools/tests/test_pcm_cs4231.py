"""CS4231 (MATE-X PCM) 再生ドライバのホスト試験。

記録: tools/tests/pcm_cs4231_tdd.md
票  : docs/tasks/v3/TASK_PCM_CS4231.md §2-3 (純粋関数) / §3 E1

実物の drivers/pcm_cs4231_math.c と drivers/pcm_cs4231.c を**1 行も写さずに**
#include して回す。include/io.h だけ tools/tests/pcm_hostshim/io.h で差し替え、
ポート操作と割り込み禁止を試験側の模型へ回す (b8_hostdrv と同じ作法)。

**NP21/W では踏めない分岐がここの主目的**:
  (a) 1 周回った観測 (同じ半分で p が戻る) — 実時間では作れない
  (b) 補充の余裕 (REFILL_MARGIN) を割った切り替え
  (c) drain の 3 段階と「出た + staged > 0 は完了しない」
  (d) 初期化列 / 停止列 / RS の列の**順序**
  (e) 入口ガードで装置アクセスが **0 回** であること
  (f) close / reclaim が各状態から **1 度だけ** 解放すること
  (g) foreground と tick の割り込み順序 (race_* — 割り込みの台本で交互に進める)

  python3 -B tools/tests/test_pcm_cs4231.py            # ホストで全ケース
  python3 -B tools/tests/test_pcm_cs4231.py --target   # + i386-elf で実物を通す
  python3 -B tools/tests/test_pcm_cs4231.py --mutate   # 否定側 (変異が RED か)
"""
import pathlib
import re
import subprocess
import sys
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[2]
HARNESS = ROOT / "tools/tests/pcm_cs4231_host.c"
SRC = ROOT / "drivers/pcm_cs4231_math.c"
TARGET_SRCS = ["drivers/pcm_cs4231_math.c", "drivers/pcm_cs4231.c"]

CASES = ["cont", "refill", "drain", "drain_short", "start", "rate",
         "close_dl", "vol", "stg", "pack", "pos", "obs", "seq",
         "open", "write", "close", "rs", "guard", "reclaim", "volume", "init",
         "race_reclaim", "race_close_rs", "race_close_restart",
         "race_close_timeout", "race_volume", "clock", "write_contract"]

FLAGS = ["-std=gnu89", "-Wall", "-Wextra", "-Werror",
         "-Wdeclaration-after-statement", "-D__cdecl="]
# pcm_hostshim は**必ず先頭** — include/io.h を差し替えるため。
INC_DIRS = ("tools/tests/pcm_hostshim", "include", "drivers", "kernel",
            "lib", "sdk/include/os32")

# 否定側。実装 (純粋部) を 1 か所だけ壊して RED になることを見る。
MUTATIONS = [
    # --- 列の順序 (票 §2-3 が名指しする 5 つ) ---
    (r"    OP\(PCM_OP_REG, PCM_I_BASE_LO, PCM_BASE_LO_2048\),\n"
     r"    OP\(PCM_OP_REG, PCM_I_BASE_HI, PCM_BASE_HI_2048\)\n\};",
     "    OP(PCM_OP_REG, PCM_I_BASE_HI, PCM_BASE_HI_2048),\n"
     "    OP(PCM_OP_REG, PCM_I_BASE_LO, PCM_BASE_LO_2048)\n};",
     "初期化列で I14 (上位) を I15 (下位) より先に書く "
     "(上位の書きが Current Count をロードするので半端な値が積まれる)"),
    (r"    OP\(PCM_OP_REG, PCM_R0_MCE \| PCM_I_IFACE, PCM_IFACE_CAL1\),",
     "    OP(PCM_OP_REG, PCM_I_IFACE, PCM_IFACE_CAL1),",
     "I9 を MCE 無しで書く (CAL/SDC/PPIO は MCE 中しか書けない — 校正が走らない)"),
    (r"    OP\(PCM_OP_WAIT_INIT, 0, 0\),\n"
     r"    OP\(PCM_OP_REG, PCM_I_MODEID, PCM_MODE2\),\n"
     r"    OP\(PCM_OP_CHK_MODE2, 0, 0\),\n",
     "    OP(PCM_OP_WAIT_INIT, 0, 0),\n"
     "    OP(PCM_OP_REG, PCM_I_ALTSTAT, 0),\n"
     "    OP(PCM_OP_REG, PCM_I_MODEID, PCM_MODE2),\n"
     "    OP(PCM_OP_CHK_MODE2, 0, 0),\n",
     "MODE2 を立てる前に I24 を触る (MODE1 では IA4 が無視されて I8 に化ける)"),
    (r"static const struct pcm_op seq_stop_tail\[\] = \{\n"
     r"    OP\(PCM_OP_WAIT_DRS, 0, 0\),\n",
     "static const struct pcm_op seq_stop_tail[] = {\n",
     "DRS を待たずに mask する (発行済みの DMA 要求がサンプルの途中で切れる)"),
    (r"static const struct pcm_op seq_stop_tail\[\] = \{\n",
     "static const struct pcm_op seq_stop_tail[] = {\n"
     "    OP(PCM_OP_DEADLINE, 0, PCM_STOP_TICKS),\n",
     "期限を毎 tick 入れ直す (待ちがいつまでも切れない)"),
    # --- 連続性と補充 ---
    (r"    if \(p1 >= p0\) return PCM_CONT_SAME;\n"
     r"    return PCM_CONT_LOST;",
     "    return PCM_CONT_SAME;",
     "同じ半分で戻った観測 (1 周 = 2 境界) を「境界なし」と読む"),
    (r"    if \(h0 != h1\) return PCM_CONT_SWITCH;",
     "    if (h0 != h1) return (p1 >= p0) ? PCM_CONT_SWITCH : PCM_CONT_LOST;",
     "1 → 0 の折り返し (p が戻って見える) を喪失と読む"),
    (r"    return \(end - p1\) >= PCM_REFILL_MARGIN;",
     "    return 1;",
     "補充の余裕を見ない (書いている最中に装置が追い越して古い音が出る)"),
    # --- 切り替え・underrun・drain ---
    (r"        changed = \(h1 != \(u32\)c->half\);\n"
     r"        old = c->half;\n"
     r"        progressed = \(changed \|\| p1 != c->pos\);\n"
     r"        c->half = \(u8\)h1;\n"
     r"        c->pos = p1;",
     "        c->half = (u8)h1;\n"
     "        c->pos = p1;\n"
     "        changed = (h1 != (u32)c->half);\n"
     "        old = c->half;\n"
     "        progressed = (changed || p1 != c->pos);",
     "changed / old / progressed を更新**後**の位置で決める (境界を取り逃がす)"),
    (r"            if \(c->state == PCM_ST_RUNNING &&\n"
     r"                c->filled\[h1\] < PCM_HALF_FRAMES\) \{\n"
     r"                c->underruns\+\+;\n"
     r"            \}\n", "",
     "underrun を数えない (無音が出ているのに status が 0 のまま)"),
    (r"            if \(act->frames > 0U\) \{\n"
     r"                c->last_data_half = \(u8\)old;\n"
     r"                c->drain = PCM_DRAIN_WAIT;\n"
     r"            \}",
     "            c->last_data_half = (u8)old;\n"
     "            c->drain = PCM_DRAIN_WAIT;",
     "0 frame の補充でも last_data_half を動かす (drain が終わらない)"),
    # `staged == 0` の条件は `frames > 0` の段階戻し (上の 11) と重なって
    # いて単独では観測できない (残りがあれば必ず補充され、段階が WAIT に
    # 戻る)。観測できるのは「完了は DRAINING のときだけ」のほう。
    (r"            if \(c->state == PCM_ST_DRAINING &&\n"
     r"                c->drain == PCM_DRAIN_OUT && c->stg.staged == 0U\) \{",
     "            if (c->drain == PCM_DRAIN_OUT && c->stg.staged == 0U) {",
     "RUNNING でも drain の完了で STOP_REQ へ落ちる (再生の途中で止まる)"),
    (r"    if \(stage == PCM_DRAIN_IN\)\n"
     r"        return \(h1 != last_data_half\) \? PCM_DRAIN_SIL : PCM_DRAIN_IN;",
     "    if (stage == PCM_DRAIN_IN) return PCM_DRAIN_SIL;",
     "「読んでいる」から無条件に進む (データの半分を通り切る前に止める)"),
    # --- 番犬 ---
    (r"    if \(\(u32\)\(now - c->last_progress\) > pcm_watchdog_us\(c->rate\)\) \{\n"
     r"        pcm_fail\(c, act\);\n    \}", "",
     "番犬を持たない (IRQ も tick も来なくなった装置に気付かない)"),
    (r"        if \(progressed\) c->last_progress = now;",
     "        c->last_progress = now;",
     "位置が進まなくても番犬の基準を進める (止まった装置を見逃す)"),
    # --- ステージング ---
    (r"u32 pcm_stg_free\(const struct pcm_stg \*s\)\n\{\n"
     r"    return PCM_STG_FRAMES - s->staged;\n\}",
     "u32 pcm_stg_free(const struct pcm_stg *s)\n{\n"
     "    return (s->w >= s->r) ? (PCM_STG_FRAMES - (s->w - s->r))\n"
     "                          : (s->r - s->w);\n}",
     "空きを w/r の差で出す (4096 消費した直後の空きが 0 に見える)"),
    (r"    head = PCM_STG_FRAMES - off;\n    if \(n < head\) head = n;",
     "    head = n;",
     "物理末尾を跨ぐ写しを 2 分割しない (先頭へ回り込むぶんを踏み外す)"),
    # --- pcm_start の配り方 ---
    (r"    \*drain_stage = \(b > 0U\) \? PCM_DRAIN_WAIT : PCM_DRAIN_IN;",
     "    *drain_stage = PCM_DRAIN_WAIT;",
     "半分 0 だけの短いストリームを「入るのを待つ」から始める (1 周遅れて "
     "close の期限を超える)"),
    # --- レート・期限・音量 ---
    (r'    if \(rate == PCM_RATE_22050\) \{ \*fmt = PCM_FMT_22050; return 0; \}',
     '    if (rate == PCM_RATE_22050) { *fmt = 0x9B; return 0; }',
     "22.05k に予約フォーマット 0x9B を積む (往復 1 B2)"),
    (r"    return \(\(num \+ den - 1U\) / den\) \+ PCM_STOP_TICKS;",
     "    return (num / den) + PCM_STOP_TICKS;",
     "close の期限を切り上げない (最後の半分を通す前に打ち切る)"),
    (r"        \*reg = \(u8\)\(PCM_DA_MUTE \| PCM_DA_ATT_MAX\);",
     "        *reg = (u8)PCM_DA_ATT_MAX;",
     "percent 0 でミュートビットを立てない"),
    (r"    if \(percent > PCM_VOL_MAX\) return OS32_ERR_INVAL;", "",
     "101 以上の percent を受ける (減衰が回り込んで大音量になる)"),
    # --- 位置の合成 ---
    (r"    pos = \(PCM_RING_BYTES - \(bytes_left % PCM_RING_BYTES\)\) % PCM_RING_BYTES;",
     "    pos = bytes_left % PCM_RING_BYTES;",
     "残バイト数をそのまま位置として読む (半分の判定が裏返る)"),
    # --- カウンタ ---
    (r"    if \(resyncs   > PCM_CNT_U16_MAX\) resyncs   = PCM_CNT_U16_MAX;", "",
     "resyncs を飽和させない (65536 回目で underruns の桁を汚す)"),
]

# 否定側 (driver)。foreground と tick の競合の修正 (Codex 実装レビュー
# blocker 1〜5、2026-09-23) を 1 つずつ元に戻し、交互の台本のケースが RED に
# なることを見る。1 本に複数の置き換えを持てる (修正が 2 重の守りのとき)。
DRIVER = "drivers/pcm_cs4231.c"
DRIVER_MUTATIONS = [
    ([(r"    if \(s_claimed \|\| s_mce_busy\) return IRQ_NONE;",
       "    if (s_mce_busy) return IRQ_NONE;"),
      (r"    if \(evid\) g_pcm.state = PCM_ST_STOP_DONE;\n"
       r"    else pcm_enter_faulted\(PCM_DIAG_FAULT_RECLAIM\);\n", "")],
     "reclaim が状態を残したまま IF を戻す (tick が RS_RESTART を実行してから "
     "リングを free する — blocker 1)"),
    ([(r"    f = irq_save\(\);\n    st = \(int\)g_pcm.state;\n"
       r"    idle = \(st == PCM_ST_OPEN && g_pcm.stg.staged == 0\);\n"
       r"    PCM_PREEMPT\(PCM_PP_CLOSE_ENTRY\);\n",
       "    st = (int)g_pcm.state;\n"
       "    idle = (st == PCM_ST_OPEN && g_pcm.stg.staged == 0);\n"
       "    PCM_PREEMPT(PCM_PP_CLOSE_ENTRY);\n"
       "    f = irq_save();\n")],
     "close の入口で状態を禁止区間の外で読む (drain の要求を取り落とす — blocker 2)"),
    ([(r"            g_pcm.state = PCM_ST_STOP_REQ;\n"
       r"            PCM_PREEMPT\(PCM_PP_CLOSE_TIMEOUT\);\n"
       r"            pcm_stop_entry\(\);\n",
       "            g_pcm.state = PCM_ST_STOP_REQ;\n"
       "            irq_restore(f);\n"
       "            PCM_PREEMPT(PCM_PP_CLOSE_TIMEOUT);\n"
       "            f = irq_save();\n"
       "            pcm_stop_entry();\n")],
     "close の期限切れで STOP_REQ を停止の入口より先に公開する "
     "(古い期限で FAULTED — blocker 3)"),
    ([(r"    if \(t < s_last_us\) t = s_last_us;",
       "    if ((u32)((u32)t - (u32)s_last_us) >= 0x80000000U) t = s_last_us;"),
      (r"    s_last_us = pcm_clock_us\(\);\n", "")],
     "時計の巻き戻りを下位 32 ビットの差で推し量り、基準も置き直さない "
     "(起動 35 分後の初回再生で番犬が死ぬ — blocker 4)"),
    ([(r"    cs_write_locked\(PCM_I_LDA, r\);\n"
       r"    cs_write_locked\(PCM_I_RDA, r\);\n    irq_restore\(f\);",
       "    irq_restore(f);\n    cs_write(PCM_I_LDA, r);\n"
       "    cs_write(PCM_I_RDA, r);")],
     "set_volume の判定と I6/I7 の書きを別の禁止区間にする "
     "(FAULTED の後に装置へ書く — blocker 5)"),
    ([(r"    if \(!buf\) return OS32_ERR_INVAL;\n", "")],
     "write が NULL を断らない (CPL=0 の直呼びで NULL から写す)"),
]


def host_build(tmp, source_text=None, driver_text=None):
    src_dir = pathlib.Path(tmp)
    exe = src_dir / "pcm-host"
    inc = ["-I" + str(ROOT / p) for p in INC_DIRS]
    if source_text is None and driver_text is None:
        cmd = ["gcc", *FLAGS, *inc, str(HARNESS), "-o", str(exe)]
    else:
        mut = src_dir / "drivers"
        mut.mkdir(exist_ok=True)
        if source_text is None:
            source_text = SRC.read_text(encoding="utf-8")
        if driver_text is None:
            driver_text = (ROOT / DRIVER).read_text(encoding="utf-8")
        (mut / "pcm_cs4231_math.c").write_text(source_text, encoding="utf-8")
        (mut / "pcm_cs4231.c").write_text(driver_text, encoding="utf-8")
        (mut / "pcm_cs4231.h").write_text(
            (ROOT / "drivers/pcm_cs4231.h").read_text(encoding="utf-8"),
            encoding="utf-8")
        shim = src_dir / "harness.c"
        shim.write_text(
            HARNESS.read_text(encoding="utf-8")
            .replace('"../../drivers/pcm_cs4231_math.c"',
                     '"drivers/pcm_cs4231_math.c"')
            .replace('"../../drivers/pcm_cs4231.c"', '"drivers/pcm_cs4231.c"'),
            encoding="utf-8")
        cmd = ["gcc", *FLAGS, "-I" + str(src_dir), "-I" + str(mut), *inc,
               str(shim), "-o", str(exe)]
    subprocess.run(cmd, cwd=ROOT, check=True)
    return exe


def run_cases(exe, cases, quiet=False):
    failed = 0
    for case in cases:
        rc = subprocess.run(
            [str(exe), case], cwd=ROOT, timeout=60,
            stderr=subprocess.DEVNULL if quiet else None).returncode
        if not quiet:
            print(f"EXIT {case}={rc}", flush=True)
        failed += rc != 0
    if not quiet:
        print(f"SUMMARY {len(cases) - failed}/{len(cases)} PASS", flush=True)
    return failed


def build_target(tmp):
    """カーネルと同じ i386-elf で I/O を出す側 (pcm_cs4231.c) ごと通す。"""
    for rel in TARGET_SRCS:
        cmd = ["i386-elf-gcc", "-std=gnu89", "-m32", "-march=i386",
               "-ffreestanding", "-fno-pie", "-fno-stack-protector", "-nostdlib",
               "-mno-red-zone", "-fcommon", "-fsigned-char", "-fno-short-enums",
               "-Os", "-Wall", "-Werror", "-Wdeclaration-after-statement",
               "-D__KERNEL_BUILD__",
               "-I" + str(ROOT), "-I" + str(ROOT / "include"),
               "-I" + str(ROOT / "arch/x86"), "-I" + str(ROOT / "platform/pc98"),
               "-I" + str(ROOT / "sdk/include/os32"), "-I" + str(ROOT / "kernel"),
               "-I" + str(ROOT / "drivers"), "-I" + str(ROOT / "lib"),
               "-c", str(ROOT / rel),
               "-o", str(pathlib.Path(tmp) / (rel.replace("/", "_") + ".o"))]
        subprocess.run(cmd, cwd=ROOT, check=True)
    print("TARGET i386-elf GNU89 -Werror PASS", flush=True)


# 変異 1 本の 1 ケースにかける時間の上限。正常なケースは 1 本 0.01 秒未満
# (2026-09-26 に 28 ケースを実測、全部 0.00 秒)。変異の中には待ちが切れなく
# なるもの (期限の入れ直しなど) があり、上限 60 秒のころは 4 本がそれを満了して
# 変異試験だけで 241 秒かかっていた。3 秒は並列段の負荷を見込んでも数百倍の余裕。
# 見逃し (GREEN) の調査は MUTANT_CASE_TIMEOUT を大きくして --mutate を手で回す。
MUTANT_CASE_TIMEOUT = 3


def run_mutant(exe):
    """変異 1 本を回す。**最初に落ちたケースで打ち切る** — RED は 1 件で決まる。"""
    for c in CASES:
        try:
            rc = subprocess.run([str(exe), c], cwd=ROOT,
                                timeout=MUTANT_CASE_TIMEOUT,
                                stdout=subprocess.DEVNULL,
                                stderr=subprocess.DEVNULL).returncode
        except subprocess.TimeoutExpired:
            return f"{c} (時間切れ {MUTANT_CASE_TIMEOUT} 秒)"  # 止まらなくなるのも RED
        if rc != 0:
            return c
    return None


def mutate(tmp):
    original = SRC.read_text(encoding="utf-8")
    driver = (ROOT / DRIVER).read_text(encoding="utf-8")
    jobs = []
    for pattern, repl, why in MUTATIONS:
        mutated, n = re.subn(pattern, repl, original, count=1)
        jobs.append((n == 1, mutated, None, why))
    for edits, why in DRIVER_MUTATIONS:
        mutated, ok = driver, True
        for pattern, repl in edits:
            mutated, n = re.subn(pattern, repl, mutated, count=1)
            ok = ok and n == 1
        jobs.append((ok, None, mutated, why))
    bad = 0
    for i, (ok, math_text, drv_text, why) in enumerate(jobs, 1):
        if not ok:
            print(f"MUTATION {i} NOT APPLICABLE: {why}", flush=True)
            bad += 1
            continue
        try:
            exe = host_build(tmp, math_text, drv_text)
        except subprocess.CalledProcessError:
            print(f"MUTATION {i} RED (compile): {why}", flush=True)
            continue
        hit = run_mutant(exe)
        status = f"RED ({hit})" if hit else "**GREEN (見逃し)**"
        print(f"MUTATION {i} {status}: {why}", flush=True)
        bad += not hit
    return bad


if __name__ == "__main__":
    args = sys.argv[1:]
    with tempfile.TemporaryDirectory(prefix="os32-pcm-") as tmp:
        exe = host_build(tmp)
        print("HOST GNU89 -Werror compile PASS "
              "(real drivers/pcm_cs4231{,_math}.c)", flush=True)
        if "--target" in args:
            build_target(tmp)
        rc = run_cases(exe, [a for a in args if not a.startswith("--")] or CASES)
        if "--mutate" in args:
            rc += mutate(tmp)
        sys.exit(bool(rc))
