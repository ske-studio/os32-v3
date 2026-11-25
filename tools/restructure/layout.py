"""Generate the A1 placement ledger and the SRC_MOVE map."""
import argparse
import csv
import json
from pathlib import Path

from tool_callers import ROOT, callers

DATA = ROOT / 'docs/tasks/restructure/data'
HEADER = ['パス', '行き先', '根拠']
CONTRACTS = {'gfx_present_contract.h': 'gfx', 'mouse_pointer_contract.h': 'input',
             'snd_wake_contract.h': 'sound'}
PPC_DIAGNOSTICS = {'tools/ppc/ata_deny.c', 'tools/ppc/learn_probe.c'}
GEFORCE_RECOVERY = {
    'recover.h': 'NV44A の BAR0 レジスタ配置を定義',
    'recover_common/log.inc': 'gf_bus_record_at と GF_MODE_VRAM_OFFSET で記録',
    'recover_common/s0.inc': 'REC_GF_ENGINE_STOP に従いエンジンを停止',
    'recover_common/session.inc': 'RECOVER_GF_DEVICE と GeForce の BAR・DAC を照合',
    'recover_plan/access.inc': 'recover_gf_runner_access と gf_bus_record を使用',
    'recover_plan/bind.inc': 'GeForce の DAC・AR・VRAM の設定値を検証',
    'recover_plan/step.inc': 'GeForce のゼロ化対象に zero_guards を適用',
}
BUILD_FOLLOWUP = {
    'build/sdk.mk': 'P2-3：CHECK_REGISTRATION が Make の解析時に旧 tools/checks/run.py --make を起動する',
    'build/programs.mk': 'P3：C_TESTS の wildcard userland/tests/*.c と除外・個別規則を userland/diag/ に合わせる',
}
UNUSED = {
    'tools/g16/scan_ui_chars.py': '文字表の手動更新用。ownership.md の再調査でも呼出元なし',
    'tools/gen_kcg_font.py': 'gen_font16.py 冒頭が置き換えを明記。旧形式生成器の起動なし',
    'tools/pack_name_tables.txt': '冒頭で旧 check_select.py --pack 用と宣言。現行の実行入口からの読込なし',
    'tools/pack_notes.txt': '旧 pack の助言表。現行登録の covers にだけ残り、表を読む入口なし',
    'tools/gen_unicode.py': 'unicode.bin は build の C 版 gen_unicode が生成。旧 Python 版の起動なし',
    'tools/np21w_restart.py': '冒頭で非推奨。停止・再起動は np21w_ctl.py に移行済み',
    'tools/split_c_functions.py': '分割時の一回用。現行の工程・手順に呼出元なし',
}
REMNANTS = {'include/' + name for name in
            ('vk32.h',)}


def read_rows(path):
    with path.open(newline='') as stream:
        return list(csv.DictReader(stream, delimiter='\t'))


def placement(path, hal, scope):
    top, _, rest = path.partition('/')
    if path in hal:
        h = hal[path]
        return (h['行き先の候補'] if h['移設'] in ('移すだけ', 'P8') else '保留',
                h['移設'] + '：' + h['根拠'])
    if path in {'include/pc98.h', 'include/pegc.h', 'include/wab_xe10.h'}:
        return 'sys/platform/pc98/' + rest, 'check_layers.NAMED の PC-98 所有ヘッダ'
    if path == 'include/v86_call.h':
        return 'sys/arch/x86/' + rest, 'check_layers.NAMED の x86 所有ヘッダ'
    if top == 'include' and rest in CONTRACTS:
        return 'sys/hal/' + CONTRACTS[rest] + '/' + rest, 'D-4：hal_layout.md の追加契約ヘッダ'
    if path in UNUSED:
        return '保留', '不要の候補：' + UNUSED[path]
    if top in {'init', 'mm', 'exec'}:
        return 'sys/kern/' + path, 'D-1：初期化・メモリ・実行の責任を下位に残す'
    if top == 'kernel':
        return 'sys/kern/' + rest, 'D-1：カーネル共通処理'
    if top == 'drivers':
        return 'sys/dev/' + rest, '装置クラスごとのドライバ'
    if top == 'hwdev':
        return 'sys/dev/' + path, 'hwdev モジュール固有の実装と生成入力'
    if path in PPC_DIAGNOSTICS:
        return 'userland/diag/' + Path(path).name, 'D-8：' + scope['根拠']
    if top == 'kdebug' and scope['所属種類'] == 'CPU固有実装':
        return 'sys/arch/x86/' + path, 'D-3：' + scope['根拠'].split('所属判断=')[-1]
    if top == 'kdebug' and scope['所属種類'] == '機種固有実装':
        return 'sys/platform/pc98/' + path, 'D-3：' + scope['根拠'].split('所属判断=')[-1]
    if top == 'kdebug' and rest.startswith(('trident_command_', 'tri_command_')):
        return 'sys/dev/video/trident/diag/' + rest, 'D-3：Trident のコマンド診断とその通信形式'
    if path == 'kdebug/kselftest/dma8237.inc':
        return 'sys/dev/dma/' + path, 'D-3：dma8237_ready と DMA チャネルのマスクを検査'
    if path == 'kdebug/probe.c':
        return 'sys/dev/char/' + path, 'D-3：serial_get_setup と serial_gate_active でシリアル診断を制御'
    if top == 'kdebug' and rest in GEFORCE_RECOVERY:
        return 'sys/dev/video/geforce/diag/' + rest, 'D-3：' + GEFORCE_RECOVERY[rest]
    if top == 'kdebug':
        return 'sys/kern/' + path, 'D-3：build/kdebug.mk で組む診断モジュール'
    if top in {'arch', 'platform', 'include', 'gfx', 'sound', 'input', 'fs', 'net', 'kapi'}:
        return 'sys/' + path, '仕様 §3.2：' + top + ' の責任を引き継ぐ'
    if path.startswith(('build/images/', 'build/features/')):
        return 'sys/conf/' + rest, 'D-6：像と機能の選択宣言'
    if path.startswith('lib/kstring') or path.startswith('lib/kprintf'):
        return 'sys/kern/' + rest, 'D-5：カーネル内文字列・診断出力'
    if path.startswith('userland/tests/'):
        return 'userland/diag/' + path.removeprefix('userland/tests/'), 'D-8：ゲストに組む・配備する診断と入力'
    return '=', '現在の配置を維持'


