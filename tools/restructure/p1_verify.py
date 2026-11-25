"""Verify P1 ledgers against the design baseline (P1.md, 母数)."""
import argparse
import ast
import csv
import io
import re
import subprocess
from collections import Counter
from pathlib import Path

import hal_inventory

ROOT = Path(__file__).resolve().parents[2]
BASE = 'c6076a6f6'
LAYER_BLOB = '{revision}:tools/check_layers.py'
SCOPE = ROOT / 'docs/tasks/restructure/data/scope.tsv'
HEADER = ['パス', '担当', '種類', '根拠', '外の呼び出し元', '所属種類']
EXCLUDED = ['# 基点の棚卸し用ファイル（母数外）']
KINDS = {
    'P1-1': {'製品', 'ビルドの定義', '公開物', '資産', '運用の道具', '第三者の物'},
    'P1-4': {'実行の基盤', '検査の本体', '試験の支援'},
    '文書': {'文書'},
}


# Reviewed tools whose primary output is a verdict (P1-0 scope review).
CHECK_BODIES = {
    'build_compare.py', 'doc_graph.py',
    'gfinit_rom_check.py', 'memmap_audit_live.py', 'fep_budget_capture.py',
    'ci/sep_identity.py', 'deploy_source_check.py',
}


# Include declarations of test inputs as well as direct invocations. A covered
# generator is a review candidate even when generation is its only purpose.
MARKERS = {
    '合否語': r'\b(?:PASS|FAIL|GREEN|RED)\b',
    '異常終了': r'\b(?:return|exit)\s+[-]?[1-9]\d*|\b(?:SystemExit|exit)\(\s*[1-9]|\belse [1-9]\b',
    '期待値・試験入力': r'(?i)\b(?:expect(?:ed)?(?:_\w+)?|oracle|mismatch|assert|fixture|observation assets)\b|期待値',
}
REVIEW_MARK = '候補印='


def review_inputs(base, paths, tools):
    """Read only the reviewed revision, including callers since removed or moved."""
    callers = {p for p in paths if p.startswith(('tools/checks/', '.github/workflows/'))
               or p == 'Makefile' or (p.startswith('build/') and p.endswith('.mk'))}
    names = sorted(callers | tools)
    request = ''.join(f'{base}:{p}\n' for p in names).encode()
    blobs = io.BytesIO(subprocess.check_output(['git', 'cat-file', '--batch'],
                                               cwd=ROOT, input=request))
    sources = {}
    references = []
    for name in names:
        size = int(blobs.readline().split()[2])
        source = blobs.read(size).decode(errors='replace')
        blobs.read(1)
        if name in tools:
            sources[name] = source
        if name in callers:
            if name == 'Makefile' or name.endswith('.mk'):
                references.extend(re.findall(r'^check-[^\n]*\n(?:\t[^\n]*\n|\n)*',
                                             source, re.M))
            else:
                references.append(source)
    return sources, set(re.findall(r'[\w.-]+', '\n'.join(references)))


def candidate_marks(source, path, callers):
    marks = [name for name, pattern in MARKERS.items() if re.search(pattern, source)]
    if Path(path).name in callers:
        marks.append('登録・check 目標・workflow の参照')
    return '、'.join(marks)


def inventory(base):
    data = subprocess.check_output(['git', 'ls-tree', '-rz', '--name-only', base],
                                   cwd=ROOT).decode()
    paths = set(data.rstrip('\0').split('\0'))
    excluded = {p for p in paths if p.startswith('tools/restructure/')
                or re.fullmatch(r'tools/checks/restructure_p0_\d+\.py', p)}
    return paths - excluded, excluded


def compare(label, actual, expected, issues):
    counts = Counter(actual)
    for path in sorted(expected - counts.keys()):
        issues.append(f'{label} 不足: {path}')
    for path in sorted(counts.keys() - expected):
        issues.append(f'{label} 余分: {path}')
    for path, count in counts.items():
        if count > 1:
            issues.append(f'{label} 重複: {path} ({count})')


OWNERSHIP_KINDS = {
    '共通実装', 'CPU固有実装', '機種固有実装', 'デバイス固有実装', '公開インターフェース',
    '開発・検査用実装', '文書', 'ビルドの定義', '資産', '第三者の物',
}


LAYER_KIND = {'common': '共通実装', 'arch': 'CPU固有実装',
              'platform': '機種固有実装', 'device': 'デバイス固有実装'}


def layer_declarations(source):
    # Read the reviewed revision's literal ownership declarations without
    # importing its scanner (which reads build artifacts when run).
    values = {}
    for node in ast.parse(source).body:
        if isinstance(node, ast.Assign) and isinstance(node.targets[0], ast.Name):
            name = node.targets[0].id
            if name in ('NAMED', 'DEVICE_ROOTS'):
                values[name] = ast.literal_eval(node.value)
    return values['NAMED'], values['DEVICE_ROOTS']


