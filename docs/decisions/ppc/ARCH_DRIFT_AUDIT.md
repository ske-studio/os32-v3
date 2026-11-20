> 出所: 本線の機械の ~/os32-tmp/run/arch-drift-audit/AUDIT.md (sol の読み取りの調べ、2026-11-20、基点 origin/main cc1260bd2)。行番号は取り込みの時に外した (パスで指す)。
> 状態: **現行 (2026-11-20)** — x86 と PowerPC の実装のぶれの洗い出し。揃える段の状態は票 TASK_PPC_ALIGN_X86 が持つ。

# PC-98／PowerMac 実装差分の監査

対象は `origin/main` の `cc1260bd2be7ef25c5722229fb4699aea3f958a5`。x86 は PC-98 製品像、PPC は既定の `kboot-link` と `kboot-core / kboot-shell / kboot-sh / kboot-link-sh`。ソース・選択式・試験コードを読み、製品を変更せず、ビルド・実機・ゲスト試験は省略した。規模は x86 のサービス契約へ寄せる概算の変更行数とファイル数。分類間で同じファイルを触る場合があるため、規模の合算には使わない。来歴の短縮 SHA は各側の先頭根拠行の `git blame origin/main`。実装全体の導入時点と異なる場合があり、分岐の導入が特定できた箇所は下に補記した。

比較基準は PACK の 2026-11-20 指示。対象 SHA の `docs/decisions/ARCH_COMPAT_CONTRACT.md` は §8 から確定記録へ続き、§9 の表を含まない。`docs/POLICY_DEV.md` 以下の §10 にも「x86 に揃える」行が見当たらない。表の「既にある」は PACK に掲げられた8項目への対応を意味する。現行 §10 の「root は RAM の読み取り専用の像」は維持して読んだ。root の統合対象は mount 手続きで、互換のために ISO RAM root 自体を HDD へ置き換える見積もりは含めていない。

## A　物理の差と共通契約

| ID | 役目 | PC-98 | PowerMac と共通の口 | 根拠 |
|---|---|---|---|---|
| A01 | 起動受渡し・保存した起動情報 | BIOS・ローダの保存域、boot_context | OF の device tree・予約情報、firmware 離脱。共通 kernel_start に合流 | `platform/pc98/kernel.c`、`platform/pc98/boot_capture.c`、`platform/powermac/kernel_boot.c`、`init/kernel_start.c` |
| A02 | メモリの検出と CPU のページ表 | BIOS＋上位 RAM プローブ、x86 PD | OF メモリ範囲と予約、PPC HPT。共通の paging・physmem・pgalloc を使う | `platform/pc98/memory.c`、`platform/powermac/memory.c`、`build/arch/ppc-sources.mk`、`build/sources/common.mk` |
| A03 | 特権遷移・アプリ ABI | CPL、iret、x86 KAPI call・system call、setjmp | SRR/MSR、sc、PPC SysV、FP 保存。exec・slot・owner の本体は共通 | `build/arch/x86-sources.mk`、`build/arch/x86-sources.mk`、`build/arch/ppc-sources.mk`、`build/arch/ppc-sources.mk`、`build/sources/common.mk` |
| A04 | IRQ コントローラ・完了 | PIC とプラットフォーム配線 | MPIC、IPI。CPU 入口・装置完了は機種側、汎用 IRQ 登録は C を参照 | `platform/pc98/boot_irq.c`、`platform/pc98/irq_complete.c`、`platform/powermac/mpic.c`、`arch/ppc/kernel_trap.c` |
| A05 | tick の物理源・µs 時計 | PIT/TSC と別途 RTC | DEC/TB と VIA RTC。共通 tick・clock service の口、100 Hz は論理 tick | `build/platform/pc98.mk`、`platform/powermac/clock.c`、`platform/powermac/system.c`、`build/sources/common.mk` |
| A06 | PCI config・窓 | PC-98 の config・resource 割当 | OF 側の窓・G4 config。列挙 decode/bind は共通。設定の起動順は B | `build/platform/pc98.mk`、`build/platform/powermac.mk`、`build/sources/common.mk` |
| A07 | 表示ハード・スクロール | TVRAM/GDC と PC-98、PEGC・Cirrus・Trident・GeForce | OF framebuffer。con_backend と gfx の契約を共有。論理出力と owner の差は B | `platform/pc98/display.c`、`platform/powermac/display.c`、`build/platform/powermac.mk`、`build/select.mk` |
| A08 | 入力の物理バスと配列 | PC-98 鍵盤・マウス、PC-98/JIS 表 | OHCI USB/HID と JIS 表。IN2・keymap registry は共通 | `platform/pc98/boot.c`、`platform/powermac/usb_boot.c`、`build/platform/powermac.mk`、`build/sources/common.mk` |
| A09 | NIC・DMA/cache | i82557 系、PC-98 DMA | GEM、固有の tree/clock と cache 同期。NIC の口と link エンジンは共通 | `build/features/lan.mk`、`build/platform/powermac.mk`、`arch/ppc/dma_cache.c`、`build/sources/common.mk` |
| A10 | リセット・起動識別子 | PC-98 リセット I/O・機種ソース | VIA リセット・機種ソース。platform_reset と共通 boot token の口 | `platform/pc98/system.c`、`platform/powermac/system.c`、`platform/pc98/boot_token_pc98.c`、`platform/powermac/boot_token_powermac.c`、`kernel/boot_token.c` |
| A11 | 文字列・データ表現 | NASM・cdecl・x86 幅／エンディアン | PPC cache copy・SysV・別幅。callconv・arch_* で分ける | `build/arch/x86-sources.mk`、`build/arch/ppc-sources.mk`、`include/callconv.h`、`arch/ppc/arch_type_contract.h` |
| A12 | レガシー互換 | V86/BIOS 呼出しと x87 エミュレーション | legacy_guest の absent と PPC 固有 FP。V86/x87 自体は CPU 固有、診断受付の有無は C | `build/select.mk`、`build/select.mk`、`arch/ppc/kernel_fp.c` |
| A13 | 物理ディスク・シリアル・音源 | IDE/FD/CD、PC-98 UART・PCM・FM | VIA/ESCC、OF root 取り込み。物理 driver の差と、mount/診断/音声サービスの有無を区別 | `build/select.mk`、`build/select.mk`、`platform/pc98/boot_disk.c`、`build/platform/powermac.mk` |

## B　揃えられる二重実装・別経路