def move_rows(rows):
    return [{'from': row['パス'], 'to': row['行き先']} for row in rows
            if row['行き先'] not in ('=', '保留')]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--caller-log', type=Path, required=True)
    parser.add_argument('--owner-empty', type=Path, required=True)
    args = parser.parse_args()
    empty = {line.split('\t')[0] for line in args.owner_empty.read_text().splitlines()}
    review = []
    scope = [r for r in read_rows(DATA / 'scope.tsv') if r['担当'] == 'P1-1']
    hal = {r['パス']: r for r in read_rows(DATA / 'hal_map.tsv')}
    hits = callers(ROOT, [r['パス'] for r in scope if r['種類'] == '運用の道具'])
    with args.caller_log.open('w') as stream:
        for path, evidence in hits.items():
            print(path, len(evidence), *(f'{p}:{n}' for p, n in evidence.items()), sep='\t', file=stream)
    with (DATA / 'layout.tsv').open('w') as stream:
        writer = csv.writer(stream, delimiter='\t', lineterminator='\n')
        writer.writerow(HEADER)
        for row in scope:
            path = row['パス']
            dest, reason = placement(path, hal, row)
            if dest == '=':
                reason += '：' + row['根拠'].partition(' 候補印=')[0]
            if path in BUILD_FOLLOWUP:
                reason += '。' + BUILD_FOLLOWUP[path]
            if path in empty:
                reason += '。持ち主：' + row['所属種類']
            if row['根拠'].startswith('担当の直し'):
                review.append(['担当の直し', path, row['根拠']])
            if path in hits:
                evidence = '、'.join(f'{p}:{n}' for p, n in hits[path].items())
                review.append(['運用の道具', path, UNUSED.get(path, row['根拠'].split(' 候補印=')[0])
                               + '。呼出元：' + (evidence or '静的参照なし')])
                reason += f'。運用の静的呼出元 {len(hits[path])} 件'
            if row['外の呼び出し元'] != '—':
                reason += '。外部利用を継続：' + row['外の呼び出し元']
            if path in REMNANTS or path.startswith('kernel/v86/') and path.endswith('.h'):
                reason += '。D-11 残骸の候補：P0-1 で像の入力なし。ただし build/kdebug.mk の HWDEV_HEADERS が拾う HWDEV_GENERATED の前提。削除前に生成依存を確認'
            writer.writerow([path, dest, reason])
    for row in read_rows(DATA / 'scope.tsv'):
        caller = row.get('外の呼び出し元')
        if caller and caller != '—':
            action = 'P7 で終了。P3 前に相手に通知'
            if row['担当'] == 'P1-1':
                dest, _ = placement(row['パス'], hal, row)
                action = '現行入口を維持' if dest == '=' else 'P3 前に参照先の変更を通知：' + dest
            review.append(['外部利用', row['パス'], caller + '。' + action])
    with (DATA / 'layout_review.tsv').open('w') as stream:
        writer = csv.writer(stream, delimiter='\t', lineterminator='\n')
        writer.writerow(['区分', 'パス', '判断・証拠'])
        writer.writerows(review)
    moves = move_rows(read_rows(DATA / 'layout.tsv'))
    (DATA / 'move_map.json').write_text(json.dumps(moves, indent=2) + '\n')


if __name__ == '__main__':
    main()
