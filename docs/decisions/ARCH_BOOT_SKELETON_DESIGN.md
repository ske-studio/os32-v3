# OS32 起動骨格の詳細設計 ①

## r2→r3 の変更
- sol P1　PPCKERN と link-sh の RAM 失敗出口と解放異常の優先順位を統一。
- sol P2-1　両 ELF の HDD pair 供給条件と異常系を明記。
- sol P2-2　layout 旗と報告実装の連動を T6 で検証。
- sol P2-3　既存 PPC 受入の計測・変異・covers を移動段ごとに移設。
- sol P2-4　校正と FD の IF を区間別に観測・変異。
- 本線 N1　NETCD と S6 の間で NIC_LEGACY の開始状態を共有。

## r1→r2 の変更
- sol P1-1　校正前に IRQ を開け、FD の媒体設定を dev_init より先に固定。
- sol P1-2　S1 を root 前に保ち、低位写像・保存・DMA の検証条件を具体化。
- sol P1-3　PC-98 の FD・pipe・面予約・exec と監査を IME より前に配置。
- sol P1-4　powermac の二つの ELF に媒体、pair、解放、書込みの契約を割当て。
- sol P1-5　相対パスの既定ドライブを機種側で確定。
- sol P2-1　FS 登録、Unicode、サービス段、設定と媒体の所有者・失敗出口を明記。
- sol P2-2　SQLite の提供者と表示仕上げの順序を既存所有側に固定。
- sol P2-3　旧入口のランナー、登録、ログ、selector、負試験を T6 の移行対象に追加。
- sol P2-4　リンク、変異、実容量と起動の検査を各変更段に割当て。
- 本線 F1　校正時の IF と未登録線の PIC マスクを別々に検査。
- 本線 F2　S5 冒頭に常駐部品と DMA 自己診断を配置。
- 本線 F3　S0 の順序を固定し、PC-98 の S2 を RTC に限定。
- 本線 F4　ページ 0、P2V_BOOT、FDC の窓の三条件を受入に追加。
- 本線 F5　appslot のシェル所有者設定をサービスより先に保証。
- 本線 F6　既存 boot-order-host の covers・pins・変異を T4 で移行。
- 本線 F7　現行表に PIT と IRQ の開始点を記載。

対象は main 3497386e7。製品の制御順を init/ に一つ置く。実装・計測は後続段で行う。 指定の git diff で a2d32a8ac までの platform/pc98・init・mm・exec に差分なし。
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
|時計・IRQ|2 BOOT_DMA・PIT・platform_boot_irq_start → _enable → cpu_calibrate。5 BOOT_CLOCK は RTC のみ。9 の _enable 時点で既に IF=1|4 pm_clock_bind、DEC、VIA、MPIC、5 _enable|FD の待ちと PPC の媒体探査は動作中の tick が前提|
|root 後|7 kernel_boot_memory 後半で kdebug、CD 等、kernel_mount_children、Unicode|7 pm_root_boot 内の kdebug、次に pm_loop_boot、kernel_mount_children|部品の依存。配置に偶然の差|
|表示・実行準備|8 FD、pipe、serial、gfx 設定・reserve、exec、IME、音源、NIC、USB、token|8 pm_console_text_start、bounds、HostFS、USB、work、pm_input_boot|面と入力源は物理。PPC の Unicode はここで読む|
|サービス|10 platform_boot_services で services_boot、検査、splash、gfx_prepare_backend、部品、shlib、ログ保存|9 platform_boot_run で静的 SQLite、条件付き FEP、pm_exec_boot|提供方式と受付可能なログ出口が異なる|
|シェル|11 kernel_shell_loop → shell_loop_run|10 shell_loop_run、root なし診断では LAN poll|製品の再実行規則は既に共通|

PC-98 の 6 と 7 は同じ kernel_boot_memory 内で連続する。6 の入口で root は既に使用可能。
両機種とも現在の共通入口は sys/kern/init/kernel_start.c:kernel_start の machine → _enable → services → run。

調査記述と実物で修正する点は次の 2 件。

