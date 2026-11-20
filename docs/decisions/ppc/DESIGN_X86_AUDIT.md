> 出所: 本線の機械の ~/os32-tmp/run/design-x86-audit/AUDIT.md (sol の読み取りの見直し、2026-11-20)。行番号は取り込みの時に外した。
> 状態: **現行 (2026-11-20)** — 未着手・実装途中の設計書 144 件を「x86 に揃える」で見直した結果。直しの進みは票 TASK_PPC_ALIGN_X86 の §5 が持つ。

# 未着手・実装途中の設計の x86 整合監査

2026-11-20。独立レビュー担当 gpt-6.2-sol。基点はローカルの `origin/main` 1951635dc。LIST.txt の 137 件と、main 未収録の設計 7 件を照合した。文書の状態より後の実装は main のソース・履歴を優先した。「実装済み」はソースへの着地を示す。ビルド・QEMU・実機試験は実施していない。

方針の根拠は `origin/wt/docs-ppc-align` の POLICY_DEV §10、ARCH_DRIFT_AUDIT、ARCH_COMPAT_CONTRACT §9。既存の差を後で解消する順序は尊重し、未実装部分が仮経路や別契約を増やす場合は発注前の修正とした。共通の RO RAM root は決定済み。FEP の RAM 複製と一時学習パスは別の判断。ATA のレジスタ窓、CPU の例外・時計、OF FB の画素・モード、USB の打鍵は物理上の差として扱った。

直しが要る 12 件、止めるべき 3 件。設計と対応票は文書ごとに計上した。5b と 6b の未収録設計は、対応コードが main に入っているため既存差として記録した。

