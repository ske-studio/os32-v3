# OS32 起動骨格の詳細設計 ①

対象は main 3497386e7。製品の制御順を init/ に一つ置く。実装・計測は後続段で行う。
完了条件は、両製品が同じ起動関数を通り、§8 の順序・障害・媒体・シェルの検査に通ること。

## 1. 実物の確認

番号は各列の実行順。PC-98 の BIOS 起動と PPC の OF 引継ぎの差を、製品の順序仕様に持ち込まない。

|観測点|PC-98|PowerMac 製品|前提と差の由来|
|---|---|---|---|
|引継ぎ|1 kernel_main → kernel_start|1 ppc_kernel_real_start → paging_init → kernel_start|物理。OF の記述子と初期写像が必要|
|早期機械|2 platform_boot_machine で kstack_hw_init、pc98_boot_bind、capture、kernel_boot_cpu|2 同関数で ESCC、stream、PCI メモリ・資源|物理。故障を表示できる出口を先に作る|
|共有実行領域|6 kernel_boot_memory で paging_init、回収、BOOT_FONT、台帳、SHM、KAPI、USER 写像|3 platform_boot_machine で台帳準備後 SHM、KAPI、USER 写像、heap|写像の作り方は物理。root に対する位置は分離可能|
|装置・FS|3 BOOT_INPUT、FLOPPY、DEV_FS、palette、PCI_ENUM、DISK|6 platform_boot_services で PCI、token、LAN、pm_ide_boot、pm_root_select|FDD・区画は物理。探査の統括位置は偶然|
|root|4 kmalloc_init、vfs_mount、BOOT_AUTO_MOUNT、machcfg_load_root|7 pm_root_boot、machcfg_load_root、apply_machine_config|媒体の選択は物理。設定解決は同じ|
|時計・IRQ|5 BOOT_CLOCK は RTC、CPU 初期化済み。全体の _enable は 9|4 pm_clock_bind、DEC、VIA、MPIC、5 _enable|物理。PPC は root 探査前に tick を使う|
|root 後|7 kernel_boot_memory 後半で kdebug、CD 等、kernel_mount_children、Unicode|7 pm_root_boot 内の kdebug、次に pm_loop_boot、kernel_mount_children|部品の依存。配置に偶然の差|
|表示・実行準備|8 FD、pipe、serial、gfx 設定・reserve、exec、IME、音源、NIC、USB、token|8 pm_console_text_start、bounds、HostFS、USB、work、pm_input_boot|面と入力源は物理。PPC の Unicode はここで読む|
|サービス|10 platform_boot_services で services_boot、検査、splash、gfx_prepare_backend、部品、shlib、ログ保存|9 platform_boot_run で静的 SQLite、条件付き FEP、pm_exec_boot|提供方式と受付可能なログ出口が異なる|
|シェル|11 kernel_shell_loop → shell_loop_run|10 shell_loop_run、root なし診断では LAN poll|製品の再実行規則は既に共通|

PC-98 の 6 と 7 は同じ kernel_boot_memory 内で連続する。6 の入口で root は既に使用可能。
両機種とも現在の共通入口は init/kernel_start.c:kernel_start の machine → _enable → services → run。

調査記述と実物で修正する点は次の 2 件。

1. order B1 の「呼び出しは確認できなかった」は製品に限る。platform/powermac/shell_boot.c:pm_shell_boot は tools/tests/ppc_sh_boot.py の test_main が直接呼ぶ。
2. order A3 の置き場は翻訳単位と定義を区別する。gfx/gfx_core.c:gfx_prepare_backend と同じ翻訳単位でも、gfx_boot_reserve の本体は gfx/gfx_core_surface.inc:gfx_boot_reserve。

POLICY_DEV §1 の統合判断をこの変更に当てる。

- 同じ役割は両機種にある。既存の boot_root、machcfg、shell_loop と部品ロード契約を使う。
- 同じ順を一つの実装にし、BIOS・OF、割込み源、媒体の選択・解放、表示面、trap の登録を提供側に残す。
- ARCH_COMPAT_CONTRACT §4.2 の AC-BOOT-1/2/3 に従う。未検証像、token、設定欠損を同じ入力条件で試す。
- §9 のシェルは製品で実際に起動する外部シェルを基準にする。入力・コマンドの意味を維持する。
- POLICY_DEV §2 に従い整数のみ、機能別ファイル、定数は所有層に一か所。PPC のソフト二段表は案 D を継承する。

## 2. init/ が所有する製品の起動順

kernel_start から下表を上から同期呼出しする。段番号は設計上の目印で、ABI や実行時の登録番号にしない。

