#!/usr/bin/env python3
"""CI と手元で同じ入口: 検査の 1 シャードを最後まで回し、結果を表にする (docs/design/tools/ci/gate.md)。

    python3 -B tools/ci/gate.py run --shard 0/6 --out <置き場> [--no-mutate]

検査の一覧は CHECK の登録 (tools/checks/run.py --list) だけから取る。1 本落ちても残りを回し、
検査ごとのログ (<置き場>/logs/<検査>.log) と結果 (<置き場>/results.json) を残す。画面に出すのは
結果の表と、落ちた検査のログの末尾だけ。回っていない検査 (結果の行が無い) は失敗に数える。
"""
import argparse
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]
TAIL_LINES = 40
SLOWEST = 10
FAILED_LOG_BYTES = 4 << 20
# この検査の束が要る道具。足りない物は全部まとめて挙げ、検査を始める前に止める
# (検査の途中で「コマンドが無い」で落ちて、原因が環境か変更かを読み分けさせない)。
TOOLS = (
    ('i386-elf-gcc', 'CROSS_DIR', 'x86 のクロスコンパイラ (tools/ci/build_cross.sh)'),
    ('powerpc-elf-gcc', 'PPC_CROSS_DIR', 'PowerPC のクロスコンパイラ (tools/ci/build_cross_ppc.sh)'),
    ('nasm', None, 'apt: nasm'),
    ('gcc-13', None, 'apt: gcc-13 (PowerPC のホスト試験が名前で呼ぶ)'),
    ('qemu-i386', None, 'apt: qemu-user'),
    ('qemu-system-ppc', None, 'apt: qemu-system-ppc'),
    ('genisoimage', None, 'apt: genisoimage'),
    ('e2fsck', None, 'apt: e2fsprogs'),
    ('cargo', None, 'Rust (rust-toolchain.toml)'),
)
# 道具は在ってもデータが無いと検査の途中で落ちる物 (QEMU の mac99 の firmware と VGA の ROM)。
FILES = (
    ('/usr/share/qemu/openbios-ppc', 'apt: qemu-system-data (qemu-system-ppc が連れてくる)'),
    ('/usr/share/seabios/vgabios-stdvga.bin', 'apt: seabios (qemu-system が連れてくる)'),
)


def missing_tools(env, which=shutil.which, exists=os.path.isfile):
    """[(道具かファイル, 入れ方)]。クロスコンパイラは <変数>/bin の下、ほかの道具は PATH。"""
    missing = []
    for name, variable, how in TOOLS:
        if variable:
            base = env.get(variable)
            found = bool(base) and os.access(os.path.join(base, 'bin', name), os.X_OK)
        else:
            found = which(name) is not None
        if not found:
            missing.append((name, how))
    missing += [(path, how) for path, how in FILES if not exists(path)]
    return missing


def parse_times(text):
    """check_times.tsv -> {検査: (秒, 終了コード)}。同じ検査が 2 回出たら後の行。"""
    rows = {}
    for line in text.splitlines():
        parts = line.split('\t')
        if len(parts) == 3:
            rows[parts[0]] = (float(parts[1]), int(parts[2]))
    return rows


def skip_lines(text):
    """ログのうち SKIP で始まる行 (環境が足りずに検査を飛ばした印の候補)。"""
    return [line for line in text.splitlines() if re.match(r'SKIP\b', line)]


def classify(expected, rows, logs):
    """結果の表。logs は {検査: ログの本文}。"""
    checks = []
    for name in expected:
        if name not in rows:
            checks.append(dict(name=name, status='missing', seconds=0.0, rc=None, skips=0))
            continue
        seconds, rc = rows[name]
        checks.append(dict(name=name, status='pass' if rc == 0 else 'fail', seconds=seconds, rc=rc,
                           skips=len(skip_lines(logs.get(name, '')))))
    return checks


def render(shard, mutate, checks, logs, tail=TAIL_LINES):
    """(本文の行, 落とすべきか)。本文は端末にも $GITHUB_STEP_SUMMARY にもそのまま出す。"""
    by = {status: [c for c in checks if c['status'] == status] for status in ('pass', 'fail', 'missing')}
    skipped = [c for c in by['pass'] if c['skips']]
    total = sum(c['seconds'] for c in checks)
    lines = ['## 検査 シャード %s (変異%s)' % (shard, 'あり' if mutate else 'なし'), '',
             '| 合計 | 通過 | 失敗 | 回っていない | SKIP の行がある | 検査の時間の和 |',
             '|---:|---:|---:|---:|---:|---:|',
             '| %d | %d | %d | %d | %d | %.0f 秒 |' % (len(checks), len(by['pass']), len(by['fail']),
                                                    len(by['missing']), len(skipped), total), '']
    if by['fail']:
        lines += ['### 失敗', '']
        for check in by['fail']:
            lines += ['- `%s` (rc=%s、%.0f 秒)' % (check['name'], check['rc'], check['seconds'])]
        for check in by['fail']:
            body = logs.get(check['name'], '').splitlines()[-tail:]
            lines += ['', '<details><summary>%s のログの末尾</summary>' % check['name'], '', '```']
            lines += body or ['(ログが無い)']
            lines += ['```', '</details>']
        lines.append('')
    if by['missing']:
        lines += ['### 回っていない (前提の準備が落ちたか、途中で打ち切られた)', '']
        lines += ['- `%s`' % check['name'] for check in by['missing']]
        lines.append('')
    if skipped:
        lines += ['### 通過したが SKIP の行がある (環境が足りずに飛ばしていないか確かめる)', '']
        lines += ['- `%s` (%d 行)' % (check['name'], check['skips']) for check in skipped]
        lines.append('')
    slow = sorted(checks, key=lambda c: -c['seconds'])[:SLOWEST]
    lines += ['### 時間のかかった検査', '', '| 検査 | 秒 |', '|---|---:|']
    lines += ['| `%s` | %.0f |' % (check['name'], check['seconds']) for check in slow if check['seconds']]
    return lines, bool(by['fail'] or by['missing'])


