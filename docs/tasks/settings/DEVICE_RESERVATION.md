# 任意デバイス予約 broker — 次の実装票

> 発行: PM (2026-09-09) / 状態: **計画 (2026-09-30)** — **U6 決定 (ユーザー 2026-09-30、[TASK_MEMMAP_V3](../v3/TASK_MEMMAP_V3.md) D33): v3 P4 で拾うが改訂する。** (i) transaction 核 (`pgalloc_device_reserve` / `sys_device_reserve_core`) と owner 台帳は TASK_MEMMAP_V3 §6 **T1 の台帳の MMIO 登録**に載せ直す (複数 span の一括 commit と永久保持は T1 の仕様として残す、`test_device_reservation.py` を流用); (ii) **時期は折衷**: 副作用のない識別と予約は**起動時** (TASK_MEMMAP_V3 §4-5 の「gfx probe + BB 確保」を「識別 + 予約 + BB 確保」と読む)、破壊的な probe / enable は **GUI 境界** (TRIDENT T8 (i) と一致); (iii) 許可範囲は「既知候補の定数だけ」から**検証済みの実測 BAR** へ広げる (Trident のため。[V3_PLAN_DRAFT](../v3/V3_PLAN_DRAFT.md) §7-2 **X4 は Codex へ回す**); (iv) §5 (後発 GUI の PEGC BB) と exec arena 全域の禁止は D19 / D3 で不要になったので改訂で削る。改訂の本文は P4 の着手時 (T1 の後) に書く — それまで §1〜§7 は 09-13 (Cirrus の帯だけ 09-29) のまま。仕分けは [U6_PENDING_REVIEW](../v3/U6_PENDING_REVIEW.md) §1-5。**X4 の Codex 回答 (2026-09-30、ユーザー承認、TASK_MEMMAP_V3 §8-4・T1)**: 生の `{base, size}` を予約権限にせず**検証済み資源レコード** (BDF・ID・BAR 番号・生値・種別・確定 decode 幅と根拠・boot 世代) を台帳に結び付ける、**予約 (decode aperture 全体) / 写像 (確認済みの必要部分) / 面 (USER lease) の範囲を分ける**、全 span を検査してから一括 commit (管理上限外も完全な範囲を台帳に、失敗時は出力も含め不変 — §4 の 1〜7 はそのまま生きる)、GUI 境界で装置の同一性を再確認して不一致なら enable を拒否 (予約の差し替えはしない)、**永久予約 (device owner) とモジュール owner の RAM 回収を分離** (§4「所有と寿命」はそのまま生きる)。改訂では §5・§7 の「GUI 境界で初回予約」「exec arena 全域の禁止」「GUI=0 なら任意予約 0」の受入項目を D33 (識別 + 予約は起動時、probe + enable は GUI 境界) に合わせ、T1〜T5 のゲートに「第二 span の衝突・台帳満杯・BAR 変更・同 owner の部分一致・丸めによる衝突・map / probe 失敗後の永久保持・モジュール回収後の予約保持」を含める。Trident の今の採取値は未検証 (幅が未測定)。
> それまでの状態: 計画 (2026-09-13)

計画・未実装。現在の dirty/untracked 作業ツリーを読んだ設計であり、ゲスト検証結果ではない。本票だけを追加する。[MEMORY_RAM_INTEGRATION.md](../../archive/settings/MEMORY_RAM_INTEGRATION.md) の旧 API 案・起動時 GFX 予約案ではなく、以下の現行コードと GUI activation 時の契約を実装基準にする。

## 1. 範囲と既定動作

- 既定 CUI / splash は PC98 標準表示のまま。`GUI=0`、`GFX=auto/pegc/cirrus` の設定読取りだけでは任意窓の予約・probe・enable をしない。15–18MiB 全域や 16MiB 直下 1MiB の予防的な穴は作らない。
- 初回予約は GUI 起動要求を受理し、旧シェルが戻った **master CR3・live AS=0・exec 不在** の境界で行う。`GUI=1` 起動と CUI→GUI を同じ経路にする。候補の全予約を確定するまで副作用 probe を呼ばない。
- 任意デバイスを有効にしない限り RAM/exec/hotdeploy の既存配置を変えない。既に物理 decode されている実在穴は別の機種情報の責務であり、CUI 設定を RAM 存在証明にしない。
- 初回は PEGC と Xe10(ID 5Bh) Cirrus のみ。KAPI/SDK、RAM 検出、PAE、汎用 hot-unplug、exec 再配置は対象外。未確認の別名窓・他ボードを推定で追加しない。