def declared_header_kind(path, named):
    parts = path.split('/')
    if len(parts) == 2 and parts[0] == 'include' and parts[1] in named:
        return LAYER_KIND[named[parts[1]][0]]
    if (len(parts) == 3 and parts[0] in ('arch', 'platform')
            and parts[2].startswith(parts[0] + '_') and parts[2].endswith('.h')):
        return LAYER_KIND[parts[0]]
    return None


def placement_kind(path, kind, devices):
    if kind != '製品':
        return {'公開物': '公開インターフェース', '運用の道具': '開発・検査用実装',
                **dict.fromkeys(KINDS['P1-4'], '開発・検査用実装')}.get(kind, kind)
    if path in ('apps', 'game'):
        return '第三者の物'
    if path.startswith('boot/'):
        return '機種固有実装'
    parts = path.split('/')
    if len(parts) > 2 and parts[0] in ('arch', 'platform'):
        return LAYER_KIND[parts[0]]
    if parts[0] in devices:
        return LAYER_KIND['device']
    return LAYER_KIND['common']


def build_kinds(definitions):
    """Literal members of ownership sets. Feature selection alone is not ownership."""
    result = {}
    for body in definitions.values():
        for line in body.replace('\\\n', ' ').splitlines():
            match = re.match(r'(?:C|ASM|GAS)_(COMMON|ARCH|PLATFORM)\w*\s*[:+]?=\s*(.*)', line)
            if match:
                for member in match[2].split('#')[0].split():
                    if re.fullmatch(r'[\w./-]+\.(?:c|S|asm)', member):
                        result.setdefault(member, set()).add(LAYER_KIND[match[1].lower()])
    return result


def verify_scope(table, base, list_candidates=False):
    expected, omitted = inventory(base)
    manifests = {p for p in expected if p.startswith(('build/arch/', 'build/platform/', 'build/sources/'))
                 and p.endswith('.mk')}
    definitions, _ = review_inputs(base, set(), manifests)
    classifier = subprocess.check_output(['git', 'show', LAYER_BLOB.format(revision=base)],
                                         cwd=ROOT, text=True)
    named, devices = layer_declarations(classifier)
    members = build_kinds(definitions)
    issues = []
    pending = []
    paths = []
    exclusions = []
    operational = {}
    footer = False
    with table.open(encoding='utf-8', newline='') as stream:
        rows = csv.reader(stream, delimiter='\t', strict=True)
        if next(rows, None) != HEADER:
            issues.append('見出し不一致')
        for row in rows:
            if row == EXCLUDED and not footer:
                footer = True
                continue
            if footer and len(row) == 2:
                if row[1] != '開発・検査用実装':
                    issues.append(f'{row[0]}: 母数外の所属種類が不正')
                exclusions.append(row[0])
                continue
            if footer or len(row) != len(HEADER):
                issues.append(f'{rows.line_num} 行目: 列数または節が不正')
                continue
            path, owner, kind, reason, caller, ownership = row
            if ownership not in OWNERSHIP_KINDS:
                issues.append(f'{path}: 所属種類が空または不正 ({ownership})')
            header_kind = declared_header_kind(path, named)
            if header_kind and ownership != header_kind:
                issues.append(f'{path}: ヘッダの所属は {header_kind} に固定')
            evidence = {header_kind or placement_kind(path, kind, devices)}
            if kind == '製品':
                evidence.update(members.get(path, ()))
            decision = re.search(r'所属判断=([^：]+)：(\S.*)', reason)
            if decision and decision[1] != ownership:
                issues.append(f'{path}: 所属判断と所属種類が不一致')
            if evidence - {ownership}:
                detail = f'{path}: 所属照合 {"/".join(sorted(evidence))} → {ownership}'
                print(detail)
                if not decision or decision[1] != ownership:
                    issues.append(detail + ' の理由がない')
            paths.append(path)
            if any(not value.strip() for value in row[:4]):
                issues.append(f'{path}: 必須欄が空')
            if kind not in KINDS.get(owner, set()):
                issues.append(f'{path}: 担当・種類が不正 ({owner}, {kind})')
            if path.startswith('tools/') and kind not in KINDS['P1-4'] | {'運用の道具'}:
                issues.append(f'{path}: tools の種類が不正')
            if path.removeprefix('tools/') in CHECK_BODIES and path.startswith('tools/') and kind != '検査の本体':
                issues.append(f'{path}: 検査の本体として分類する')
            if path.startswith('tools/') and kind == '運用の道具' and path in expected:
                operational[path] = reason
            if any(tag in reason for tag in ('仮', '保留', '未確定')):
                pending.append((path, reason))
    sources, callers = review_inputs(base, expected, set(operational))
    for path, reason in operational.items():
        marks = candidate_marks(sources[path], path, callers)
        if marks and list_candidates:
            print(f'候補\t{path}\t{marks}')
        if marks and REVIEW_MARK + marks + '。' not in reason:
            issues.append(f'{path}: {REVIEW_MARK}{marks}。用途を確認')
    if not footer:
        issues.append('母数外の節がない')
    compare('担当表', paths, expected, issues)
    compare('母数外', exclusions, omitted, issues)
    for path, reason in pending:
        print(f'A1\t{path}\t{reason}')
    for issue in issues:
        print(issue)
    if issues:
        return 1
    print(f'OK: 対象 {len(paths)}、母数外 {len(exclusions)}、差 0、重複 0、'
          f'必須欄の空 0、A1 確認 {len(pending)}')
    return 0