| ID | 役目 | §9 対応 | x86 の形・根拠 | PowerPC の形・根拠 | 来歴 x86 / PPC | 規模（行・ファイル） | 変更時に照合する受入 |
|---|---|---|---|---|---|---|---|
| B01 | 起動の段取りと exec 初期化 | 新しい（起動文脈の下） | machine で FD・pipe・exec、services で設定後処理、run で shell。`platform/pc98/kernel.c`、`platform/pc98/kernel.c`、`init/kernel_start.c` | services で root・USB/入力、その後 run で FD・pipe・exec。nest=1 の設定も別。共通の三相入口は済み。`platform/powermac/ppc_kernel.c`、`platform/powermac/ppc_kernel.c`、`platform/powermac/exec_boot.c` | `94d67d544` / `86c874057` | 450–950 行、9–18 ファイル | NP21/W HDD・FD、QEMU 5 像の到達点・root/exec エラー・初回子 ID。診断の待機と順序を独立して検証 |
| B02 | 表示選択・予約・描画 owner | 新しい（端末の隣） | 候補列挙と資源予約、選択 backend の CLIENT を boot から gshell へ。`platform/pc98/gfx_surfaces.c`、`platform/pc98/gfx_selector.c`、`platform/pc98/kernel.c` | OF 単一 provider、prepare・shutdown と present_owner/generation を別管理。gfx core と HAL v2 は共有。`platform/powermac/display_provider.c`、`platform/powermac/display_provider.c`、`platform/powermac/display_bundle.c` | `6f93946db` / `6f93946db` | 400–850 行、8–15 ファイル | NP21/W backend・GUI/CUI 遷移、QEMU OF の幅・lease・display guest と owner 切替 |
| B03 | シェルの枠・再起動 | 既にある（枠） | shell/gshell を選択し消費、GUI 障害を数え CUI へ。`platform/pc98/kernel.c`、`userland/shell/main.c` | 共有 CUI ソースに対し別の常駐起動・結果表示。PLN1 初回入力待ち、link は fault も再起動。`platform/powermac/shell_boot.c`、`build/ppc-user.mk` | `3af7d5b84` / `9543775ee` | 350–700 行、6–12 ファイル | shell 切替、連続 fault、missing image、link 継続、初回キーを共通の状態遷移で照合 |
| B04 | LAN 運用・link ポーリング・rshell | 新しい（端末と一部重複） | LAN モジュール、link の tick 登録と rshell Serial/LAN。`platform/pc98/kernel_boot_memory.inc`、`platform/pc98/link_platform.c`、`userland/shell/rshell_lan.c` | GEM の pm_lan_start/poll と LAN 診断、link シェルで idle に登録、非 link は PLN1。link/rshell_lan 本体と NIC 契約は共有。`platform/powermac/lan_service.c`、`platform/powermac/link_platform.c`、`platform/powermac/ppc_kernel.c` | `9ad21254c` / `b53b49589` | 320–680 行、8–14 ファイル | link HELLO/reopen・drop・RSH 結果/epoch・idle/foreground 進行、PLN1 の端末観測を別条件化 |
| B05 | root と /host・デバイス名と mount の手続き | 既にある（root と /host） | 選択 root を直接 mount、BOOT_AUTO_MOUNT、子 mount、HostDrv と設定に応じた SFS。`platform/pc98/kernel.c`、`platform/pc98/boot.c`、`platform/pc98/kernel.c` | ISO RAM root＋ramfs、path の drive を設定。通常 sh は LAN peer、link-sh は link peer 経由の SerialFS。VFS/SerialFS は共有。`platform/powermac/root_boot.c`、`platform/powermac/hostfs_boot.c`、`build/platform/powermac.mk` | `530f40c2f` / `cd8aa6cf6` | 260–600 行、7–13 ファイル | NP21/W HDD/FD・HostDrv、QEMU root CRC/RO・tmp・両 host transport・mount 失敗 |
| B06 | 端末・入力の接続ポリシー | 既にある（端末と入力） | PC-98 入力登録と TVRAM、con dual が GFX 出力も列挙。`platform/pc98/boot.c`、`platform/pc98/display_dual.c`、`platform/pc98/console.c` | 入力ブリッジが USB と LAN を統合、fbcon と複数ストリーム、raw/diag の一部未提供。共通の input_legacy・console dual は使用。`platform/powermac/input_boot.c`、`platform/powermac/display_dual.c`、`platform/powermac/console_text.c` | `f2b82fb3b` / `9e34e73dc` | 250–560 行、7–12 ファイル | NP21/W キー/描画と QEMU USB・LAN の待機/取消・出力先・日本語表示、raw の能力 |
| B07 | ビルド・配布・試験像の作り方 | 新しい | legacy 順序、製品ターゲットと個別 module。`build/select.mk`、`build/targets/x86-pc98.mk`、`build/sources/order.mk` | kboot 順序と 5 像、/bin を ISO に焼く、独自のターゲット拒否・監査。source owner の選択基盤は共通。`build/select.mk`、`build/targets/ppc-powermac.mk`、`build/images/ppc-flags.mk` | `63e0565bf` / `322c326bf` | 240–520 行、8–15 ファイル | source selection・sep identity・全像組立・配布 target の契約・CI 判定 |
| B08 | SQLite 初期化とリンク | 既にある（部品のロード） | モジュールのロードと import。`kernel/sqlite_outer.c`、`build/sqlite-module.ld` | 同じ engine/VFS を静的初期化。hard-float 境界と属性処理、SQLITE フラグで DB KAPI を選択。`build/targets/ppc-powermac.mk`、`platform/powermac/ppc_kernel.c`、`build/sources/hooks.mk` | `b2a1ed009` / `b2a1ed009` | 210–450 行、6–11 ファイル | x86 module ロード/欠損・製品 SQL、PPC static enable/disable、ABI/FP、両方の VFS identity |
| B09 | シェル libc 接着・登録コマンドと既定値 | 新しい（枠／特権の下） | 通常シェルのコマンド登録、製品の既定パスと各コマンドの全経路。`userland/shell/main.c`、`userland/shell/cmd_sys.c` | SHELL_AS_APP と SH_LAUNCH_SYNC、限定登録/動作、PPC の newlib_runtime。旧 sh private runtime/string/stdio は現行既定リンクから外れる。`userland/ppc/sh/profile.h`、`userland/ppc/sh/newlib_runtime.c`、`build/ppc-user.mk` | `936029b55` / `e00a04a54` | 170–420 行、7–13 ファイル | 同じスクリプト・引数・CWD・日本語・コマンド在否と同期子の終了値を両ゲスト。CRT ABI は A |
| B10 | ログの経路と起動診断出力 | 新しい | bootlog 採取、kdebug とローカル画面、ファイルへの退避は C。`platform/pc98/kernel.c`、`init/bootlog.c` | ESCC、ログの stream 選択、LAN ring、KBOOT 段マーカー。kprintf と一部の console は共有。`platform/powermac/log_printf.c`、`platform/powermac/console_log.c`、`platform/powermac/ppc_kernel.c` | `a238ab174` / `f9fbc4035` | 160–360 行、6–11 ファイル | QEMU pre-user/ready・LAN ログ圧と ANSI、NP21/W 起動ログ順序・診断先 |
| B11 | ver/sysinfo とメモリの内訳 | 新しい | VerSnapshot に機種装置・CRC・debug とメモリ項目を提供。`platform/pc98/ver_backend.c`、`platform/pc98/platform_mem_stat.h`、`userland/tests/mem_stat_test.c` | PVR・OF/PCI由来の装置・個別の image 項目。共通 collect/format・KAPI sysinfo、memstat の一部期待を分ける。`platform/powermac/ver_backend.c`、`platform/powermac/platform_mem_stat.h`、`userland/ppc/verprob.c` | `3e2741123` / `15425a2b2` | 110–270 行、5–9 ファイル | test_ver_accept と test_ppc_ver。共通 field の意味とメモリ項目、機種 field の可用性 |
| B12 | SFS scope の実行ラッパー | 新しい（/host の下） | 常駐シェルの sfs run、前景部品あり。`userland/shell/rshell.c`、`fs/serialfs/sfs_session_fg.c` | link profile の LAN 1 行経路が scope を囲み、cwd と終了値を別コードで復元。link-sh だけ scope KAPI。`userland/shell/rshell_lan.c`、`userland/ppc/sh/link_profile.h` | `a238ab174` / `2964a2003` | 90–240 行、4–8 ファイル | 同じ sfs run の cwd・子 exit・scope open/close/drop、ppc-sfs-guest と x86 のセッション試験 |
| B13 | exec 回収とカーネル親の形 | 新しい | 共通 exec/appslot/owner/heap、製品の GUI、IME、音、shlib、R1、reclaim フック。`build/sources/common.mk`、`build/sources/hooks.mk`、`exec/exec.c` | 同じ exec/appslot/owner/heap、提供者が無い領域に absent shim。root から nest=1、arch の selftest あり。`exec/absent/exec_reclaim.c`、`platform/powermac/exec_boot.c`、`arch/ppc/exec_selftest.c` | `e407702a0` / `74e57f8a2` | 85–220 行、5–9 ファイル | 共通の exit/fault と FD・owner・lease 回収を両ゲストで比較。未提供サービスの実装量は C |
| B14 | KCG 提供者の接続 | 新しい（端末・部品の下） | KCG モジュールと ROM/LAN 等の機種側経路、共通 font source。`build/features/kcg.mk`、`drivers/video/kcg/kcg_module.c` | kcg_rom_read_* の口をファイル提供者へ結線。KCG 自体の機能旗は両方で有効化可能。`platform/powermac/kcg.c`、`build/features/kcg.mk` | `43ce1e6d5` / `381492970` | 45–150 行、4–7 ファイル | 日本語字形・内蔵／ファイル不在・再選択、fbcon font probe と x86 GUI |
| B15 | sys_switch_shell の提供判定 | 既にある（門のマクロ） | 生成 wrapper が GUI 実体を呼び、実体で owner を判定。shell_service.h の i386 分岐は構造体サイズ検査。`kernel/gui.c`、`kapi/kapi_generated.c`、`exec/shell_service.h` | PPC では ABI の slot が残り GUI provider group で absent。対象は shell service と KAPI の別々の判定。`sdk/kapi_target_ppc.py`、`build/sources/hooks.mk` | `a238ab174` / `bc840ee3d` | 40–140 行、3–6 ファイル | 切替要求の権限・パスの消費と未提供時の NOSYS、ABI slot 同一 |

