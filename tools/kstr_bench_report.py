#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
kstr_bench_report.py — kstr_bench の出力を読んで「C 版 / asm 版」の比の表を出す

票: docs/archive/portability/TASK_KSTRING_BENCH.md §2 の 3 (判断の材料)

    python3 tools/kstr_bench_report.py run1.txt [run2.txt run3.txt] [--tsv]
    ゲストの出力をそのまま食わせる (引数が無ければ標準入力)。

**判定はここでは出さない。** 票の目安 (全体 1.5 倍以内なら C 版に一本化する
価値、2 倍を超える関数があれば個別判断) を当てるのに要る数字 —
全体の中央値・全体の最悪・関数ごとの最大 — をそのまま並べるだけで、
「切り替えてよい」とは書かない。決めるのは票を持っている側 (§0)。

読む行 (userland/tests/kstr_bench.c の固定書式):

    KSTR <関数> <asm|c> <長さ> <ずれ> <ティック> <回数>
    KSTR MISMATCH <関数> <長さ> <ずれ>
    KSTR DONE

比の作り方: 回数は asm 版と C 版で違いうる (遅いほうは倍化が早く止まる) ので、
**1 回あたりのティック** (ティック / 回数) に直してから割る。
ティックが 0 の升は分解能不足なので `-` にして**中央値からも外す** —
0 を「速い」と読むと表が嘘になる ([V4])。