|段|開始条件|完了条件|失敗時|
|---|---|---|---|
|S0 引継ぎ|CPU 入口が C を呼べる|検証した引継ぎを固定し早期出力、例外入口、設定 seed を確立|実行不能なら早期出口に理由を残して停止|
|S1 メモリ|引継ぎ捕捉済み|paging、予約・台帳、SHM、KAPI、trampoline、共有写像、heap が利用可|必須写像・台帳失敗は停止。PPC 大 heap 不足は従来の小 heap|
|S2 時計|例外入口・必要 MMIO が有効|割込み源を設定し、媒体が要する開始条件を満たす|必須時計・MPIC 不成立は停止|
|S3 装置登録|heap、媒体が必要とする IRQ 条件成立|装置名・対応 FS が登録され、root 候補が一つ決定|任意装置失敗は記録。必須候補なしは停止|
|S4 root|選択済み候補、必要 FS 登録済み|/ が使用可、既定ドライブ確定、設定を一度解決|製品の mount/path 失敗は停止。設定読取失敗は全既定|
|S5 子マウント|root・台帳が有効|必要な部品・loop 等の登録後に子を一度 mount。Unicode・フォントの用意|任意子や文字資源の欠損を記録し縮退|
|S6 表示・入力準備|設定、媒体、文字資源の結果あり|必要面を先に予約し、早期出力を維持、入力・ログ提供者を結線|面不足は既存 CUI/stream、USB 不在は残存入力源|
|S7 サービス|root と出力が安定、割込み有効|SQLite/FEP の公開状態が確定、起動時の部品処理完了|既存の巻戻し・隔離を実施しシェルへ|
|S8 実行準備|面予約済み、exec 枠を確保可能|FD、pipe、exec、CPU 固有復帰口、シェルの前提が有効|必須準備の失敗を通知する API なら停止|
|S9 シェル|root・実行準備完了|既存 shell_loop_run に一度制御を渡す|子の異常終了・ロード不能は現行 ops の判断|

- root と paging/shm/KAPI は **両製品とも S1 が先**。これなら PPC の引継ぎ写像を遅延せず、部品ロードに root と台帳の双方を保証できる。
- PC-98 は kernel_boot_memory を丸ごと前倒ししない。paging と ledger に必要な loader 報告を S0 で保存し、回収範囲が FDD・V86 作業域と重ならないことを試す。
- BOOT_FONT の kcg_init は ROM/cache の準備、ファイルや部品のロードは S5。DMA 予約は台帳の後、DMA 不足なら従来通り PCI/PCM を縮退。
- PPC の paging_init は C 共通入口以前に一度だけ実行。S1 は既存写像を引き継ぎ、同じ表を初期化し直さない。
- 初期化と割込み受付を分ける。S2 の終端で PPC は _enable、PC-98 はデバイス登録中の IRQ を従来通り禁止して S6 の終端で _enable。
- この二つの境界には各機種が一方だけで有効化する固定フックを置く。IRQ 開放を共通化するには BIOS I/O・USB と ISR の途中状態の別検証を要する。
- 現在 PC-98 で IRQ 開放前の FM・IME・USB・token は S6 の完了前に済ませる。PPC の token は S3 の LAN 起動より先。
- PC-98 の gfx 予約は exec の大きな確保より先。PPC は S6 で fbcon を結線してから S7 の SQLite 診断を流す。
- PC-98 の splash・backend prepare・Cirrus・shlib・ログ保存はサービス後の表示仕上げに残す。exec を必要とする監査は S8 の exec 初期化直後に置く。
- 可視順の基準は画面の内容・選択シェル・エラー時の操作。初期化前倒しに伴う PAGE/EXEC 等の進捗時刻は変わる。診断表示を偽装しない。

## 3. 機種の口と状態所有

include/boot_machine_contract.h の提案。下記はカーネル内部のソース契約。実際の boot_root_image と shell_loop_ops は既存宣言を参照する。

~~~c
struct boot_root_image;
struct shell_loop_ops;
enum boot_medium { BOOT_BLOCK, BOOT_RAM_IMAGE };
struct boot_candidate {
    enum boot_medium medium;
    const char *device;
    const char *fs;
    const struct boot_root_image *image;
};
void platform_boot_early(void);
int  platform_boot_memory_base(void);
void platform_boot_heap_finish(void);
void platform_boot_after_children(void);
int  platform_boot_clock_prepare(void);
void platform_boot_irq_before_probe(void);
int  platform_boot_probe(struct boot_candidate *out);
int  platform_boot_root_mounted(const struct boot_candidate *root);
int  platform_boot_media_finish(const struct boot_candidate *root, int registered);
void platform_boot_apply_config(void);
void platform_boot_child_devices(void);
void platform_boot_text_input(void);
void platform_boot_irq_after_input(void);
int  platform_boot_fep_eligible(void);
void platform_boot_console_finish(void);
void platform_boot_exec_fault_bind(void);
const struct shell_loop_ops *platform_boot_shell_ops(void);
~~~