## C　片方の像で提供される機能

| ID | 役目 | §9 対応 | x86 の形・根拠 | PowerPC の形・根拠 | 来歴 x86 / PPC | 規模（行・ファイル） | 変更時に照合する受入 |
|---|---|---|---|---|---|---|---|
| C01 | GUI シェル・画面統合・共有ライブラリ | 新しい（枠・モジュールの下） | gshell、kernel/gui、実体の shlib、GUI 設定とフォールバック。`platform/pc98/kernel_gui_shlib.inc`、`build/sources/common.mk` | USER sh と fbcon/GFX はある。gui/shlib hook は absent。`exec/absent/exec_gui.c`、`exec/absent/exec_shlib.c`、`build/platform/powermac.mk` | `244cbdac5` / `74e57f8a2` | 850–2400 行、16–30 ファイル | GUI/CUI 往復・gshell owner・shlib の欠損と回収、PPC GUI 起動像 |
| C02 | 製品ストレージ・ext2/FAT・起動媒体 | 既にある（root の先） | ATA/FD、ext2/FAT、loop ext2、rw root と子 mount。`platform/pc98/boot.c`、`build/sources/common.mk`、`build/sources/common.mk` | RO ISO＋ramfs。loop device は sh 像で共通、ext2 loop backend は x86 限定。PPC の ext2 formatter は compile-only。`build/platform/powermac.mk`、`build/targets/ppc-powermac.mk`、`platform/powermac/loop_boot.c` | `f2b82fb3b` / `15425a2b2` | 700–2000 行、13–25 ファイル | rw disk/loop・異常マウント・永続化、両側で mount/umount。ATA 現物担当は A |
| C03 | IME/FEP と字形の実体 | 新しい（端末・KAPI の下） | IME 初期化・辞書・TVRAM save/restore と KCG provider。`platform/pc98/kernel.c`、`platform/pc98/ime_display.c` | KCG 互換の入口と fbcon/font source はある。IME は absent。powermac/ime_display.c の stub は platform 選択に現れない。`sdk/kapi_target_ppc.py`、`platform/powermac/kcg.c`、`platform/powermac/ime_display.c`、`build/platform/powermac.mk` | `67b8c4ae4` / `bc840ee3d` | 520–1450 行、11–23 ファイル | F10 と辞書、変換・描画・回収、KCG/fontprobe と無辞書起動 |
| C04 | 汎用モジュール loader と kdebug/hwdev | 既にある（部品のロード、診断の内訳は新しい） | image/runtime/debug_probe、kdebug/hwdev・ロード失敗診断。`build/sources/common.mk`、`build/select.mk` | SQLite 静的など。製品 module/kdebug/hwdev target が拒否、診断は PPC 内蔵。`build/targets/ppc-powermac.mk`、`platform/powermac/ppc_kernel.c` | `5d672444d` / `cb94edd22` | 450–1200 行、10–22 ファイル | 部品の版・ロード失敗・欠損と diag の照合、PPC 静的 SQLite の回帰 |
| C05 | 自己試験・障害検出の対象 | 新しい | kaudit、kselftest、kdebug と gfx map selftest を段ごとに実行。`platform/pc98/kernel.c`、`platform/pc98/kernel.c`、`build/sources/common.mk` | PPC 固有 DSI/ISI、IPI、DEC/TB と exec selftest。x86 の汎用レポート一式は pending。`platform/powermac/ppc_kernel.c`、`platform/powermac/ppc_kernel.c`、`arch/ppc/exec_selftest.c` | `2d51bd11a` / `667b404df` | 330–820 行、8–16 ファイル | 共通メモリ/exec/表示の smoke と fail 集約、CPU probe は固有のまま |
| C06 | KAPI 提供者の未接続 | 既にある（KAPI の不在） | 製品側生成表、GUI・IME・sound・IDE・ext2・gfx2 など提供。`build/sources/common.mk`、`build/sources/hooks.mk` | EXEC 像に PPC ABI と生成表あり。slot ごとの NULL/0/NOSYS、診断像は生成表無し。PPC SQLite・mem_stat/ver/time と link-sh の SFS は実体あり。`sdk/kapi_target_ppc.py`、`sdk/kapi_target_ppc.py`、`build/targets/ppc-powermac.mk` | `5d672444d` / `bc840ee3d` | 250–650 行、7–13 ファイル | slot と能力・返値の照合。B/C の各機能を接続する変更量は別 |
| C07 | ランタイム設定 system.cfg | 新しい | 設定テーブル、GUI/GFX・boot の読み込み。`init/sysconfig.c`、`platform/pc98/kernel_gfx_config.inc`、`platform/pc98/kernel.c` | root があっても sysconfig は pending、起動像の flags、provider の既定。`build/sources/common.mk`、`platform/powermac/display_provider.c`、`build/images/ppc-flags.mk` | `a238ab174` / `5d672444d` | 180–480 行、5–10 ファイル | 設定あり／なし／壊れた値、root RO/RW・優先順位と起動した provider |
| C08 | 汎用 IRQ 登録・IRQ 統計の製品機能 | 新しい | irq と irq_math、PIC completion、IRQ topology 診断。`platform/pc98/boot_irq.c`、`kernel/irq/irq.c` | MPIC controller と arch trap、共通 tick。汎用 irq.c は compile-only。`platform/powermac/mpic.c`、`arch/ppc/kernel_trap.c`、`build/sources/common.mk` | `21b7c5952` / `7f5ab6fd9` | 160–430 行、4–9 ファイル | 共通の登録・解除・EOI 順・tick を照合、PIC/MPIC route は固有 |
| C09 | 常駐カーネル CLI | 既にある（起動の文脈） | 通常起動に resident CLI なし。外部 shell loop。`build/images/pc98.mk`、`platform/pc98/kernel.c` | kboot-shell のみ CLI。kboot-sh/link-sh は別の USER sh、既定の link 診断像はシェル無し。`build/images/kboot-shell.mk`、`platform/powermac/cli_boot.c`、`kernel/cli/resident_cli.c` | `67a629ffa` / `67a629ffa` | 120–360 行、4–8 ファイル | kboot-shell 台本で resident command、USER sh に移す場合は各像の起動とコマンド再確認 |
| C10 | boot.log 保存とローテーション | 新しい | root 対応 FS で boot.log と前回ログを保存。`init/bootlog_save.c`、`platform/pc98/kernel.c` | stream/ESCC/LAN 診断。bootlog_save は exec 像の選択外。`build/sources/common.mk`、`platform/powermac/console_log.c` | `a238ab174` / `5d672444d` | 110–300 行、4–7 ファイル | test_bootlog、書込不能 root・保存先の扱い、完了前後の境界 |
| C11 | 音声サービス・buzzer | 新しい（KAPI の下） | FM/PCM、snd hook・実体のブザー。`platform/pc98/system.c`、`build/select.mk`、`platform/pc98/kernel.c` | FM/PCM hook と buzzer は absent。物理デバイスとの差は A。`exec/absent/exec_snd.c`、`sdk/kapi_target_ppc.py`、`sdk/kapi_target_ppc.py` | `b6fab4ee8` / `74e57f8a2` | 90–280 行、4–8 ファイル | 能力照会・出力無し／あり、アプリ終了時の sound focus・持続 |
| C12 | シェル特権とアプリからの実行 | 既にある（特権） | 製品 shell/gshell が L1、通常の子は USER になりうる。`platform/pc98/kernel.c`、`exec/shell_service.h` | USER sh が共有 exec の子。PPC の L1 入口の実装と起動としての可用性は別。`userland/ppc/sh/profile.h`、`platform/powermac/shell_boot.c`、`arch/ppc/exec_entry.c` | `3af7d5b84` / `e00a04a54` | 85–250 行、4–9 ファイル | NP21/W shell level・QEMU USER sh の同期子と分離、共有の親子・終了契約 |
| C13 | 緊急回復／R1・描画復帰 | 新しい | 製品 recovery と gfx fallback・VRAM 診断を提供。`platform/pc98/display.c`、`build/sources/hooks.mk` | R1 hook は absent。OF の表示復旧手続きと、機種側診断は別。`exec/absent/exec_r1.c`、`platform/powermac/display_provider.c` | `12cb68e83` / `74e57f8a2` | 80–240 行、4–8 ファイル | R1 recovery の状態表・復帰可否、OF console の再接続 |
| C14 | PPC の OF/実機観測と診断専用像 | 新しい | 製品の起動診断、x86 固有 fault fixture。`platform/pc98/boot_diag.c`、`build/arch/x86-sources.mk` | OF detach・実機 RH 観測、raw PLN1/link 診断 slice。物理 probe の違いから起動モードの差を抽出。`platform/powermac/rh_observe.c`、`platform/powermac/ppc_kernel.c`、`build/images/kboot-link.mk` | `21b7c5952` / `54eec0f31` | 70–220 行、4–8 ファイル | PPC diag の KBOOT/MPIC/ALIVE/RH と、製品の共通起動 smoke |
| C15 | PPC USER の診断バイナリ群 | 新しい | 製品の userland/tests・各コマンド。mem_stat_test は共有。`userland/tests/mem_stat_test.c`、`build/targets/x86-pc98.mk` | PPC の root に probes を個別リンクする。等価機能のテスト本体の重複を共通化可能。`build/ppc-user.mk`、`build/ppc-user.mk`、`userland/ppc/gfx2prob.c` | `69a3b40c2` / `49b7e7207` | 65–200 行、6–12 ファイル | 同じフィールド・意味は共有、ISO の 8.3・ゲストログと実行 runner は各像 |