def keep_failed_logs(out, logs_dir, checks, limit=FAILED_LOG_BYTES):
    """落ちた検査のログだけを <置き場>/failed-logs に残す (末尾 limit バイトまで)。持ち出すのはこれだけ。"""
    kept = out / 'failed-logs'
    if kept.exists():
        shutil.rmtree(kept)
    kept.mkdir(parents=True)
    for check in checks:
        source = logs_dir / (check['name'] + '.log')
        if check['status'] == 'fail' and source.is_file():
            data = source.read_bytes()
            (kept / source.name).write_bytes(data[-limit:])


def run(args):
    env = dict(os.environ)
    missing = missing_tools(env)
    if missing:
        print('環境が足りない (検査は始めていない):')
        for name, how in missing:
            print('  %s — %s' % (name, how))
        return 2
    out = args.out.resolve()
    logs_dir = out / 'logs'
    if logs_dir.exists():
        shutil.rmtree(logs_dir)
    logs_dir.mkdir(parents=True)
    for name in ('MAKEFLAGS', 'MFLAGS', 'MAKEOVERRIDES', 'GNUMAKEFLAGS', 'CHECK_ROUTINE'):
        env.pop(name, None)
    if args.no_mutate:
        env['CHECK_ROUTINE'] = '1'  # make check-fast と同じ列 (full_only を除く)
    listed = subprocess.run([sys.executable, '-B', 'tools/checks/run.py', '--list', '--shard', args.shard],
                            cwd=ROOT, env=env, capture_output=True, text=True, check=True)
    expected = listed.stdout.split()
    times = out / 'check_times.tsv'
    env.update(CHECK_LOG_DIR=str(logs_dir), OS32_REQUIRE_PPC='1')
    mutate = '0' if args.no_mutate else '1'
    target = 'check-fast' if args.no_mutate else 'check'
    with open(out / 'make.log', 'wb') as log:
        make = subprocess.run(['make', '-k', '-j' + str(args.jobs), target, 'CHECK_SHARD=' + args.shard,
                               'MUTATE=' + mutate, 'CHECK_TIMES=' + str(times)],
                              cwd=ROOT, env=env, stdout=log, stderr=subprocess.STDOUT)
    rows = parse_times(times.read_text()) if times.is_file() else {}
    logs = {path.stem: path.read_text(errors='replace') for path in sorted(logs_dir.glob('*.log'))}
    checks = classify(expected, rows, logs)
    lines, failed = render(args.shard, not args.no_mutate, checks, logs)
    keep_failed_logs(out, logs_dir, checks)
    if make.returncode and not failed:
        # 検査は全部通ったのに make が落ちた: 準備か、木が変わっていないことの照合。
        lines += ['', '### make が失敗した (rc=%d)、make.log の末尾' % make.returncode, '', '```']
        lines += (out / 'make.log').read_text(errors='replace').splitlines()[-TAIL_LINES:] + ['```']
        failed = True
    (out / 'results.json').write_text(json.dumps(
        dict(shard=args.shard, mutate=not args.no_mutate, make_rc=make.returncode, checks=checks), indent=1))
    text = '\n'.join(lines) + '\n'
    print(text)
    summary = os.environ.get('GITHUB_STEP_SUMMARY')
    if summary:
        with open(summary, 'a', encoding='utf-8') as handle:
            handle.write(text)
    return 1 if failed else 0


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest='command', required=True)
    runner = sub.add_parser('run')
    runner.add_argument('--shard', required=True, help='番号/総数 (例 0/6)')
    runner.add_argument('--out', type=Path, required=True, help='ログと結果の置き場 (作業木の外)')
    runner.add_argument('--no-mutate', action='store_true')
    runner.add_argument('--jobs', type=int, default=4)
    args = parser.parse_args(argv)
    return run(args)


if __name__ == '__main__':
    sys.exit(main())
