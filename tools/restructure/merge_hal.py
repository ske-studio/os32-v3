"""Merge P0-6 tables and the ten P1-3 reviews into a file-level ledger."""
import argparse
import csv
from collections import defaultdict
from pathlib import Path


DOMAINS = {'音': 'sound', '入力': 'input', '描画': 'gfx', '時刻': 'time',
           'ブロック': 'block', 'LAN': 'net'}

# Calls identified by P0 whose entire file was outside the ten review chunks.
# The concrete dependency, or the existing boundary that makes relocation
# sufficient, decides the disposition.
CALL_REASONS = {
    'exec/appslot.c': '終了時に弱い gf_display_owner_exit を直接選ぶ。表示所有者の終了通知を登録口に渡す。',
    'exec/exec_gfx_poll.h': 'exec_gfx_poll が GeForce の期限と前景処理を直に選ぶ。表示サービスに期限と実行を問い合わせる口が要る。',
    'exec/exec_syscall.inc': 'sw_write などは snd_writer_probe.h の計測口で、システムコールの実装は同じまま移せる。',
    'fs/ext2/ext2_medium_ide.c': 'IDE 用の ext2_medium_ide_ops が媒体・区画の解決を一手に受け持つ。',
    'fs/ext2/ext2_medium_loop.c': 'ext2_medium_loop_ops は loop_contract.h の読み書きに委譲する独立アダプタ。',
    'fs/ext2/ext2_vfs.c': 'マウント先の選択は既存の platform_ext2_vfs_mount に委譲する。',
    'fs/fat/fatfs_vfs.c': 'マウントが FDD と HDD を選び IDE 物理セクタ長と区画を直接設定する。媒体アダプタに設定を渡す必要がある。',
    'fs/fat/fatfs_vfs_read_cache.inc': 'キャッシュの無効化が FDC の世代に直結する。媒体共通の世代取得を介す必要がある。',
    'fs/isofs/iso9660.c': 'iso_read_device は Device に対するブロック読み取りだけを要求する。',
    'fs/isofs/iso9660_media.c': 'iso9660_resource_check は Device の媒体世代でキャッシュの寿命を判定する。',
    'gfx/gfx_core_init.inc': 'gfx_init_mode が planar のページ順と 200 行時の例外を所有する。初期化順を対応 provider に渡す。',
    'include/kernel_mount_children.h': '子の列挙はデバイス登録表の名前と検索口を使う。',
    'init/kernel_start.c': '初期化の骨格が共通名の platform_boot_machine に機種の起動を委譲する。',
    'kernel/console/console_dual.c': '二重出力の可否は display_dual_outputs で表示提供側に問い合わせる。',
    'kernel/dev/dev.c': 'dev_name_present は登録済みの Device の名前を照合する。',
    'kernel/dev/device_ide.c': 'platform_ide_read_sector は IDE を選んだ像用の委譲実装である。',
    'kernel/dev/device_kcg.c': 'platform_kcg_load_font は KCG の提供するフォント読込口のアダプタ。',
    'kernel/dev/device_loop.c': 'platform_loop_dev_read_lba は loop デバイスの読み取り口に委譲する。',
    'kernel/dev/device_pci.c': 'platform_pci_cfg_read32 は PCI アダプタで構成空間の参照を受ける。',
    'kernel/dev/ram_ro.c': 'ram_ro_register は読み取り専用の Device を既存の登録口に渡す。',
    'kernel/ime/ime.c': 'IME は kbd_contract.h のキー取得と既存の表示口を使用する。',
    'kernel/ime/ime_dict.c': '辞書の診断表示は serial_contract.h の文字列出力口を使用する。',
    'kernel/ime/ime_render.inc': '描画は ime_display_save 等の表示サービスを介して行う。',
    'kernel/irq/irq.c': '共通の割込配送が PCM 専用診断を前後に差し込む。装置別の計測を登録する口が要る。',
    'kernel/irq/irq_math.c': 'irq_dyn_index は機種の既存の topology 取得口を使用する。',
    'kernel/irq/tick_budget.c': 'tick_budget_cnt はクロックの換算を既存の arch_tick_budget_cnt に委譲する。',
    'kernel/kselftest_report.c': 'kselftest_skipped は部品の公開する状態名を表示する。',
    'kernel/module/kdebug_call.c': 'kdebug_call が PCM 専用 opcode を識別して pcm_e4_run を起動する。診断要求の受付を部品へ渡す。',
    'kernel/sys.c': 'sys_halt が GeForce を直接 poll する。待機時の前景処理を登録する口に変更する。',
    'net/nic.c': '送受信の委譲とリンク制御には NetNicOps と機種選択済みの link_platform 契約がある。',
    'sound/hal/snd2_pos.c': 'snd2_stream_wait は共通の待機状態と snd_writer_probe.h の計測口を使用する。',
    'sound/hal/snd2_stream.c': 'snd2_stream_open は共通の寿命管理を行い、ハードウェア停止は操作表に渡す。',
}
SPLIT = {'exec/appslot.c', 'exec/exec_gfx_poll.h', 'fs/fat/fatfs_vfs.c',
         'fs/fat/fatfs_vfs_read_cache.inc', 'gfx/gfx_core_init.inc', 'kernel/sys.c',
         'kernel/module/kdebug_call.c', 'kernel/irq/irq.c'}
