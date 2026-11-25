"""P0-6 source inventory. Run from any directory with an output directory argument."""
import argparse
from collections import defaultdict
import csv
from pathlib import Path
import re
import subprocess

ROOT = Path(__file__).resolve().parents[2]
COMMON = 'init kernel mm exec fs gfx net input sound kapi lib include'.split()
OWNERS = COMMON + ['drivers', 'hwdev', 'kdebug', 'platform', 'arch']
# Source-level signatures of current contracts. A file can have several roles.
INTERFACES = {
    '音': 'SndBackend|SndChipBackend',
    '入力': 'InputOps|PointerOps',
    '描画': 'GfxBackend|GfxBackendV2',
    'ブロック': 'ext2_medium_ops',
    'LAN': 'NetNicOps|NetAdapter',
    '時刻': 'ClockOps',
}
# These paths delimit the search. Only an observed expression assigns a role.
RULES = [
    ('音', 'sound/hal/', '振り分け', r'->(?:hw_\w+|caps)\s*\('),
    ('音', 'sound/hal/', '共通処理', r'\bsnd2_(?:stg|seg|stream)_\w+\([^;{}]*\)\s*\{'),
    ('入力', 'kernel/input_service.c', '振り分け', r'->(?:getkey|poll)\('),
    ('入力', 'input/hal/', '共通処理', r'\b(?:in2_ring_push|input_src_register)\([^;{}]*\)\s*\{'),
    ('描画', 'gfx/', '振り分け', r'->(?:enter|present|page_flip)\('),
    ('描画', 'gfx/', '共通処理', r'\b(?:gfx_dirty_\w+|gfx_reinit_surfaces)\([^;{}]*\)\s*\{'),
    ('ブロック', 'kernel/dev/', '契約', r'\(\*blk_read\)'),
    ('ブロック', 'drivers/base/', '共通処理', r'\bdev_managed_transfer\([^;{}]*\)\s*\{'),
    ('ブロック', 'drivers/base/', '振り分け', r'->blk_(?:read|write)\w*\('),
    ('ブロック', 'drivers/', 'バックエンド', r'\b(?:fdc|ide)_(?:read|write)_sector\([^;{}]*\)\s*\{'),
    ('LAN', 'net/', '共通処理', r'\bnet_rx_demux_budget\([^;{}]*\)\s*\{'),
    ('LAN', 'net/', '振り分け', r'->(?:acquire|tx|rx)\('),
    ('時刻', 'kernel/time/', '共通処理', r'\b(?:time_clamp|sys_time_now)\([^;{}]*\)\s*\{'),
    ('時刻', 'kernel/time/', '振り分け', r'->read_(?:date|legacy)\('),
    ('時刻', 'include/', '契約', r'\bplatform_clock_read_us\([^;{}]*\);'),
    ('時刻', 'platform/', 'バックエンド', r'\bplatform_clock_read_us\([^;{}]*\)\s*\{'),
]
# A path alone establishes scope, never the role of an individual source.
SCOPES = {'音': ('sound/hal/', 'drivers/sound/'), '入力': ('input/hal/', 'drivers/input/'),
          '描画': ('gfx/', 'drivers/video/'), 'LAN': ('net/', 'drivers/net/'),
          'ブロック': ('drivers/base/', 'drivers/block/', 'drivers/ata/'),
          '時刻': ('kernel/time/',)}
GF = [('drivers/video/geforce/', '描画バックエンド・常駐接着'),
      ('hwdev/', 'GPU エンジン・表示制御・サービス'),
      ('kdebug/', '診断・初期化スクリプトの解析と実行')]
# File-level evidence within the three groups. Keep the group inference
# separate when no rule identifies the individual file.
GF_SIGNALS = [
    ('HAL 描画操作表', r'\bGfxBackend(?:V2)?\s+\w+\s*=\s*\{'),
    ('表示面への転送', r'\bint gf_present_rect\s*\('),
    ('常駐側の前景 poll', r'\bvoid geforce_foreground_poll\s*\('),
    ('2D コピー命令生成', r'\bint gf3_copy_emit\s*\('),
    ('表示モードの設定値計算', r'\bvoid gf_mode_\w+\s*\('),
    ('初期化命令のデコード', r'\bcase GF_INIT_\w+'),
    ('復旧セッションの中断確認', r'\brecover_safe_point\s*\('),
]
LITERALS = re.compile(r'''/\*.*?\*/|//[^\n]*|"(?:\\.|[^"\\])*"|'(?:\\.|[^'\\])*' ''', re.S | re.X)
PROTO = re.compile(r'^\s*(?:extern\s+)?(?:unsigned\s+)?(?:struct\s+)?\w+[ \t*]+(\w+)\s*\([^;{}]*\)\s*;', re.M)
DEVICE = re.compile(r'(?:geforce|gf|kcg|fdc|atapi|ide|e82557|lgy98|ne2000|kbd|mouse|pcm|fm|opna|cs4231|serial|v86|pci|dma|loop)_')


