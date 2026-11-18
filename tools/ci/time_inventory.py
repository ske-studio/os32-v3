#!/usr/bin/env python3
"""CI の検査の所要の棚卸し: シャードの偏り・重い検査・重みのずれを、成果物の記録から表にする。

    gh run download <RUN_ID> --pattern 'check-times-*' --dir <DIR>     (build と check の回)
    python3 -B tools/ci/time_inventory.py <DIR>... [--max-imbalance 1.5] [--drift 2.0]

読む物は check_times.tsv (検査・秒・終了コード)。x86 のシャード (`check-times-<sha>-<i>`) と、
PowerPC の QEMU の段 (`check-times-<sha>-ppc-qemu-<i>`) を別の組として数える。
終了コード: 0 = 手当ては要らない、1 = 偏りか重みのずれが閾値を超えた (重みを作り直す)、2 = 記録が無い。
手順は .claude/skills/os32-ci-inventory/SKILL.md、決まりは docs/POLICY_CI.md (G10)。
"""
import argparse
from collections import defaultdict
import json
from pathlib import Path
import statistics
import sys

ROOT = Path(__file__).resolve().parents[2]
QEMU_ARTIFACT = '-ppc-qemu-'
TOP = 15
FLOOR = 30.0          # これより軽い検査は、重みのずれの一覧に出さない (秒)


def group(artifact):
    """成果物の名前 -> 組。PowerPC の QEMU の段は 'qemu-ppc'、ほかは 'x86'。"""
    return 'qemu-ppc' if QEMU_ARTIFACT in artifact else 'x86'


def load(paths):
    """[(成果物の名前, {検査: 秒})]。成功した行だけ (落ちた検査の所要は途中までなので数えない)。"""
    result = []
    for path in sorted(set(paths)):
        rows = {}
        for line in path.read_text().splitlines():
            parts = line.split('\t')
            if len(parts) == 3 and parts[0].startswith('check-') and int(parts[2]) == 0:
                rows[parts[0][6:]] = float(parts[1])
        result.append((path.parent.name, rows))
    return result


def shard_sums(artifacts):
    """{組: [(成果物, 検査の本数, 秒の和)]}。"""
    sums = defaultdict(list)
    for name, rows in artifacts:
        sums[group(name)].append((name, len(rows), sum(rows.values())))
    return dict(sums)


def imbalance(rows):
    """シャードの秒の和の 最大 ÷ 最小。1 本しか無い・0 秒のシャードがある時は None。"""
    sums = [total for _, _, total in rows]
    if len(sums) < 2 or min(sums) <= 0:
        return None
    return max(sums) / min(sums)


def medians(artifacts, envs):
    """{検査: 秒の中央値}。QEMU の検査は QEMU の段の記録だけ、ほかの検査は x86 のシャードの記録だけ。"""
    samples = defaultdict(list)
    for name, rows in artifacts:
        for check, seconds in rows.items():
            if ('qemu-ppc' in envs.get(check, ())) == (group(name) == 'qemu-ppc'):
                samples[check].append(seconds)
    return {check: statistics.median(values) for check, values in samples.items()}


def drift(measured, weights, factor, floor=FLOOR):
    """重みと実測が factor 倍以上ずれた検査 [(検査, 実測, 重み)]、ずれの大きい順。重みの無い検査は重み None。"""
    out = []
    for check, seconds in measured.items():
        weight = weights.get(check)
        if seconds < floor and (weight is None or weight < floor):
            continue
        if weight is None or weight <= 0:
            out.append((check, seconds, None))
        elif seconds >= weight * factor or seconds * factor <= weight:
            out.append((check, seconds, weight))
    return sorted(out, key=lambda row: (-(row[1] / row[2] if row[2] else float('inf')), row[0]))


def render(sums, measured, drifted, max_imbalance, top=TOP):
    """(本文の行, 手当てが要るか)。"""
    lines, action = [], False
    for name in sorted(sums):
        rows = sorted(sums[name])
        ratio = imbalance(rows)
        lines += ['## %s のシャード (%d 個の記録)' % (name, len(rows)), '',
                  '| 記録 | 検査 | 検査の秒の和 | 分 |', '|---|---:|---:|---:|']
        lines += ['| %s | %d | %.0f | %.1f |' % (artifact, count, total, total / 60) for artifact, count, total in rows]
        if ratio is not None:
            over = ratio > max_imbalance
            action = action or over
            lines += ['', '偏り (最大 ÷ 最小): %.2f%s' % (ratio, ' — 閾値 %.2f を超えた' % max_imbalance if over else '')]
        lines.append('')
    heavy = sorted(measured.items(), key=lambda item: (-item[1], item[0]))[:top]
    total = sum(measured.values())
    lines += ['## 重い検査 (中央値、上位 %d 本)' % len(heavy), '', '| 検査 | 秒 | 全体に占める割合 |', '|---|---:|---:|']
    lines += ['| `%s` | %.0f | %.1f%% |' % (check, seconds, 100 * seconds / total if total else 0)
              for check, seconds in heavy]
    lines.append('')
    if drifted:
        action = True
        lines += ['## 重みが実測とずれている検査 (%d 本)' % len(drifted), '',
                  '| 検査 | 実測 (秒) | 重み (秒) |', '|---|---:|---:|']
        lines += ['| `%s` | %.0f | %s |' % (check, seconds, '無い' if weight is None else '%.0f' % weight)
                  for check, seconds, weight in drifted[:top]]
        if len(drifted) > top:
            lines.append('| ほか %d 本 | | |' % (len(drifted) - top))
        lines.append('')
    if action and 'x86' in sums:
        lines += ['注意: x86 のシャードは検査を並列に回すので、記録の秒は並んで回るほかの検査に左右される。',
                  '      作り直した後は CI のジョブの実時間で前後を比べ、縮まなければ戻す (QEMU の段は直列なので秒が重みになる)。', '']
    lines.append('判定: ' + ('重みを作り直す (python3 -B tools/checks/calibrate_weights.py [--merge] <DIR>...)' if action
                           else '手当ては要らない'))
    return lines, action


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    parser.add_argument('artifacts', nargs='+', type=Path)
    parser.add_argument('--max-imbalance', type=float, default=1.5)
    parser.add_argument('--drift', type=float, default=2.0)
    parser.add_argument('--weights', type=Path, default=ROOT / 'tools/checks/weights.json')
    args = parser.parse_args(argv)
    paths = [p for source in args.artifacts for p in
             ([source] if source.is_file() else source.rglob('check_times.tsv'))]
    artifacts = load(paths)
    if not any(rows for _, rows in artifacts):
        print('check_times.tsv の記録が無い')
        return 2
    sys.path.insert(0, str(ROOT / 'tools/checks'))
    import run
    envs = {name: check.env for name, check in run.discover(ROOT).items()}
    weights = json.loads(args.weights.read_text())['seconds'] if args.weights.is_file() else {}
    measured = medians(artifacts, envs)
    lines, action = render(shard_sums(artifacts), measured, drift(measured, weights, args.drift), args.max_imbalance)
    print('\n'.join(lines))
    return 1 if action else 0


if __name__ == '__main__':
    sys.exit(main())