## 2. 現行コードとの差分（根拠は関数単位）

| ソース | 確認した現行動作 / 不足 |
|---|---|
| `kernel/physmem.[ch]`: `physmem_exclude`, `physmem_reserve_ram` | 単一変更はコピー上で正規化後 commit。永久除外、owner/live allocation 情報なし。MMIO > RESERVED > RAM > UNKNOWN。最大64区間。 |
| `kernel/pgalloc.c`: `init_core`, `pgalloc_reserve_pfn` | model を eligibility bitmap に snapshot。単一 `[first,end)` だけ IRQ 保存区間で全 allocated bit を確認後 eligible を落とす。既に非 eligible は成功扱い、`end > limit_pfn` は拒否。owner、複数範囲 transaction、管理外 MMIO 記録はない。 |
| `kernel/pgalloc.h`, `include/sys.h` と実体 | `sys_memory_bootstrap_model(model,layout,verify)` → `sys_memory_stage_online()` が現存。BOOTSTRAP は一般 alloc 不可、workspace で高位 RAM mapping 後 ONLINE。stage は master/live AS=0 が必要、途中失敗は以前の map が残り得る fail-stop。`sys_memory_init_model` は metadata のみで ONLINE にはできない。 |
| `kernel/kernel.c`: paging/pgalloc 初期化 | 製品入口は依然 `paging_init(mem_kb)` → `pgalloc_init(mem_kb)`。モデル stage/provider は未接続。broker のために boot をモデル経路へ強制変更しない。 |
| `kernel/boot_splash.c`: `splash_gfx_init` | 希望値を保存→PC98 強制→`gfx_init()`→希望値復元。任意 probe をしない現行変更を維持。kernel.c の「splash が最初に任意 backend を選ぶ」と読める旧コメントを根拠にしない。 |
| `gfx/gfx_core.c`: 選択/init | auto は Cirrus→PEGC→PC98、強制候補失敗は PC98。`probe()` を選択時と init 後に呼び、init は void。副作用なしの候補記述、予約許可、init 成功状態の内部契約が必要。 |
| `gfx/backend_pegc.c`: `pegc_probe`, `pegc_linear_selftest`, `pegc_init` | probe 自体が FF2/モード/MMIO を変更し、窓 enable→map→VRAM 書込み試験。成功時 map を保持。init は `sys_reserve_top`、enable→map（戻り値未確認）→clear。予約を init にだけ追加しても遅い。 |
| `gfx/backend_cirrus.c`, `drivers/wab_cirrus.c`, `drivers/wab_glue_xe10.c` | Xe10 probe も index OUT、chip probe は SR6 を書き解錠状態を残す。glue init は FF82/銀行窓/linear enable を map より前に実行。純粋な照会ではない。 |
| `kernel/sys.c`: `sys_reserve_top` | boot/exec 前専用。今は pgalloc 永久予約に成功してから上端を下げる。同サイズのみ冪等、model freeze 後は拒否。GUI activation 時の BB 取得にそのまま転用できない。 |

`physmem` を後から書き換えても allocator は追随しない。既存単一 reserve を複数回呼び、後半失敗時に free する案も不可（永久予約は解除不能）。

## 3. 候補が要求する範囲

全て **PFN 半開区間**。以下の byte 表記は既存定数の説明で、実装は定数を参照して境界/加算を検査する [C4]。描画サイズと decode aperture を区別する。