1. order B1 の「呼び出しは確認できなかった」は製品に限る。sys/platform/powermac/shell_boot.c:pm_shell_boot は ppc_sh_boot.py (旧版 5a9698ae3a9a) の test_main が直接呼ぶ。
2. order A3 の置き場は翻訳単位と定義を区別する。sys/hal/gfx/gfx_core.c:gfx_prepare_backend と同じ翻訳単位でも、gfx_boot_reserve の本体は sys/hal/gfx/gfx_core_surface.inc:gfx_boot_reserve。

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
|S2 時計|S1 完了|PC-98 は RTC のみ。PPC は時計・MPIC を準備して _enable|必須時計・MPIC 不成立は停止|
|S3 装置登録|heap、媒体が必要とする IRQ 条件成立|装置名・対応 FS が登録され、root 候補が一つ決定|任意装置失敗は記録。PPC の RAM 候補失敗は §4 の媒体出口へ。他の必須候補なしは停止|
|S4 root|選択済み候補、必要 FS 登録済み|/ が使用可、既定ドライブ確定、設定を一度解決|PPCKERN・link-sh の RAM 失敗は §4 の媒体出口から LAN poll。HDD と PC-98 の失敗は停止。設定読取失敗は全既定|
|S5 子マウント|root・台帳が有効|必要な部品・loop 等の登録後に子を一度 mount。Unicode・フォントの用意|任意子や文字資源の欠損を記録し縮退|
|S6 表示・入力・機種別の実行準備|子 mount と文字資源の結果あり|PC-98 は下記順で exec と装置を初期化。PPC は console・入力を結線|面不足は CUI/stream、USB 不在は残存入力源|
|S7 サービス|root と出力が安定、割込み有効|SQLite/FEP の公開状態が確定、起動時の部品処理完了|既存の巻戻し・隔離を実施しシェルへ|
|S8 シェル実行準備の完了|S7 完了|PPC はここで pm_exec_boot 相当を実行。PC-98 は S6 で確立した枠を引き継ぐ|必須 API が失敗を返せば停止|
|S9 シェル|root・実行準備完了|既存 shell_loop_run に一度制御を渡す|子の異常終了・ロード不能は現行 ops の判断|

- root と paging/shm/KAPI は **両製品とも S1 が先**。これなら PPC の引継ぎ写像を遅延せず、部品ロードに root と台帳の双方を保証できる。
- PC-98 の S0 を kstack_hw_init → bind → capture（bootinfo_capture・v86_bios_save_real）→ memory_boot_detect → kernel_boot_cpu（GDT/TSS/IDT/PIC、BOOT_DMA、PIT、irq_start、_enable、calibrate）→ machcfg_begin に固定。BOOT_DMA は dma8237_init と 0439h bit2 を FDC より先に設定する。
- PC-98 の mount は固定 KHEAP の kmalloc に依存し、S1 は root・設定ファイルを読まない。staging は無い。reclaim は V86_LOW の頁を再写像し、アロケータに返さない。
- 不変条件 (i) reclaim 後は頁 0 が非 present。BDA は v86_bios_real_lowmem の保存値から読む。0x1000〜0x9FFFF は RW、ゲスト・フォント・KCG の scratch は既存どおり共有。
- (ii) P2V_BOOT を使う kstack_hw_init・save_real・memory_boot_detect（16 MiB 超の書込み検査を含む）は paging_init より先。
- (iii) FDC の静的 BSS 窓は dma_range_ok(s_dma_pa, FDC_DMA_BUF_SIZE, DMA_PHYS_LIMIT) を満たす。
- BOOT_FONT の kcg_init は ROM/cache の準備、ファイルや部品のロードは S5。DMA 予約は台帳の後、DMA 不足なら従来通り PCI/PCM を縮退。
- PPC の paging_init は C 共通入口以前に一度だけ実行。S1 は既存写像を引き継ぎ、同じ表を初期化し直さない。
- PC-98 の IF は S0 終端の校正から有効のまま。kbd・serial は初期化した線だけを開ける。FDC の tick/IRQ 待ち、IDE の IRQ 動作を S3〜S4 でも維持する。
- S3 は FDC 初期化 → BIOS DA/UA に対応する媒体設定（1.44 MB は 512 B × 18）→ dev_init の順。PPC は token を LAN より先に作る。
- S6 の PC-98 は fd_redirect_init → pipe_buffer_init → serial・gfx 設定 → gfx_boot_reserve → kaudit_boot_pages → exec_init（appslot_init で APP_ID_SHELL を設定）→ kaudit_post_exec・gfx2 検査・selftest_post_exec/post_unicode → IME → FM → S5 で未開始なら NIC_LEGACY → USB/token。
- PC-98 の gfx 予約は exec の大きな確保より先。PPC は S6 で fbcon を結線してから S7 の SQLite 診断を流す。
- PC-98 の S7 は services_boot → splash → crt 診断・必要時 Trident → gfx_prepare_backend → Cirrus・必要時 GeForce → kaudit_post_probe・selftest_post_probe → shlib_init → idle_work_run → USB ログ flush → bootlog_save。
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
int  platform_boot_probe(struct boot_candidate *out);
int  platform_boot_root_mounted(const struct boot_candidate *root);
int  platform_boot_media_finish(const struct boot_candidate *root, int registered);
void platform_boot_apply_config(void);
void platform_boot_child_devices(void);
void platform_boot_text_input(void);
void platform_boot_services_start(void);
void platform_boot_unicode(void);
void platform_boot_console_finish(void);
void platform_boot_exec_finish(void);
const struct shell_loop_ops *platform_boot_shell_ops(void);
~~~

