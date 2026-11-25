"""Merge P0-6 tables and the ten P1-3 reviews into a file-level ledger."""
import argparse
import csv
from collections import defaultdict
from pathlib import Path

import layout
from p1_verify import boundary_symbol


DOMAINS = {'音': 'sound', '入力': 'input', '描画': 'gfx', '時刻': 'time',
           'ブロック': 'block', 'LAN': 'net'}

# Calls identified by P0 whose entire file was outside the ten review chunks.
# The concrete dependency, or the existing boundary that makes relocation
# sufficient, decides the disposition.
CALL_REASONS = {
    'sys/kern/exec/appslot.c': '終了時に弱い gf_display_owner_exit を直接選ぶ。表示所有者の終了通知を登録口に渡す。',
    'sys/kern/exec/exec_gfx_poll.h': 'exec_gfx_poll が GeForce の期限と前景処理を直に選ぶ。表示サービスに期限と実行を問い合わせる口が要る。',
    'sys/kern/exec/exec_syscall.inc': 'sw_write などは snd_writer_probe.h の計測口で、システムコールの実装は同じまま移せる。',
    'sys/platform/pc98/storage/ext2_medium_ide.c': 'IDE 用の ext2_medium_ide_ops が媒体・区画の解決を一手に受け持つ。',
    'sys/dev/block/ext2_medium_loop.c': 'ext2_medium_loop_ops は loop_contract.h の読み書きに委譲する独立アダプタ。',
    'sys/fs/ext2/ext2_vfs.c': 'マウント先の選択は既存の platform_ext2_vfs_mount に委譲する。',
    'sys/fs/fat/fatfs_vfs.c': 'マウントが FDD と HDD を選び IDE 物理セクタ長と区画を直接設定する。媒体アダプタに設定を渡す必要がある。',
    'sys/fs/fat/fatfs_vfs_read_cache.inc': 'キャッシュの無効化が FDC の世代に直結する。媒体共通の世代取得を介す必要がある。',
    'sys/fs/isofs/iso9660.c': 'iso_read_device は Device に対するブロック読み取りだけを要求する。',
    'sys/fs/isofs/iso9660_media.c': 'iso9660_resource_check は Device の媒体世代でキャッシュの寿命を判定する。',
    'sys/hal/gfx/gfx_core_init.inc': 'gfx_init_mode が planar のページ順と 200 行時の例外を所有する。初期化順を対応 provider に渡す。',
    'sys/include/kernel_mount_children.h': '子の列挙はデバイス登録表の名前と検索口を使う。',
    'sys/kern/init/kernel_start.c': '初期化の骨格が共通名の platform_boot_machine に機種の起動を委譲する。',
    'sys/kern/console/console_dual.c': '二重出力の可否は display_dual_outputs で表示提供側に問い合わせる。',
    'sys/kern/dev/dev.c': 'dev_name_present は登録済みの Device の名前を照合する。',
    'sys/dev/ide/device_ide.c': 'platform_ide_read_sector は IDE を選んだ像用の委譲実装である。',
    'sys/dev/kcg/device_kcg.c': 'platform_kcg_load_font は KCG の提供するフォント読込口のアダプタ。',
    'sys/dev/loop/device_loop.c': 'platform_loop_dev_read_lba は loop デバイスの読み取り口に委譲する。',
    'sys/dev/pci/device_pci.c': 'platform_pci_cfg_read32 は PCI アダプタで構成空間の参照を受ける。',
    'sys/kern/dev/ram_ro.c': 'ram_ro_register は読み取り専用の Device を既存の登録口に渡す。',
    'sys/kern/ime/ime.c': 'IME は kbd_contract.h のキー取得と既存の表示口を使用する。',
    'sys/kern/ime/ime_dict.c': '辞書の診断表示は serial_contract.h の文字列出力口を使用する。',
    'sys/kern/ime/ime_render.inc': '描画は ime_display_save 等の表示サービスを介して行う。',
    'sys/kern/irq/irq.c': '共通の割込配送が PCM 専用診断を前後に差し込む。装置別の計測を登録する口が要る。',
    'sys/kern/irq/irq_math.c': 'irq_dyn_index は機種の既存の topology 取得口を使用する。',
    'sys/kern/irq/tick_budget.c': 'tick_budget_cnt はクロックの換算を既存の arch_tick_budget_cnt に委譲する。',
    'sys/kern/kselftest_report.c': 'kselftest_skipped は部品の公開する状態名を表示する。',
    'sys/kern/module/kdebug_call.c': 'kdebug_call が PCM 専用 opcode を識別して pcm_e4_run を起動する。診断要求の受付を部品へ渡す。',
    'sys/kern/sys.c': 'sys_halt が GeForce を直接 poll する。待機時の前景処理を登録する口に変更する。',
    'sys/net/nic.c': '送受信の委譲とリンク制御には NetNicOps と機種選択済みの link_platform 契約がある。',
    'sys/hal/sound/snd2_pos.c': 'snd2_stream_wait は共通の待機状態と snd_writer_probe.h の計測口を使用する。',
    'sys/hal/sound/snd2_stream.c': 'snd2_stream_open は共通の寿命管理を行い、ハードウェア停止は操作表に渡す。',
}
SPLIT = {'sys/kern/exec/appslot.c', 'sys/kern/exec/exec_gfx_poll.h', 'sys/fs/fat/fatfs_vfs.c',
         'sys/fs/fat/fatfs_vfs_read_cache.inc', 'sys/hal/gfx/gfx_core_init.inc', 'sys/kern/sys.c',
         'sys/kern/module/kdebug_call.c', 'sys/kern/irq/irq.c'}