| 候補 | 一括予約に含める資源 |
|---|---|
| PEGC | `PEGC_LINEAR_BASE/SIZE`: `[0x00F00000,0x00F80000)` 全512KiB。表示300KiBだけの予約は禁止。別途 `MEM_GFX_BB8_SIZE` の主記憶 BB（確認済み RAM、既存 owner の再利用または新規取得）。 |
| Xe10 Cirrus | glue の `win_base/win_size`: `[0x00F60000,0x00F68000)` と `lin_base/lin_size`: `[0xFE000000,0xFE200000)` (2026-09-29 に v3 のデバイス窓の帯へ移設。NP21/W は `[0xFE000000,0xFE400000)` の 4MiB を窓として出すので予約はこの全体) を **同一 transaction** にする。CPU 描画が銀行窓を使わなくても glue が設定するので省かない。BB は linear 内の client 面であり、追加の主記憶 BB はない。 |
| 低位固定資源 | PEGC の `PEGC_MMIO_BASE` 側（E0000h〜E7FFFh、標準 plane I と役割交替）、A8000h/B0000h の既存表示窓は一般 RAM を新規取得しない。PC98 表示 owner が GUI 遷移時に明示許可する既存固定表示資源の貸与として検査・記録する。`RESERVED` だから自由に上書き可、とはしない。 |

PEGC と Xe10 銀行窓は重なる。片方の永久所有後に他方へ切替える共有許可は初回範囲外（拒否→PC98/CUI、理由を報告）。現行 Xe10 glue は `mmio=NULL` で BLT は I/O 経由。存在しない追加 MMIO 窓を作らない。PEGC の `PEGC_LINEAR_ALT_BASE` は現行未使用で本票では対応しない。

## 4. 必要な内部 transaction（API 名・署名は未定、追加が必要）

broker は kernel が owner 台帳と固定配置を所有し、driver は候補識別子と定数由来の要求だけを渡す。台帳は bounded 配列、動的確保・I/O を検証中に呼ばない。pgalloc に **複数 PFN 範囲の検証/一括 commit 核** を追加する。既存公開単一 reserve の意味は変更せず、共通核を利用する。

1. 未初期化/BOOTSTRAP、非 idle、再入、無効 owner、空配列、容量超過、空/逆順/PFN上限超過を拒否する。raw byte 入力なら非整列と丸め overflow も拒否。4GiB end を `u32` byte に変換しない。
2. IRQ を保存して禁止し、候補全範囲を正規化（重複を二重計数しない）。台帳/固定配置の候補コピーを作り、全検証から commit まで alloc/free/reserve と同じ排他にする。SMP/NMI は対象外。
3. 全窓を **exec arena 全域**、metadata、PT workspace 全域、hotdeploy、kernel/shlib、既存 BB、他 owner と照合。paging の動的 PT/PD など通常確保は allocated bit で捕捉するが、workspace は別 bitmap なので固定配置検査も必須。非 eligible であることを所有権の証明にしない。
4. MMIO 要求は UNKNOWN/管理上限外も記録可能。ただし権限は既知候補の定数範囲だけ。allocator 操作は各窓と `[0,pgalloc_limit_pfn())` の交差に限定し、交差なしは bitmap 無操作で成功可能。clip した範囲で owner 台帳を代用しない。全交差の allocated bit を **先に全件** 検査する。
5. 主記憶 BB は MMIO と別種。全域 eligible RAM・未確保・適切な supervisor identity RW/cacheable mapping・全 arena/私有 APP mapping 帯外を検査する。未知範囲を BB として予約しない。候補探索と取得を同じ排他に入れ、単なる `physmem_find()` の結果を所有扱いしない。
6. 台帳容量、任意の診断用 physmem コピーの全 overlay/64区間制限、全固定 claim、bitmap 検証に成功した後だけ commit。**ここから失敗可能な処理を置かない**。eligible を union 分だけ落とし total を減らす。allocated/used は不変、limit は上端として保持。台帳と BB base を同時公開する。
7. 失敗なら台帳、bitmap、統計、model、sys 上端、出力引数を全て不変にして元 IF を復元。paging/I/O はまだ行わない。成功後の mapping/probe 失敗はこの transaction の rollback とは呼ばない。

live allocator の正典は frozen eligibility + broker 台帳とする。boot model を外部から live API として再利用しない。model を診断用に保持するなら broker 管理のコピーだけを原子的に更新する。legacy init は元 model を保持しないため、**固定予約由来（metadata/workspace を含む）を読み出す内部 snapshot、legacy 固定領域の登録、exec 不在を確認する内部入口** が追加で必要。`paging_boot_context()` だけでは CPL0 exec 不在を証明できない。