- int 段は原則 0 成功、非 0 は既存 rc。void の任意機能は内部で従来の縮退理由を記録する。
- 候補は kernel_start の自動変数。文字列と image は機種が media_finish まで所有。OS 起動中に二回目の probe はしない。 PC-98 は機種内 static boot_context を全段で共有。各機種が platform_machcfg_seed() を供給して S0 で machcfg_begin。
- RAM 登録の成否は別の int に保持する。mount 失敗でも ram_ro_register 成功後なら registered=1 を渡し、生きた backing を解放しない。 probe 前に候補をゼロ初期化し registered=0。画像検証・FS 初期化の失敗も未登録として解放出口へ。
- mount と /tmp の途中失敗を含め media_finish は媒体取得後の共通出口で一度。release 異常は元の mount 結果より優先して停止。
- PPC の HDD pair は §4 の ELF 別条件で供給する。PPCKERN は IME 有効かつ frame ありの場合だけ未登録扱いで検証・解放し、解放異常で停止。PRODUCT は検証済み handoff/plan の pair を使用する。
- PC-98 の path_init は S3 の dev/fs 登録時に一度。FD/HDD とも fdd0 を既定に保つ。PPC は mount 成功後 path_init → validator → path_set_drive(vfs_devname("/"))。相対名を各起動元で現行と照合する。
- root_mounted は PPC の ext2 書込み許可を mount 成功直後に行う。PC-98 は従来の /host 自動マウントをここで実行。
- machcfg_load_root は共通から一度呼ぶ。apply_config には HID0 等の適用と機種の報告のみ。root なし診断は machcfg_finish(0)。
- S5 の共通列は child_devices → kernel_mount_children → platform_boot_unicode → after_children。PC-98 の child_devices 冒頭（S4 後）で dma_pool_init → legacy_init（/sys/v86.mod）→ kdebug_load → running 時 selftest_run、他は skipped。DMA の池を再初期化する検査を PCI/LAN/PCM の span 取得前に完了。
- 続いて PC-98 が sysconfig の NETCD alias を判定・保持。指定時だけ LAN → netcd 登録、両経路で CD → KCG → 子 mount → SERVICES_ROOT_READY → unicode_file_load → SERVICES_UNICODE。after_children で未指定時 LAN → DMA 利用可なら PCM。任意部品欠損は現行の縮退で続行。
- PC-98 の boot_context に boot_legacy_started を保持する。初期値は 0。S5 の NETCD 用 kernel_boot_hostsvc が NIC_LEGACY を起動して 1 にし、S6 の FM 後に同じ旗を参照する。
- PPC は S3 に pm_root_boot の名前・FS 登録を移す（HDD は ext2、RAM は boot_root_fs_init と構成上必要な ext2）。登録失敗は当該 root の失敗出口へ。S5 で loop・子 mount 後、pm_console_text_start から移した Unicode 読込みを一度実行。文字資源欠損時は S6 の stream を維持し FEP を省略。
- memory_base は PC-98 の paging/reclaim/arch_boot_after_paging/memory_boot_init、PPC は既存資源登録と台帳の順を保持。
- SHM → exec_kapi_init → ring3_trampoline_init → paging_boot_user_shared は共通が直呼びする。heap_finish は PPC の arena 選択を所有。
- S3 の PC-98 は boot.c の FDD・dev/fs・PCI・disk。PPC は PCI 列挙、時刻依存 LAN 初期化、ide_boot.c の APM 選択。
- 入力 provider は input_boot.c または既存鍵盤実装の所有。PPC の寸法取得 → bounds → USB → work → input の順を保持。
- 共通列は S6 platform_boot_text_input → S7 platform_boot_services_start → platform_boot_console_finish → S8 platform_boot_exec_finish。PC-98 は S6 内で共通 FD/pipe ヘルパと exec_init を §2 の位置で呼び、S8 は空。PPC は S8 で FD/pipe・exec と resident_fault を結線する。
- shell_ops の寿命は静的。シェル終了後に再度 S0〜S8 を走らせない。shell_loop_run 自体は既存コードを使う。
- 共通は include/ の契約だけを読む。機種内で共有する宣言は各機種ディレクトリの platform_boot.h。共通に機種マクロを追加しない。