- int 段は原則 0 成功、非 0 は既存 rc。eligible だけは真偽値。void の任意機能は内部で従来の縮退理由を記録する。
- 候補は kernel_start の自動変数。文字列と image は機種が media_finish まで所有。OS 起動中に二回目の probe はしない。
- RAM 登録の成否は別の int に保持する。mount 失敗でも ram_ro_register 成功後なら registered=1 を渡し、生きた backing を解放しない。
- mount と /tmp の途中失敗を含め media_finish は媒体取得後の共通出口で一度。release 異常は元の mount 結果より優先して停止。
- HDD pair の検証・解放は PPC 提供側。製品の検証済み情報を診断像の埋込み RAM 像と取り違えない。
- 既定ドライブ名は mount 成功後の VFS から取得。PC-98 の起動時ドライブ別名を維持し path_init の再実行で状態を消さない。
- root_mounted は PPC の ext2 書込み許可を mount 成功直後に行う。PC-98 は従来の /host 自動マウントをここで実行。
- machcfg_load_root は共通から一度呼ぶ。apply_config には HID0 等の適用と機種の報告のみ。root なし診断は machcfg_finish(0)。
- child_devices は NETCD 指定時 LAN → netcd → CD/KCG の順、未指定時 LAN は子 mount 後。後者は after_children に置き、Unicode 読込みとの現行順を保つ。
- memory_base は PC-98 の paging/reclaim/arch_boot_after_paging/memory_boot_init、PPC は既存資源登録と台帳の順を保持。
- SHM → exec_kapi_init → ring3_trampoline_init → paging_boot_user_shared は共通が直呼びする。heap_finish は PPC の arena 選択を所有。
- S3 の PC-98 は boot.c の FDD・dev/fs・PCI・disk。PPC は PCI 列挙、時刻依存 LAN 初期化、ide_boot.c の APM 選択。
- 入力 provider は input_boot.c または既存鍵盤実装の所有。PPC の寸法取得 → bounds → USB → work → input の順を保持。
- exec_fault_bind は PPC の resident_fault を設定。PC-98 の復帰入口が CPU 初期化済みなら空実装でよい。
- shell_ops の寿命は静的。シェル終了後に再度 S0〜S8 を走らせない。shell_loop_run 自体は既存コードを使う。
- 共通は include/ の契約だけを読む。機種内で共有する宣言は各機種ディレクトリの platform_boot.h。共通に機種マクロを追加しない。

## 4. 診断像の切替

build/images/ppc-flags.mk が入力を正規化し、build/targets/ppc-powermac.mk が入口を一つだけリンクする。
製品は init/ の製品列を直接呼ぶ。診断は同じ段関数を選ぶ小さな PPC 専用入口で、製品の関数表に診断用空欄を足さない。

|IMAGE 指定|現行の実効構成|移行後の終端・検査|
|---|---|---|
|powermac|製品、SH+LINK、HDD 必須|製品列、HDD 不在は停止。RAM fallback を作らない|
|kboot-core|シェルなし、link なし|保持。代用 user_gate 1 頁、trap/時計/MPIC 観測、LAN poll|
|kboot-link|シェルなし、link あり|保持。core の初期列と link の観測・poll|
|kboot-link-sh|診断 SH+LINK|保持。HDD または埋込み RAM、root 成功は製品と同じシェル、失敗時の診断 poll を維持|
|kboot-sh|link-sh へ正規化|指定名を残し同じ診断入口に畳む。単独 SH として再実装しない|
|kboot-shell|link-sh へ正規化|指定名を残し同じ入口に畳む。旧 CLI を起動しない|

- NEGATIVE=1/2 は従来許される core だけ。期待する trap の前後を観測し、通常シェルへ到達したら失敗。
- PM_PRODUCT_BOOT は製品オブジェクト選択に、KBOOT_LINK は transport 選択に、SH/SHELL はメイク入力の互換に閉じる。
- trap 負試験、layout 表示、self-IPI、時計測定、時限 LAN poll は観測専用入口へ抽出。製品にも現に出る行・停止条件は製品用の機種フックから同じ実装を呼ぶ。
- HDD 選択・mount 失敗は診断でも現在の fatal を維持。poll へ戻るのは RAM 経路で start_media_root が偽の時。
- 旧 shell_boot.c/.h と cli_boot.c/.h は製品参照 0 を各像の map と未解決参照で再確認して削除。
- tools/tests/ppc_sh_boot.py と tools/ppc/test_cli_boot.py の旧入口直接呼出しは、現行 shell_loop_ops の再起動・入力と画像選択の検査に置換する。
- 診断 IMAGE 名の削除は 0。別名に付随するビルド・受入コマンドはそのまま実行できることを検査する。