| 文書 | 状態 | 判定 | 段・根拠 | 直し案 |
|---|---|---|---|---|
| `origin/wip/kfill-designs:docs/decisions/ppc/PPC_KAPI_GUI_SWITCH_DESIGN.md` | 未着手 | 止めるべき | 5d §2-2・§5 M1〜M3/P1〜P4・確定追記 A6〜A8。SESSION_SHELL_CALLER と swprobe の初回置換が ID 1 の仮親と USER sh を契約にする。x86 は platform/pc98/kernel.c の起動ループ、PPC は platform/powermac/exec_boot.c の仮親。 | owner 1 の shell 枠と共通の切替ループを先に揃え、sys_switch_shell だけ通常のシェルで受け入れる。 |
| `origin/wip/kfill-designs:docs/decisions/ppc/PPC_KAPI_FEP_INPUT_DESIGN.md` | 途中 | 止めるべき | 7a §2-1〜6・§4-1 M1/M4/M5/S1〜S3。P1〜P4b は main 1c9ed35c5。新規 fep_static、OS32_FEP_STATIC、/db・/dbu の RAM 複製と学習先の別規則を残段が固定する。 | x86 の fep.mod の起動・回収と辞書/学習パスを基準に設計し、USB/表示/媒体の物理差だけを後端に置く。 |
| `origin/wip/kfill-designs:docs/decisions/ppc/PPC_KAPI_GFX_REST_DESIGN.md` | 未着手 | 止めるべき | 5c §2-3・§4 A2/B1。機種マクロで公開の門を分け、部分転送の dirty 消去まで PowerMac 専用にする。OF FB の固定モード・pitch・画素形式には物理理由がある。 | 所有・世代・転送済み範囲を共通 HAL で判断させ、OF の能力を既存の後端から返す。 |
| `docs/decisions/ppc/PPC_KAPI_FILL_DESIGN.md` | 途中 | 直しが要る | 段 5c/5d・7a/7b (§段表、187–200 行)。表示・シェル・FEP/DB の提供者の前提が各確定設計と新方針にずれている。段 S の CPU 別スタック入口は正当。 | shell 枠・共通表示所有・module/DB 媒体の順を依存表に反映し、共通 provider と既存差の解消を発注単位にする。 |
| `docs/tasks/ppc/TASK_PPC_KAPI_FILL.md` | 途中 | 直しが要る | 冒頭と親設計の 5c/5d・7a/7b。着地済みの 5b・6b と残る 6c D3・7a 静的部を区別する必要がある。 | 実装 SHA を段表に反映し、5d と 7a の矛盾する残段を差し替える。 |
| `docs/tasks/ppc/TASK_PPC_SHELL.md` | 途中 | 直しが要る | §0・§1 W8/W9。製品を USER sh.bin にする前提と PLN1 が残る。W7 は着地済み。 | W9 を x86 と同じ shell 枠・起動・回収に更新し、保存先は共通 VFS/link に結ぶ。 |
| `docs/decisions/ppc/PPC_SHELL_DESIGN.md` | 途中 | 直しが要る | §2 W8/W9 と §12。W7 の仮 USER sh を製品として続ける計画。開発 CLI は独立した起動診断として扱える。 | 残段から USER sh の製品契約を外し、共通 shell 枠で W8/W9 を記述する。 |
| `docs/decisions/ppc/ARCH_BRINGUP_ORDER.md` | 途中 | 直しが要る | 起動順の USER の sh と CLI 凍結節。後続アーキにも KBOOT_SH/KBOOT_SHELL の足場を製品受入の手本として渡す。 | 製品受入を x86 の shell 枠と起動ループに置き、診断の例は既存の立ち上げ記録にする。 |
| `docs/decisions/network/LAN_OPERATOR_DESIGN.md` | 途中 | 直しが要る | 段表 P3a〜c と受入 P3。旧 CLI adapter 案が残り、後継の PPC P3 は仮 USER sh を使う。link wire と共通 Host Services の向きは妥当。 | x86 rshell のコマンド実行・結果の契約を共通化し、機種側は NIC と端末の後端を供給する。 |
| `docs/tasks/network/TASK_LAN_OPERATOR.md` | 途中 | 直しが要る | §2 の PowerPC P3/P5。親設計と後継の製品シェル前提が食い違う。 | 完了した link 化を記録し、残る端末受入を共通シェル経路に結び直す。 |
| `docs/decisions/ppc/PPC_P3_DESIGN.md` | 途中 | 直しが要る | v2 の③d/⑤は main 585dbbdb4、⑥の受入と残追従。専用 link profile、起動・再起動試験が仮 sh の寿命を固定する 。 | 既存の wire・結果 capture を使い、残る受入を製品 shell 枠で実行する。 |
| `docs/decisions/ppc/PPC_P5_REST_DESIGN.md` | 途中 | 直しが要る | §2–4 の残る移行・追従。KBOOT_SH/KBOOT_LINK の指定を台本へ広げる設計が既存像を固定する。 | 共通 link transport は残し、製品経路を選ぶ受入入口に寄せて旧プロファイルの延命を避ける。 |
| `docs/tasks/ppc/TASK_PPC_LAN.md` | 途中 | 直しが要る | 状態行の初期 LAN は済み。追加の表 P3/P5 は仮 sh と kboot 像の受入が残る。 | P3/P5 の残りを製品シェルへの統合と照合して起票する。 |
| `docs/decisions/ARCH_COMPAT_CONTRACT.md` | 途中 | 直しが要る | §4・W3 の適合と整合枝 §9。§9 の「新しく作った違いを追記」と POLICY_DEV §10 の新規二重禁止が衝突し、5d の例外を既成事実にする。 | 物理差と既存の未整合を記録し、未実装の 5d 等を x86 寄せの先行条件に直す。 |
| `docs/decisions/ppc/PPC_RESET_DESIGN.md` | 途中 | 直しが要る | §1:36・§7。PMU/Cuda は物理差だが残る受入は link-sh の仮シェルに結び付く。 | reset の装置手順を保ち、受入を製品 shell の reboot と再起動後 READY に移す。 |
| `docs/decisions/ARCH_CTX_DESIGN.md` | 未着手 | 寄せている | ARCH_CTX の境界、IO_TIER の E/P、tick の C/P は既存 x86 の協調処理を共通にし、入口・時計源を分離。 | — |
| `docs/decisions/INPUT_HAL_DESIGN.md` | 途中 | 寄せている | 入力の段表と USB 段 1 は状態・HID 解釈を共有し、鍵盤・PCI/OHCI/EHCI の接続を物理後端に置く。 | — |
| `docs/decisions/INPUT_KEYMAP_DESIGN.md` | 途中 | 寄せている | 入力の段表と USB 段 1 は状態・HID 解釈を共有し、鍵盤・PCI/OHCI/EHCI の接続を物理後端に置く。 | — |
| `docs/decisions/IO_TIER_DESIGN.md` | 途中 | 寄せている | ARCH_CTX の境界、IO_TIER の E/P、tick の C/P は既存 x86 の協調処理を共通にし、入口・時計源を分離。 | — |
| `docs/decisions/display/SURFACE_LEASE_BUNDLE_DESIGN.md` | 未着手 | 寄せている | 段 3a/b (71 行付近)。同じ CLIENT・lease 契約に対して物理面の供給元が異なる。 | — |
| `docs/decisions/gui/GFX_ACCEL_HAL_DESIGN.md` | 途中 | 寄せている | HAL・PCI IRQ の段表。描画・所有とルーティングの共通契約に、装置別の VRAM・レジスタ・配線を接続する。 | — |
| `docs/decisions/gui/HAL_V2_DESIGN.md` | 途中 | 寄せている | HAL・PCI IRQ の段表。描画・所有とルーティングの共通契約に、装置別の VRAM・レジスタ・配線を接続する。 | — |
| `docs/decisions/network/CONV_IMAGE_DESIGN.md` | 未着手 | 寄せている | Host Services の共通メッセージ・ホスト処理、LE wire と期限は両機種で共有。新規の機種別経路は見当たらない。 | — |
| `docs/decisions/network/LAN_BULK_DESIGN.md` | 未着手 | 寄せている | Host Services の共通メッセージ・ホスト処理、LE wire と期限は両機種で共有。新規の機種別経路は見当たらない。 | — |
| `docs/decisions/ppc/DISPLAY_HOOKS_DESIGN.md` | 途中 | 寄せている | §5–7。共通の exec 終了順と、TVRAM/OF FB の park・復帰の差。実装済みフックの後続は 5b/5c。 | 5c の門は共通所有モデルに載せる。 |
| `docs/decisions/ppc/PCI_COMMON_DESIGN.md` | 未着手 | 寄せている | §2・§7。x86 の列挙・bind を共通化、OF 設定窓・BAR 測定可否は物理差。 | — |
| `docs/decisions/ppc/PPC_FP_NEWLIB_DESIGN.md` | 途中 | 寄せている | §7 の S/T。newlib 同版・同構成、libgcc を kernel に入れず、FP は CPU 固有命令と保存だけ。 | — |
| `docs/decisions/ppc/PPC_RAM_ROOT_W6_PLAN.md` | 途中 | 寄せている | W6 C1〜C4 は済み、P1〜P4/PB が残る。FD 容量とローダの物理制約の下で共通 RAM root。 | — |
| `docs/decisions/ppc/PPC_SQLITE_STACK_DESIGN.md` | 途中 | 寄せている | S-a/S-b と追記 A1〜A4。SQLite 別スタックを x86 と同じ契約へ、例外入口・レジスタ保存は CPU 固有。 | 段 7b の配置・module の判断と切り離して受け入れる。 |
| `docs/decisions/realhw/PCI_IRQ_ROUTE_DESIGN.md` | 未着手 | 寄せている | HAL・PCI IRQ の段表。描画・所有とルーティングの共通契約に、装置別の VRAM・レジスタ・配線を接続する。 | — |
| `docs/decisions/sdk/CRT_EXIT_DESIGN.md` | 未着手 | 寄せている | CRT の後始末と共有 libc を既存 x86 の ABI・newlib に置く。ISA の CRT 差と shlib の共通化は別。 | — |
| `docs/decisions/sdk/LIBC_SHLIB_DESIGN.md` | 未着手 | 寄せている | CRT の後始末と共有 libc を既存 x86 の ABI・newlib に置く。ISA の CRT 差と shlib の共通化は別。 | — |
| `docs/decisions/settings/MACHCFG_DESIGN.md` | 途中 | 寄せている | §6 D〜L の共通設定と起動での確定。HID0/NAP と機種の既定値を後端に置く。 | — |
| `docs/decisions/sound/AV_HAL_DESIGN.md` | 未着手 | 寄せている | AV HAL と OPNA 共通整数合成。OPNA/PCM の実装の差は装置能力。専用 PPC 合成器を作る段は無い。 | — |
| `docs/decisions/sound/SYNTH_OPNA_DESIGN.md` | 未着手 | 寄せている | AV HAL と OPNA 共通整数合成。OPNA/PCM の実装の差は装置能力。専用 PPC 合成器を作る段は無い。 | — |
| `docs/decisions/time/OS32_TIME_DESIGN.md` | 未着手 | 寄せている | Host Services の共通メッセージ・ホスト処理、LE wire と期限は両機種で共有。新規の機種別経路は見当たらない。 | — |
| `docs/decisions/time/TICK_1KHZ_DESIGN.md` | 途中 | 寄せている | tick の派生設計。残る期限/計測・winmm は論理時計契約を使い、PIT/DEC を分ける。 | — |
| `docs/decisions/time/TICK_1KHZ_NEXT_DESIGN.md` | 途中 | 寄せている | tick の派生設計。残る期限/計測・winmm は論理時計契約を使い、PIT/DEC を分ける。 | — |
| `docs/decisions/time/TICK_ARCH_DESIGN.md` | 途中 | 寄せている | ARCH_CTX の境界、IO_TIER の E/P、tick の C/P は既存 x86 の協調処理を共通にし、入口・時計源を分離。 | — |
| `docs/decisions/usb/USB_DESIGN.md` | 途中 | 寄せている | 入力の段表と USB 段 1 は状態・HID 解釈を共有し、鍵盤・PCI/OHCI/EHCI の接続を物理後端に置く。 | — |
| `docs/tasks/gui/TASK_GFX_ACCEL_HAL.md` | 途中 | 寄せている | HAL・PCI IRQ の段表。描画・所有とルーティングの共通契約に、装置別の VRAM・レジスタ・配線を接続する。 | — |
| `docs/tasks/gui/TASK_HAL_V2.md` | 途中 | 寄せている | HAL・PCI IRQ の段表。描画・所有とルーティングの共通契約に、装置別の VRAM・レジスタ・配線を接続する。 | — |
| `docs/tasks/gui/TASK_VIDEO_HAL.md` | 未着手 | 寄せている | HAL・PCI IRQ の段表。描画・所有とルーティングの共通契約に、装置別の VRAM・レジスタ・配線を接続する。 | — |
| `docs/tasks/network/TASK_LAN_BULK.md` | 未着手 | 寄せている | Host Services の共通メッセージ・ホスト処理、LE wire と期限は両機種で共有。新規の機種別経路は見当たらない。 | — |
| `docs/tasks/portability/TASK_ARCH_CTX.md` | 未着手 | 寄せている | 段表。arch 入口と復帰だけを分離し、仕事・待ちは共通。 | — |
| `docs/tasks/ppc/TASK_PPC_FP_NEWLIB.md` | 途中 | 寄せている | §1 S/T。共通 newlib と別スタック契約、FP 命令・状態の保存は物理差。 | 残る受入を共通製品シェルへ移す。 |
| `docs/tasks/ppc/TASK_PPC_SPLASH.md` | 未着手 | 寄せている | 題目のみ。OF FB は物理後端。 | x86 の splash/描画起動を比較して同じ入口へ載せる。 |
| `docs/tasks/realhw/TASK_PCI_IRQ_ROUTE.md` | 未着手 | 寄せている | HAL・PCI IRQ の段表。描画・所有とルーティングの共通契約に、装置別の VRAM・レジスタ・配線を接続する。 | — |
| `docs/tasks/realhw/TASK_USB.md` | 途中 | 寄せている | 入力の段表と USB 段 1 は状態・HID 解釈を共有し、鍵盤・PCI/OHCI/EHCI の接続を物理後端に置く。 | — |
| `docs/tasks/v3/TASK_AV_HAL.md` | 未着手 | 寄せている | AV HAL と OPNA 共通整数合成。OPNA/PCM の実装の差は装置能力。専用 PPC 合成器を作る段は無い。 | — |
| `docs/tasks/v3/TASK_INPUT_HAL.md` | 途中 | 寄せている | §2 段 4c・keymap2・残り。共通の入力状態とイベント契約、機器固有の打鍵・修飾のみ分離。 | — |
| `docs/tasks/v3/TASK_IO_TIER.md` | 途中 | 寄せている | 25–26 行 E1/E2・P1。共通予算を測り、必要時だけ CPU の ABI adapter。 | — |
| `docs/tasks/v3/TASK_MACHCFG.md` | 途中 | 寄せている | 段 G-b・H〜L/N1。共通の machine.cfg と確定ライフサイクル、PVR/HID0 は物理差。 | 像名は機能・起動段の試験条件に読み替える。 |
| `docs/tasks/v3/TASK_SYNTH_OPNA.md` | 未着手 | 寄せている | AV HAL と OPNA 共通整数合成。OPNA/PCM の実装の差は装置能力。専用 PPC 合成器を作る段は無い。 | — |
| `docs/tasks/v3/TASK_TICK_1KHZ.md` | 途中 | 寄せている | §3 C/P。B14 後の x86 物理 1kHz を基準に論理 tick を共有。DEC/TB と PIT/TSC は物理。 | C2 の古い x86 100Hz 記載を現行既定に直して受け入れる。 |
| `origin/wip/kfill-designs:docs/decisions/ppc/PPC_KAPI_LOOP_EXT2_DESIGN.md` | 途中 | 寄せている | 6c §2・§4 D3・夕の U1。N/P/D1/D2 の共通化・公開は main。D3 は段 8 後に同じ formatter と HD provider を選ぶ。P1 の標本は実装では /host。 | D3 の受入を製品シェルで回し、残る古い像・標本の記述を実装に合わせる。 |
| `/home/hight/os32-tmp/run/ata-hal/DESIGN.final.md` | 未着手 | 寄せている | 段 8 §2–3・§6。x86 の PIO・polling・LBA28・block 窓口を共有。ポート/MMIO、IRQ、BE の語と媒体バイト列の差は物理。 | P1/P2 の配備・受入は共通製品経路の機能として選ぶ。 |
| `docs/decisions/MEMMAP_V3.md` | — | 対象外 | 現行決定・観測・受入台帳。独立した新規実装段は確認できない。 | — |
| `docs/decisions/NOFPU_DESIGN.md` | 未着手 | 対象外 | 対象装置・PC-98 の観測、手順または実装の残り。PowerMac に同一サービスの別経路を追加する段は見当たらない。 | — |
| `docs/decisions/gui/API_CONTRACTS.md` | — | 対象外 | 現行決定・観測・受入台帳。独立した新規実装段は確認できない。 | — |
| `docs/decisions/gui/CONSOLE_DUAL_DESIGN.md` | 途中 | 対象外 | D2〜D6 の主題は PC-98 の TVRAM/PC-98 コンソールの分離。PPC FB の製品側判断は 5c に記載。 | — |
| `docs/decisions/gui/CONTROL_PANEL_DESIGN.md` | 未着手 | 対象外 | GUI/SDK 利用側・予約の機能。PPC 専用の製品シェルやバックエンドを追加する段は見当たらない。 | — |
| `docs/decisions/gui/DESIGN.md` | — | 対象外 | 現行決定・観測・受入台帳。独立した新規実装段は確認できない。 | — |
| `docs/decisions/gui/GFX_MODES_DESIGN.md` | — | 対象外 | 現行決定・観測・受入台帳。独立した新規実装段は確認できない。 | — |
| `docs/decisions/gui/OSRI_VIEWER_DESIGN.md` | 未着手 | 対象外 | GUI/SDK 利用側・予約の機能。PPC 専用の製品シェルやバックエンドを追加する段は見当たらない。 | — |
| `docs/decisions/gui/P8_GUI_LAYER_DESIGN.md` | 未着手 | 対象外 | GUI/SDK 利用側・予約の機能。PPC 専用の製品シェルやバックエンドを追加する段は見当たらない。 | — |
| `docs/decisions/network/HSYNC_MANIFEST_DESIGN.md` | 実装済み | 対象外 | 現在の実装範囲は済み。受入の持越し、または別票で扱う拡張は各票を参照。 | — |
| `docs/decisions/ppc/PPC_BOOT_DESIGN.md` | 実装済み | 対象外 | 現在の実装範囲は済み。受入の持越し、または別票で扱う拡張は各票を参照。 | — |
| `docs/decisions/ppc/PPC_FBCON_DESIGN.md` | 実装済み | 対象外 | 現在の実装範囲は済み。受入の持越し、または別票で扱う拡張は各票を参照。 | — |
| `docs/decisions/ppc/PPC_IMPL_RULES.md` | 実装済み | 対象外 | 現在の実装範囲は済み。受入の持越し、または別票で扱う拡張は各票を参照。 | — |
| `docs/decisions/ppc/PPC_KBOOT_DESIGN.md` | 実装済み | 対象外 | 現在の実装範囲は済み。受入の持越し、または別票で扱う拡張は各票を参照。 | — |
| `docs/decisions/ppc/PPC_KCG_LZ4_DESIGN.md` | 実装済み | 対象外 | 現在の実装範囲は済み。受入の持越し、または別票で扱う拡張は各票を参照。 | — |
| `docs/decisions/ppc/PPC_LAN_DESIGN.md` | 実装済み | 対象外 | 現在の実装範囲は済み。受入の持越し、または別票で扱う拡張は各票を参照。 | — |
| `docs/decisions/ppc/PPC_LAN_MARKS_DESIGN.md` | 実装済み | 対象外 | 現在の実装範囲は済み。受入の持越し、または別票で扱う拡張は各票を参照。 | — |
| `docs/decisions/ppc/PPC_LAN_SERVICE_DESIGN.md` | 実装済み | 対象外 | 現在の実装範囲は済み。受入の持越し、または別票で扱う拡張は各票を参照。 | — |
| `docs/decisions/ppc/PPC_MMU_DESIGN.md` | 実装済み | 対象外 | 現在の実装範囲は済み。受入の持越し、または別票で扱う拡張は各票を参照。 | — |
| `docs/decisions/ppc/PPC_NOTES.md` | — | 対象外 | 現行決定・観測・受入台帳。独立した新規実装段は確認できない。 | — |
| `docs/decisions/ppc/PPC_REALHW_G4_DESIGN.md` | 実装済み | 対象外 | 現在の実装範囲は済み。受入の持越し、または別票で扱う拡張は各票を参照。 | — |
| `docs/decisions/ppc/PPC_SHELL_EXEC_DESIGN.md` | 実装済み | 対象外 | 現在の実装範囲は済み。受入の持越し、または別票で扱う拡張は各票を参照。 | — |
| `docs/decisions/ppc/PPC_SHELL_SC_DESIGN.md` | 実装済み | 対象外 | 現在の実装範囲は済み。受入の持越し、または別票で扱う拡張は各票を参照。 | — |
| `docs/decisions/ppc/PPC_SHELL_W8_DESIGN.md` | 実装済み | 対象外 | 現在の実装範囲は済み。受入の持越し、または別票で扱う拡張は各票を参照。 | — |
| `docs/decisions/ppc/PPC_USB_KBD_DESIGN.md` | 実装済み | 対象外 | 現在の実装範囲は済み。受入の持越し、または別票で扱う拡張は各票を参照。 | — |
| `docs/decisions/realhw/BARLEN_DESIGN.md` | — | 対象外 | 現行決定・観測・受入台帳。独立した新規実装段は確認できない。 | — |
| `docs/decisions/realhw/DEVICE_STATES.md` | 未着手 | 対象外 | 対象装置・PC-98 の観測、手順または実装の残り。PowerMac に同一サービスの別経路を追加する段は見当たらない。 | — |
| `docs/decisions/realhw/GF6200_4A_DESIGN.md` | — | 対象外 | 現行決定・観測・受入台帳。独立した新規実装段は確認できない。 | — |
| `docs/decisions/realhw/GF6200_4C_DESIGN.md` | — | 対象外 | 現行決定・観測・受入台帳。独立した新規実装段は確認できない。 | — |
| `docs/decisions/realhw/GF_ACCEL_DESIGN.md` | 途中 | 対象外 | 対象装置・PC-98 の観測、手順または実装の残り。PowerMac に同一サービスの別経路を追加する段は見当たらない。 | — |
| `docs/decisions/realhw/GF_FEAT1_DESIGN.md` | 途中 | 対象外 | 対象装置・PC-98 の観測、手順または実装の残り。PowerMac に同一サービスの別経路を追加する段は見当たらない。 | — |
| `docs/decisions/realhw/GF_FEAT2_DESIGN.md` | 途中 | 対象外 | 対象装置・PC-98 の観測、手順または実装の残り。PowerMac に同一サービスの別経路を追加する段は見当たらない。 | — |
| `docs/decisions/realhw/GF_FEAT3_DESIGN.md` | 途中 | 対象外 | 対象装置・PC-98 の観測、手順または実装の残り。PowerMac に同一サービスの別経路を追加する段は見当たらない。 | — |
| `docs/decisions/realhw/GF_FEAT4_DESIGN.md` | 途中 | 対象外 | 対象装置・PC-98 の観測、手順または実装の残り。PowerMac に同一サービスの別経路を追加する段は見当たらない。 | — |
| `docs/decisions/realhw/GF_FEAT5_DESIGN.md` | 途中 | 対象外 | 対象装置・PC-98 の観測、手順または実装の残り。PowerMac に同一サービスの別経路を追加する段は見当たらない。 | — |
| `docs/decisions/realhw/GF_FEATURE_COMPARE.md` | 未着手 | 対象外 | 対象装置・PC-98 の観測、手順または実装の残り。PowerMac に同一サービスの別経路を追加する段は見当たらない。 | — |
| `docs/decisions/realhw/GF_GUARD_LIFETIME_DESIGN.md` | 途中 | 対象外 | 対象装置・PC-98 の観測、手順または実装の残り。PowerMac に同一サービスの別経路を追加する段は見当たらない。 | — |
| `docs/decisions/realhw/GF_GUARD_THERM_DESIGN.md` | 途中 | 対象外 | 対象装置・PC-98 の観測、手順または実装の残り。PowerMac に同一サービスの別経路を追加する段は見当たらない。 | — |
| `docs/decisions/realhw/GF_STAGES_DESIGN.md` | — | 対象外 | 現行決定・観測・受入台帳。独立した新規実装段は確認できない。 | — |
| `docs/decisions/realhw/GF_STOP_REF_DESIGN.md` | 実装済み | 対象外 | 現在の実装範囲は済み。受入の持越し、または別票で扱う拡張は各票を参照。 | — |
| `docs/decisions/realhw/HWDEV_DESIGN.md` | — | 対象外 | 現行決定・観測・受入台帳。独立した新規実装段は確認できない。 | — |
| `docs/decisions/realhw/HW_BASIC_DESIGN.md` | 途中 | 対象外 | 対象装置・PC-98 の観測、手順または実装の残り。PowerMac に同一サービスの別経路を追加する段は見当たらない。 | — |
| `docs/decisions/realhw/KDBG_PROBE_DESIGN.md` | 未着手 | 対象外 | 対象装置・PC-98 の観測、手順または実装の残り。PowerMac に同一サービスの別経路を追加する段は見当たらない。 | — |
| `docs/decisions/realhw/RA266.md` | — | 対象外 | 現行決定・観測・受入台帳。独立した新規実装段は確認できない。 | — |
| `docs/decisions/realhw/REALHW_RECOVER_DESIGN.md` | — | 対象外 | 現行決定・観測・受入台帳。独立した新規実装段は確認できない。 | — |
| `docs/decisions/realhw/TRIDENT_FULL_DESIGN.md` | 途中 | 対象外 | 対象装置・PC-98 の観測、手順または実装の残り。PowerMac に同一サービスの別経路を追加する段は見当たらない。 | — |
| `docs/decisions/realhw/TRIDENT_V6_DESIGN.md` | 途中 | 対象外 | 対象装置・PC-98 の観測、手順または実装の残り。PowerMac に同一サービスの別経路を追加する段は見当たらない。 | — |
| `docs/decisions/settings/DEVICE_RESERVATION_DESIGN.md` | 未着手 | 対象外 | GUI/SDK 利用側・予約の機能。PPC 専用の製品シェルやバックエンドを追加する段は見当たらない。 | — |
| `docs/decisions/time/TICK_T123_DESIGN.md` | 実装済み | 対象外 | 現在の実装範囲は済み。受入の持越し、または別票で扱う拡張は各票を参照。 | — |
| `docs/tasks/DEFERRED_TESTS.md` | — | 対象外 | 現行決定・観測・受入台帳。独立した新規実装段は確認できない。 | — |
| `docs/tasks/agents/HANDOVER_2026-10-25.md` | — | 対象外 | 進行管理・規定・調査または上位計画。具体的な機種別実装は参照先の票で判定。 | — |
| `docs/tasks/agents/PARALLEL_BRANCHES.md` | — | 対象外 | 進行管理・規定・調査または上位計画。具体的な機種別実装は参照先の票で判定。 | — |
| `docs/tasks/agents/PORT_FLOW.md` | — | 対象外 | 進行管理・規定・調査または上位計画。具体的な機種別実装は参照先の票で判定。 | — |
| `docs/tasks/gui/TASK_CONSOLE_DUAL.md` | 途中 | 対象外 | D2〜D6 の主題は PC-98 の TVRAM/PC-98 コンソールの分離。PPC FB の製品側判断は 5c に記載。 | — |
| `docs/tasks/gui/TASK_CONTROL_PANEL.md` | 未着手 | 対象外 | GUI/SDK 利用側・予約の機能。PPC 専用の製品シェルやバックエンドを追加する段は見当たらない。 | — |
| `docs/tasks/gui/TASK_I18N.md` | 未着手 | 対象外 | GUI/SDK 利用側・予約の機能。PPC 専用の製品シェルやバックエンドを追加する段は見当たらない。 | — |
| `docs/tasks/gui/TASK_OSRI_VIEWER.md` | 未着手 | 対象外 | GUI/SDK 利用側・予約の機能。PPC 専用の製品シェルやバックエンドを追加する段は見当たらない。 | — |
| `docs/tasks/gui/TASK_P8_GUI_LAYER.md` | 未着手 | 対象外 | GUI/SDK 利用側・予約の機能。PPC 専用の製品シェルやバックエンドを追加する段は見当たらない。 | — |
| `docs/tasks/network/TASK_HSYNC_MANIFEST.md` | 実装済み | 対象外 | 現在の実装範囲は済み。受入の持越し、または別票で扱う拡張は各票を参照。 | — |
| `docs/tasks/ppc/TASK_PPC_MMU.md` | 実装済み | 対象外 | 現在の実装範囲は済み。受入の持越し、または別票で扱う拡張は各票を参照。 | — |
| `docs/tasks/ppc/TASK_PPC_REALHW.md` | 実装済み | 対象外 | 現在の実装範囲は済み。受入の持越し、または別票で扱う拡張は各票を参照。 | — |
| `docs/tasks/ppc/TASK_PPC_SEP.md` | 実装済み | 対象外 | 現在の実装範囲は済み。受入の持越し、または別票で扱う拡張は各票を参照。 | — |
| `docs/tasks/realhw/GEFORCE6200_VBIOS.md` | — | 対象外 | 現行決定・観測・受入台帳。独立した新規実装段は確認できない。 | — |
| `docs/tasks/realhw/PLAN.md` | — | 対象外 | 進行管理・規定・調査または上位計画。具体的な機種別実装は参照先の票で判定。 | — |
| `docs/tasks/realhw/TASK_CCU2_MOUSE.md` | 未着手 | 対象外 | 対象装置・PC-98 の観測、手順または実装の残り。PowerMac に同一サービスの別経路を追加する段は見当たらない。 | — |
| `docs/tasks/realhw/TASK_GEFORCE6200.md` | 途中 | 対象外 | 段 4c 後の Ra266 の実機固有作業。 | — |
| `docs/tasks/realhw/TASK_GF_FEATURES.md` | 途中 | 対象外 | 層 1〜5 後の GeForce の実機受入・固有項目。 | — |
| `docs/tasks/realhw/TASK_GF_STAGES.md` | 途中 | 対象外 | T1 後の GeForce の実機段。 | — |
| `docs/tasks/realhw/TASK_HPT370.md` | 未着手 | 対象外 | 対象装置・PC-98 の観測、手順または実装の残り。PowerMac に同一サービスの別経路を追加する段は見当たらない。 | — |
| `docs/tasks/realhw/TASK_HWDEV.md` | 途中 | 対象外 | 対象装置・PC-98 の観測、手順または実装の残り。PowerMac に同一サービスの別経路を追加する段は見当たらない。 | — |
| `docs/tasks/realhw/TASK_HW_BASIC.md` | 途中 | 対象外 | 対象装置・PC-98 の観測、手順または実装の残り。PowerMac に同一サービスの別経路を追加する段は見当たらない。 | — |
| `docs/tasks/realhw/TASK_LAN_82557.md` | 途中 | 対象外 | 対象装置・PC-98 の観測、手順または実装の残り。PowerMac に同一サービスの別経路を追加する段は見当たらない。 | — |
| `docs/tasks/realhw/TASK_REALHW_RECOVER.md` | 途中 | 対象外 | 対象装置・PC-98 の観測、手順または実装の残り。PowerMac に同一サービスの別経路を追加する段は見当たらない。 | — |
| `docs/tasks/realhw/TASK_TRIDENT_DRIVER.md` | 途中 | 対象外 | 対象装置・PC-98 の観測、手順または実装の残り。PowerMac に同一サービスの別経路を追加する段は見当たらない。 | — |
| `docs/tasks/settings/DEVICE_RESERVATION.md` | 未着手 | 対象外 | GUI/SDK 利用側・予約の機能。PPC 専用の製品シェルやバックエンドを追加する段は見当たらない。 | — |
| `docs/tasks/settings/F2_OWNERSHIP.md` | 実装済み | 対象外 | 現在の実装範囲は済み。受入の持越し、または別票で扱う拡張は各票を参照。 | — |
| `docs/tasks/v3/PERF_PROFILE_PLAN.md` | 未着手 | 対象外 | 進行管理・規定・調査または上位計画。具体的な機種別実装は参照先の票で判定。 | — |
| `docs/tasks/v3/PORT_CANDIDATES.md` | 未着手 | 対象外 | 進行管理・規定・調査または上位計画。具体的な機種別実装は参照先の票で判定。 | — |
| `docs/tasks/v3/RUST_VS_C11.md` | — | 対象外 | 進行管理・規定・調査または上位計画。具体的な機種別実装は参照先の票で判定。 | — |
| `docs/tasks/v3/T2H_EXTERNAL_GATE.md` | — | 対象外 | 進行管理・規定・調査または上位計画。具体的な機種別実装は参照先の票で判定。 | — |
| `docs/tasks/v3/TASK_CI_REWORK.md` | 途中 | 対象外 | 進行管理・規定・調査または上位計画。具体的な機種別実装は参照先の票で判定。 | — |
| `docs/tasks/v3/TASK_POLICY_LIGHT.md` | 途中 | 対象外 | 進行管理・規定・調査または上位計画。具体的な機種別実装は参照先の票で判定。 | — |
| `docs/tasks/v3/TASK_REDUNDANCY.md` | 途中 | 対象外 | 進行管理・規定・調査または上位計画。具体的な機種別実装は参照先の票で判定。 | — |
| `docs/tasks/v3/TASK_T7_AND_FOLLOWUPS.md` | 実装済み | 対象外 | 現在の実装範囲は済み。受入の持越し、または別票で扱う拡張は各票を参照。 | — |
| `docs/tasks/v3/V3_PLAN.md` | 途中 | 対象外 | §3 は P 段の入口。具体設計の判定は各票。 | — |
| `origin/wip/kfill-designs:docs/decisions/ppc/PPC_KAPI_SFS_SESSION_DESIGN.md` | 実装済み | 対象外 | 6b main 2964a2003・2f4ed7d73。SHELL_REMOTE_SFS は既存差。物理理由は LAN/NIC の後端までで、コマンド行の処理には及ばない。 | shell 統合で x86 の sfs run の共通化に含める。 |
| `origin/wip/kfill-designs:docs/decisions/ppc/PPC_KAPI_GFX_PRESENT_DESIGN.md` | 実装済み | 対象外 | 5b A〜C、main e407702a0・caa544473・0227413b7。既存 DISPLAY_V1_PRESENT_OK/WIDTH は 5c の新設の根拠から外す。 | 既存の所有の差は共通 HAL への整理に計上する。 |

## ユーザー判断が要る物

- **PPC の module 対応を 7a/7b の先行範囲に含めるか。** FEP/SQLite の静的結線には初期移植時の承認がある一方、7a は新規の別起動・別回収を追加する。推奨は、x86 の module 契約を PPC で成立させる段を先に置き、媒体が整うまで該当 slot の公開を待つ。モジュールローダーの追加は実装範囲の判断になる。
- **辞書と学習に必要な永続媒体を 7a の受入条件にするか。** 推奨は x86 と同じ辞書・学習パスを保ち、共通 VFS 上で書ける媒体を得てから学習を受け入れる。媒体未整備の時期に公開する必要があるなら、学習の公開範囲をユーザーが決める。
- **開発 CLI と旧 kboot 像の維持範囲をどこまで縮めるか。** 推奨は製品受入を共通シェルに統一し、従来承認の独立した起動診断は最小限残す。旧像の廃止まで含める場合は、診断・移植手順の範囲変更として決める。