def mask(source):
    return LITERALS.sub(lambda m: re.sub(r'[^\n]', ' ', m[0]), source)


def location(code, pos):
    return code.count('\n', 0, pos) + 1


def inventory(files):
    hal, calls, gf = [], [], []
    decls = defaultdict(list)
    for path, code in files.items():
        if path.startswith('include/') and path.endswith('_contract.h'):
            for m in PROTO.finditer(code):
                if DEVICE.match(m[1]):
                    decls[m[1]].append(f'{path}:{location(code, m.start(1))}')
    definitions = defaultdict(list)
    for path, code in files.items():
        for m in re.finditer(r'\b(\w+)\s*\([^();{}]*\)\s*\{', code):
            if m[1] in decls:
                definitions[m[1]].append((path, location(code, m.start())))
    for path, code in sorted(files.items()):
        found = set()

        def record(domain, role, match):
            if (domain, role) in found:
                return
            found.add((domain, role))
            pos = match.start()
            hal.append((domain, path, location(code, pos), role, '実コード',
                        ' '.join(match[0].split())[:160]))

        for domain, types in INTERFACES.items():
            for role, expr in [('契約', rf'\bstruct (?:{types})\s*\{{'),
                               ('バックエンド', rf'\b(?:{types})\s+\w+\s*=\s*\{{')]:
                if role == 'バックエンド' and path.split('/')[0] not in ('drivers', 'platform'):
                    continue
                m = re.search(expr, code)
                if m:
                    record(domain, role, m)
        for domain, prefix, role, pattern in RULES:
            if path.startswith(prefix):
                m = re.search(pattern, code)
                if m:
                    record(domain, role, m)
        for domain, prefixes in SCOPES.items():
            if path.startswith(prefixes) and not any(d == domain for d, _ in found):
                hal.append((domain, path, '', '未確定', '未確定', '対象ディレクトリのみ'))
        for prefix, description in GF:
            if path.startswith(prefix) and (prefix.startswith('drivers/') or 'gf' in path.lower()):
                for detail, pattern in GF_SIGNALS:
                    signal = re.search(pattern, code)
                    if signal:
                        gf.append((path, description, '推測', detail, '実コード',
                                   f'{path}:{location(code, signal.start())} {signal[0]}'))
                        break
                else:
                    gf.append((path, description, '推測', '未確定', '未確定',
                               f'配置 {prefix}（群の役割のみ）'))
        if path.split('/')[0] not in COMMON:
            continue
        for m in re.finditer(r'\b(\w+)\s*\(', code):
            name = m[1]
            if name not in decls:
                continue
            before = code[:m.start()].rsplit('\n', 1)[-1]
            if re.search(r'(?:->|\.)\s*$', before):
                continue
            if re.fullmatch(r'\s*(?:(?!return\b)\w+[ \t*]+)+', before):
                continue  # typed declaration / definition
            targets = definitions[name]
            if targets and all(p.split('/')[0] in COMMON for p, _ in targets):
                continue
            owners = ', '.join(f'{p}:{n}' for p, n in targets)
            calls.append((path, location(code, m.start()), name, ', '.join(decls[name]),
                          owners, 'P8候補' if targets and all(p.split('/')[0] not in COMMON for p, _ in targets) else '未確定', '推測', '契約宣言と同名の直接呼出し式'))
    impacted = {row[0] for row in calls if row[5] == 'P8候補'}
    def disposition(row):
        if row[1] in impacted:
            return 'P8候補', 'p8.tsv の呼出し箇所'
        if row[3] == '契約' and row[1].endswith('.h'):
            return '移動のみ候補', '既存の契約宣言を保持して移設可能（推測）'
        if row[3] == 'バックエンド' and re.search(r'=\s*\{', row[5]) and row[1].startswith(('drivers/', 'platform/')):
            return '移動のみ候補', '装置側に独立した実装表がある（推測）'
        return '未確定', 'ファイル全体の移設可否は未確認'
    return [row + disposition(row) for row in hal], calls, gf


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('output', type=Path)
    out = parser.parse_args().output
    paths = subprocess.check_output(['git', 'ls-files', '-z', '--', *OWNERS], cwd=ROOT)
    files = {p: mask((ROOT / p).read_text(errors='replace')) for p in paths.decode().split('\0')
             if p.endswith(('.h', '.c', '.inc'))}
    tables = inventory(files)
    out.mkdir(parents=True, exist_ok=True)
    for name, columns, rows in zip(('hal', 'p8', 'geforce'), (
            '分野 file 行 役割 判定 根拠 移設判断 判断根拠', 'file 行 関数 宣言 定義 変更 判定 根拠', 'file 群の役割 群の判定 個別の役割 判定 根拠'), tables):
        with (out / f'{name}.tsv').open('w', newline='') as stream:
            writer = csv.writer(stream, delimiter='\t', lineterminator='\n')
            writer.writerow(columns.split())
            writer.writerows(rows)


if __name__ == '__main__':
    main()