### 所有と寿命

- owner は exec nest level/プロセスではなく、kernel 寿命の「PEGC」「Xe10」等の安定 ID。固定用途 owner と区別する。台帳に正規化要求集合、資源種別、BB base、予約済み/準備済み/有効/失敗を持つ。
- **同 owner・同一要求集合・同一配置のみ冪等成功**。size が同じだけ、部分一致、追加範囲、他 owner の一致を成功にしない。再呼出しで total/free を再減算しない。
- commit 後は probe 未到達の map 失敗も含め永久保持。shutdown、CUI 復帰、アプリ終了、free/mark、初期化再実行で解除・RAM 復活をしない。実際に窓が閉じると保証できないためである。失敗理由と保持資源を診断可能にする。
- generic RESERVED の乗っ取りは禁止。低位表示資源の貸与は上記の明示例外のみ。起動時機種 MMIO が既知 device に属すると判明した場合も、由来付き登録がなければ自動採用しない。

## 5. exec/BB と起動境界の決定

`exec/exec.c:exec_child_claim` は A/B の間を `EXEC_DYN_RESERVE` として空け、起動時 mark/終了時 free する。broker はその時点の bit だけを見ず、**`[MEM_EXEC_LOAD_ADDR, sys_usable_mem_end())` 全体を将来用途として禁止**する。子不在・動的穴が空きでも同じ。16MiB legacy 構成では PEGC 窓・Xe10 銀行窓がこの arena と衝突するので拒否する。窓を通すために上端を暗黙に15MiBへ縮めない。

PEGC BB は具体的な未解決依存である。PC98-only splash では従来の boot BB がもう作られず、現行 `sys_reserve_top()` を後発 GUI で呼ぶのは契約違反。**本票の最小実装は、既存同 owner BB を再利用、または ONLINE の確認済み・mapped RAM から arena/私有帯外の連続 BB を §4 の一括要求として永久取得する。無ければ probe 前に PEGC を拒否する。** legacy boot にはその空きがない場合があり、従来8MiB PEGC の動作維持を本票だけで達成したとはしない。idle 時の exec 縮小/BB carve-out を必要とする場合は別設計・別受入ゲート（A/B cleanup、minimum、guard、hotdeploy 不変まで）を先に承認する。CUI 用に boot BB を常時確保する迂回は禁止。

GUI 初回の挿入点は `kernel/kernel.c` の `is_gui` 判定後、`exec_run(cur_shell)` より前。失敗は候補を採用せず PC98 GUI または既存 CUI 復帰へ進む。live AS/exec の不在を確かめられなければ拒否する。auto は候補ごとに transaction→準備を行い、全候補の窓をまとめて予防予約しない。

単独 CPL3 アプリからの初回 `gfx_init/gfx_init_200` は既に exec 後なので、任意 backend の新規 activation を禁止して PC98 を使用する。GUI 境界で準備済みの候補の再 init のみ許可する。この制約を隠して既存単独アプリ互換を合格扱いしない。live backend の変更を希望値変更だけで即実行せず、安全な shell 境界へ送る内部調停が必要。

## 6. mapping と公開順

1. 副作用のない定数/保存済み機種情報の照会（新しい内部契約が必要）→broker commit。現行 `probe`/Xe10 ID OUT をこの照会の代用にしない。
2. **probe より前**に必要 PT と master device mapping を `paging_map_phys(..., PAGE_RW | PTE_PCD)` で準備、全戻り値を検査。PEGC 制御窓も mapping/属性を確認する。BB RAM は cacheable のまま。RAM identity mapping 済みの窓を転用する場合の cache/TLB 同期を paging 層で明示し、単なる eligibility 変更では済ませない（必要な内部処理は追加する）。
3. 準備済み予約の許可を確認してから FF2/MMIO/SR6/glue init/linear enable/VRAM selftest。現行の enable→map 順を逆転する。失敗は表示/relay を戻し、予約保持、未完成 BB を公開しない。複数 map の後半失敗に対する安全な mapping 後始末も追加し、既存 PTE を無条件に0で壊さない。
4. 成功した BB 記述子/backend 状態を公開→gshell の exec/AS 作成→`gfx_bb_phys_range()` から **client 面だけ** `paging_addrspace_map_user_keep()`。表示面は supervisor、Cirrus client の PCD/PWT を維持する。exec 側 map の戻り値も確認して失敗時は launch abort。
5. 再 init は mapping/BB を作り直さず既存準備を使用する。Cirrus の現在の shutdown 後 mapping/BB 保持を維持し、共有 PT 上の USER を supervisor で上書きしない。予約失敗を `s_probed` の「不存在」として永久 cache しない。予約拒否と hardware probe 失敗を分ける。

