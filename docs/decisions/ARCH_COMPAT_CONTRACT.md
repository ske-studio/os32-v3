# ARCH_COMPAT_CONTRACT — アーキテクチャ間の互換契約

> 2026-11-20 の見直しで x86 の形に改めた ([DESIGN_X86_AUDIT](ppc/DESIGN_X86_AUDIT.md))。

> 状態: **確定 (2026-11-14)** — アーキ間の互換契約の正典 (ユーザーの提案「最大公約数と最小公倍数の互換層」、astra 起草・Opus 5.5 の独立レビュー 2 回・PowerPC PM の見直し)。契約の確定と実装の適合は分け、適合は §4 の各項目と W3 で更新する。

## 1. 方針と参照の正

既存の口に「単位・能力・進行文脈・最小容量・不在」を足す。共通コードが頼れる保証を最大公約数、能力照会を含む口の集合を最小公倍数とし、巨大な HAL は挟まない。装置の性能や容量を一律に切り下げない。
数値は C のヘッダを正とし、本文には記号名と `file:line` だけを置く。例外は link 時計の単位 (論理 100Hz の jiffy) と、返値・文脈の表記。満たしていることの測定は PowerPC の受入の記録 (TASK_PPC_LAN) にあり、本書には写さない。実装内の定数は所在を示し、公開が必要なものを §6 でヘッダへ移す。公開 ABI の正は引き続き `sdk/kapi.json`、生成物は編集しない。