## 比較基準の訂正と来歴

- 起動実体は `kboot-link` の診断、`kboot-shell` の常駐 CLI、`kboot-sh` と `kboot-link-sh` の USER シェルの三形態。二つの USER シェルは同じ `userland/shell/*.c` を使用する。既知の「KAPI の不在」は像全体の不在と、EXEC 像内のスロット別不在に分ける。 `build/images/kboot-link.mk`、`build/images/kboot-shell.mk`、`build/images/kboot-sh.mk`、`build/images/kboot-link-sh.mk`、`build/ppc-user.mk`、`sdk/gen_kapi.py`
- `sys_switch_shell` の差を CPU マクロによる権限判定とする記述は訂正が必要。i386 分岐は ShellService のサイズ検査、x86 の切替許可は実体の owner 判定、PPC の未提供判定は生成側の provider group。 `exec/shell_service.h`、`kernel/gui.c`、`sdk/kapi_target_ppc.py`
- PPC の exec boot は B30/B31 の `900d4674d`、USER sh の常駐 loop は S-C P04 の `9543775ee`、描画の owner と世代の門は 5b-A3/A4 の `73192b894`。link-sh の SFS 提供と LAN wrapper は P5/P5b の `2964a2003`、cwd/終了値の追従は `2f4ed7d73`。 `platform/powermac/exec_boot.c`、`platform/powermac/shell_boot.c`、`platform/powermac/display_provider.c`、`userland/shell/rshell_lan.c`
- PC-98 の NIC は製品 LAN モジュールの i82557 系と、起動時 legacy 候補の NE2000 系を区別する。tick budget は x86 の TSC/PIT と PPC の TB で内部単位が異なり、共通側は同じ provider の `elapsed` と `cnt` を比較する。 `build/features/lan.mk`、`build/features/pc98-devices.mk`、`platform/pc98/tick_budget.c`、`platform/powermac/tick_budget.c`、`kernel/irq/tick_budget.c`