## 4. 診断像の切替

sys/conf/images/ppc-flags.mk が入力を正規化し、ppc-powermac.mk と build/ppc-install-boot.mk が ELF ごとに入口をリンクする。
製品は init/ の製品列を直接呼ぶ。診断は同じ段関数を選ぶ小さな PPC 専用入口で、製品の関数表に診断用空欄を足さない。

|IMAGE 指定|現行の実効構成|移行後の終端・検査|
|---|---|---|
|powermac / PPCKERN.ELF|ISO 用、埋込み frame あり、SH+LINK|媒体選択入口。HDD 指定時は選択・ext2 mount 成功直後に区画の書込みを許可。OS32_HOOK_IME 有効かつ frame ありの時だけ埋込み pair を検証し未登録として staging を返す。不一致・frame なし・IME 無効は FEP を省略して HDD 起動続行。HDD 未指定時は RAM 読取専用 root、/tmp を mount、登録済み backing は保持、残余 staging を返す|
|powermac / PRODUCT.ELF|install 用再リンク、PM_PRODUCT_BOOT=1、PM_ROOT_FRAME=0|handoff/plan 不正は停止。検証後 HDD 専用入口。pair は製品リンクの pm_distribution_pair。frame 登録・staging 解放なし。区画選択・mount/path 失敗または HDD 未指定は停止。ext2 mount 成功時のみ書込み許可|
|kboot-core|シェルなし、link なし|保持。代用 user_gate 1 頁、trap/時計/MPIC 観測、LAN poll|
|kboot-link|シェルなし、link あり|保持。core の初期列と link の観測・poll|
|kboot-link-sh|診断 SH+LINK|保持。HDD または埋込み RAM、root 成功は製品と同じシェル。RAM 失敗時は LAN poll、HDD 選択・mount/path 失敗または解放異常は停止|
|kboot-sh|link-sh へ正規化|指定名を残し同じ診断入口に畳む。単独 SH として再実装しない|
|kboot-shell|link-sh へ正規化|指定名を残し同じ入口に畳む。旧 CLI を起動しない|

- core の NEGATIVE=1 は KBOOT MEMORY 後に DSI（vector=0x300 reason=0x2 addr=0x80000）、2 は SC（0xc00, 0x108, 0x0）。両方 ppc_trap_stop に到達すること。
- PM_PRODUCT_BOOT は製品オブジェクト選択に、KBOOT_LINK は transport 選択に、SH/SHELL はメイク入力の互換に閉じる。
- trap 負試験、layout 表示、self-IPI、時計測定、時限 LAN poll は観測専用入口へ抽出。製品にも現に出る行・停止条件は製品用の機種フックから同じ実装を呼ぶ。
- PPCKERN.ELF と link-sh は S3/S4 の RAM 出口を共有。frame なしは直接 LAN poll。検証・登録・mount/path 失敗は取得済み媒体を §3 の registered に従い片付け、LAN poll に入り S5 以降を呼ばない。解放異常は常に優先して停止。HDD 選択・mount/path 失敗は両 ELF・link-sh とも停止。
- 旧 shell_boot.c/.h と cli_boot.c/.h は製品参照 0 を各像の map と未解決参照で再確認して削除。
- ppc_sh_boot.py (旧版 5a9698ae3a9a) と test_cli_boot.py (旧版 9522f1110567) の旧入口直接呼出しは、現行 shell_loop_ops の再起動・入力と画像選択の検査に置換する。
- IMAGE 省略・旧旗なしは powermac。明示した KBOOT_LINK/SH/SHELL は現行の省略値と 0/1 判定を保持。複数・未知 IMAGE、非二値、LINK+SHELL、SH+SHELL、LINK/SH/SHELL と NEGATIVE の併用、正規化後の IMAGE 不一致は make エラー。
- T6 は KBOOT_SHELL_LAYOUT_DIAG=1 を旧 ppc_kernel.o から layout 表示の移動先へ渡し、旗変更時の再コンパイルと build/platform/powermac.mk の報告実装選択を連動させる。通常の powermac/link-sh は report_product.c（READY/ALIVE 出力なし）、layout 指定と core/link は report_diag.c。

