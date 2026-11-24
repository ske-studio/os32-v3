A 4件 / B 3件 / C 2件

対象は OS32 `3497386e7`。起動の三段階、シェルの再起動ループ、KAPI 表の生成、tick の時刻計算はすでに共通化されています。一方、**各機種の起動関数が、共通機能をいつ呼ぶかをそれぞれ決めています**。PC-98 は root をマウントしてからページングと共有メモリを初期化し、PowerMac はページングと共有メモリを整えてから root をマウントします。共通の骨格を厚くするには、現行の呼び順をそのまま移す前に、前提条件を明示する必要があります。

## 確認した所見

| 重さ・領域 | 証拠と現状 | 共通に寄せる順と、機種・CPU 側に残す処理 |
|---|---|---|
| **A1 起動の順序** | `init/kernel_start.c:kernel_start` が決めるのは `platform_boot_machine → _enable → platform_boot_services → platform_boot_run`。PC-98 は `platform/pc98/kernel.c:platform_boot_machine` 内で入力・FS 登録・IDE・root を進め、その後 `kernel_boot_memory` に入る。PowerMac は `platform/powermac/kernel_boot.c:ppc_kernel_real_start` でページングまで済ませて `kernel_start` に入り、`ppc_kernel.c:platform_boot_machine` で時計・DEC・MPIC を準備し、サービス段で root・表示・入力を立ち上げる。PowerMac の同じサービス段は `KBOOT_SHELL`、`KBOOT_SH`、`PM_PRODUCT_BOOT` で経路が変わる。 | `init/` が、割り込み有効化前の準備、root を提供する装置と FS の登録、root 確定後のサービス、シェル開始の依存を持つ。早期コンソールやルート装置探査には必要な先行段を置く。BIOS・Open Firmware 引継ぎ、初期写像、CPU 例外、PIC・MPIC の設定は各所有層に置く。 |
| **A2 記憶装置・FS・root** | PC-98 は `platform/pc98/boot.c:pc98_boot_dev_fs` で ext2・FAT 等とパスを初期化し、`pc98_boot_disk` で IDE を初期化して HDD を登録。`kernel.c:platform_boot_machine` が選択済み root をマウントする。PowerMac は `platform/powermac/ide_boot.c:pm_ide_boot/pm_root_select`、`root_boot.c:pm_root_boot` で HDD の ext2 準備・マウント・パス確定を実施。RAM 像は `init/boot_root.c:boot_root_start_registered` で `rd0` の ISO9660 root と `/tmp` を立ち上げる。root 設定と子マウントは両機種で同じ共通関数を呼ぶが、呼び元と順序の管理が分かれている。 | root 候補が決まった後の、必要な FS 登録、マウント、既定デバイス確定、root 設定と子マウントを共通で組む。ATA のプローブ本体 `ide_init` は共有済み。PC-98 の BIOS 幾何・区画、PowerMac の APM・OF 用 HFS、ATA ポート／MMIO と語のバイト順、RAM 像の配置と解放は各所有層に残す。 |
| **A3 表示・コンソールの立ち上げと復帰** | `gfx/gfx_core.c:gfx_select_and_init_backend/gfx_prepare_backend` には、面の準備・モード選択・クライアント結線・失敗時のフォールバックに重複する手順がある。`platform/pc98/kernel.c` は起動側で `gfx_boot_reserve` と `gfx_prepare_backend` を呼ぶ。PowerMac は `platform/powermac/console_text.c:pm_console_text_start` がフォント・Unicode を確認して共通の `gfx_boot_reserve → gfx_init` を呼び戻す。さらに、共通の終了側から呼ばれる `display_provider_console_return` が、PPC 側で再び `gfx_init` を呼ぶ。 | 共通側で表示面の準備、端末への引き渡し、GUI 終了後の再取得を進める。プロバイダには候補、面の制約、公開・解除を任せる。PC-98 の TVRAM・GDC・プレーナ面と、OF FB の記述子・色・シャドウ転送・`fbcon` の描画はそれぞれ保持する。 |
| **A4 SQLite・FEP と部品の起動** | PC-98 の `platform/pc98/kernel.c:platform_boot_services` は `kernel/sqlite_engine.c:services_boot` を呼び、SQLite を用意して `fep_boot_start` に進む。PowerMac の `platform/powermac/ppc_kernel.c:platform_boot_run` は、機能マクロの内側で `sqlite_static_boot → 条件付き fep_boot_static` を組む。`kernel/fep_boot.c:fep_boot_static` も共通の `fep_boot_start` と巻き戻しを使うため、入口の順序・利用可否判定と失敗処理に整理の余地がある。動的部品の検査・配置は `kernel/module/module_runtime.c` と `module_image.c` に共通処理がある。 | SQLite 提供者の起動、FEP の前提確認、開始、失敗時の巻き戻しを root 後の共通サービスに置く。静的結線と動的ロードを提供方式として扱う。CPU ごとの再配置・命令キャッシュ処理、媒体と部品の pair の取得・検証はそれぞれの境界に置く。 |
| **B1 シェル・実行の準備** | 製品側の PC-98 `kernel_shell_loop` と PowerMac `platform_boot_run` は、どちらも `init/shell_loop.c:shell_loop_run` に入り、`exec_run` と再起動判断を共有する。現行の `platform/powermac/exec_boot.c:pm_exec_boot` は `exec_nest_level = 1` を設定していない。現存する重複は `fd_redirect_init → pipe_buffer_init → exec_init` の起動時手順。別の `platform/powermac/shell_boot.c:pm_shell_boot` に `/bin/sh.bin` 専用ループが残るが、ソース検索で呼び出しは確認できなかった。 | FD・パイプ・実行枠の初期化を共通の一段にまとめ、シェルループに渡す。障害フレームからの復帰、レジスタ保存、特権遷移は `arch/x86/kernel/exec_context.c`、`arch/ppc/exec_context.c` と各 CPU の入口に置く。専用ループの到達性はビルド構成で確認する。 |
| **B2 入力の結線** | `drivers/input/kbd.c` と `platform/powermac/input_boot.c` は、ともに `input_legacy_register` で提供者を登録し、`input_legacy_try/wait` を呼ぶ入口を持つ。USB の入力源登録は `input/hal/usb_input.c` にある。PowerMac `ppc_kernel.c:platform_boot_services` は表示寸法からポインタ範囲を決め、USB を起動してから `pm_input_boot` を呼ぶ。VIA の `system.c:pm_via_prepare` で確認できた結線は RTC とリセットで、調査した入力入口では ADB キーの供給を確認できなかった。 | 入力提供者の登録、待ち API、表示寸法をポインタへ渡す時点を共通側に寄せる。PC-98 鍵盤の IRQ・走査コード、USB コントローラの引継ぎ、PPC のローカル／LAN 入力の採取は提供側に置く。 |
| **B3 LAN・link の進行** | `net/nic.c` と `net/link.c` が NIC の受信進行、link のハンドシェイク・再送・送信を持つ。PowerMac の `platform/powermac/lan_service.c:pm_lan_poll` は GEM 起動後、`KBOOT_LINK` で共通 `net_start` に結線する一方、起動時の監視や端末サービスの進行も持つ。`ppc_kernel.c:platform_boot_services` の時限ポーリングと、`link_platform.c:pm_link_service_once` の協調進行とで、同じ LAN サービスの呼び出し元が分かれる。 | NIC 利用開始後の RX・link・端末処理の呼び出し順を、既存の共通待ち処理に寄せられるか詰める。GEM の DMA リング・PHY 立ち上げ・リセット、共通の待ち処理が使える前の診断ポーリングは PowerMac 側に置く。 |
| **C1 tick・待ち** | `arch/x86/kernel/isr_stub.asm:irq_stub_0` は `tick_clock_step` の jiffy 判定で `isr_handlers.c:timer_handler → tick_dispatch`。`arch/ppc/kernel_dec.c:ppc_decrementer_interrupt` にも同じ判定と dispatch がある。両機種の `link_platform_idle` に割り込み禁止時の `_idle` と通常の `idle_wait_step` があり、PPC 側には再入ガードもある。 | jiffy 判定と dispatch の接続、待ちの共通部分を小さくまとめる候補。PC-98 の PIT サンプル・PIC EOI・V86 反映、PPC の DEC 再装填・TB 読み出しは各側に置く。共通化時は既存の割り込み順と再入条件を維持する。 |
| **C2 KAPI 表を公開する順** | `exec/exec.c:exec_kapi_init` と生成済み `exec/exec_kapi_init.inc` が表を埋める。`arch/ppc/exec_kapi.c:exec_kapi_table` は PPC の表の格納先を返す。重複は PC-98 の `kernel_boot_memory` と PowerMac の `platform_boot_machine` にある `shm_init → exec_kapi_init → ring3_trampoline_init → paging_boot_user_shared`。PPC のシェルなし構成は別の共有ページを使う。 | 実行機能を持つ構成の公開順を共通化する。表の格納場所、トランポリンの機械語、例外入口、共有ページ番地は CPU 側の契約に置く。SDK・KAPI の名前と意味は共通のまま扱える。 |