RESOLVED = {
    'gfx/gfx_core.c': 'shell_print は既存の契約を使えるが、gfx_mode_fits の機種例外が同じファイルに残る。',
    'drivers/video/geforce/geforce_module.h': '操作表の型と公開・借用の手順を一緒に宣言するモジュール契約。',
    'drivers/video/geforce/gf_display_data.c': 'GF 専用の状態と操作表を常駐側から貸し出す装置実装。',
    'drivers/video/geforce/leave_log.h': '診断も geforce_leave_log が GPU のレジスタを選択・復元する装置操作。',
    'drivers/video/geforce/geforce_tick.c': 'PC98_TICK_GF の配線は PC-98 側が所有する。',
    'drivers/video/geforce/geforce_tick.h': 'PC-98 の tick 配線の宣言を同じ機種側に置く。',
    'drivers/video/geforce/geforce_body.c': 'GPU 初期化を装置に残し、route98 の機種配線を分ける。',
    'drivers/base/dev.c': '登録・転送の共通処理と具体的な FDD/IDE アダプタが同居する。',
    'kernel/input_service.c': '空の実装も登録できるが、ファイルの公開入口は InputOps への振り分け。',
    'kernel/time/clock_service.c': '既定値の提供も含め、公開入口は登録した ClockOps への振り分け。',
    'net/nic_rx.c': '受信後の選別とキュー管理はネットワーク層の共通処理。',
    'sound/hal/snd2_v1.c': '旧 PCM API の変換は音声共通層にまとめる。',
}


def read(path):
    with path.open(encoding='utf-8', newline='') as stream:
        return list(csv.DictReader(stream, delimiter='\t'))


def fixed(path, tables):
    hal, gf, calls = (tables[name].get(path, []) for name in ('hal', 'geforce', 'p8'))
    role = '共通処理'
    dest = 'sys/' + path.replace('kernel/', 'kern/', 1)
    if path.startswith(('exec/', 'init/')):
        dest = 'sys/kern/' + path
    reason = ''
    if hal:
        row = hal[0]
        role = row['役割']
        if path.startswith('drivers/'):
            dest = 'sys/dev/' + path[len('drivers/'):]
        elif not path.startswith(('platform/', 'fs/')):
            dest = f"sys/hal/{DOMAINS[row['分野']]}/{Path(path).name}"
        reason = f"{path}:{row['行']} {row['根拠']}。{row['分野']}の{role}を定義する。"
    if gf:
        role = '契約' if path.endswith('.h') else 'バックエンド'
        dest = 'sys/dev/video/geforce/'
        reason = f"{gf[0]['根拠']}。{gf[0]['個別の役割']}を装置側に置く。"
    if calls:
        row = calls[0]
        reason = f"{path}:{row['行']} {row['関数']} — {CALL_REASONS[path]}"
        if '/device_' in path:
            role = 'バックエンド'
            dest = 'sys/dev/' + Path(path).stem.removeprefix('device_') + '/'
    return role, 'P8' if path in SPLIT else '移すだけ', dest, reason


def merge(p0, reviews):
    tables = {}
    for name in ('hal', 'geforce', 'p8'):
        groups = defaultdict(list)
        for row in read(p0 / f'{name}.tsv'):
            groups[row['file']].append(row)
        tables[name] = groups
    reviewed = defaultdict(list)
    for prefix, count in [('hal', 5), ('gf', 3), ('p8', 2)]:
        for i in range(count):
            for row in read(reviews / f'{prefix}-{i}/rows.tsv'):
                reviewed[row['file']].append(row)
    expected = set().union(*tables.values())
    if reviewed.keys() - expected:
        raise ValueError('P0-6 にない判断')
    for path in sorted(expected):
        if path in reviewed:
            rows = reviewed[path]
            r = next((r for r in rows if r['移設'] == 'P8'), rows[0])
            role, move, dest, reason = (r[key] for key in ('役割', '移設', '行き先の候補', '根拠'))
            move = {'移すだけで済む': '移すだけ'}.get(move, move)
            if path in RESOLVED:
                reason += ' ' + RESOLVED[path]
            if path == 'drivers/video/geforce/geforce_module.h':
                role = '契約'
            elif path.endswith(('gf_display_data.c', 'leave_log.h')):
                role = 'バックエンド'
            elif path in ('drivers/base/dev.c', 'net/nic_rx.c'):
                role = '共通処理'
            elif path in ('kernel/input_service.c', 'kernel/time/clock_service.c'):
                role = '振り分け'
        else:
            role, move, dest, reason = fixed(path, tables)
        if path.startswith('drivers/video/pc98/') and move == '移すだけ':
            dest = 'sys/platform/pc98/video/'
        if path == 'fs/ext2/ext2_medium_ide.c':
            dest = 'sys/platform/pc98/storage/'
        if path == 'fs/ext2/ext2_medium_loop.c':
            dest = 'sys/dev/block/'
        if move == '移すだけ' and dest.endswith('/'):
            name = Path(path).name
            if dest.startswith('sys/dev/video/geforce/'):
                for prefix in ('hwdev/gf/', 'kdebug/', 'hwdev/generated/'):
                    if path.startswith(prefix):
                        name = path.removeprefix(prefix)
                        break
            dest += name
        yield [path, role, move, dest, reason]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('p0', 'reviews', 'output'):
        parser.add_argument(name, type=Path)
    args = parser.parse_args()
    with args.output.open('w', encoding='utf-8', newline='') as stream:
        writer = csv.writer(stream, delimiter='\t', lineterminator='\n')
        writer.writerow(['パス', '役割', '移設', '行き先の候補', '根拠'])
        writer.writerows(merge(args.p0, args.reviews))


if __name__ == '__main__':
    main()