## 5. シングルタスクでの整理範囲

|対象|今回の処置|所有権・後送り|
|---|---|---|
|A2 root|S4 で block の vfs_mount または boot_root_start_registered、S5 で kernel_mount_children を一度ずつ|/tmp は RAM 専用。APM、BIOS、pair、write enable は提供側|
|A3 表示|初回 reserve、prepare、取得済み面の引渡しを区別し、S6 と仕上げに配置|GUI 復帰は既存 display_provider_console_return のまま。再取得の全面統合は起動外で別途|
|A4 サービス|一回限りの共通起動から静的/動的 provider を選び、既存 fep_boot_start/rollback を利用|動的配置・静的 SQL の寿命は現状維持|
|B1 exec|FD → pipe を共通ヘルパ化。exec_init の呼出位置は §2 の機種順|appslot・所有者設定を一度だけ実行。親子枠、CPU 文脈は現状維持|
|C2 KAPI|公開関数の呼出順だけを共通化|番号、表長、格納先、共有位置、SDK 形式は②|

- 動的 SQLite は services_stage==SERVICES_UNICODE で開始し、必須部品の stat、起動・自己試験の順を維持。
- 辞書の明確な NOTFOUND のみ回復媒体の SQLite-only。他の stat エラーは通常検査と巻戻しに渡す。
- 静的 SQLite は成功後に HDD 適格・Unicode・platform_module_pair が全部真なら FEP。偽なら静的 SQL のみ継続。
- S7 はリンクした機種提供者を一度呼ぶ。PC-98 は services_boot が動的 SQL の開始・非公開化・FEP 巻戻し・module_abort を所有。PPC は静的 SQL 開始後に上記条件で FEP を起動し、FEP 失敗でも SQL を保持。必須段の rc は kernel_start で直ちに停止する。
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
|T1 x86 本線|変更 sys/kern/init/boot_root.c、新 init/boot_mount.c|各 10/65 行。媒体 mount、解放出口、設定、子 mount|
|T2 x86 本線|変更 sys/kern/sqlite_engine.c、sys/kern/fep_boot.c の既存サービス提供口|約 20 行。既存の開始・巻戻しを呼ぶ|
|T3 x86 本線|新 init/boot_exec.c、init/boot_shared.c|各 15〜25 行。FD/pipe と SHM 公開を別ファイルに|
|T4 x86 本線|変更 sys/platform/pc98/kernel.c、kernel_boot_memory.inc、boot.c（後二者も同ディレクトリ）|各 130/65/30 行。旧三段を機能別に取り出す|
|T4 同担当|新 platform/pc98/boot_memory.c、boot_media.c、boot_console.c、platform_boot.h|各 40〜90 行。予約、媒体、画面・入力と機種内宣言|
|T5 PowerPC|変更 sys/platform/powermac/ppc_kernel.c、kernel_boot.c、ide_boot.c、root_boot.c|各 180/10/20/40 行。OF 引継ぎ、APM、解放の口|
|T5 同担当|変更 同 console_text.c、input_boot.c、exec_boot.c、shell_loop_ops.c|各 10〜35 行。文字資源、寸法、trap、ops を提供|
|T5 同担当|新 同 boot_memory.c、boot_media.c、platform_boot.h|各 30〜80 行。製品と診断が使う機種側の段|
|T6 PowerPC|新 同 boot_diag.c、変更 sys/conf/images/ppc-flags.mk、build/targets/ppc-powermac.mk、build/ppc-install-boot.mk|各 100/15/30 行。正規化後の入口を選択|
|T6 同担当|変更 build/platform/powermac.mk、ppc_shell_layout.py (旧版 0abf3e9faafe) と selector 試験|layout のコンパイル・リンク選択、既存の指定と変異を維持|
|T6 同担当|削除 同 shell_boot.*、cli_boot.*、変更 §4 の直接試験 2 本|旧ファイル全行、試験各 30〜60 行|
|T7 x86 本線|変更 sys/kern/init/kernel_start.c、新 init/boot_product.c、変更 build/sources/common.mk、order.mk、build/platform/pc98.mk と PPC のソース列|各 10/55/10 行。製品入口を切替|
|T8 x86 本線|T4 で拡張する既存 test_boot_order.py (旧版 f296f5418a3e) と容量・契約試験|最終 fixture 比較。boot-order-host の登録を継承|