|参照名|担当する正典（本書では詳細を複製しない）|
|---|---|
|TIME|[TICK_ARCH_DESIGN §2](time/TICK_ARCH_DESIGN.md#2-arch-に依らない-tick_clock-の口) — 有理周期・読み出し・駆動|
|IO_TIER|[IO_TIER_DESIGN §3・§4](IO_TIER_DESIGN.md) — 協調進行・µs 時計・設定表|
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

FG0/FG1 は割り込みの状態であり、ユーザー/カーネルという呼出元の由来とは別軸である。ユーザー syscall も排他中は FG0、通常カーネル前景も許可中は FG1。

各口は `呼出={文脈}; 進行={文脈:駆動主体}; 復帰={EE/IF・待ち条件}` を記す。単なる読み出しは「進行=なし」、起動限定は文脈に「起動時」を添える。既存宣言で保証しない文脈は利用可と推定しない。未記載は担当段で確定するまで共通利用の根拠にしない。
`_idle` と `link_platform_idle` は前景専用で **EE/IF を許可したまま戻る**。`_halt` は EE/IF を許可しない (呼び手が先に許可する) 原始操作で、許可状態を保って戻る。この復帰契約には含めない。復帰後の条件再検査を可能にする。`_idle` は CPU を次の割り込みまで休ませ (空回りしない。x86 は `hlt`、PowerPC 7450/7455 は NAP、ARM は `wfi` に当たる)、周期のタイマか装置の割り込みで遅くとも 1 tick 以内に戻る。固定の待ち時間は保証しない。待機の原始操作には取りこぼしの窓を作らない。`_stop` の永久停止とは別契約とする。

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
|CTX|実行文脈・入口/出口・前景の進行|
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
|IO|I/O の協調進行・予算・設定表|

適合の状態は、本稿の時点では両アーキとも試験で確かめていないので、全項目を「未 (T1 で確かめる)」とする。T1 の契約試験が両アーキで通った項目から、W3 で「済」に書き換える。

## 4. 項目の表

### 4.0 実行文脈

実装前のため AC-CTX-1〜7 は両アーキとも適合 **未**。受入条件と段は [ARCH_CTX_DESIGN](ARCH_CTX_DESIGN.md) を参照する。

|ID|G: 保証|C: 能力|A: 不在・禁止|
|---|---|---|---|
|AC-CTX-1|通常ユーザー syscall は arch が完全なフレーム・安全な kernel stack・翻訳を用意してから IF/EE を許可し、共通 dispatch に FG1 で渡す。短い排他は保存/復元し、正常本体終了時も FG1|入口形式・保存方法は arch。V86/起動/異常例外は別入口|入口未適合の像を FG1 必須の共通サービスへ接続しない。個別 I/O で不足を隠さない|
|AC-CTX-2|FG1 を IRQ が中断し、レジスタ・PC・stack・翻訳・許可状態を保って戻れる。IRQ 本体では待機/前景 dispatcher を呼ばない|IRQ→IRQ の再許可は別能力。初版 x86/PPC とも提供なし|深さ0だけで IRQ 出口/例外を前景扱いしない。IRQ の再許可を共通コードが要求しない|
|AC-CTX-3|通常 kernel 前景は各入口に許可可能条件を宣言。FG0 の排他区間を勝手に FG1 へ変えず、所有者が区間終了時に復元する|起動完了・stack/root 有効・trap slot 非保持で IRQ を受けられる kernel 前景を arch が申告。PPC はこの限定範囲だけ、PR=0 trap 内は含めない|能力なしの kernel 経路では idle_work は何もせず戻り、次の FG1 協調点で回る。待ちしか進行手段がないサービスは接続しない|
|AC-CTX-4|idle_work/Tier は FG1・IRQ外・通常前景の協調点のみ。再入は仕事なし、予算/単一所有は既存設計どおり。callback は許可状態と所有を復元して戻る|登録サービスと予算は IO_TIER の能力|IRQ出口/例外/排他中に実行しない。idle_work の既存 void 無処理を保持。結果付き口は各既存エラー/再試行規則に従う|
|AC-CTX-5|sys_halt/yield・I/O待ちは FG1 で条件再検査を繰返す。IRQ・共有busy/lock保持・再入状態で待たない。終了/park/kill もフレーム・深さ・所有状態を整合|halt は AC-SYNC-2 のとおり CPU を休ませ、1 tick 以内に戻る。FG0 poll 待ちは AC-SFS-1 のような時計＋transport の個別保証がある口のみ|FG0 の一般的待ちを新たに保証しない。I/O の口で FG1 不足を自動 enable して救済しない。AC-SYNC-2 の原始操作（_idle、link_platform_idle の _enable、`platform/powermac/link_platform.c`）の許可は別枠で残す。trap busy を持つ文脈（PR=0 の trap dispatch）と IRQ からは呼ばない。無能力は型別エラーで要求前に返す|
|AC-CTX-6|起動完了後の正常 FG1 syscall/待ち/ユーザー計算中、timer IRQ を抑止し続けない。短い FG0・IRQ処理から有限時間で許可文脈へ戻す。PPC は DEC 再arm・経過分 step を維持|物理源/停止中に読む時計は AC-TIME-1/2/4。マスク中も µs が進むことと tick 配達を区別|故障・stop・無限排他・IRQ占有下で時刻進行は保証しない。長時間禁止中の x86 tick 全回収を保証しない|
|AC-CTX-7|入口/出口・同期例外の所有は arch。共有 scratch/SRR/stack の再利用前に保存し、復元途中は IRQ 禁止。irq_depth は一つの IRQ entry に一度加減する。onfault の arm から clear までは irq_save で囲み終了後に復元する（`platform/powermac/lan_monitor.c` の pm_lan_copy8 の型）。probe は1個の大域（`arch/ppc/kernel_trap.c`・`ppc_trap_onfault_data()`）なので IRQ handler の onfault と衝突させない|同期 fault の回復範囲・専用 slot 数は arch。PPC は独立 spill 1段|spill 失敗＋trap busy 等の非対応入れ子は既存 fatal。例外を処理するために EE を許可しない。trap busy 保持中に EE が閉じる根拠は、例外入口で CPU が EE=0 にすること（`kernel_vectors.S`・`ppc_kernel_frame_complete`）と、許可点が PR=1 の 0xc00 の枝だけであること|

### 4.1 時計・排他・IRQ

正常な FG1 での timer IRQ の進行は AC-CTX-6 に従う。物理源と論理周期の契約は変わらない。

|ID|保証 / 能力 / 不在、文脈|正にする記号・所在|x86 の満たし方|PowerPC の満たし方|両アーキの試験 ID / 担当設計|
|---|---|---|---|---|---|
|AC-TIME-1|G: 物理周期は有理数、公開ジフィと ms を混同しない。C: 物理源。A: 不正 config は状態不変でエラー。config は起動 FG0、step は IRQ、ms は全三文脈、raw/jiffy64 は IRQ または FG0。進行は step|`OS32_TICK_HZ` (`sdk/include/os32/os32_kapi_shared.h`)、`TICK_CLOCK_MS_HZ`・`TICK_CLOCK_EINVAL`・`tick_clock_*` (`kernel/time/tick_clock.h`)|PIT 周期を設定 (`platform/pc98/clock_source.c`)、共通 tick|DEC 端数配分と共通 tick (`arch/ppc/kernel_dec.c`)。一律の割り込み回数でジフィを割らない|`AC-TIME-1/rational-wrap-context`（T1 で作る） / TIME §2|
|AC-TIME-2|G: read_us 成功時のみ µs 値有効、単位は精度を保証しない。C: 源・分解能・割り込み停止中の進行を照会可能にする。A: 未準備 NOSYS、再試行 AGAIN。呼出は IRQ/FG0、進行は時計源依存|`clock_info`・`platform_clock_read_us` (`include/clock_source.h`)、`arch_tick_budget_*` (`arch/x86/arch_tick_budget.h`、`arch/ppc/arch_tick_budget.h`)|TSC/PIT (`platform/pc98/clock_source.c`)。counter_hz を使用中の µs 源と決めつけない|TB (`platform/powermac/clock.c`)。予算の内部 count を µs と決めつけず換算口を使う|`AC-TIME-2/source-unavailable`（T1 で作る、照会は F9） / TIME・PPC §4|
|AC-TIME-3|G: link_platform_now は論理 100Hz の jiffy。C: 物理源。A: 単位の異なる代替時計を黙って返さない。読出は全三文脈、進行の約束は W2|`link_platform_now` (`P:net/link_platform.h`)、`tick_count`、`OS32_TICK_HZ`|既存 tick_count を利用。LAN §5 の境界へ移行|`P:arch/ppc/kernel_dec.c` の `tick_clock_config(1, tick_hz)` で満たす|`AC-TIME-3/jiffy-unit`（T1 で作る） / TIME・LAN §5|
|AC-SYNC-1|G: 保存状態は不透明、入れ子を逆順に復元する CPU ローカル排他。C: CPU 実装。A: SMP lock の代用不可。呼出は全三文脈、区間中は待たず、復帰は元の状態|`irq_save`・`irq_restore`・`_irq_enabled` (`include/io.h`)、`link_platform_lock/unlock` (`P:net/link_platform.h`)|EFLAGS (`arch/x86/arch_io.h`)|MSR (`arch/ppc/arch_io.h`)、前景の条件は AC-CTX-3|`AC-SYNC-1/nested-restore`（T1 で作る） / TIME §2-3・LAN §5|
|AC-SYNC-2|G: `_idle`・`link_platform_idle` は §2 の復帰契約 (`_halt` は許可状態を保つ別の約束)。C: 次の割り込みまで CPU を休ませる (空回りしない)、周期のタイマか装置の割り込みで遅くとも 1 tick 以内に戻る。A: IRQ/再入からの利用を許可しない。呼出 FG0/FG1、進行は許可後の IRQ、復帰 FG1|`_idle`・`_halt` (`include/io.h`)、`link_platform_idle` (`P:net/link_platform.h`)|不可分の許可・待機 (`arch/x86/arch_io.h`)|PowerPC は 7450/7455 で NAP (`arch/ppc/kernel_dec.c` の arch_cpu_idle、ppc-cpu-nap)。原始操作の許可は AC-CTX-5 の別枠として残し、PR=0 の trap dispatch と IRQ からは呼ばない|`AC-SYNC-2/return-enabled-no-lost-wakeup`（T1 で作る、guest 必須） / 本書 §2・LAN §5|
|AC-IRQ-1|G: claim 成功=1、成功時全欄有効。不正は負、出力不変。deliver と needs_finish を分離し必要な完了だけ一度行う。C: topology/token。A: spurious は配達しない。操作は IRQ/FG0、進行は dispatcher|`irq_claim`・`platform_irq_claim/complete` (`include/platform_irq.h`)、`irq_topology` |`platform/pc98/irq_complete.c`。PIC 固有完了を保持|`platform/powermac/mpic.c` は失敗0・部分初期化。専用 dispatch の判定も F3 で同時修正。共通 dispatcher へ接続する前が関門、topology 結線は別段|`AC-IRQ-1/claim-spurious-complete`（T1 で作る） / 本書・PPC 立上げ順|

`platform_timer_program/sample` (`include/platform_timer.h`) は PIT 専用。汎用の時計照会や PowerPC の DEC 契約に流用しない。AC-TIME-2 の照会項目は現 `clock_info` には不足しており、F9 完了前に利用者が存在を仮定してはならない。

### 4.1.1 I/O の時計・協調進行・設定表

IO_TIER §3 の契約を追加する。AC-TIME-4 の適合は両アーキとも **未**。AC-IO の検証範囲は下表。guest の受入は未実施。

|ID|契約（IO_TIER §3）|正にする口 / 担当設計|
|---|---|---|
|AC-TIME-4|io_clock_usはIRQ/FG0/FG1から読める単調µs時計。成功時のみ出力有効、未準備NOSYS・一時失敗AGAIN。短いirq_save/restoreを内包し、源・分解能・停止中進行をAC-TIME-2と揃える。|`io_clock_us` (`kernel/io_work.h`) / IO_TIER §3・§4|
|AC-IO-1|FG1の協調進行・再入無動作・欠如NOSYS。|`io_dispatch` (`kernel/io_work.h`)・`io_pump` / IO_TIER §3・§4、文脈は AC-CTX-1・4|
|AC-IO-2|設定の検証・原子的置換・LE形式・owner終了時復元。|`io_policy_set/get/reset` / IO_TIER §3・§4（公開口 `kernel/io_kapi.c`、KAPI v87）|

|ID|x86 の適合範囲|PowerPC の適合範囲|
|---|---|---|
|AC-IO-1|host: 公開4口の FG1 制限、pump の予算/INVAL/NOSYS、共通 syscall の自動/明示二重巡回防止・累計差。入口からの guest 復帰は **未**|host BE: 同じ公開境界と pump。EE/DEC を含む guest 復帰は **未**|
|AC-IO-2|host LE: 境界 INVAL/BUSY/FULL、owner/世代、実 exec_finish/kill の共通回収と親子逆順復元。guest は **未**|host BE: 同じ bytes と境界・回収を user-mode 実行。guest は **未**|

試験: `io-kapi-host`・`io-policy-host`・`io-work-host`・`exec-syscall-fgpoll-host`。
C4/P1: x86 `int80_stub` と PPC `ppc_trap_dispatch` は許可後に共通 dispatch へ渡す。
N4 に従い I/O 専用 adapter は追加しない。上表は arch 入口全体の適合を宣言するものではない。

### 4.2 KAPI・入力・メモリ・起動

|ID|保証 / 能力 / 不在、文脈|正にする記号・所在|x86 の満たし方|PowerPC の満たし方|両アーキの試験 ID / 担当設計|
|---|---|---|---|---|---|
|AC-KAPI-1|G: slot 順・公開 ABI を保持。C: 機能群の利用可否。A: pointer=NULL、指定 query=0、その他の値=NOSYS、void は無動作と事前照会を対にする。文脈は元 API を継承し、不在 stub で拡張しない|`ABSENT_PROVIDER_SLOTS`・`ZERO_SLOTS`・`absent_result` (`sdk/kapi_target_ppc.py`)、生成分岐 (`sdk/gen_kapi.py`)|通常 provider と予約 slot。欠けた provider の型別試験も行う|不在群を生成。IME 等の既存例外を一律 query=0 に書き換えない。void と組にする照会の欠落は F8 で分類|`AC-KAPI-1/absent-types-capability`（T1 で作る） / ABI|
|AC-INPUT-1|G: 共通のキー符号・非待機 try、空=-1。C: pressed/raw/modifier/interruptible を独立照会。A: 能力なしを「未押下」と同一視しない。待機は前景で進行可能な場合だけ。IRQ 利用を新たに保証しない|`InputOps` (`include/input_service.h`)、`input_getkey_wait` 。能力表現は F6 で追加|`platform/pc98/d2_bind.c` が既存入力へ委譲|`platform/powermac/input_boot.c` は interruptible 無視、pressed/modifiers は固定値。F6/F7 で区別し USB 状態の提供範囲を明示|`AC-INPUT-1/empty-vs-absent-cancel`（T1 で作る） / 本書・PPC 立上げ順|
|AC-MEM-1|G: 地図は byte の半開区間、probe は確認済み end PFN、予約域を除外。C: 採用範囲・属性。A: probe=0 は高 RAM 未確認であり物理 RAM 不在の断定ではない。probe は起動 FG0|`platform_memory_geometry/span`・`platform_memory_probe_high` (`include/platform_memory.h`)、`MEM_PAGE_SIZE` (`include/memmap.h`)|`platform/pc98/memory.c` と platform_memory_desc|`platform/powermac/memory.c` と platform_memory_desc。実機搭載量から未採用 RAM を使わず、compat BIOS 帯を実在 BIOS と解釈しない|`AC-MEM-1/ranges-reserved-unconfirmed`（T1 で作る） / MEM・PPC|
|AC-BOOT-1|G: 検証済み image だけ valid。C: 起動元・CRC 検証。A: 未検証は image_valid=0、既存 NONE を保持。捕捉は起動 FG0、読出は公開後|`boot_common`・`BOOTINFO_SRC_NONE` (`include/bootinfo.h`)|`platform/pc98/boot_capture.c`|`platform/powermac/boot_capture.c`。OF 起動を FD/HDD に偽装しない|`AC-BOOT-1/unverified-image`（T1 で作る） / PPC 立上げ順|
|AC-BOOT-2|G: boot-token は起動中不変、サイズは宣言に従う。認証・暗号強度・絶対一意性は保証しない。C: 生成材料。A: 未初期化を有効 token として公開しない。初期化は起動 FG0、利用はその後|`boot_token_init/get`・`platform_boot_token` (`include/boot_token.h`)、保持 (`kernel/boot_token.c`)|`platform/pc98/boot_token_pc98.c`|`platform/powermac/boot_token_powermac.c`。既に共通化済みで作り直さない|`AC-BOOT-2/stable-after-init`（T1 で作る） / LAN §5・PPC|

### 4.3 表示・字形・Host/SFS・ビルド

|ID|保証 / 能力 / 不在、文脈|正にする記号・所在|x86 の満たし方|PowerPC の満たし方|両アーキの試験 ID / 担当設計|
|---|---|---|---|---|---|
|AC-DISPLAY-1|G: geometry と safe を照会して文字を出す。C: 形状・色。A: safe でない画面へ描かない、raw 属性の共通意味を仮定しない。文脈は console の既存 safe 条件を継承|`con_backend`・`CON_COLOR_DEFAULT`・`con_backend_safe`・`CON_WIDE_COLS` (`kernel/console/console_backend.h`)|PC98 console (`platform/pc98/console.c`)|FB console (`platform/powermac/console.c`)、raw 属性は既定へ正規化|`AC-DISPLAY-1/geometry-safe-attributes`（T1 で作る） / HAL・PPC §5|
|AC-DISPLAY-3| 表示提供者が認めた束を query 順で一括取得し、取得失敗時は先行面も公開しない | `gfx_surface_lease` | PC98 DISPLAY 4 件 | OF_FB CLIENT 1 件（公開配線は 5c） | 共通 host の 4 件・1 件取得と失敗時の復元 / [束の設計 §5](display/SURFACE_LEASE_BUNDLE_DESIGN.md) |
|AC-DISPLAY-2|G: provider は不在用を含め非 NULL backend。C: scanout と描画、内部と公開 KAPI を別々に照会。A: 未対応操作は能力なし、成功を捏造しない。列挙・操作文脈は HAL に従う|`display_provider_select` (`include/display_provider.h`)、`GFX2_Caps`・`gfx2_caps` (`sdk/include/os32/os32_gfx2.h`)|`platform/pc98/gfx_selector.c`、共通 HAL|`platform/powermac/display_provider.c`。内部 HAL のリンクと公開 gfx2 不在 (`sdk/kapi_target_ppc.py`) を区別|`AC-DISPLAY-2/internal-vs-public-caps`（T1 で作る） / HAL §3・§4・§9、ABI|
|AC-GLYPH-1|G: ANK/漢字の寸法は共通ヘッダに従う。C: ROM/resident file、内部と公開の可否。A: 字体読込み失敗を成功扱いせず画面の可否へ反映。resident 化後の描画と起動時読込みを区別|`KCG_ANK_W/H`・`KCG_KANJI_W/H` (`include/kcg_contract.h`)、`kcg_read_ank/kanji` |ROM (`drivers/video/kcg/kcg.c`)|file (`kernel/console/font_source_kcg.c`)、公開 KCG は不在 (`sdk/kapi_target_ppc.py`)|`AC-GLYPH-1/dimensions-missing-font`（T1 で作る） / HAL・PPC §1・§5|
|AC-HOST-1|G: owner・handle の寿命、部分 write、借用 read buffer は次操作前にコピー。C: transport。A: HOSTDOWN/STALE を保存、不在は型別。文脈は Host API の既存契約を継承し IRQ 待機を導入しない|`platform_host_*` (`include/platform_host.h`)、`host_alive_snapshot` (`net/host_mailbox.h`)|`platform/pc98/device_host.c`|main の公開 Host 群は不在。PLN1 は代替 ABI ではない。LAN 移行で `platform/powermac/hostfs_peer.c` の世代実体を共通化し二重リンクを防ぐ|`AC-HOST-1/owner-buffer-generation`（T1 で作る） / LAN §5・ABI|
|AC-SFS-1|G: can_wait は IRQ 外かつ期限時計と transport が協調して進められること。絶対期限を全段で共有し、新 sid で write を再実行しない。C: IRQ 駆動/poll。A: can_wait=0 は要求を出さず IO。FG0/FG1 の可否は provider ごとに判定|`SfsIo.now/idle/can_wait` (`fs/serialfs/sfs_client.h`)、deadline 契約 (`sfs_client_call()`)|UART/LAN の既存 IF 条件を保持 (`fs/serialfs/serialfs_session.c`、`fs/serialfs/sfs_lan.c`)|`pm_hfs_can_wait` は IRQ 外、TB 時計と poll により FG0 でも進行 (`platform/powermac/hostfs_io.c`)。現実装を保ち F1 は注釈・試験だけ|`AC-SFS-1/fg0-timeout-irq-reject-no-replay`（T1 で作る） / LAN §5|
|AC-LIBC-1|G: ユーザーランドの C ライブラリはどのアーキも newlib nano (x86 と同じ版・configure・無改変)。syscall は KAPI の関数表、sbrk は nano の adapter。C: アーキごとの crt0・syscalls.c・allocator の adapter・浮動小数点の旗。A: 自前の libc やアプリごとの代用の関数で置き換えない。カーネルは newlib とリンクしない (ユーザー決定 2026-11-16、POLICY_DEV §1)|`tools/ci/build_cross.sh` の NEWLIB_VER・configure、`sdk/allocator/nano_inputs.json`、`sdk/crt/syscalls.c`|i386-elf の newlib nano 4.4.0 を `-lc -lgcc` で静的にリンク (`build/programs.mk`)|**未** (ユーザーランドは libc なし、`build/ppc-user.mk`)。PowerPC の段で入れる|共通の試験は移植の段で決める / POLICY_DEV §1|
|AC-BUILD-1|G: 共通口の provider と共通ソースの closure を両像で検査。C: 像の profile・能力一覧。A: 不在は明示 manifest、未解決参照を成功 stub で埋めない。実行文脈は対象外|`C_KERNEL` 等のソース選択 (`build/sources/common.mk`)、順序 (`build/sources/order.mk`)、PPC 選択 (`build/targets/ppc-powermac.mk`)|`build/config.mk` の arch/platform 選択|製品シェル経路の closure を照合する。BE/ILP32 を含む ABI 検査を保つ|`AC-BUILD-1/closure-profiles`（T1 で作る） / BUILD・PPC_IMPL_RULES §1|

## 5. W2 で確定した NIC・link の契約

P1 系列と link-closeack を含む main `4e0e8b1e` の実物で確定した。担当は **PowerPC P1 系列、本線はレビュー**。本節の参照は `P:` でなくこの基点の作業木を指す。§1 の旧 P1g 未照合の留保は本節では解消する。容量・予算の正典は LAN §5、数値の正は下表の実装記号とする。適合の状態は §3 のまま、全項目 **未 (T1 で確かめる)**。

|ID|保証 / 能力 / 不在、文脈|正にする記号・所在|x86 の満たし方|PowerPC の満たし方|両アーキの試験 ID / 担当設計|
|---|---|---|---|---|---|
|AC-NET-1|G: 消費は link page。NIC の byte credit と本文リングの空きを page 単位に切り捨て、小さい方から margin を引く (下限ゼロ)。1 frame の admit には MAXFRAME 分が必要。C: NIC の空き容量。A: 正の credit でも frame 分に不足すれば admit しない|`NetNicOps.rx_credit_bytes` (`net/nic.h`)、`LINK_STREAM_BUF`・`LINK_MAXFRAME_PAGES`・`LINK_CREDIT_MARGIN`・`LINK_PAGE_BYTES` (`net/link.h`)、計算 (`net/link.c`)|NE2K は ring と queue の空きの小さい方 (`drivers/net/ne2000/nic_lgy98.c`)、e82557 は空き frame 数×frame 長 (`drivers/net/e82557/nic_e82557.c`)|GEM は FIFO の空き枠×frame 長 (`platform/powermac/nic_gem.c`)。保留時の admission 境界は LAN §5、F5 へ|`AC-NET-1/credit-admission`（T1 で作る、F4/F5） / LAN §5|
|AC-NET-2|G: RX の所有者は共通 NIC 1つ。共通 NIC の poll と link の進行は tick と `idle_work_run` の FG1 の地点 (待ち・syscall 入口・`net_tx` の後) から1msに1回まで。IRQ 内・EE/IF=0 の前景からは進めない。pm_lan_poll は独自に RX を汲まない (P1g A1)。poll は gem_rx を高々一度、内部 descriptor 走査の上限を外側 work 回数と掛け合わせない。時間予算は共通 tick_budget に従う。C: NIC のサービス単位・容量。A: 空/予算切れは無制限走査へ逃がさない|`NET_RX_SCAN_MAX` (`net/nic.h`)、`RX_SLOTS`・`RX_FRAME_BYTES` (`net/nic_rx.c`)、`net_jiffy` (`net/nic.c`)、`tick_budget`|既存 NIC の IRQ/tick と RX work の上限を保持。GEM の descriptor 数を NE2K/e82557 の単位に読み替えない|`poll` (`platform/powermac/nic_gem.c`) と `gem_rx` (`drivers/net/gem/gem_rx.c`)。tick あたりの総走査上限・TX 回収は LAN §5、時間実測は F5 へ|`AC-NET-2/rx-owner-budget`（T1 で作る、F4/F5） / LAN §5|
|AC-NET-3|G: RTO は NIC 受理から論理 100Hz の jiffy で数える。close の保留 ACK は RELEASE より先に送る。C: IRQ の駆動源。A: EE=0 の syscall 内で進行を仮定しない。syscall 内の扱いは W4 (AC-CTX) で決めた|`LINK_RTO_TICKS`・`LINK_TRIES` (`net/link.h`)、`link_platform_now` (`net/link_platform.h`)、`link_free_handle` (`net/link_host_api.inc`)、ACK→RELEASE (`net/link.c`)|既存 tick_count と再送・close の共通実装を保持|USER は EE=1 (`arch/ppc/exec_enter.S`)。USER に戻るたび DEC が net_jiffy/link_tick を進め、現状の syscall 内 (EE=0) では進まない。W4 適合後の通常 syscall は AC-CTX-1/6 に従う。時計の実測は `docs/tasks/ppc/TASK_PPC_LAN.md`。独自時計を作らない|`AC-NET-3/rto-close-ack-context`（T1 で作る、文脈は W4 (AC-CTX)） / LAN §5、link-closeack (`9e39176a`)|

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
|B1|本線、PowerPC 照合|`build/targets/ppc-powermac.mk`、`build/sources/common.mk`、T1 試験 / 250 行|T1a、対象の共通変更後。差分 manifest と両像リンクで closure を検査、大規模な一覧移動は行わない|
|W4|本線、PowerPC 確認|[ARCH_CTX_DESIGN](ARCH_CTX_DESIGN.md) の各段|実行文脈の契約と実装を受け入れる。適合は受入まで未|
|W3|本線、PowerPC 確認|本書、`PPC_NOTES.md`、`ARCH_BRINGUP_ORDER.md` / 150 行|各担当段の受入後。5d の適合には owner 1 の通常シェルで切替・回収・再起動を確認する。W4 の受入後は AC-CTX も結線対象に含め、適合 ID と能力差を結線、未実機項目は延期台帳の関門へ。試験ログを契約本文へ複製しない|

T1 は試験土台の作成であり適合完了ではない。fake では単位・周回・返値・容量境界を検証し、idle と割り込み進行は NP21/W と mac99 の guest probe で同じ契約 ID を記録する。ホスト時計だけの一致で実機時計精度を主張せず、NIC 飽和と G4 入力は実機の受入に残す。
実装段は対象像をビルドし、共通変更では PPC SH 像もリンク、追加・変更試験の変異が実行時 RED になることと lint を確認する。全体検査は GitHub CI、全変異は full-check。最後のソース変更後のログだけを受入に使い、未実施は `docs/tasks/DEFERRED_TESTS.md` の関門と結ぶ。W1 自体の成果物は設計稿のみ。

## 7. POLICY_DEV §1 への結線

別のアーキテクチャの設計 §1 の三つの判断に、「ARCH_COMPAT の該当 ID・能力差・両アーキ共通の試験」を添える。

## 8. 未決

ユーザー判断を要する未決は **0 件**。推奨は、W1 の保証と担当境界を採用し、§5 の W2 確定契約を本線でレビューすること。試験 ID 登録・実機受入は担当工程の作業であり、ユーザー判断の未決には数えない。


## 9. 機種の作りの違いの一覧 (x86 に揃える前提の例外)

POLICY_DEV §10「x86 に揃える」(ユーザー決定 2026-11-20、動作実績は x86 だけ) の例外と、まだ揃えていない違いを 1 か所に並べる。 **順 (ユーザー決定 2026-11-20「とりあえず x86 前提でまず KAPI の機能を揃えましょう。そのうえで 2 重実装を取り除きます」)**: PPC KAPI の埋めを先に終え、その後にこの一覧の「未揃え」と 2 重の実装を取り除く段を起こす。新しく実装する物は x86 に寄せ、新しい違いは作らない (ユーザー 2026-11-20「今から実装するものはよせて」)。既に実装されている違いにはわざわざ手を付けない (手が余っている時はこの限りでない)。この表に記録するのは物理差と既存の未整合だけとする。物理の理由で違いを残す段は、ここに行を足すことを完了条件にする。揃えたら行を消す (経緯は git)。

| 項目 | x86 (PC-98) | PowerPC (PowerMac) | 状態 | 揃える段・残す理由 |
|---|---|---|---|---|
| シェルの枠 (実行の枠の ID 1) | カーネルの起動のループ (`platform/pc98/kernel.c` の外部シェル起動) が外部シェル (`shell.bin`・`gshell.bin`) をシェルの枠 ID 1 (`MEM_SHELL_LOAD_ADDR`) に `exec_run` し、終わったら `gui_take_next_shell` を見て起こし直す | `platform/powermac/exec_boot.c` の `pm_exec_boot` が `exec_nest_level = 1` にして起動の文脈を ID 1 に見立て、`sh` は ID 2 以降の普通の枠で動く | **未揃え** (2026-11-20 に見つけた) | PPC_SHELL_ALIGN_X86_DESIGN の段 A・B・C は済み、E は実装中。 5d の公開前に owner 1 の製品シェルと切替ループを揃える。 |
| シェルの特権 | 外部シェルはシェルの枠で CPL=0、AS を作らない (`exec/exec.c` の注) | `sh` は USER の SDK で組んだ PR=1 のアプリ、アプリごとの AS | **未揃え** | 製品シェルの枠と一緒に揃える |
| 起動の文脈 | 起動から外部シェルのループ | 常駐 CLI (`kernel/cli/resident_cli.c`、LAN の端末の入口) が起動の文脈で、そこから `sh` を起こす | **未揃え** | 製品シェルの枠と一緒に揃える |
| 部品の読み込み | FEP・SQLite は module | 初期移植では静的に結線 | 未揃え | 共通 loader の L1 → L2 → FEP → KCG。SQLite も共通 module と VFS を使う。 |
| root と /host | ディスクと共通 VFS | インストール媒体は RO RAM root、/host は link の SFS CALL | 製品媒体は未揃え | HDD の ext2 root を整え、辞書・学習を x86 と同じパスで公開する。 |
| 端末と入力 | TVRAM、PC-98 の鍵盤 | LAN の link (RSH) と FB の文字端末、USB の HID | 違いを残す (物理) | 装置が違う |
| KCG の字形の読み出し | CG ROM/CG RAM を `drivers/video/kcg/kcg_module.c` (kcg.mod の提供者の口) が読む | 常駐の KCG1 ファイル (`platform/powermac/kcg.c`) | ANK・漢字の読み出しの転送は共有した (`drivers/video/kcg/kcg_read_provider.inc`、B14 2026-11-20)。字形の出所の違いは残す (物理) | 部品のロードは B08 (module の読み込み) の段で揃える |
| ATA Data のバイト順 (D8) | LE の 16 bit ポート | BE の窓。IDENTIFY の語はバイト交換し、セクタは媒体の並びを保つ | 残す (物理) | CPU のバイト順が違う。語の値とセクタのバイト列を別の窓口で渡す |
| BIOS 由来のディスク幾何 (D9) | BIOS の幾何を区画表の位置の決定に使う | BIOS 由来の幾何が無く、`hdd_geom_info` は不在 | 残す (物理) | ファームウェアが違う。PowerMac の ATA はこの幾何による区画探索を行わない |
| KAPI の不在 | 全部ある | 型ごとの不在 (`sdk/kapi_target_ppc.py`) | 埋めている途中 | PPC KAPI の埋め (PPC_KAPI_FILL_DESIGN) |

## 確定 (2026-11-14、Opus 5.5 第 2 回は「確定してよい」、P3 の 4 件を PM が反映)
- A1: `_halt` を idle の復帰契約から外し、「許可状態を保つ」を別に書いた。
- A2: §3 に領域名の表と、足すときの規則を置いた。
- A3: 適合の状態は、全項目を「未 (T1 で確かめる)」とし、W3 で「済」に書き換えると決めた。
- A4: 状態行を確定に書き換えた。測定値は本文から外し、PowerPC の受入の記録を参照する形にした。
- 段: H1 は W1 の直後に置き、W2 を待たない。F9a・F9b (時計の源の照会) は観測された不具合から来た段ではないので、任意の段とした (発注は PM が決める)。
