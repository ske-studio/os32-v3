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
# Match a typed signature at file scope. Index external definitions even when
# no contract header declares them. Local definitions shadow external names.
SIGNATURE = re.compile(
    r'^(?P<type>[A-Za-z_]\w*(?:[ \t]+\w+)*[ \t*]+(?:\n[ \t]*)?)'
    r'(?P<name>[A-Za-z_]\w*)\s*\((?:[^();{}]|\([^();{}]*\))*\)\s*(?P<end>[;{])', re.M)
BODY_ROLE = {
    'atapi_read_sectors': ('ブロック', 'ATAPI セクタ読み', 'atapi_read10'),
    'gf_v2_fence_emit': ('描画', 'GPU フェンスの発行', 'gf2_fence_emit'),
    'gf_v2_alloc': ('描画', '表示面用 VRAM の予約', 'gf_vram_reserve'),
    'gf_v2_gart_bind': ('描画', 'GART ページの登録', 'pte_write'),
    'gf_v2_mode_set': ('描画', '表示モードの設定', 'set_mode'),
    'gf4_edid_modes': ('描画', 'EDID 表示モードの解析', 'checksum'),
}


def body_end(code, start):
    depth = 1
    for brace in re.finditer(r'[{}]', code[start:]):
        depth += 1 if brace[0] == '{' else -1
        if depth == 0:
            return start + brace.start()
    return len(code)


def mask(source):
    return LITERALS.sub(lambda m: re.sub(r'[^\n]', ' ', m[0]), source)


def location(code, pos):
    return code.count('\n', 0, pos) + 1


def object_symbols(objdir):
    objects = sorted(objdir.rglob('*.o'))
    if not objects:
        raise ValueError(f'no objects under {objdir}')
    result = defaultdict(list)
    output = subprocess.check_output(['nm', '-A', '-g', '--defined-only', *objects], text=True)
    for line in output.splitlines():
        match = re.fullmatch(r'(.+):[0-9a-fA-F]+ [TW] (\w+)', line)
        if match:
            result[match[2]].append(Path(match[1]).relative_to(objdir).as_posix())
    return result


def inventory(files, symbols=None):
    hal, calls, gf = [], [], []
    decls = defaultdict(list)
    definitions = defaultdict(list)
    locals_by_path = defaultdict(set)
    alternate = defaultdict(list)
    bodies = defaultdict(list)
    for path, code in files.items():
        for macro in re.finditer(r'^\s*#\s*define\s+(\w+)\(', code, re.M):
            alternate[macro[1]].append(f'{path}:{location(code, macro.start(1))}')
        end = 0
        for m in SIGNATURE.finditer(code):
            if m.start() < end:
                continue
            name = m['name']
            if m['end'] == ';':
                decls[name].append(f'{path}:{location(code, m.start("name"))}')
                continue
            end = body_end(code, m.end())
            bodies[path].append((m, end))
            if 'static' in m['type'].split():
                locals_by_path[path].add(name)
                if path.endswith(('.inc', '.h')):
                    alternate[name].append(f'{path}:{location(code, m.start("name"))}')
            else:
                definitions[name].append((path, location(code, m.start("name"))))
    symbols = symbols or {}
    for name, objects in symbols.items():
        for obj in objects:
            stem = obj[:-2]
            if not any(p.rsplit('.', 1)[0] == stem for p, _ in definitions[name]):
                definitions[name].append((obj, 'nm'))
    device_names = {name for name, sites in definitions.items()
                    if any(p.startswith(('drivers/', 'platform/', 'hwdev/', 'kdebug/')) for p, _ in sites)}
    for name in device_names.copy():
        machines = {parts[1] for path, _ in definitions[name]
                    if (parts := path.split('/'))[0] == 'platform' and len(parts) >= 3}
        common_header = any(site.startswith('include/') and site.rsplit(':', 1)[0].endswith('.h')
                            for site in decls.get(name, ()))
        if common_header and len(machines) >= 2:
            device_names.remove(name)
    for path, code in sorted(files.items()):
        found = set()
        individual = None

        def record(domain, role, match, evidence=None):
            if (domain, role) in found:
                return
            found.add((domain, role))
            hal.append((domain, path, location(code, match.start()), role, '実コード',
                        evidence or ' '.join(match[0].split())[:160]))

        for m, end in bodies[path]:
            spec = BODY_ROLE.get(m['name'])
            if spec:
                domain, detail, callee = spec
                signal = re.search(r'\b' + callee + r'\s*\(', code[m.end():end])
                if signal:
                    evidence = f"{m['name']} → {callee} ({path}:{location(code, m.end()+signal.start())})"
                    individual = (detail, evidence)
                    record(domain, 'バックエンド', m, evidence)

        for domain, types in INTERFACES.items():
            for role, expr in [('契約', rf'\bstruct (?:{types})\s*\{{'),
                               ('バックエンド', rf'\b(?:{types})\s+\w+\s*=\s*\{{')]:
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
                if individual:
                    gf.append((path, description, '推測', individual[0], '実コード', individual[1]))
                    continue
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
        for signature, end in bodies[path]:
            for m in re.compile(r'\b(\w+)\s*\(').finditer(code, signature.end(), end):
                name = m[1]
                if name not in device_names or name in locals_by_path[path]:
                    continue
                if code[:m.start()].rstrip().endswith(('->', '.')):
                    continue
                targets = definitions[name]
                owners = ', '.join(f'{p}:{n}' for p, n in targets)
                status = 'P8候補' if name not in alternate and all(p.split('/')[0] not in COMMON for p, _ in targets) else '未確定'
                calls.append((path, location(code, m.start()), name, ', '.join(decls[name]),
                              owners, status, '推測', '直接呼出し式と同名の外部定義' +
                              (' (別のマクロ・局所定義: ' + ', '.join(alternate[name]) + ')' if name in alternate else '') +
                              (' (nm: ' + ', '.join(symbols[name]) + ')' if name in symbols else '')))
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
    parser.add_argument('--objects', type=Path, help='照合するビルドの obj ディレクトリ')
    args = parser.parse_args()
    out = args.output
    paths = subprocess.check_output(['git', 'ls-files', '-z', '--', *OWNERS], cwd=ROOT)
    files = {p: mask((ROOT / p).read_text(errors='replace')) for p in paths.decode().split('\0')
             if p.endswith(('.h', '.c', '.inc'))}
    tables = inventory(files, object_symbols(args.objects) if args.objects else None)
    out.mkdir(parents=True, exist_ok=True)
    for name, columns, rows in zip(('hal', 'p8', 'geforce'), (
            '分野 file 行 役割 判定 根拠 移設判断 判断根拠', 'file 行 関数 宣言 定義 変更 判定 根拠', 'file 群の役割 群の判定 個別の役割 判定 根拠'), tables):
        with (out / f'{name}.tsv').open('w', newline='') as stream:
            writer = csv.writer(stream, delimiter='\t', lineterminator='\n')
            writer.writerow(columns.split())
            writer.writerows(rows)


if __name__ == '__main__':
    main()