## 8. 受入・変異と着手順

各変異は独立に一行だけ変えて試験が失敗することを確認し、元に戻す。下表は実装後に取る証跡。

|受入|試験環境・期待値|一行変異|
|---|---|---|
|段の欠落・重複なし|host の偽 provider が S0〜S9 の順、回数、最初の fatal 以降の呼出し 0 を照合|S4 呼出しを削る|
|層の分離|host で tools/check_layers.py、共通製品列の機種条件 0、両ターゲットをリンク|共通に機種ヘッダを追加|
|メモリ先行|NP21/W の低メモリ・通常 FD/HDD と QEMU PPC の HDD で台帳、SHM、root 読出しを照合|共有写像を trampoline 生成前に移動|
|IRQ と時計|既存 host 試験で calibrate 時 flags==1、未登録線は PIC mask。NP21/W で FD 待ちを完走|irq_start で未登録線を一本開ける|
|早期 IRQ・媒体|host で校正時 IF=1、NP21/W で FDC 初期化・root 読出し各区間の IF=1 と DA/UA を記録し、両 FD を読む|別試行で校正前 _enable を削除、FD 側の再 _enable 後に対象区間のみ IF を落とす、1.44 MB 設定を削除。各観測点で検出|
|低位メモリ|host で §2 (i)〜(iii) を照合、NP21/W の 16 MiB 超構成で FD・V86・KCG を確認|memory_boot_detect を paging_init 後へ移す|
|常駐部品と exec|偽 provider が §3 の NETCD 有無両列と §2 S6・S7 の監査・所有者を照合|selftest_run を LAN 後へ移す|
|NIC_LEGACY|PC-98 の NETCD 指定時は S5、未指定時は S6 の FM 後・USB 前に各 1 回だけ起動|S6 の旗判定を外して指定時の二重起動を検出|
|媒体|QEMU PPC で PRODUCT.ELF は HDD 不在で停止、PPCKERN.ELF と RAM 診断は /tmp 可、登録後 mount 失敗でも参照中頁を保持|media_finish に常に registered=0 を渡す|
|設定・子 mount|両機種で設定欠損時の既定、子の一覧、PC-98 の NETCD/CD 順を照合|子登録より前に mount を移す|
|起動元・token|両機種で未検証 image_valid=0、同一起動中の二回の token が一致|token 生成前に LAN に渡す|
|表示・入力|NP21/W で splash→CUI→GUI→CUI、QEMU で font 欠損時の stream、USB/LAN 入力とポインタ端を照合|bounds 設定を USB 後に移す|
|サービス|host 障害注入と両媒体で辞書なし、破損、pair 不一致、巻戻し失敗の公開状態・残予約を照合|NOTFOUND 限定を全 stat エラーに広げる|
|exec・shell|両製品で /sys/shell.bin 起動、終了・fault・GUI 切替後の前景とコマンドを基準ログと比較|pipe_buffer_init を削る|
|診断像|QEMU で §4 全指定・負試験を起動。root の有無ごとの poll と shell 到達、別名の同一構成を照合|製品選択を link-sh 診断入口に接続|
|PPC 媒体出口|QEMU の PPCKERN と link-sh で RAM frame なし・検証不良・RAM 登録失敗 (`registered=0` で staging を解放)・mount/path 失敗ごとに LAN poll、shell 未到達を観測。解放異常を重ねると停止。HDD 選択・mount/path 失敗は各 HDD 像で停止|解放失敗を poll に流す|
|HDD pair|PPCKERN の pair 不一致は検証・解放各 1 回、frame なし・IME 無効は各 0 回。いずれも FEP なしで shell 到達。解放異常は停止。PRODUCT の不正 handoff/plan は pair 供給前に停止|frame 条件を外す／別試行で plan 検証を迂回|
|報告切替|QEMU で通常両 ELF・link-sh、ppc_shell_layout.py の旗付き像、core/link の各基準ログに layout と READY/ALIVE の有無・順・フィールド・CRLF を照合|layout 旗だけを表示側から外す|
|容量|host で §6 の全上限と両 FD の実クラスタ空きを検査|fixture の kdebug 展開長を上限+1 にする|