## 5. シングルタスクでの整理範囲

|対象|今回の処置|所有権・後送り|
|---|---|---|
|A2 root|S4 で block の vfs_mount または boot_root_start_registered、S5 で kernel_mount_children を一度ずつ|/tmp は RAM 専用。APM、BIOS、pair、write enable は提供側|
|A3 表示|初回 reserve、prepare、取得済み面の引渡しを区別し、S6 と仕上げに配置|GUI 復帰は既存 display_provider_console_return のまま。再取得の全面統合は起動外で別途|
|A4 サービス|一回限りの共通起動から静的/動的 provider を選び、既存 fep_boot_start/rollback を利用|動的配置・静的 SQL の寿命は現状維持|
|B1 exec|FD → pipe → exec → fault_bind を一か所にする|exec_nest_level を起動時に代入しない。親子枠、CPU 文脈は現状維持|
|C2 KAPI|公開関数の呼出順だけを共通化|番号、表長、格納先、共有位置、SDK 形式は②|

- 動的 SQLite は services_stage==SERVICES_UNICODE で開始し、必須部品の stat、起動・自己試験の順を維持。
- 辞書の明確な NOTFOUND のみ回復媒体の SQLite-only。他の stat エラーは通常検査と巻戻しに渡す。
- 静的 SQLite は成功後に HDD 適格・Unicode・platform_module_pair が全部真なら FEP。偽なら静的 SQL のみ継続。
- 共通起動は SQL 開始 → 適格性 → FEP 開始の制御を所有。動的 SQL 失敗は非公開化、FEP 巻戻し、module_abort の順。静的版は SQL を残す。
- cleanup 失敗は隔離して予約を保持する。再試行キューや起動段のロックを新設しない。
- single 報告 A1 の前景 I/O 二系統、A3 の OSRI、B3 の SHM 全回収は別機能。今回の起動変更による削減量に含めない。

## 6. 容量見積もり

以下はリンク前の設計予算（KiB）。関数移動の二重計上を避け、削除した旧入口の未リンク部分は 0 とする。

|部品|増減見込み|検証する値|
|---|---:|---|
|共通の段・媒体処理|+0.5〜1.5|両 ELF の text/rodata と map|
|機種側旧制御の除去|各 −0.3〜1.0|同じ最適化・機能設定で旧関数の残存を確認|
|契約と候補|常駐 0、起動 stack +0.1 以下|map と stack 使用量、再帰なし|
|vmkernel.lz4|概算 −1〜+2|圧縮後 508 KiB 以下|
|kdebug.bin|0|ファイル・展開メモリ各 262144 B 以下|
|両 FD|クラスタ境界により変動|2HD 1232 KiB と 1440 KiB の外側 FAT 空き各 64 KiB 以上|

PPC は PPCKERN.ELF と root 像を別計測する。限界を超えた段はリンク重複を除いて再計測し、媒体から既存部品を落とさない。

## 7. 変更単位・ファイル割当て

各行は一人の担当と一つの取り込み単位。行数は変更する行の概算。両機種が同じファイルを同時編集しない。