## 受入コードの対応

| 対象 | x86 の観測 | PPC の観測 | 共通に照合する契約 |
|---|---|---|---|
| シェル | `tools/tests/test_kernel_shell.py` の GUI 選択・復帰 | `tools/tests/ppc_sh_boot.py` と `tools/ppc/link_sh_run.py` の起動・link シェル | 終了／fault／ロード失敗の区別と再起動。初回接続待ちは像の条件にする |
| ver・メモリ | `tools/tests/test_ver_accept.py`、`userland/tests/mem_stat_test.c` | `tools/accept/ppc_ver.py`、同一 memstat の `userland/tests/mem_stat_test.c` | 版・build とメモリ指標。装置、CPU、採取可能なメモリ指標を別に期待 |
| rshell・link | `tools/tests/test_rshell_serial.py`、`tools/tests/test_link_platform.py` | `tools/tests/test_ppc_rshell_link.py`、`tools/tests/test_ppc_link_platform.py` | command/status と link の進行。Serial ACK・LAN epoch の観測経路は個別 |
| SFS | `tools/tests/test_serialfs.py` と `userland/shell/rshell.c` | `tools/accept/ppc_sfs.py` | cwd 復帰、子の exit、回収と再接続。PPC は RSH OUT と行境界を追加で観測 |
| SQLite | `tools/tests/test_sqlite_product_sql.py` | `tools/accept/ppc_sqlite_static.py`、`tools/accept/ppc_sqlite_stack.py` | DB と VFS の契約、PPC 固有の FP/stack 境界、x86 のモジュール供給 |
| 表示 | `tools/tests/test_ver_gfx.py` と `tools/tests/test_recover_gf.py` | `tools/accept/ppc_display.py`、`tools/accept/ppc_gfx2.py` | 表示 lease・初期化／終了・選択中 mode、装置ごとの recover と出力 |
| 時計・起動 | `tools/tests/test_tick_clock.py`、`tools/tests/test_tick_irq.py`、`tools/tests/test_bootlog.py` | `tools/ppc/kboot_accept.py`、`tools/accept/ppc_rtc.py` | logical tick・µs の進行と起動段、PIT/TSC・DEC/TB の誤差は別 |
| USER probe | `userland/tests/` の製品テスト | `build/ppc-user.mk` の hello/fault/nest（起動・子）、outprobe/nlprobe/celltest/fontprob/kcgprob（文字）、shmprobe/surfprob/gfx2prob/dispprb/mousprob（資源・表示・入力）、infoprob/sysiprob/verprob/pciprob/bootprob/portprob/rtcprob（情報・装置）、pathprob/loopprb/dbprob/sfsprob（パス・媒体・DB・SFS）。FP は受入専用の `build/ppc-user.mk` | 個別 probe の同名ファイルを探すより、検証する KAPI と観測値を共有。memstat は既に同じソース |

## ソース選択の照合台帳

`build/select.mk`–`262` の機構で共通集合・CPU・機種・機能を合成する。legacy の順序表は `build/sources/order.mk`、kboot は集合順。以下は宣言行に明記されるソース名の索引。変数名は宣言元、wildcard は式とその条件、製品と診断の選択は像の値を参照。補助ターゲットでコンパイルするソースと製品にリンクするソースを区別した。