PowerMac の `root_pair` は、`platform/powermac/module_pair.c:pm_verify_entry_pair/platform_module_pair` が ELF の配置情報と RAM root の ISO の CRC・長さを照合する仕組みです。PC-98 の `platform/pc98/module_pair.c:platform_module_pair` は有効な起動像の CRC を返します。両者を使う共通の部品ロード順と、媒体ごとの照合方法は独立に扱えます。

## 上位 5 件

以下は設計案です。順位は、同じ順序を複数箇所で変更する負担と、後続処理への影響で付けました。

1. **A1 起動** — 現行の順を両機種で表にし、各段の開始条件と完了条件を定義する。`kernel_start` から呼ぶ共通段は、両機種で前提を確認できたものから増やす。
2. **A2 root** — HDD・RAM 像から root 候補を渡す口を定義し、マウント後のパス確定と子マウントを一本化する。HDD の書き込み許可と RAM 像の所有権解放は、それぞれ発行・解放の条件を明示する。
3. **A3 表示** — 共通側が面の確保とコンソール復帰を進め、プロバイダが装置への結線を行う形にする。初回起動、CUI→GUI→CUI、準備失敗からの復帰を受入条件にする。
4. **A4 部品** — SQLite と FEP の起動・失敗時の巻き戻しを一つのサービス手順にする。提供方式の変更とは独立に設計できる。
5. **B1 実行準備** — 既存の共通シェルループの前に、両機種が使う実行準備を一段置く。旧シェル入口の整理は、製品像・診断像ごとの到達性を確かめてから判断する。

## 調べきれなかった範囲

- 各ビルド構成での旧シェル・CLI 入口の到達性、ADB の実機入力経路、PC-98 の NIC ごとの初回 link 接続順は未確認です。
- IRQ 有効化の移動、表示復帰の統合、root 後処理の順序変更が、低メモリ機・フォント欠損・媒体エラー時にも成立するかは静的調査では確定していません。シングルタスクに対する状態管理の過剰さも、この段では判定していません。
- 読み取り専用の調査として実施しました。ファイル変更、修正付き preflight、ビルド、lint、変異検査、エミュレータ・実機試験は実施していません。