|単位・担当|新規または変更するファイル|規模と責務|
|---|---|---|
|T0 x86 本線|新 include/boot_machine_contract.h、init/boot_steps.h|各 30〜45 行。契約と共通段の宣言|
|T1 x86 本線|変更 init/boot_root.c、新 init/boot_mount.c|各 10/65 行。媒体 mount、解放出口、設定、子 mount|
|T2 x86 本線|新 init/boot_services.c、変更 kernel/sqlite_engine.c、kernel/fep_boot.c|各 40/55/20 行。開始・判定・終了を分離|
|T3 x86 本線|新 init/boot_exec.c、init/boot_shared.c|各 15〜25 行。FD/pipe と SHM 公開を別ファイルに|
|T4 x86 本線|変更 platform/pc98/kernel.c、kernel_boot_memory.inc、boot.c（後二者も同ディレクトリ）|各 130/65/30 行。旧三段を機能別に取り出す|
|T4 同担当|新 platform/pc98/boot_memory.c、boot_media.c、boot_console.c、platform_boot.h|各 40〜90 行。予約、媒体、画面・入力と機種内宣言|
|T5 PowerPC|変更 platform/powermac/ppc_kernel.c、kernel_boot.c、ide_boot.c、root_boot.c|各 180/10/20/40 行。OF 引継ぎ、APM、解放の口|
|T5 同担当|変更 同 console_text.c、input_boot.c、exec_boot.c、shell_loop_ops.c|各 10〜35 行。文字資源、寸法、trap、ops を提供|
|T5 同担当|新 同 boot_memory.c、boot_media.c、platform_boot.h|各 30〜80 行。製品と診断が使う機種側の段|
|T6 PowerPC|新 同 boot_diag.c、変更 build/images/ppc-flags.mk、build/targets/ppc-powermac.mk|各 100/15/30 行。正規化後の入口を選択|
|T6 同担当|削除 同 shell_boot.*、cli_boot.*、変更 §4 の直接試験 2 本|旧ファイル全行、試験各 30〜60 行|
|T7 x86 本線|変更 init/kernel_start.c、新 init/boot_product.c、変更 build/sources/common.mk、order.mk、build/platform/pc98.mk と PPC のソース列|各 10/55/10 行。製品入口を切替|
|T8 x86 本線|新 tools/tests/test_boot_order.py、変更既存容量・契約試験の対象列|約 110/15 行。共通 fixture、生成物の比較|

## 8. 受入・変異と着手順

各変異は独立に一行だけ変えて試験が失敗することを確認し、元に戻す。下表は実装後に取る証跡。

|受入|試験環境・期待値|一行変異|
|---|---|---|
|段の欠落・重複なし|host の偽 provider が S0〜S9 の順、回数、最初の fatal 以降の呼出し 0 を照合|S4 呼出しを削る|
|層の分離|host で tools/check_layers.py、共通製品列の機種条件 0、両ターゲットをリンク|共通に機種ヘッダを追加|
|メモリ先行|NP21/W の低メモリ・通常 FD/HDD と QEMU PPC の HDD で台帳、SHM、root 読出しを照合|共有写像を trampoline 生成前に移動|
|IRQ と時計|両エミュレータで入力・tick が進み、装置未登録期間の IRQ を観測しない|PPC の _enable を削る|
|媒体|QEMU PPC で HDD 不在は停止、RAM 診断は /tmp 可、登録後 mount 失敗でも参照中頁を保持|media_finish に常に registered=0 を渡す|
|設定・子 mount|両機種で設定欠損時の既定、子の一覧、PC-98 の NETCD/CD 順を照合|子登録より前に mount を移す|
|起動元・token|両機種で未検証 image_valid=0、同一起動中の二回の token が一致|token 生成前に LAN に渡す|
|表示・入力|NP21/W で splash→CUI→GUI→CUI、QEMU で font 欠損時の stream、USB/LAN 入力とポインタ端を照合|bounds 設定を USB 後に移す|
|サービス|host 障害注入と両媒体で辞書なし、破損、pair 不一致、巻戻し失敗の公開状態・残予約を照合|NOTFOUND 限定を全 stat エラーに広げる|
|exec・shell|両製品で /sys/shell.bin 起動、終了・fault・GUI 切替後の前景とコマンドを基準ログと比較|pipe_buffer_init を削る|
|診断像|QEMU で §4 全指定・負試験を起動。root の有無ごとの poll と shell 到達、別名の同一構成を照合|製品選択を link-sh 診断入口に接続|
|容量|host で §6 の全上限と両 FD の実クラスタ空きを検査|fixture の kdebug 展開長を上限+1 にする|

- T0 後は T1/T2/T3 と T4/T5 の作業を並列化できる。共通部の試験は偽 provider を使い、機種側は旧入口を維持してリンクする。
- T6 は T5 後。T7 は T1〜T6 の両機種リンク後。T8 の fixture 作成は T0 後、最終受入は T7 後。
- T0 と PPC 単独段は x86 の kernel.bin、vmkernel.lz4、kdebug.bin、両 FD の基準ハッシュと一致させる。
- T1〜T3 は未リンクなら一致、既存リンク対象を変更した段と T4/T7 は x86 kernel/FD の差を許し map と展開内容を照合する。
- 全段で SDK、シェル、ユーザー部品、KAPI 生成表は同じ入力でバイト比較。T8 は試験だけなので T7 の製品と一致。
- 同一ツール・設定・生成日時で基準と候補を別出力に作り、サイズ、ハッシュ、画面、シリアル記録と最初の不一致を保存する。

## 9. 未決

ユーザー判断を要する未決は 0 件。機種別 IRQ 境界と媒体ごとの縮退は既存製品の条件として維持し、§8 の実装受入で確認する。