現行 backend の `sys_get_mem_kb()*1024` による上端判定は ownership 判定ではなく overflow もあり得る。削除するなら broker と安全な機種/decode 条件で置換する。**RAM を予約できたことは PEGC がその窓に実在する証明ではない**。未確認機種では保守的拒否を維持する。paging の現在の map は単一呼出し全件不変で失敗する（`kernel/paging.h:87-102`）。backend 内の「部分適用される」という旧コメントには従わない。

## 7. 実装範囲と受入ゲート

次回コード範囲: `kernel/pgalloc.[ch]`, `kernel/sys.c`, `include/sys.h`（台帳/transaction/固定配置）、`exec/exec.[ch]`, `kernel/kernel.c`（idle 境界/公開順）、`gfx/gfx_core.c`, `include/gfx_hal.h`, 両 backend、`drivers/wab_glue*.{c,h}`, `drivers/wab_cirrus.c`（副作用分離）。必要時のみ `kernel/paging.[ch]` の cache/mapping 準備処理、既存定数ヘッダ。共有ファイルはこの順に直列統合。C89 [C1]、kernel 文字列 [C2]、KAPI 変更なし [ABI1–3]。

| ゲート | 合格条件（全て次回実装時。今回未実行） |
|---|---|
| T1 transaction | ILP32/GNU89 の実 pgalloc/sys 核で、2範囲目の live allocation、台帳満杯、model 容量不足、BB 不足を注入。全 state/output/IF 不変、I/O/map=0。重複範囲は union 集計。同 owner exact retry は統計不変、他 owner/部分一致拒否。 |
| T2 範囲/所有 | PEGC全512KiB、Xe10銀行+全2MiBの末尾まで除外。legacy limit 外 UNKNOWN の窓も台帳登録できる。疎な RAM、最終PFN、overflow/逆順拒否。metadata/PT workspace/hotdeploy/shlib/BB、exec A/B **と動的穴** に個別衝突。free/mark/再 init/終了で永久予約が復活しない。 |
| T3 順序 | mock I/O/mapping traceで `commit→全map成功→probe→enable→BB公開→AS作成` を検証。PEGC map/BB失敗、Cirrus map/setup失敗で副作用境界・保持予約・PC98復帰を確認。準備前の直接 backend 呼出しも拒否。 |
| T4 起動/再利用 | GUI=0 と全 GFX 希望値で splash はPC98のみ、任意予約0・任意probe0・旧sys上端不変。GUI=1/CUI→GUIは同じgate。非idle初回拒否。再 init/shutdown/reentryでBB baseとclient USER/PCD維持、表示面は非USER。 |
| T5 回帰/限界 | 既存 `test_physmem.py`, `test_pgalloc_range.py`, `test_pgalloc_model.py`, `test_highram_stage.py`, `test_paging_bounds.py`, `test_boot_splash_native.py` と新規broker host試験、`make kernel`/`make check`。モデルstageはfail-stopのまま、CUIで高位providerを捏造しない。PEGC低RAM拒否は未対応として明記。 |
| 実行時（別承認） | 配備binary同定後、PC98 CUI、GUI往復、予約衝突拒否、単独描画、ネスト/abort、`ring3_guard` 相当の表示面隔離を確認 [V1/V4]。本票作成ではビルド、エミュレータ、ネットワーク、設定/秘密情報、`docs/hw/` を扱わない。 |