RESOLVED = {
    'sys/hal/gfx/gfx_core.c': 'shell_print は既存の契約を使えるが、gfx_mode_fits の機種例外が同じファイルに残る。',
    'sys/dev/video/geforce/geforce_module.h': '操作表の型と公開・借用の手順を一緒に宣言するモジュール契約。',
    'sys/dev/video/geforce/gf_display_data.c': 'GF 専用の状態と操作表を常駐側から貸し出す装置実装。',
    'sys/dev/video/geforce/leave_log.h': '診断も geforce_leave_log が GPU のレジスタを選択・復元する装置操作。',
    'sys/platform/pc98/video/geforce/geforce_tick.c': 'PC98_TICK_GF の配線は PC-98 側が所有する。',
    'sys/platform/pc98/video/geforce/geforce_tick.h': 'PC-98 の tick 配線の宣言を同じ機種側に置く。',
    'sys/dev/video/geforce/geforce_body.c': 'GPU 初期化を装置に残し、route98 の機種配線を分ける。',
    'sys/dev/base/dev.c': '登録・転送の共通処理と具体的な FDD/IDE アダプタが同居する。',
    'sys/hal/input/input_service.c': '空の実装も登録できるが、ファイルの公開入口は InputOps への振り分け。',
    'sys/hal/time/clock_service.c': '既定値の提供も含め、公開入口は登録した ClockOps への振り分け。',
    'sys/net/nic_rx.c': '受信後の選別とキュー管理はネットワーク層の共通処理。',
    'sys/hal/sound/snd2_v1.c': '旧 PCM API の変換は音声共通層にまとめる。',
}