- T0 後は T1/T2/T3 と T4/T5 の作業を並列化できる。共通部の試験は偽 provider を使い、機種側は旧入口を維持してリンクする。
- T6 は T5 後。T7 は T1〜T6 の両機種リンク後。T8 の fixture 作成は T0 後、最終受入は T7 後。
- 各票の取込み条件は対象リンクと担当変異の検出。T1 は媒体解放、T2 は SQL 障害、T3 は pipe/共有写像、T4 は PC-98 の IRQ・媒体・常駐部品列。T4 完了までに既存 boot-order-host の covers・pins・変異の目印を移す。
- T5 は PPC の HDD/RAM・文字欠損、T6 は両 ELF・全 selector・USB と trap の変異、T7 は両製品の通し起動と fatal 後の呼出しゼロを検査。T8 は全行の最終比較。
- T5 の媒体・機種処理移動、T6 の診断入口・selector 移動、T7 の pm_exec_boot 呼出し・製品入口切替では、移す行と同じ票で usb_run.py、hdd_root_accept.py、ata_accept.py、fep_boot_accept.py の計測挿入先、test_build_selection.py の Make 行変異先、対応する 旧検査登録 の covers を追随させる。各段で該当通常系成功と各変異の意図した観測差による失敗を取込み条件とする。
- T0 と PPC 単独段は x86 の kernel.bin、vmkernel.lz4、kdebug.bin、両 FD の基準ハッシュと一致させる。
- T1〜T3 は未リンクなら一致、既存リンク対象を変更した段と T4/T7 は x86 kernel/FD の差を許し map と展開内容を照合する。 差を許す各段で §6 の実容量と両 FD の起動も測る。
- 全段で SDK、シェル、ユーザー部品、KAPI 生成表は同じ入力でバイト比較。T8 は試験だけなので T7 の製品と一致。
- 同一ツール・設定・生成日時で基準と候補を別出力に作り、サイズ、ハッシュ、画面、シリアル記録と最初の不一致を保存する。

## 9. 未決

ユーザー判断を要する未決は 0 件。機種別 IRQ 境界と媒体ごとの縮退は既存製品の条件として維持し、§8 の実装受入で確認する。

前回の反論（r1 レビュー） sol P1-2 の root 後案は採用しない。PC-98 の固定 heap・BSS の FDC 窓と S0 の BIOS 保存により S1 は root に依存せず、root を読む常駐部品だけを S5 で起動できる。

## 確定 (2026-11-24、sol 第 3 回「PM が 2 か所を直せば確定可」、PM が反映。本線は第 2 回で PC-98 の側を確定可、N1 は r3 で反映済み・再レビュー不要)

- R3-1 (P2・§4 表): kboot-link-sh の行の失敗の扱いを「RAM 失敗時は LAN poll、HDD 選択・mount/path 失敗または解放異常は停止」に (現行 sys/platform/powermac/ppc_kernel.c の platform_boot_services の HDD の失敗の出口)。
- R3-2 (P2・§8 PPC 媒体出口): RAM 登録失敗の注入 (`registered=0` で staging を解放、LAN poll、シェル未到達) を両像の受入に追加 (現行 ppc_kernel.c の RAM の失敗の出口)。

状態: **設計まで (ユーザー 2026-11-24「実装はしないでね」)**。実装の段 (x86 は本線・PowerPC は PowerPC) は起こさない。