| 選択行 | ソースまたは式 |
|---|---|
| `build/sources/common.mk` | `C_COMMON_CORE = init/kernel_start.c kernel/boot_token.c mm/paging.c mm/physmem.c mm/pgalloc.c mm/memory_boot.c mm/shm.c mm/kmalloc.c kernel/sys.c kernel/idle_work.c kernel/io_work.c kernel/io_policy.c kernel/irq/tick_budget.c kernel/irq/tick.c kernel/time/tick.c fs/fd_redirect.c lib/kprintf.c lib/divdi3.c lib/kstring_c.c net/nic.c` |
| `build/sources/common.mk` | `C_COMMON_LINK = kernel/dev/device_host.c net/link.c net/link_notify.c net/host_mailbox.c net/nic_rx.c net/rx_work.c` |
| `build/sources/common.mk` | `C_COMMON_EXEC = kernel/ver_collect.c kernel/ver_format.c kernel/dev/device_fs.c kernel/time/ktime.c kernel/time/time_math.c init/bootinfo.c kernel/console/font_guard.c kernel/time/cpu_calibrate_math.c mm/paging_app.c input/hal/in2_dev.c input/hal/in2_state.c input/hal/mouse_ext.c input/hal/pointer_state.c input/hal/mouse_pointer.c input/hal/kbd_merge.c input/hal/legacy_keymap.c input/hal/keymap_registry.c input/hal/keymap_eval.c input/hal/keymap_state.c input/hal/in2_ring.c input/hal/in2_time.c kernel/input_legacy.c kernel/input_wait.c kernel/input_service.c kernel/time/clock_service.c kernel/console/console.c kernel/console/console_dual.c kernel/console/con_sink.c init/bootlog.c kernel/kbd_inject.c kernel/io_kapi.c kernel/console/font_source.c kernel/console/font_source_kcg.c lib/lz4.c kernel/console/unicode_file.c gfx/gfx_core.c gfx/gfx_dirty.c gfx/gfx_present.c gfx/gfx_policy.c gfx/gfx_scroll.c gfx/palette.c fs/vfs.c fs/vfs_fd.c fs/pipe_buffer.c exec/exec.c exec/shell_service.c exec/appmem.c exec/appmem_map.c exec/appmem_unmap.c exec/ring3_ls.c exec/redir_access.c exec/access_walk.c exec/surface_query.c exec/system_surface.c exec/lease.c exec/exec_heap.c exec/appslot.c exec/launch.c exec/ring3_str.c exec/os32x_hdr.c kapi/kapi_print.c kapi/kapi_strings.c kapi/kapi_sysinfo.c kapi/kapi_port.c kapi/kapi_host.c kapi/kapi_host_notice.c lib/path.c lib/utf8.c lib/kprintf_attr.c lib/os_time.c lib/kstring.c $(filter-out gfx/v2/a0_weak.c gfx/sync/a0_weak.c,$(wildcard gfx/v2/*.c gfx/sync/*.c)) drivers/base/dev.c kernel/dev/dev.c input/hal/legacy_ring.c input/hal/legacy_mode.c kernel/port_service.c kernel/serial_diag.c lib/crc32.c gfx/gfx_emergency.c` |
| `build/sources/common.mk` | `C_COMMON_USERSHELL = fs/serialfs/sfs_proto.c fs/serialfs/sfs_client.c fs/serialfs/serialfs.c fs/serialfs/serialfs_session.c fs/serialfs/serialfs_persistent.c` |
| `build/sources/common.mk` | `C_COMMON_USB = $(wildcard drivers/usb/usb_*.c drivers/usb/ohci_*.c drivers/usb/hid_*.c) drivers/usb/ehci_quiesce.c` |
| `build/sources/common.mk` | `C_COMMON_CLI = kernel/cli/resident_cli.c` |
| `build/sources/common.mk` | `C_COMMON_PC98_PENDING = mm/dma_pool.c mm/dma_pool_math.c mm/shlib.c kernel/gui.c init/bootlog_save.c init/sysconfig.c kernel/kaudit.c init/kaudit_boot.c kernel/kselftest_report.c kernel/irq/irq.c kernel/irq/irq_math.c kernel/module/module_image.c kernel/module/module_runtime.c kernel/module/debug_probe.c lib/dma_contract.c gfx/gfx_hw_submit.c gfx/v2/a0_weak.c gfx/sync/a0_weak.c gfx/gfx_hw_mono_cpu.c gfx/gfx_osri.c gfx/gfx_osri_view.c fs/fat/fatfs/ff.c fs/fat/fatfs_vfs.c fs/ext2/ext2_super.c fs/ext2/ext2_inode.c fs/ext2/ext2_dir.c fs/ext2/ext2_file.c fs/ext2/ext2_vfs.c fs/serialfs/sfs_session_fg.c kapi/kapi_generated.c kapi/kapi_db.c kapi/kapi_sys.c kapi/kapi_osri.c lib/kutf16.c lib/kmath.c` |
| `build/sources/common.mk` | `C_COMMON_EXEC += drivers/pci/pci.c drivers/pci/pci_barsize.c drivers/pci/pci_decode.c drivers/pci/pci_bind.c drivers/pci/pci_bind_match.c kernel/dev/device_pci.c` |
| `build/sources/common.mk` | `C_EXT2_FORMATTER := fs/ext2/ext2_fmt.c fs/ext2/ext2_layout.c fs/ext2/ext2_time.c` |
| `build/sources/common.mk` | `C_COMMON_PC98_PENDING += $(C_EXT2_FORMATTER)` |
| `build/sources/common.mk` | `C_COMMON_COMPILE_ONLY_PPC := kernel/sqlite_engine.c $(C_EXT2_FORMATTER)` |
| `build/sources/common.mk` | `C_COMMON_LOOP = kernel/dev/device_loop.c drivers/block/loop_dev.c` |
| `build/sources/common.mk` | `C_COMMON_LOOP += fs/ext2/ext2_medium_loop.c` |
| `build/sources/common.mk` | `C_COMMON = $(C_COMMON_CORE) $(foreach f,$(IMAGE_FEATURES),$(if $(filter 1,$(FEATURE_$(f))),$(C_COMMON_$(f))))` |
| `build/sources/hooks.mk` | `C_COMMON_HOOKS_ABSENT = $(foreach h,$(EXEC_HOOKS),$(if $(filter 0,$(HOOK_$(h))),exec/absent/exec_$(h).c))` |
| `build/arch/ppc-sources.mk` | `GAS_ARCH_CORE = arch/ppc/kernel_entry.S arch/ppc/entry.S arch/ppc/kernel_takeover.S arch/ppc/kernel_vectors.S arch/ppc/kernel_mmu.S arch/ppc/kernel_probe.S arch/ppc/mmu.S arch/ppc/cache_copy.S arch/ppc/fp_context.S` |
| `build/arch/ppc-sources.mk` | `GAS_ARCH_EXEC = arch/ppc/exec_resume.S arch/ppc/exec_enter.S arch/ppc/setjmp.S` |
| `build/arch/ppc-sources.mk` | `C_ARCH_CORE = platform/powermac/kernel_boot.c arch/ppc/kernel_cpu.c arch/ppc/kernel_hpt.c arch/ppc/kernel_trap.c arch/ppc/kernel_dec.c arch/ppc/pd_walk.c arch/ppc/dma_cache.c arch/ppc/kernel_fp.c` |
| `build/arch/ppc-sources.mk` | `C_ARCH_EXEC = arch/ppc/exec_cache.c arch/ppc/exec_context.c arch/ppc/exec_entry.c arch/ppc/exec_kapi.c arch/ppc/exec_selftest.c arch/ppc/syscall_code.c arch/ppc/kapi_call.c kernel/time/cpu_calibrate.c` |
| `build/arch/ppc-sources.mk` | `C_ARCH_NOEXEC = arch/ppc/kboot_unavailable.c` |
| `build/arch/x86-sources.mk` | `C_ARCH += arch/x86/kernel/boot_cpu.c arch/x86/kernel/boot_after_paging.c arch/x86/kernel/fault_fixture.c arch/x86/kernel/mtrr_wc.c arch/x86/kernel/video_wc.c kernel/kstack_hw.c arch/x86/kernel/gdt.c arch/x86/kernel/double_fault.c arch/x86/kernel/tss.c arch/x86/kernel/idt.c arch/x86/kernel/isr_handlers.c kernel/time/cpu_calibrate.c platform/pc98/sysclk.c` |
| `build/arch/x86-sources.mk` | `ASM_ARCH += arch/x86/kernel/kentry.asm arch/x86/kernel/isr_stub.asm arch/x86/kernel/ring3_entry.asm arch/x86/kernel/setjmp.asm lib/kstring_asm.asm` |
| `build/arch/x86-sources.mk` | `ASM_EXTRA_OBJ = arch/x86/kernel/ring3_ls_shim.o` |
| `build/arch/x86-sources.mk` | `C_ARCH += arch/x86/kernel/exec_entry.c` |
| `build/arch/x86-sources.mk` | `C_ARCH += arch/x86/kernel/exec_context.c` |
| `build/arch/x86-sources.mk` | `C_ARCH_NEW += arch/x86/kernel/exec_entry.c` |
| `build/arch/x86-sources.mk` | `C_ARCH_NEW += arch/x86/kernel/exec_context.c` |
| `build/arch/x86-sources.mk` | `C_ARCH += arch/x86/kernel/kapi_call.c arch/x86/kernel/syscall_entry.c` |
| `build/arch/x86-sources.mk` | `C_ARCH_NEW += arch/x86/kernel/kapi_call.c arch/x86/kernel/syscall_entry.c` |
| `build/arch/x86-sources.mk` | `C_ARCH += arch/x86/kernel/syscall_code.c` |
| `build/platform/pc98.mk` | `C_PLATFORM += platform/pc98/kernel.c platform/pc98/boot_token_pc98.c input/keymaps/pc98_jp.c input/keymaps/hid_jp.c platform/pc98/input_keymap.c platform/pc98/boot_irq.c platform/pc98/boot_platform.c platform/pc98/boot_display.c platform/pc98/emergency.c platform/pc98/boot.c platform/pc98/boot_splash.c platform/pc98/splash.c platform/pc98/splash_image.c platform/pc98/clock.c platform/pc98/timer.c platform/pc98/lan_match.c platform/pc98/device_ide.c platform/pc98/device_host.c kernel/dev/device_kcg.c platform/pc98/pci_config.c platform/pc98/pci_resources.c platform/pc98/boot_disk.c platform/pc98/memory.c platform/pc98/d2_bind.c platform/pc98/boot_names.c platform/pc98/console.c platform/pc98/display.c platform/pc98/display_dual.c platform/pc98/gfx_scroll.c platform/pc98/gfx_init.c platform/pc98/gfx_selector.c platform/pc98/gfx_surfaces.c platform/pc98/display_bundle.c platform/pc98/link_platform.c platform/pc98/system.c platform/pc98/pic.c platform/pc98/irq_policy.c platform/pc98/irq_complete.c platform/pc98/boot_diag.c platform/pc98/ver_backend.c platform/pc98/console_attr.c` |
| `build/platform/pc98.mk` | `C_PLATFORM += platform/pc98/tsc_math.c platform/pc98/tsc_clock.c` |
| `build/platform/pc98.mk` | `C_PLATFORM += platform/pc98/pit.c platform/pc98/tick_budget.c platform/pc98/pit_math.c platform/pc98/pit_time.c platform/pc98/clock_source.c` |
| `build/platform/pc98.mk` | `C_PLATFORM += platform/pc98/bootinfo_capture.c platform/pc98/bootinfo_geom.c platform/pc98/bootinfo_report.c platform/pc98/bootinfo_check.c platform/pc98/bios_snapshot.c platform/pc98/boot_capture.c` |
| `build/platform/pc98.mk` | `C_PLATFORM += fs/ext2/ext2_medium_ide.c` |
| `build/platform/powermac.mk` | `C_PLATFORM_CORE = platform/powermac/rh_observe.c platform/powermac/ofw.c platform/powermac/escc.c platform/powermac/escc_tree.c platform/powermac/kboot_capture.c platform/powermac/via_tree.c platform/powermac/system.c drivers/macintosh/via.c platform/powermac/boot_token_powermac.c platform/powermac/gmac_tree.c platform/powermac/gmac_clock.c platform/powermac/kboot_reserve.c platform/powermac/memory.c platform/powermac/mpic_tree.c platform/powermac/mpic.c platform/powermac/clock.c platform/powermac/tick_budget.c platform/powermac/log.c platform/powermac/pci_config.c platform/powermac/gmac_dma.c platform/powermac/lan_gem_io.c platform/powermac/lan_log.c platform/powermac/lan_monitor.c platform/powermac/lan_diag.c platform/powermac/lan_service.c platform/powermac/ppc_kernel.c drivers/net/gem/gem_reset.c drivers/net/gem/gem_phy.c drivers/net/gem/gem_ring.c drivers/net/gem/gem_mac.c drivers/net/gem/gem_tx.c drivers/net/gem/gem_rx.c` |
| `build/platform/powermac.mk` | `C_PLATFORM_LINK = platform/powermac/link_platform.c platform/powermac/nic_gem.c platform/powermac/link_log.c platform/powermac/link_diag.c` |
| `build/platform/powermac.mk` | `C_PLATFORM_NOLINK = platform/powermac/lan_wire.c platform/powermac/lan_terminal.c platform/powermac/terminal_input.c platform/powermac/host_unavailable.c` |
| `build/platform/powermac.mk` | `C_PLATFORM_EXEC = platform/powermac/ver_backend.c platform/powermac/pci_backend.c platform/powermac/root_boot.c platform/powermac/root_image.c platform/powermac/console_stream.c platform/powermac/console_log.c platform/powermac/log_printf.c platform/powermac/exec_boot.c platform/powermac/console.c platform/powermac/console_text.c platform/powermac/display.c platform/powermac/display_provider.c platform/powermac/display_bundle.c platform/powermac/display_dual.c fs/isofs/iso9660.c fs/isofs/iso9660_media.c kernel/dev/ram_ro.c init/boot_root.c fs/ramfs/ramfs.c kernel/console/fbcon.c platform/powermac/kcg.c drivers/video/of_fb/backend_of_fb.c drivers/video/of_fb/of_fb_v2.c gfx/gfx_console.c` |
| `build/platform/powermac.mk` | `C_PLATFORM_NOEXEC = platform/powermac/pci_unavailable.c platform/powermac/kboot_console.c` |
| `build/platform/powermac.mk` | `C_PLATFORM_USERSHELL = platform/powermac/shell_boot.c platform/powermac/input_boot.c platform/powermac/input_decode.c platform/powermac/hostfs_boot.c input/keymaps/hid_jp.c $(if $(filter 1,$(FEATURE_LINK)),platform/powermac/hostfs_link_io.c platform/powermac/hostfs_link_peer.c,platform/powermac/hostfs_io.c platform/powermac/hostfs_peer.c)` |
| `build/platform/powermac.mk` | `C_PLATFORM_USB = platform/powermac/input_usb.c platform/powermac/input_keymap.c platform/powermac/usb_tree.c platform/powermac/usb_platform.c platform/powermac/usb_boot.c` |
| `build/platform/powermac.mk` | `C_PLATFORM_LOOP = platform/powermac/loop_boot.c` |
| `build/platform/powermac.mk` | `C_PLATFORM_CLI = platform/powermac/cli_boot.c` |
| `build/features/cd.mk` | `C_FEATURES += drivers/ata/cd_module.c` |
| `build/features/cd.mk` | `MODULE_MK_cd = build/cd-module.mk` |
| `build/features/cirrus.mk` | `C_FEATURES += drivers/video/cirrus/cirrus_module.c drivers/video/cirrus/cirrus_identify.c` |
| `build/features/cirrus.mk` | `MODULE_MK_cirrus = build/cirrus-module.mk` |
| `build/features/fep.mk` | `C_FEATURES += kernel/ime/fep_module.c` |
| `build/features/fep.mk` | `MODULE_MK_fep = build/fep-module.mk` |
| `build/features/fm.mk` | `C_FEATURES += drivers/sound/fm/fm_module.c` |
| `build/features/fm.mk` | `MODULE_MK_fm = build/fm-module.mk` |
| `build/features/geforce.mk` | `C_FEATURES += kdebug/gf5_test.c kdebug/gf_display_test.c drivers/video/geforce/edid.c drivers/video/geforce/tv_data.c drivers/video/geforce/gf_display_data.c drivers/video/geforce/geforce_module.c drivers/video/geforce/geforce_platform.c drivers/video/geforce/gf_wc.c` |
| `build/features/geforce.mk` | `MODULE_MK_geforce = build/geforce-module.mk` |
| `build/features/hwdev.mk` | `C_FEATURES += hwdev/hwdev_module.c` |
| `build/features/hwdev.mk` | `MODULE_MK_hwdev = build/hwdev.mk` |
| `build/features/kcg.mk` | `C_FEATURES += drivers/video/kcg/kcg.c` |
| `build/features/kcg.mk` | `C_FEATURES += drivers/video/kcg/kcg_module.c` |
| `build/features/kcg.mk` | `MODULE_MK_kcg = build/kcg-module.mk` |
| `build/features/kcg.mk` | `C_FEATURES += kernel/dev/device_kcg.c` |
| `build/features/kdebug.mk` | `C_FEATURES += kernel/module/kdebug_call.c kdebug/kdebug_module.c` |
| `build/features/kdebug.mk` | `MODULE_MK_kdebug = build/kdebug.mk` |
| `build/features/lan.mk` | `C_FEATURES += drivers/net/e82557/lan_module.c` |
| `build/features/lan.mk` | `MODULE_MK_lan = build/lan-module.mk` |
| `build/features/pc98-devices.mk` | `C_FEATURES += drivers/video/pc98/gfx_vram.c drivers/block/fat_diskio.c` |
| `build/features/pc98-devices.mk` | `C_FEATURES += drivers/hostdrv/hostdrvfs.c` |
| `build/features/pc98-devices.mk` | `C_FEATURES += drivers/input/kbd.c drivers/input/kbd_hal.c drivers/input/kbd_status.c drivers/input/kbd_dlog.c drivers/char/serial.c drivers/char/serial_plan.c drivers/block/fdc.c drivers/block/fdc_decide.c drivers/block/fdc_track.c drivers/dma/dma8237.c drivers/dma/dma8237_math.c drivers/pci/pci_hwb.c drivers/ata/ide.c drivers/ata/ide_addr.c fs/pc98pt.c drivers/char/rtc.c drivers/char/np2sysp.c drivers/input/mouse.c drivers/input/mouse_bus.c drivers/input/mouse_hal.c drivers/input/mouse_seamless.c drivers/net/ne2000/lgy98.c drivers/net/ne2000/ne2000.c drivers/net/ne2000/ne2000_ring.c drivers/video/pc98/pc98_route.c drivers/net/ne2000/nic_lgy98.c drivers/video/pc98/backend_pc98.c drivers/video/pc98/backend_pc98_osri.c drivers/video/pc98/backend_pegc.c drivers/video/pc98/pegc_v2.c drivers/video/pc98/splash_raster.c drivers/video/pc98/splash_raster_image.c drivers/video/pc98/splash_raster_plan.c drivers/video/pc98/splash_raster_play.c drivers/video/pc98/splash_raster_run.c` |
| `build/features/pc98-devices.mk` | `ASM_FEATURES += drivers/net/ne2000/ne2000_io.asm` |
| `build/features/pcm.mk` | `C_FEATURES += drivers/sound/cs4231/pcm_module.c` |
| `build/features/pcm.mk` | `MODULE_MK_pcm = build/pcm-module.mk` |
| `build/features/sqlite-static.mk` | `C_FEATURES += kernel/sqlite_static.c kapi/kapi_db.c lib/shift_di.c lib/softfloat_bits.c $(SOFTFLOAT_RESIDENT_SOURCES)` |
| `build/features/sqlite.mk` | `C_FEATURES += kernel/sqlite_engine.c kernel/sqlite_outer.c` |
| `build/features/sqlite.mk` | `GAS_ARCH_EXEC += arch/ppc/sqlite_stack_ppc.S` |
| `build/features/sqlite.mk` | `C_FEATURES += lib/sqlite3/os32_sqlite_test.c` |
| `build/features/sqlite.mk` | `ASM_FEATURES += lib/sqlite3/sqlite_stack.asm` |
| `build/features/sqlite.mk` | `MODULE_MK_sqlite = build/sqlite-proto.mk` |
| `build/features/trident.mk` | `C_FEATURES += drivers/video/trident/trident_module.c drivers/video/trident/resources.c` |
| `build/features/trident.mk` | `MODULE_MK_trident = build/trident-module.mk` |
| `build/features/v86.mk` | `C_FEATURES += platform/pc98/legacy/v86.c platform/pc98/legacy/v86_mem.c platform/pc98/legacy/v86_module.c platform/pc98/legacy/v86_gcap.c platform/pc98/legacy/v86_call.c platform/pc98/legacy/guest.c platform/pc98/legacy/lowmap.c` |
| `build/features/v86.mk` | `ASM_FEATURES += platform/pc98/legacy/v86_entry.asm` |
| `build/features/v86.mk` | `MODULE_MK_v86 = build/v86-module.mk` |
| `build/features/x87.mk` | `X87_EMU_SOURCES := arch/x86/kernel/x87_emu_decode.c \` |
| `build/features/x87.mk` | `C_FEATURES += arch/x86/kernel/x87.c $(X87_EMU_SOURCES) $(filter-out $(SOFTFLOAT_RESIDENT_SOURCES),$(SOFTFLOAT_X87_SOURCES))` |

### 像・共通コードの条件分岐

| 条件 | 影響・根拠 |
|---|---|
| 5 像の機能表と legacy | `build/images/pc98.mk`、`build/images/kboot-core.mk`、`build/images/kboot-link.mk`、`build/images/kboot-shell.mk`、`build/images/kboot-sh.mk`、`build/images/kboot-link-sh.mk`。PPC flags と名前の検査は `build/images/ppc-flags.mk`。 |
| static SQLite と特別ビルド | `kernel/sqlite_outer.c`、`build/targets/ppc-powermac.mk`。hard-float 専用 group、stack/restore、生成 KAPI と ROM/RAM root の依存を target が追加。x86 側は `build/targets/x86-pc98.mk` と `build/features/sqlite.mk`。 |
| pending の機能フック | `build/sources/hooks.mk`。gfx/surface/font lease は EXEC、DB は PC98_PENDING または SQLITE、他は PC98_PENDING が既定。absent provider がプレースホルダーを供給。 `exec/exec_hooks.h`、`exec/absent/exec_gui.c` |
| SDK 生成のターゲット枝 | PPC の typed invocation、sys_halt wait、tvram_clear、機能なし／loop／DB と link SFS の選択。 `sdk/gen_kapi.py`、`sdk/gen_kapi.py`、`sdk/gen_kapi.py` |
| 共通部の CPU マクロ | 自前 C/H の顕著な直接分岐は cdecl・サイズ assert・memstat の試験分岐。vendor SQLite のコンパイラ別最適化も存在。 `include/callconv.h`、`exec/shell_service.h`、`userland/tests/mem_stat_test.c`、`lib/sqlite3/sqlite3.c` |
| 共通シェルの profile | PPC の `SHELL_AS_APP`、`SH_LAUNCH_SYNC`、最小 base/sys、コマンド選択、path/home、link のリモート SFS。機能枝は main、cmd_base/dir/env/file/hdprep/mnt/pci/peek/script/sys/tri、rshell/rshell_lan、serial_watchdog、ui に及ぶ。 `userland/ppc/sh/profile.h`、`userland/ppc/sh/link_profile.h`、`userland/shell/main.c`、`userland/shell/cmd_sys.c`、`userland/shell/rshell_lan.c` |
| 同名ファイルの扱い | `boot.c` は PC-98 の boot phase 登録と PPC の OF 用入口で役目が異なる。PPC 製品像の入口は kernel_boot。PPC の boot_capture と ime_display、旧 shell_putchar は platform 選択リストに現れず、有効な provider 同士の比較から除外。 `platform/pc98/boot.c`、`platform/powermac/boot.c`、`build/arch/ppc-sources.mk`、`build/platform/powermac.mk` |