BOUNDARY_SPLITS = {
    'sys/dev/net/e82557/lan_module.c': (
        'sys/dev/net/e82557/ + sys/platform/pc98/',
        'e82557_tick の登録が PC98_TICK_E82557 を固定する。スロットを機種配線で決める。'),
    'sys/dev/net/ne2000/lgy98.c': (
        'sys/dev/net/ne2000/ + sys/platform/pc98/',
        'lgy98_tick の登録が PC98_TICK_LGY98 を選ぶ。割付を機種側に渡す。'),
    'sys/dev/net/ne2000/ne2000.c': (
        'sys/dev/net/ne2000/ + sys/platform/pc98/',
        'rd の装置操作と同じ翻訳単位で PC98_TICK_COUNT を検査する。取り込む ne2000_control.inc と共に機種別スロット割付を分ける。'),
    'sys/dev/net/ne2000/ne2000_control.inc': (
        'sys/dev/net/ne2000/ + sys/platform/pc98/',
        'ne2k_timer_tick の登録で PC98_TICK_NE2K を決め打ちする。機種からスロットを渡す。'),
    'sys/dev/video/geforce/backend_geforce.c': (
        'sys/dev/video/geforce/ + sys/platform/pc98/',
        'pc98_route_to_gf と pc98_console_set_safe による表示経路変更を機種側に分ける。'),
    'sys/dev/video/geforce/geforce_module.c': (
        'sys/dev/video/geforce/ + sys/platform/pc98/',
        'pc98_route_safe と pc98_route_to_98 を直接使うモジュール停止経路に機種側の委譲口が要る。'),
    'sys/dev/video/trident/present.c': (
        'sys/dev/video/trident/ + sys/platform/pc98/',
        'trident_port8 に PC98_IO_WAIT_PORT を指定する待ち操作を機種側に渡す。'),
    'sys/hal/gfx/v2/lock.c': (
        'sys/hal/gfx/ + sys/arch/x86/',
        'gfx2_lock の写像が PTE_PCD・PTE_PWT と CR3 を読む。キャッシュ属性の継承とアドレス空間の照合を CPU 側に渡す。'),
    'sys/kern/ime/ime.c': (
        'sys/kern/ime/ + sys/hal/input/',
        'ime_getchar_wait と非同期入力が kbd_getkey_wait・kbd_trygetchar・kbd_trygetkey を直に呼ぶ。入力 HAL に待機・取得を委譲する。'),
    'sys/net/nic.c': (
        'sys/hal/net/ + sys/dev/net/',
        'IRQ 時の受信を NET_NIC_E82557 と LINK_PLATFORM_TICK_RX_LGY98_SLOT で決める。選択順と受信能力を登録側から渡す。'),
    'sys/net/nic.h': (
        'sys/hal/net/ + sys/dev/net/',
        'NetNicOps と net_lgy98_adapter・net_e82557_adapter の extern を分け、具体表を登録側に公開する。'),
    'sys/include/ext2_medium_contract.h': (
        'sys/hal/block/ + sys/platform/pc98/ + sys/dev/block/',
        'ext2_medium_ops と登録口を HAL に残し、ext2_medium_ide_ops・ext2_medium_loop_ops の宣言を sys/platform/pc98/boot.c が使う登録側へ分ける。'),
    'sys/hal/gfx/gfx_core_surface.inc': (
        'sys/hal/gfx/ + sys/arch/x86/',
        'gfx_reinit_surface_roles 周辺で paging_load_cr3 が描画中のページ表を切り替える。写像の切替と復帰を CPU 側で対にする。'),
    'sys/kern/kaudit.c': (
        'sys/kern/ + sys/arch/x86/',
        'kselftest_run_audit が PTE_USER・PTE_PCD を解釈する。ページ属性の照合を CPU 側に分ける。'),
    'sys/kern/mm/memory_boot.c': (
        'sys/kern/mm/ + sys/arch/x86/ + sys/dev/video/',
        'memory_boot_init のページ予算が PTE_COUNT と GF_PRODUCTION_PT_PAGES に依存する。ページ表と装置予約の必要量を各提供側から受ける。'),
    'sys/kern/exec/exec_launch.inc': (
        'sys/kern/exec/ + sys/arch/x86/',
        'paging_current_cr3 と paging_load_cr3 による起動前後の空間切替を CPU 側へ分ける。'),
    'sys/kern/exec/exec_syscall.inc': (
        'sys/kern/exec/ + sys/arch/x86/',
        'paging_load_cr3 にカーネルの PD を渡す終了経路をアドレス空間切替の口に移す。'),
    'sys/include/gfx_hal.h': (
        'sys/hal/gfx/ + sys/dev/video/ + sys/platform/pc98/',
        'GfxBackend と各機種・装置の具体表、pegc_restore_text_sync・trident_boot_reserve の宣言を分ける。'),
    'sys/hal/input/in2_int.h': (
        'sys/hal/input/ + sys/dev/input/',
        'kbd_hal_init と kbd_hal_capture は sys/platform/pc98/input/kbd_hal.c の装置入口。共通リングの内部宣言から分ける。'),
    'sys/hal/input/keymap_eval.c': (
        'sys/hal/input/ + sys/platform/pc98/',
        'KM_MAP_PC98 でバンクと拡張表の解釈が変わる。マップの提供側に評価を渡す。'),
    'sys/hal/input/keymap_registry.c': (
        'sys/hal/input/ + sys/platform/pc98/',
        'valid_map が KM_MAP_PC98 の件数とバンク構造を解釈する。マップ固有の検証を登録側に渡す。'),
    'sys/input/input_policy.c': (
        'sys/input/ + sys/platform/',
        'ikm_policy_init と既定キー選択が機種番号でプロファイルと制御文字を選ぶ。既定値を機種から渡す。'),
    'sys/kern/ime/ime_dict.c': (
        'sys/kern/ime/',
        '辞書の読込と再開が装置実装の serial_puts を直接呼ぶ。診断出力を kprintf に渡す。'),
    'sys/dev/video/geforce/geforce_platform.c': (
        'sys/dev/video/geforce/ + sys/arch/x86/',
        'reserve の BAR 写像が PTE_PCD を指定する。MMIO 用の属性設定を CPU 側へ渡す。'),
    'sys/dev/video/trident/resources.c': (
        'sys/dev/video/trident/ + sys/arch/x86/',
        'trident_resources_rollback が CR3 を保存・交換する。装置資源の解放と空間切替を分ける。'),
    'sys/dev/video/geforce/native.c': (
        'sys/dev/video/geforce/ + sys/arch/x86/',
        'geforce_range_reserved の写像照合で PTE_PRESENT・PTE_RW・PTE_PCD を解釈する。CPU 側から照合結果を受ける。'),
    'sys/platform/pc98/clock_source.c': (
        'sys/platform/pc98/ + sys/arch/x86/',
        'PIT の配線と rdtsc の読み出しを分け、CPU 側からカウンタを受ける。'),
    'sys/platform/powermac/clock.c': (
        'sys/platform/powermac/ + sys/arch/ppc/',
        'platform_clock_read_us の周波数換算から mftb・mftbu の読出しを分ける。'),
    'sys/platform/powermac/system.c': (
        'sys/platform/powermac/ + sys/arch/ppc/',
        'RTC 配線と mftb・mftbu による Time Base 読出し、eieio バリアを分ける。'),
    'sys/kapi/kapi_generated.c': (
        'sys/kapi/ + sys/dev/',
        'wrap_kcg_read_ank などが具体装置に直接委譲する。生成の入力で委譲先を変更し公開 ABI を保つ。'),
}