HAL_MAP = SCOPE.with_name('hal_map.tsv')
HAL_HEADER = ['パス', '役割', '移設', '行き先の候補', '根拠']
HAL_ROLES = {'契約', '型', '共通処理', '振り分け'}


# Inspect identifiers in executable text, including declarations and macro bodies.
BOUNDARY = re.compile(
    r'\b(?:PTE_\w+|(?:\w+_)?cr[034]|cpuid|__asm__|__asm|asm|rdtsc|invlpg|wbinvd|'
    r'(?:in|out)[bwl]|(?:\w+_)?(?:pc98|powermac|lgy98|e82557|pegc|cirrus|trident|'
    r'geforce|cs4231|pcm86)(?:_\w+)?|(?:kbd|gf[234]?|fdc|ide|atapi|kcg|v86s?|'
    r'egc|gdc|serial|ext2_medium_(?:ide|loop))_\w+)\b', re.I)


def boundary_symbol(source):
    match = BOUNDARY.search(hal_inventory.mask(source))
    return match.group() if match else ''


def hal_targets(base):
    tracked, _ = inventory(base)
    inputs = {p for p in tracked if p.split('/')[0] in hal_inventory.OWNERS
              and p.endswith(('.c', '.inc', '.h'))}
    sources, _ = review_inputs(base, set(), inputs)
    hal, calls, geforce = hal_inventory.inventory(
        {p: hal_inventory.mask(body) for p, body in sources.items()})
    return {r[1] for r in hal} | {r[0] for r in calls} | {r[0] for r in geforce}


def hal_table_targets(directory):
    expected = set()
    for name in ('hal.tsv', 'geforce.tsv', 'p8.tsv'):
        with (directory / name).open(encoding='utf-8', newline='') as stream:
            expected.update(row['file'] for row in csv.DictReader(stream, delimiter='\t'))
    return expected


def verify_hal(table, expected, sources):
    issues = []
    paths = []
    destinations = {}
    moves = Counter()
    pending = []
    with table.open(encoding='utf-8', newline='') as stream:
        rows = csv.reader(stream, delimiter='\t', strict=True)
        if next(rows, None) != HAL_HEADER:
            issues.append('HAL 表の見出し不一致')
        for row in rows:
            if len(row) != len(HAL_HEADER):
                issues.append(f'HAL {rows.line_num} 行目: 列数不正')
                continue
            path, role, move, dest, reason = row
            paths.append(path)
            moves[move] += 1
            if move == '移すだけ':
                symbol = boundary_symbol(sources.get(path, ''))
                if symbol and f'境界照合 {symbol}。' not in reason:
                    issues.append(f'{path}: 移すだけの境界根拠がない ({symbol})')
                if dest in destinations:
                    issues.append(f'{path}: 行き先が {destinations[dest]} と衝突 ({dest})')
                destinations[dest] = path
            if any(not cell.strip() for cell in row):
                issues.append(f'{path}: HAL 必須欄が空')
            if move not in ('移すだけ', 'P8', '未確定', '保留'):
                issues.append(f'{path}: 移設判断が不正')
            if role not in HAL_ROLES | {'バックエンド', 'その他', '未確定', '保留'}:
                issues.append(f'{path}: HAL 役割が不正')
            if any(tag in cell for cell in (role, move, dest) for tag in ('未確定', '保留')):
                pending.append((path, reason))
            elif 'sys/hal/' in dest and role not in HAL_ROLES:
                issues.append(f'{path}: HAL に装置固有の実装を配置')
    compare('HAL', paths, expected, issues)
    for path, reason in sorted(pending):
        print(f'A1 HAL\t{path}\t{reason}')
    for issue in issues:
        print(issue)
    if issues:
        return 1
    print(f'OK: HAL {len(paths)}、移すだけ {moves["移すだけ"]}、P8 {moves["P8"]}、'
          f'差 0、重複 0、必須欄の空 0、未確定 {len(pending)}')
    return 0


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--base', default=BASE)
    parser.add_argument('table', nargs='?', type=Path, default=SCOPE)
    parser.add_argument('--list-candidates', action='store_true',
                        help='list every operational tool with review markers')
    parser.add_argument('--hal-map', type=Path, default=HAL_MAP)
    parser.add_argument('--p0-dir', type=Path, help='P0-6 の元の3表と直接照合する')
    args = parser.parse_args()
    scope_status = verify_scope(args.table, args.base, args.list_candidates)
    expected = hal_table_targets(args.p0_dir) if args.p0_dir else hal_targets(args.base)
    sources, _ = review_inputs(args.base, set(), expected)
    return verify_hal(args.hal_map, expected, sources) or scope_status


if __name__ == '__main__':
    raise SystemExit(main())