同じ条件で 3 回測ったファイルを並べると (受入 K2)、升ごとに中央値を取り、
ばらつき (最大 - 最小) / 最小 も出す。10% を超える升はそこに出るので、
「回数を増やす」判断がその場でできる。
"""

import argparse
import re
import sys

DATA_RE = re.compile(
    r"^KSTR (?P<fn>[a-z0-9_]+) (?P<var>asm|c) (?P<len>\d+) (?P<al>\d+)"
    r" (?P<ticks>\d+) (?P<reps>\d+)$")
MISS_RE = re.compile(r"^KSTR MISMATCH (?P<fn>[a-z0-9_]+) (?P<len>\d+) (?P<al>\d+)$")

ALIGN_LABEL = {0: "4 境界", 1: "1 ずれ"}

# 票 §2 の 3 の目安。**判定には使わず**、どの升がその線を越えているかを
# 印で示すためだけに使う (判断は票を持っている側)。
GUIDE_OVERALL = 1.5
GUIDE_PER_FN = 2.0


def median(values):
    vs = sorted(values)
    n = len(vs)
    if n == 0:
        return None
    if n % 2:
        return vs[n // 2]
    return (vs[n // 2 - 1] + vs[n // 2]) / 2.0


def parse_run(text):
    """1 回ぶんの出力 → ({(fn,var,len,al): (ticks, reps)}, mismatch, 問題)"""
    data = {}
    miss = []
    junk = []
    done = False
    for line in text.splitlines():
        line = line.rstrip("\r")
        if not line.strip():
            continue
        if line == "KSTR DONE":
            done = True
            continue
        m = MISS_RE.match(line)
        if m:
            miss.append((m.group("fn"), int(m.group("len")), int(m.group("al"))))
            continue
        m = DATA_RE.match(line)
        if m:
            key = (m.group("fn"), m.group("var"),
                   int(m.group("len")), int(m.group("al")))
            data[key] = (int(m.group("ticks")), int(m.group("reps")))
            continue
        if line.startswith("KSTR"):
            junk.append(line)
    return data, miss, junk, done


def per_call(ticks, reps):
    """1 回あたりのティック。ティック 0 は分解能不足として None。"""
    if reps <= 0 or ticks <= 0:
        return None
    return float(ticks) / float(reps)


def collect(runs):
    """升ごとに各回の「1 回あたり」を集める。{key: [v, ...]}"""
    cells = {}
    for data in runs:
        for key, (ticks, reps) in data.items():
            v = per_call(ticks, reps)
            cells.setdefault(key, []).append(v)
    return cells


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("files", nargs="*", help="kstr_bench の出力 (省略で標準入力)")
    ap.add_argument("--tsv", action="store_true", help="表を TSV で出す")
    args = ap.parse_args()

    texts = []
    if args.files:
        for path in args.files:
            with open(path, encoding="utf-8", errors="replace") as f:
                texts.append((path, f.read()))
    else:
        texts.append(("<stdin>", sys.stdin.read()))

    runs = []
    all_miss = []
    problems = []
    for path, text in texts:
        data, miss, junk, done = parse_run(text)
        runs.append(data)
        for m in miss:
            all_miss.append((path,) + m)
        if junk:
            problems.append("%s: 読めなかった KSTR 行 %d 本 (先頭: %s)"
                            % (path, len(junk), junk[0]))
        if not done:
            problems.append("%s: KSTR DONE が無い — 途中で落ちた出力かもしれない"
                            % path)
        if not data:
            problems.append("%s: 計測行が 1 行も無い" % path)

    cells = collect(runs)
    fns = sorted(set(k[0] for k in cells))
    lens = sorted(set(k[2] for k in cells))
    aligns = sorted(set(k[3] for k in cells))

    if not fns:
        print("計測行が無い。入力を間違えていないか確認する。")
        return 2

    # ---- 比の表 ----------------------------------------------------------
    ratios = {}        # (fn, len, al) -> 比 (C / asm)
    skipped = []       # 分解能不足などで比が作れなかった升
    spreads = {}       # (fn, var, len, al) -> ばらつき (最大-最小)/最小

    for fn in fns:
        for ln in lens:
            for al in aligns:
                med = {}
                for var in ("asm", "c"):
                    vals = [v for v in cells.get((fn, var, ln, al), []) if v]
                    n_all = len(cells.get((fn, var, ln, al), []))
                    if len(vals) != n_all or not vals:
                        med[var] = None
                        continue
                    med[var] = median(vals)
                    if len(vals) > 1 and min(vals) > 0:
                        spreads[(fn, var, ln, al)] = (max(vals) - min(vals)) / min(vals)
                if med.get("asm") and med.get("c"):
                    ratios[(fn, ln, al)] = med["c"] / med["asm"]
                elif (fn, "asm", ln, al) in cells or (fn, "c", ln, al) in cells:
                    skipped.append((fn, ln, al))

    def cell(fn, ln, al):
        r = ratios.get((fn, ln, al))
        return "-" if r is None else "%.2f" % r

    if args.tsv:
        print("\t".join(["function", "align"] + [str(x) for x in lens]))
        for fn in fns:
            for al in aligns:
                print("\t".join([fn, str(al)] + [cell(fn, ln, al) for ln in lens]))
    else:
        width = max(9, max(len(f) for f in fns) + 1)
        for al in aligns:
            print("")
            print("== C 版 / asm 版 の比  ずれ=%d (%s) ==（1.00 = 同じ速さ、"
                  "大きいほど C 版が遅い）"
                  % (al, ALIGN_LABEL.get(al, "?")))
            print("%-*s %s" % (width, "関数",
                              " ".join("%8s" % ln for ln in lens)))
            for fn in fns:
                print("%-*s %s" % (width, fn,
                                   " ".join("%8s" % cell(fn, ln, al) for ln in lens)))

    # ---- まとめ ----------------------------------------------------------
    vals = sorted(ratios.values())
    print("")
    print("== まとめ ==")
    print("升の数        %d (比が作れた升) / %d (計測した升)"
          % (len(vals), len(vals) + len(skipped)))
    if vals:
        print("全体の中央値  %.2f   (票の目安 %.1f)" % (median(vals), GUIDE_OVERALL))
        print("全体の最悪    %.2f" % vals[-1])
        print("全体の最良    %.2f" % vals[0])
    print("")
    print("関数ごとの最大比 (票の目安 %.1f):" % GUIDE_PER_FN)
    for fn in fns:
        mine = [r for (f, _l, _a), r in ratios.items() if f == fn]
        if not mine:
            print("  %-10s -  (比が作れた升なし)" % fn)
            continue
        mark = "  <- 目安 %.1f 超" % GUIDE_PER_FN if max(mine) > GUIDE_PER_FN else ""
        print("  %-10s 最大 %.2f  中央 %.2f%s"
              % (fn, max(mine), median(mine), mark))

    # ---- 測れなかったもの ([V4] 抜けは抜けと書く) -------------------------
    if skipped:
        print("")
        print("比が作れなかった升 %d (ティックが 0 = 分解能不足、または片側欠け):"
              % len(skipped))
        for fn, ln, al in skipped[:20]:
            print("  %s len=%d ずれ=%d" % (fn, ln, al))
        if len(skipped) > 20:
            print("  ... 他 %d 升" % (len(skipped) - 20))

    if all_miss:
        print("")
        print("MISMATCH (両版の結果が違った = その関数は測っていない):")
        for path, fn, ln, al in all_miss:
            print("  %s: %s len=%d ずれ=%d" % (path, fn, ln, al))

    if len(runs) > 1 and spreads:
        worst = sorted(spreads.items(), key=lambda kv: -kv[1])
        over = [kv for kv in worst if kv[1] > 0.10]
        print("")
        print("同じ升の回ごとのばらつき (最大-最小)/最小 — 受入 K2 は 10% 以内:")
        print("  10%% を超えた升 %d / %d" % (len(over), len(spreads)))
        for (fn, var, ln, al), v in worst[:10]:
            print("  %-10s %-3s len=%-7d ずれ=%d  %.1f%%"
                  % (fn, var, ln, al, v * 100.0))

    if problems:
        print("")
        print("入力の問題:")
        for p in problems:
            print("  " + p)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