RETAINED = {
    'sys/dev/char/rtc.c': 'pc98_clock は PC-98 内蔵カレンダ時計の操作表であり、この提供側で完結する。',
    'sys/dev/video/geforce/geforce_module.h': 'pc98_safe は常駐モジュールの操作表の欄で、経路切替の実装は含まない。',
    'sys/dev/video/trident/lifecycle.c': 'trident_pc98_init は同じ装置の FIFO・MCLK の設定段を呼ぶ。',
    'sys/dev/video/trident/trident.h': 'TRI_STEP_PC98_INIT は装置内部の初期化段を記録する列挙値。',

    'sys/fs/ext2/ext2_fmt.c': 'ide_drive は媒体操作表に渡す引数。',
    'sys/fs/ext2/ext2_super.c': 'ide_drive は platform_ext2_resolve に渡す識別値。',
    'sys/fs/fat/fatfs/ff.c': 'PTE はディスク区画表の欄を示す。',
    'sys/kern/gfx/osri/gfx_osri_view_internal.h': 'serial_gate_active は登録済みの非常時出力への問い合わせ。',
    'sys/hal/gfx/sync/sync_write.c': '空の asm はコンパイラバリアで CPU 命令を発行しない。',
    'sys/hal/gfx/v2/a0_weak.c': 'NOSYS の既定実装は pte_flags の値を使用しない。',
    'sys/hal/gfx/v2/gfx2_int.h': '台帳と共有する識別値の一致を静的に検査し、装置操作は行わない。',
    'sys/input/input_policy.h': '入力設定形式の識別値と初期化の宣言。機種別の既定値は実装で分離する。',
    'sys/kern/console/console.c': 'kbd_inject_discard は sys/kern/kbd_inject.c の注入キューを破棄する。',
    'sys/kern/dev/install_management.c': 'serial_crc は媒体識別値の比較欄。',
    'sys/kern/kprintf_format.inc': '属性は機種が実装する変換口へ渡す。',
    'sys/net/host_mailbox.c': 'serial_now は通知の通し番号。',
    'sys/kern/v86/a0_weak.c': 'V86 未提供時の弱いサービス入口をまとめてサービス所有者へ移す。',
    'sys/dev/video/geforce/gf_v2_gart.c': 'pte_write の対象は GPU の GART 表。',
    'sys/platform/pc98/kdebug/kselftest/gfx_ledger.inc': 'PC-98 の VRAM の x86 キャッシュ属性そのものを検査する機種専用試験。',
    'sys/platform/pc98/time/cpu_calibrate.c': 'NOP ループは PC-98 の tick に対して計測する対象そのもの。',
}


