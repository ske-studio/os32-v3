# ARCH_COMPAT_CONTRACT — アーキテクチャ間の互換契約

> 状態: **確定 (2026-11-14)** — アーキ間の互換契約の正典 (ユーザーの提案「最大公約数と最小公倍数の互換層」、astra 起草・Opus 5.5 の独立レビュー 2 回・PowerPC PM の見直し)。契約の確定と実装の適合は分け、適合は §4 の各項目と W3 で更新する。

## 1. 方針と参照の正

既存の口に「単位・能力・進行文脈・最小容量・不在」を足す。共通コードが頼れる保証を最大公約数、能力照会を含む口の集合を最小公倍数とし、巨大な HAL は挟まない。装置の性能や容量を一律に切り下げない。
数値は C のヘッダを正とし、本文には記号名と `file:line` だけを置く。例外は link 時計の単位 (論理 100Hz の jiffy) と、返値・文脈の表記。満たしていることの測定は PowerPC の受入の記録 (TASK_PPC_LAN) にあり、本書には写さない。実装内の定数は所在を示し、公開が必要なものを §6 でヘッダへ移す。公開 ABI の正は引き続き `sdk/kapi.json`、生成物は編集しない。

|参照名|担当する正典（本書では詳細を複製しない）|
|---|---|
|TIME|[TICK_ARCH_DESIGN §2](time/TICK_ARCH_DESIGN.md#2-arch-に依らない-tick_clock-の口) — 有理周期・読み出し・駆動|
|LAN|[LAN_OPERATOR_DESIGN §5](network/LAN_OPERATOR_DESIGN.md#5-powerpc-を-link-に統合する) — link・SFS・RX 所有者の統合|
|HAL|[HAL_V2_DESIGN §3・§4・§9](gui/HAL_V2_DESIGN.md) — 形式・能力・表示の列挙|
|PPC|[PPC_NOTES](ppc/PPC_NOTES.md)、[ARCH_BRINGUP_ORDER §各段で用意するもの](ppc/ARCH_BRINGUP_ORDER.md#各段で用意するもの)|
|ABI / MEM / BUILD|[KAPI_SPEC](../KAPI_SPEC.md)、[02_memory](../02_memory.md)、[08_build](../08_build.md)・[PPC_IMPL_RULES](ppc/PPC_IMPL_RULES.md)|

所在の基点は main `282738d0`。`P:` は `origin/wt/ppc-p1` の今回確認した `e5088400bba2402857535d6bed201b1e5c251f67`（P1f）を指す。参照は取り込み時に更新する。
PowerPC PM のレビューは P1g の credit 修正済みを報告するが、ローカル参照はその前である。P1g の修正内容は PM 報告として採り、コード確認済みとはしない。F5 の依頼は P1 の節目が main に入り、その SHA と差分を照合してから出す。
本稿は `SURVEY.md` と両レビューを反映した設計のみ。リポジトリ読み取り専用の指定により preflight の修正・ビルド・変異・lint・ゲスト試験は未実施で、適合の合格は宣言しない。

## 2. 進行文脈の語彙

|表記|意味|呼べることと進むことの区別|
|---|---|---|
|IRQ|IRQ の内側。割り込み深さが正|待機・再入・無制限の走査を許さない。EE/IF の値だけで前景と判断しない|
|FG0|前景で EE/IF=0|排他中でも呼べる口と、poll で期限・transport が進む口を個別に宣言する|
|FG1|前景で EE/IF=1|割り込みによる進行が可能。待機から戻ったら条件を再検査する|

各口は `呼出={文脈}; 進行={文脈:駆動主体}; 復帰={EE/IF・待ち条件}` を記す。単なる読み出しは「進行=なし」、起動限定は文脈に「起動時」を添える。既存宣言で保証しない文脈は利用可と推定しない。未記載は担当段で確定するまで共通利用の根拠にしない。
`_idle` と `link_platform_idle` は前景専用で **EE/IF を許可したまま戻る**。`_halt` は EE/IF を許可しない (呼び手が先に許可する) 原始操作で、許可状態を保って戻る。この復帰契約には含めない。復帰後の条件再検査を可能にし、低消費電力や固定の待ち時間は保証しない。待機の原始操作には取りこぼしの窓を作らない。`_stop` の永久停止とは別契約とする。

## 3. 分類と ID

|分類|規則|
|---|---|
|保証（G）|両アーキで共通利用者が依存できる意味・単位・最小容量。表で未適合とした実体は担当段を通るまで接続しない|
|能力（C）|アーキ・装置・像で違ってよい。呼出し前に既存照会口で判定し、口がなければ担当段で追加する。CPU 名から推測しない|
|不在（A）|利用不可・一時不足・切断・空を区別する。既存の型別返値に従い、未実装の操作を成功にしない。返値のない操作は能力照会と対にする|
|ID|`AC-領域-連番`。意味を変えて再利用しない。G/C/A は同じ ID の欄として管理し、試験は `AC-…/条件名` で紐づける|

表の「T1 で作る」は両アーキ共通の契約試験 ID が未登録であることを示す。既存の片側試験や host fake の合格を両アーキの適合と読み替えない。

領域名は次の表に限る。新しい領域は、この表に足してから §4 に節を足す (同じ物を別名で並べない)。

|領域|範囲|
|---|---|
|TIME|tick・時計・期限|
|SYNC|排他・待ち・idle|
|IRQ|割り込みの claim・完了・topology|
|KAPI|公開 ABI の不在と能力照会|
|INPUT|入力の能力|
|MEM|メモリの地図・予約|
|BOOT|起動の情報・boot-token|
|DISPLAY|表示・console の後端|
|GLYPH|字形 (KCG)|
|HOST|Host Services の口|
|SFS|SerialFS の client と待ち|
|BUILD|ビルドの一覧・closure|
|NET|NIC・link・credit・RX の予算|

適合の状態は、本稿の時点では両アーキとも試験で確かめていないので、全項目を「未 (T1 で確かめる)」とする。T1 の契約試験が両アーキで通った項目から、W3 で「済」に書き換える。

## 4. 項目の表

### 4.1 時計・排他・IRQ

|ID|保証 / 能力 / 不在、文脈|正にする記号・所在|x86 の満たし方|PowerPC の満たし方|両アーキの試験 ID / 担当設計|
|---|---|---|---|---|---|
|AC-TIME-1|G: 物理周期は有理数、公開ジフィと ms を混同しない。C: 物理源。A: 不正 config は状態不変でエラー。config は起動 FG0、step は IRQ、ms は全三文脈、raw/jiffy64 は IRQ または FG0。進行は step|`OS32_TICK_HZ` (`sdk/include/os32/os32_kapi_shared.h:17`)、`TICK_CLOCK_MS_HZ`・`TICK_CLOCK_EINVAL`・`tick_clock_*` (`kernel/time/tick_clock.h:6`)|PIT 周期を設定 (`platform/pc98/clock_source.c:21`)、共通 tick|DEC 端数配分と共通 tick (`arch/ppc/kernel_dec.c:25`)。一律の割り込み回数でジフィを割らない|`AC-TIME-1/rational-wrap-context`（T1 で作る） / TIME §2|
|AC-TIME-2|G: read_us 成功時のみ µs 値有効、単位は精度を保証しない。C: 源・分解能・割り込み停止中の進行を照会可能にする。A: 未準備 NOSYS、再試行 AGAIN。呼出は IRQ/FG0、進行は時計源依存|`clock_info`・`platform_clock_read_us` (`include/clock_source.h:4`)、`arch_tick_budget_*` (`arch/x86/arch_tick_budget.h:4`、`arch/ppc/arch_tick_budget.h:4`)|TSC/PIT (`platform/pc98/clock_source.c:78`)。counter_hz を使用中の µs 源と決めつけない|TB (`platform/powermac/clock.c:75`)。予算の内部 count を µs と決めつけず換算口を使う|`AC-TIME-2/source-unavailable`（T1 で作る、照会は F9） / TIME・PPC §4|
|AC-TIME-3|G: link_platform_now は論理 100Hz の jiffy。C: 物理源。A: 単位の異なる代替時計を黙って返さない。読出は全三文脈、進行の約束は W2|`link_platform_now` (`P:net/link_platform.h:9`)、`tick_count`、`OS32_TICK_HZ`|既存 tick_count を利用。LAN §5 の境界へ移行|`P:arch/ppc/kernel_dec.c:31` の `tick_clock_config(1, tick_hz)` で満たす|`AC-TIME-3/jiffy-unit`（T1 で作る） / TIME・LAN §5|
|AC-SYNC-1|G: 保存状態は不透明、入れ子を逆順に復元する CPU ローカル排他。C: CPU 実装。A: SMP lock の代用不可。呼出は全三文脈、区間中は待たず、復帰は元の状態|`irq_save`・`irq_restore`・`_irq_enabled` (`include/io.h:92`)、`link_platform_lock/unlock` (`P:net/link_platform.h:13`)|EFLAGS (`arch/x86/arch_io.h:42`)|MSR (`arch/ppc/arch_io.h:6`)|`AC-SYNC-1/nested-restore`（T1 で作る） / TIME §2-3・LAN §5|
|AC-SYNC-2|G: `_idle`・`link_platform_idle` は §2 の復帰契約 (`_halt` は許可状態を保つ別の約束)。C: 睡眠または短い待機、電力削減は任意。A: IRQ/再入からの利用を許可しない。呼出 FG0/FG1、進行は許可後の IRQ、復帰 FG1|`_idle`・`_halt` (`include/io.h:108`)、`link_platform_idle` (`P:net/link_platform.h:21`)|不可分の許可・待機 (`arch/x86/arch_io.h:70`)|現 `_idle` は永久 spin (`arch/ppc/kernel_dec.c:64`)。P1d は `_enable`・idle work・`_halt` の回避実装 (`P:platform/powermac/link_platform.c:17`)。F2 で整合|`AC-SYNC-2/return-enabled-no-lost-wakeup`（T1 で作る、guest 必須） / 本書 §2・LAN §5|
|AC-IRQ-1|G: claim 成功=1、成功時全欄有効。不正は負、出力不変。deliver と needs_finish を分離し必要な完了だけ一度行う。C: topology/token。A: spurious は配達しない。操作は IRQ/FG0、進行は dispatcher|`irq_claim`・`platform_irq_claim/complete` (`include/platform_irq.h:13`)、`irq_topology` (`:8`)|`platform/pc98/irq_complete.c:9`。PIC 固有完了を保持|`platform/powermac/mpic.c:93` は失敗0・部分初期化。専用 dispatch の判定も F3 で同時修正。共通 dispatcher へ接続する前が関門、topology 結線は別段|`AC-IRQ-1/claim-spurious-complete`（T1 で作る） / 本書・PPC 立上げ順|

`platform_timer_program/sample` (`include/platform_timer.h:19`) は PIT 専用。汎用の時計照会や PowerPC の DEC 契約に流用しない。AC-TIME-2 の照会項目は現 `clock_info` には不足しており、F9 完了前に利用者が存在を仮定してはならない。

### 4.2 KAPI・入力・メモリ・起動

|ID|保証 / 能力 / 不在、文脈|正にする記号・所在|x86 の満たし方|PowerPC の満たし方|両アーキの試験 ID / 担当設計|
|---|---|---|---|---|---|
|AC-KAPI-1|G: slot 順・公開 ABI を保持。C: 機能群の利用可否。A: pointer=NULL、指定 query=0、その他の値=NOSYS、void は無動作と事前照会を対にする。文脈は元 API を継承し、不在 stub で拡張しない|`ABSENT_PROVIDER_SLOTS`・`ZERO_SLOTS`・`absent_result` (`sdk/kapi_target_ppc.py:251`、`:255`、`:269`)、生成分岐 (`sdk/gen_kapi.py:659`)|通常 provider と予約 slot。欠けた provider の型別試験も行う|不在群を生成。IME 等の既存例外を一律 query=0 に書き換えない。void と組にする照会の欠落は F8 で分類|`AC-KAPI-1/absent-types-capability`（T1 で作る） / ABI|
|AC-INPUT-1|G: 共通のキー符号・非待機 try、空=-1。C: pressed/raw/modifier/interruptible を独立照会。A: 能力なしを「未押下」と同一視しない。待機は前景で進行可能な場合だけ。IRQ 利用を新たに保証しない|`InputOps` (`include/input_service.h:10`)、`input_getkey_wait` (`:43`)。能力表現は F6 で追加|`platform/pc98/d2_bind.c:13` が既存入力へ委譲|`platform/powermac/input_boot.c:47` は interruptible 無視、pressed/modifiers は固定値。F6/F7 で区別し USB 状態の提供範囲を明示|`AC-INPUT-1/empty-vs-absent-cancel`（T1 で作る） / 本書・PPC 立上げ順|
|AC-MEM-1|G: 地図は byte の半開区間、probe は確認済み end PFN、予約域を除外。C: 採用範囲・属性。A: probe=0 は高 RAM 未確認であり物理 RAM 不在の断定ではない。probe は起動 FG0|`platform_memory_geometry/span`・`platform_memory_probe_high` (`include/platform_memory.h:7`、`:12`、`:21`)、`MEM_PAGE_SIZE` (`include/memmap.h:33`)|`platform/pc98/memory.c:42` と platform_memory_desc|`platform/powermac/memory.c:26` と platform_memory_desc。実機搭載量から未採用 RAM を使わず、compat BIOS 帯を実在 BIOS と解釈しない|`AC-MEM-1/ranges-reserved-unconfirmed`（T1 で作る） / MEM・PPC|
|AC-BOOT-1|G: 検証済み image だけ valid。C: 起動元・CRC 検証。A: 未検証は image_valid=0、既存 NONE を保持。捕捉は起動 FG0、読出は公開後|`boot_common`・`BOOTINFO_SRC_NONE` (`include/bootinfo.h:5`、`:8`)|`platform/pc98/boot_capture.c:4`|`platform/powermac/boot_capture.c:5`。OF 起動を FD/HDD に偽装しない|`AC-BOOT-1/unverified-image`（T1 で作る） / PPC 立上げ順|
|AC-BOOT-2|G: boot-token は起動中不変、サイズは宣言に従う。認証・暗号強度・絶対一意性は保証しない。C: 生成材料。A: 未初期化を有効 token として公開しない。初期化は起動 FG0、利用はその後|`boot_token_init/get`・`platform_boot_token` (`include/boot_token.h:4`)、保持 (`kernel/boot_token.c:4`)|`platform/pc98/boot_token_pc98.c:9`|`platform/powermac/boot_token_powermac.c:7`。既に共通化済みで作り直さない|`AC-BOOT-2/stable-after-init`（T1 で作る） / LAN §5・PPC|

### 4.3 表示・字形・Host/SFS・ビルド

|ID|保証 / 能力 / 不在、文脈|正にする記号・所在|x86 の満たし方|PowerPC の満たし方|両アーキの試験 ID / 担当設計|
|---|---|---|---|---|---|
|AC-DISPLAY-1|G: geometry と safe を照会して文字を出す。C: 形状・色。A: safe でない画面へ描かない、raw 属性の共通意味を仮定しない。文脈は console の既存 safe 条件を継承|`con_backend`・`CON_COLOR_DEFAULT`・`con_backend_safe`・`CON_WIDE_COLS` (`kernel/console/console_backend.h:8`)|PC98 console (`platform/pc98/console.c:26`)|FB console (`platform/powermac/console.c:11`)、raw 属性は既定へ正規化|`AC-DISPLAY-1/geometry-safe-attributes`（T1 で作る） / HAL・PPC §5|
|AC-DISPLAY-2|G: provider は不在用を含め非 NULL backend。C: scanout と描画、内部と公開 KAPI を別々に照会。A: 未対応操作は能力なし、成功を捏造しない。列挙・操作文脈は HAL に従う|`display_provider_select` (`include/display_provider.h:23`)、`GFX2_Caps`・`gfx2_caps` (`sdk/include/os32/os32_gfx2.h:118`、`:127`)|`platform/pc98/gfx_selector.c:69`、共通 HAL|`platform/powermac/display_provider.c:38`。内部 HAL のリンクと公開 gfx2 不在 (`sdk/kapi_target_ppc.py:204`) を区別|`AC-DISPLAY-2/internal-vs-public-caps`（T1 で作る） / HAL §3・§4・§9、ABI|
|AC-GLYPH-1|G: ANK/漢字の寸法は共通ヘッダに従う。C: ROM/resident file、内部と公開の可否。A: 字体読込み失敗を成功扱いせず画面の可否へ反映。resident 化後の描画と起動時読込みを区別|`KCG_ANK_W/H`・`KCG_KANJI_W/H` (`include/kcg_contract.h:7`)、`kcg_read_ank/kanji` (`:25`、`:29`)|ROM (`drivers/video/kcg/kcg.c:45`)|file (`drivers/video/kcg/kcg_file.c:85`、`:104`)、公開 KCG は不在 (`sdk/kapi_target_ppc.py:106`)|`AC-GLYPH-1/dimensions-missing-font`（T1 で作る） / HAL・PPC §1・§5|
|AC-HOST-1|G: owner・handle の寿命、部分 write、借用 read buffer は次操作前にコピー。C: transport。A: HOSTDOWN/STALE を保存、不在は型別。文脈は Host API の既存契約を継承し IRQ 待機を導入しない|`platform_host_*` (`include/platform_host.h:6`)、`host_alive_snapshot` (`net/host_mailbox.h:15`)|`platform/pc98/device_host.c:31`|main の公開 Host 群は不在。PLN1 は代替 ABI ではない。LAN 移行で `platform/powermac/hostfs_peer.c:14` の世代実体を共通化し二重リンクを防ぐ|`AC-HOST-1/owner-buffer-generation`（T1 で作る） / LAN §5・ABI|
|AC-SFS-1|G: can_wait は IRQ 外かつ期限時計と transport が協調して進められること。絶対期限を全段で共有し、新 sid で write を再実行しない。C: IRQ 駆動/poll。A: can_wait=0 は要求を出さず IO。FG0/FG1 の可否は provider ごとに判定|`SfsIo.now/idle/can_wait` (`fs/serialfs/sfs_client.h:29`)、deadline 契約 (`:96`)|UART/LAN の既存 IF 条件を保持 (`fs/serialfs/serialfs_session.c:79`、`fs/serialfs/sfs_lan.c:150`)|`pm_hfs_can_wait` は IRQ 外、TB 時計と poll により FG0 でも進行 (`platform/powermac/hostfs_io.c:37`、`:46`)。現実装を保ち F1 は注釈・試験だけ|`AC-SFS-1/fg0-timeout-irq-reject-no-replay`（T1 で作る） / LAN §5|
|AC-BUILD-1|G: 共通口の provider と共通ソースの closure を両像で検査。C: 像の profile・能力一覧。A: 不在は明示 manifest、未解決参照を成功 stub で埋めない。実行文脈は対象外|`C_KERNEL` 等のソース選択 (`build/sources/common.mk:1`)、順序 (`build/sources/order.mk:1`)、PPC 選択 (`build/ppc-kboot.mk:71`)|`build/config.mk:142` の arch/platform 選択|PPC SH 像もリンク。KBOOT_SH/SHELL は像の選択であり CPU 能力ではない。BE/ILP32 を含む ABI 検査を保つ|`AC-BUILD-1/closure-profiles`（T1 で作る） / BUILD・PPC_IMPL_RULES §1|

## 5. W2 で確定する項目

以下は名前と境界の予約だけであり、W1 の容量・予算・進行保証にはしない。担当は **os32powerpc の P1 系列、本線はレビューだけ**。

|予約 ID|W2 で確定する項目|正にする既存口・所在 / 担当設計|
|---|---|---|
|AC-NET-1|credit の admission と消費の単位（NE2K の page・e82557 の slot・GEM の descriptor）|`NetNicOps.rx_credit_bytes` (`net/nic.h:16`)、`LINK_STREAM_BUF`・`LINK_MAXFRAME_PAGES`・`LINK_CREDIT_MARGIN`・`LINK_PAGE_BYTES` (`net/link.h:123`、`:127`) / LAN|
|AC-NET-2|RX の予算（総走査数と時間、サービス単位、単一 RX 所有者）|`NET_RX_SCAN_MAX` (`net/nic.h:47`)、`RX_SLOTS`・`RX_FRAME_BYTES` (`net/nic_rx.c:11`)、`poll` (`P:platform/powermac/nic_gem.c:44`) / LAN|
|AC-NET-3|link の進行（EE=0 の間の RTO、close 時の保留 ACK）|`LINK_RTO_TICKS` (`net/link.h:112`)、`link_free_handle` (`P:net/link_host_api.inc:334`、ACK 消去 `:354`・`:357`) / LAN、link-closeack|

P1g の credit は空き枠から算出する修正済みとの PM 報告を前提とするが、保留が生じると既存 admission の閾値に近づく点は W2/F5 の受入に残る。P1f の小容量 adapter を像へ入れると発火する型の欠陥として扱い、「main に無いから問題なし」とはしない。
GEM の RX は poll から gem_rx を一度呼ぶ形を確認した。外側ループとの積で走査量を数えるという旧調査の懸念は訂正し、時間予算を持ち越す。再送回数の観測は台本の sleep によるとの PM 訂正を採用し、原因未確定の不具合として残さない。EE=0 の RTO は本節の担当へ集約し、時計の単位とは混同しない。

## 6. W1 の後の段

各行は試験・登録・fixture の追従を含め **1〜3 ファイル・差分 400 行以下**。表の行数は上限目安。超える場合は発注前に分割し、契約切替を半端な状態で接続しない。既存試験のリンク一覧・固定 hash・dep-dir・変異 anchor を発注時に照合する。

|段|担当|対象（最大 3 ファイル）/ 差分目安|依存・完了条件|
|---|---|---|---|
|W1 採用|本線|本書、`docs/INDEX.md`、`docs/POLICY_DEV.md` / 300 行|独立レビュー後。§7 を結線、状態行の案の注記を外す。実装適合とは別|
|W2|PowerPC P1、本線レビュー|`LAN_OPERATOR_DESIGN.md`、必要な参照整合のみ `TICK_ARCH_DESIGN.md` / 180 行|W1、P1d〜P1g の実物確認。§5 を確定し link-closeack と整合、独自時計を増やさない|
|T1a|本線、PowerPC が provider 確認|新 `tools/tests/test_arch_compat.py`、新 `tools/tests/arch_compat_vectors.c` / 350 行|W1。CHECK inputs/covers と共通 ID の土台。まず TIME/SYNC/IRQ を登録、未実装ケースを合格にしない|
|T1b〜T1d（各段）|本線、PowerPC が provider 確認|T1a の 2 ファイル、必要なら領域 fixture 1 本 / 各 350 行|T1a。順に KAPI/INPUT、MEM/BOOT、DISPLAY/GLYPH/HOST/SFS を追加。各領域の同じ期待値を両 provider へ結ぶ|
|F1|本線|`fs/serialfs/sfs_client.h`、T1 の 2 ファイル / 150 行|T1d。can_wait と関連する IF=0/idle 注釈だけを進行可能性に整合。PPC 製品実装は変更せず FG0 の期限・IRQ 拒否・write 非再実行を試験|
|F2a|PowerPC|`arch/ppc/kernel_dec.c`、`arch/ppc/arch_io.h`、`arch/ppc/ppc_trap.h` / 150 行|P1d・W1・T1a。noreturn 依存と全呼出し元を確認し、idle が EE 許可のまま戻る原始操作を揃える|
|F2b|PowerPC|`platform/powermac/link_platform.c`、T1 の 2 ファイル / 180 行|F2a・W2。**link_platform_idle を _idle() に戻す**。idle work の順序・再入条件を保持。両 guest で復帰・許可・再検査を確認して F2 を完了|
|F3|PowerPC|`platform/powermac/mpic.c`、T1 の 2 ファイル / 240 行|T1a、共通 dispatcher 接続前。全欄・負の返値・失敗時出力不変と専用 dispatch の成功判定を同時整合、spurious/完了一度を試験|
|H1|本線、PowerPC 照合|`net/nic_rx.c`、`net/nic.h`、対応する既存 RX 試験 / 150 行|W1 の直後 (値を保って移すだけなので W2 を待たない)。`RX_SLOTS`・`RX_FRAME_BYTES` を共有ヘッダへ移し値を維持。変異 anchor と公開名参照を追従|
|F4|本線、PowerPC レビュー|`net/nic.h`、T1 の 2 ファイル / 250 行|W2・H1。確定した消費単位・容量・総走査を LGY/e82557/GEM 共通ベクトルへ。W1 の旧案の閾値を oracle にしない|
|F5|PowerPC P1、本線レビュー|`platform/powermac/nic_gem.c`、同 `nic_gem.h`、対応する既存 GEM adapter 試験 / 300 行|P1 節目の main 取り込み・SHA 照合、W2・F4。P1f/P1g の実装は重複発注せず、時間予算と admission 閾値の扱いだけ。adapter 内の容量リテラルは確定したヘッダ記号へ。保留時境界と単一 RX 所有を確認|
|F6|本線|`include/input_service.h`、`kernel/input_service.c`、対応する既存入力試験 / 220 行|W1・T1b。内部能力照会と不在表現、provider 未指定時の扱いを確定。公開 KAPI 追加は別の ABI 段|
|F7|PowerPC|`platform/powermac/input_boot.c`、同 `input_usb.c`、対応する入力試験 / 250 行|F6。能力と実状態の一致、interruptible の提供可否。既存文字入力と G4 実機 USB キーボードの回帰を含む|
|F8|本線、PowerPC 照合|`sdk/kapi_target_ppc.py`、T1 の 2 ファイル / 220 行|T1b・T1d。型別不在・void と照会の対応・内部描画と公開不在を検査。照会の無い群は利用不可とし ABI 追加を勝手に行わない|
|F9a (任意)|本線、PowerPC レビュー|`include/clock_source.h`、T1 の 2 ファイル / 220 行|W1・T1a。源・分解能・停止中進行の内部照会を宣言。未提供は unknown として利用側で拒否|
|F9b (任意)|本線、PowerPC が PPC 実体担当|`platform/pc98/clock_source.c`、`platform/powermac/clock.c`、対応する時計試験 / 250 行|F9a。能力を実際の源に一致させ、counter_hz の既存意味を保持。両実体の登録が済むまで利用側を結線しない|
|B1|本線、PowerPC 照合|`build/ppc-kboot.mk`、`build/sources/common.mk`、T1 試験 / 250 行|T1a、対象の共通変更後。差分 manifest と両像リンクで closure を検査、大規模な一覧移動は行わない|
|W3|本線、PowerPC 確認|本書、`PPC_NOTES.md`、`ARCH_BRINGUP_ORDER.md` / 150 行|各担当段の受入後。適合 ID と能力差を結線、未実機項目は延期台帳の関門へ。試験ログを契約本文へ複製しない|

T1 は試験土台の作成であり適合完了ではない。fake では単位・周回・返値・容量境界を検証し、idle と割り込み進行は NP21/W と mac99 の guest probe で同じ契約 ID を記録する。ホスト時計だけの一致で実機時計精度を主張せず、NIC 飽和と G4 入力は実機の受入に残す。
実装段は対象像をビルドし、共通変更では PPC SH 像もリンク、追加・変更試験の変異が実行時 RED になることと lint を確認する。全体検査は GitHub CI、全変異は full-check。最後のソース変更後のログだけを受入に使い、未実施は `docs/tasks/DEFERRED_TESTS.md` の関門と結ぶ。W1 自体の成果物は設計稿のみ。

## 7. POLICY_DEV §1 への結線

別のアーキテクチャの設計 §1 の三つの判断に、「ARCH_COMPAT の該当 ID・能力差・両アーキ共通の試験」を添える。

## 8. 未決

ユーザー判断を要する未決は **0 件**。推奨は、W1 の保証と担当境界を採用し、§5 の仕様決定は PowerPC P1 系列の W2 でレビューして確定すること。P1g の SHA 照合・試験 ID 登録・実機受入は担当工程の作業であり、ユーザー判断の未決には数えない。


## 確定 (2026-11-14、Opus 5.5 第 2 回は「確定してよい」、P3 の 4 件を PM が反映)
- A1: `_halt` を idle の復帰契約から外し、「許可状態を保つ」を別に書いた。
- A2: §3 に領域名の表と、足すときの規則を置いた。
- A3: 適合の状態は、全項目を「未 (T1 で確かめる)」とし、W3 で「済」に書き換えると決めた。
- A4: 状態行を確定に書き換えた。測定値は本文から外し、PowerPC の受入の記録を参照する形にした。
- 段: H1 は W1 の直後に置き、W2 を待たない。F9a・F9b (時計の源の照会) は観測された不具合から来た段ではないので、任意の段とした (発注は PM が決める)。