def retained_reason(path, dest):
    if path in RETAINED:
        return RETAINED[path]
    if path.startswith('input/hal/'):
        return '入力の共通状態・リング・方針の参照で、装置側の読取りを呼ばない。'
    if dest.startswith(('sys/dev/', 'sys/platform/', 'sys/arch/')):
        return '上記の実装と固有宣言は候補先の所有者の内部で使用する。'
    raise ValueError(f'{path}: 境界の判断が必要')


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


def p8_destination(path):
    # U17 moves the whole file now. Keep the proposed internal separation in the reason.
    parent = Path(path).parent.as_posix()
    special = {'drivers/input': 'sys/platform/pc98/input', 'gfx': 'sys/hal/gfx',
               'gfx/v2': 'sys/hal/gfx/v2', 'hwdev/gf': 'sys/dev/video/geforce',
               'input/hal': 'sys/hal/input'}
    if path == 'sys/input/input_policy.c':
        return 'sys/input/input_policy.c'
    if parent in special:
        return special[parent] + '/' + Path(path).name
    return layout.placement(path, {}, {})[0]


def sibling_destination(path, dest):
    source = Path(path)
    if source.parent == Path('gfx/v2'):
        return 'sys/hal/gfx/v2/' + source.name
    if source.parent == Path('kdebug') and Path(dest).parent == Path('sys/dev/video/geforce'):
        return 'sys/dev/video/geforce/diag/' + source.name
    if source.parent == Path('kdebug/kselftest') and '/kselftest/' in dest and '/kdebug/' not in dest:
        return dest.replace('/kselftest/', '/kdebug/kselftest/')
    if path in ('sys/kern/console/console.c', 'sys/kern/dev/install_management.c'):
        return 'sys/kern/' + path.removeprefix('kernel/')
    return dest


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
            if path == 'sys/dev/video/geforce/geforce_module.h':
                role = '契約'
            elif path.endswith(('gf_display_data.c', 'leave_log.h')):
                role = 'バックエンド'
            elif path in ('sys/dev/base/dev.c', 'sys/net/nic_rx.c'):
                role = '共通処理'
            elif path in ('sys/hal/input/input_service.c', 'sys/hal/time/clock_service.c'):
                role = '振り分け'
        else:
            role, move, dest, reason = fixed(path, tables)
        if path.startswith('drivers/video/pc98/') and move == '移すだけ':
            dest = 'sys/platform/pc98/video/'
        if path == 'sys/platform/pc98/storage/ext2_medium_ide.c':
            dest = 'sys/platform/pc98/storage/'
        if path == 'sys/dev/block/ext2_medium_loop.c':
            dest = 'sys/dev/block/'
        if move == '移すだけ' and dest.endswith('/'):
            name = Path(path).name
            if dest.startswith('sys/dev/video/geforce/'):
                for prefix in ('hwdev/gf/', 'kdebug/', 'hwdev/generated/'):
                    if path.startswith(prefix):
                        name = path.removeprefix(prefix)
                        break
            dest += name
        if path == 'sys/kern/v86/a0_weak.c':
            dest = 'sys/kern/v86/a0_weak.c'
        if path in BOUNDARY_SPLITS:
            dest, detail = BOUNDARY_SPLITS[path]
            move = 'P8'
            reason = f'{path} — {detail}'
        elif move == '移すだけ':
            source = (Path(__file__).resolve().parents[2] / path).read_text()
            symbol = boundary_symbol(source)
            if symbol:
                reason += f' 境界照合 {symbol}。' + retained_reason(path, dest)
        if move == 'P8':
            reason += '。P8 の再配置候補：' + dest
            dest = p8_destination(path)
        yield [path, role, move, sibling_destination(path, dest), reason]


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
