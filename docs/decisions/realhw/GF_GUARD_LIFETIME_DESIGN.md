# GF_GUARD_LIFETIME_DESIGN — GeForce の手本のガードの追記: 表示と予約の寿命 (監査の台帳の複数の窓・復元に失敗した保存像・tile の初期化と寿命)

> 出所: ~/os32-tmp/run/gf-addA/DESIGN.final.md (astra 起草、Opus 2 回のレビュー、2026-11-06 確定)。

> 状態: **現行 (2026-11-06)** — 実装は票 TASK_GF_FEATURES の段。洗い出しの元は ~/os32-tmp/run/guard-gf/report.md。

# NV44A 表示と予約の寿命 — GF_FEAT2/4/5 確定設計への追記 r2

r1→r2 の変更

- ID 1: firmware import/FOREIGN/除外を撤回し、devinit後・gf2_start前に12枠をゼロでprogする。F2との差分理由を明記。
- ID 2: OWNEDはleaveをまたがず、通常stopのquiesce後・release_all前にdrainなしでretireする。
- ID 3: cleanup_pendingをctxに保持し、saved_release後の部分失敗でもenter/unloadを拒否する。
- ID 4: release_all後の全リセットを共通helperに切り出し、通常stopとabandonから呼ぶ。
- ID 5: display_use_endedを削除し、既存の!engaged・OFF・!readyを終了証拠とする。
- ID 6: -411の直接原因を訂正し、abandon成功時だけCUI owner/sessionをゼロにする。
- ID 7: platformのrid保存・照会をM5前に追加し、instmem_map host試験をH段へ追加。
- ID 8: PORT_FLOW §4-1・gfx_hal §9の参照先をmainに訂正。
- ID 9: 手本の保存像破棄は無条件と訂正し、PM決定で復元失敗像の終端破棄を確定。
- ID 10: hwdev_layout.jsonのstruct gf4_ctx/gf5追随を統合段に追加。

- 対象 HEAD: `b91b27af`。設計のみ。シングルタスク・協調切替、整数演算、C1〜D3を維持。
- O = `/home/hight/wt/os32-v3/gf-cui/`。以下の製品パスは O 相対。
- R = `/home/hight/os32-tmp/refs/geforce6200/`、N = R`nouveau/drivers/gpu/drm/nouveau/`、X = R`xf86-video-nouveau/src/`。
- 手本は INDEX.md 記載版の MIT 部分。メーカー資料は R`nvidia-open-gpu-doc/classes/twod/cl0042.h:81-83`（surface pitch の欄）。この資料は PFB tile の許容 pitch 集合を規定しない。tile の最小ハード条件を推測せず、X の生成範囲を契約にする。
- 観点: main `/home/hight/os32-v3/docs/tasks/agents/PORT_FLOW.md:77-94` §4-1 と `/home/hight/os32-v3/docs/design/include/gfx_hal.md:123-154` §9。生成時の制約検査とドライバ内部の順序・失敗後寿命を分けて照合する。

## 1. 実物の確認

| # | 前提との食い違い（実コード） | この追記の処置 |
|---|---|---|
| 1 | F2 §4の部分extent所有に対し `mm/pgalloc.h:174` は単一区間。`instmem_map.c:15`（以下 hwdev/gf/）はPTEだけを開く。`mm/paging.c:550-560` は台帳外presentを拒否 | 静的区間＋厳密な動的区間集合を監査 |
| 2 | F4 A3-case `GF_FEAT4_DESIGN.md:577` は成功復元後だけrelease。`kdebug/gf_display_test.c:843-865` は復元失敗後もshutdown、`display/service.c:467` はreadyを落とす。`display/save.c:72,377` は以後の復元を拒否 | 停止確認後の終端破棄を別経路化（§8決定）。-411の直接原因は `gf_display_test.c:845,866` の残存 `display_session.valid/display_cui_owner` |
| 3 | A08: `tile.c:132-142` はpitch非ゼロだけ。X`nv_accel_common.c:94-100` のNV4x生成制約を満たさない値が通る | 生成範囲の境界検査 |
| 4 | A09: `vram_view.c:62-77` は借用なしに予約解除。viewは`:144-159` で予約を見つけてもkeyを保存しない | 中央の借用記録を解除条件へ |
| 5 | U09: `gr.c:78-92` は既存12slotを転記するが `tile.c:124-125` の空き判定はsoftwareだけ | devinit後・gf2_start前に全12枠ゼロprog |
| 6 | `tile.c:99-117` は全成功時だけtile_liveを立てる。途中write/unpause失敗後、実際に有効かもしれないtileの借用が残らない | 書込み前に旧/新両借用、失敗はUNKNOWNで保持 |

- 材料: `/home/hight/os32-tmp/run/gf-cui/report.md:25-40`、`/home/hight/os32-tmp/run/guard-gf/report.md:29-30,89,110-111`。
- 欠け2（実機clock/-509の原因）は対象外。今回で解消したと扱わない。

## 2. 動的窓と監査（F2 §4への追記）

```c
/* mm/pgalloc.h: 内部のみ。既存resourceの32B配置と静的map区間を維持。 */
#define LEDGER_MAX_DEVICE_MAPS 64U
struct ledger_device_map { u32 owner, rid, first, end; }; /* PFN半開区間 */
int ledger_resource_page_mapped(u32 rid, u32 pfn); /* static OR dynamic */
/* mm/paging.h: 恒等UC、supervisor RW専用。0成功/-1失敗 */
int paging_device_map(u32 owner, u32 rid, u32 first, u32 end);
int paging_device_unmap(u32 owner, u32 rid, u32 first, u32 end);
```

| 手順 | 所有・検査・失敗 | 手本と差分 |
|---|---|---|
| 起動 | BAR0とDISPLAYは既存static区間。BAR2/tailは共有PT準備とdecode予約だけ。資源rid/ownerはkernel台帳が所有 | O`drivers/video/geforce/geforce_platform.c:56-77`。N`nvkm/subdev/instmem/nv40.c:238-253` のmapを起動予約と動的mapへ分割するOS32既存方式 |
| 開く前 | kernel内固定配列64件（1024B）。空き・device owner・regionのres_mask・decode内・UC・全共有PT・全PTE NPを検査。静的/動的区間の重複拒否、隣接は別件。容量超過は変更なし | N`nouveau_bo.c:669-680` のBO単位map。差分: TTMを用いず有限台帳。64件は上限であり足りない時に監査を緩めない |
| 開く | irq保存→全前提再確認→PTE全頁書込み→TLB flush→台帳登録→irq復帰。成功後だけout公開。commit区間に確保・callback・yield・失敗点を置かない | O`mm/paging_map.inc:107-120` の準備後commitを専用APIへ。差分: 共有PTの事前確保を必須化して全成否を保証 |
| 監査 | static∪dynamic内だけpresent必須、その外はNP必須。従来の恒等物理・U/S・RW・cache検査を維持。map区間を広げない。surfaceの照会も同じhelperを使用 | O`mm/paging.c:550-560`、`mm/pgalloc_surface.inc:43-44`。OS32固有監査で手本に同等口なし |
| 閉じる | owner/rid/開始/末尾が完全一致する1件だけ。全頁と共有PTを先に照合→irq区間で全NP→TLB flush→登録削除。失敗はPTE・登録・呼出し側所有を全部保持 | N`nouveau_bo.c:683-689` の対称unmap。差分: 下位の部分成功を許さず再試行可能にする |
| 呼出し側 | `instmem_map.c` がgeforce予約から一意のrid/ownerを解決して専用APIを呼ぶ。BAR2/push/各viewで同じ経路。失敗は既存GF1/GF2_EMAPへ変換、詳細段/元rc記録 | O`instmem_map.c:8-29`。既存map_ops署名は維持し、kernel固定配列で追跡するため借用nodeのABI追加不要 |

- platformは `geforce_platform.c:48` の局所ids[]を予約成功後に固定保存し、有効フラグで公開する。`geforce_module.h`に `int geforce_resource_lookup(u32 base, u32 bytes, u32 *owner, u32 *rid);`（0成功/-1失敗）を追加。完全包含する予約済みBARを一意に照合し、overflow/未予約/複数候補は失敗・out不変。ownerはLEDGER_OWNER_GFX。instmem_map.cのmap/unmapはともにこの照会を使う。静的DISPLAY窓との重複はpaging側が拒否する。手本のresource解決 N`nvkm/subdev/instmem/nv40.c:238-253`をOS32台帳へ結ぶ内部口で、KAPI追加ではない。
- `ledger_resource_set_map` は静的区間だけを扱う。動的窓生存中の静的区間変更・該当owner/資源の破棄は拒否。通常の旧利用者は不変。
- unmap直前にGPU/posted writeの完了を確認する責任は従来どおりGF側。PTE操作はGPU待ちをしない。
- 失敗注入点はpreflightまで。commit途中の擬似失敗を成功と偽装しない。既存汎用map/unmapを2回呼んで「原子的」と称しない。
- AS-launchはmaster/AS/lease/ledger全部を従来どおり呼ぶ。enter中のhello_gfxを監査除外しない。

## 3. 予約の借用を中央で保持（F2/F5共通）

```c
/* hwdev/gf/vram_view.h: nodeは呼出し側常駐、リンク中は移動禁止 */
enum gf_vram_borrow_kind { GF_BORROW_VIEW, GF_BORROW_TILE };
struct gf_vram_borrow {
    struct gf_vram_reservation key; u32 off, bytes;
    enum gf_vram_borrow_kind kind;
    struct gf_vram_borrow *next;
};
i32 gf_vram_borrow_get(struct gf2_chan *, struct gf_vram_borrow *,
    const struct gf_vram_reservation *, u32 off, u32 bytes,
    enum gf_vram_borrow_kind);
i32 gf_vram_borrow_put(struct gf2_chan *, struct gf_vram_borrow *);
int gf_vram_borrow_overlaps(struct gf2_chan *, u32 off, u32 bytes,
    enum gf_vram_borrow_kind); /* 0なし/1あり、入力不正は負rc */
/* viewにborrowを内包。tileには旧/新の2nodeを内包する。 */
```

- 所有: registry（vram_view.c）が借用リストを持つ。view/F5がnodeの記憶を持つ。owner/id/generation/off/bytesをmatchして登録し、ゼロ初期化nodeの二重登録を拒否。F5を直接呼ぶ逆依存をV0に作らない。
- 手本 N`nouveau_bo.c:75-103,136-145,669-689`。差分: BO参照/fenceを、単一協調実行の明示node＋既存drainへ置換。
- `gf_vram_release` は同じ予約の借用が1件でもあれば失敗・node不変。新規内部 `GF3_EBUSY=-306` を末尾追加し、F4/F5境界では各EBUSYへ明示変換。公開KAPI変更なし。
- view mapは借用get→PTE map→view公開。map失敗はgetを取り消しout不変。unmap失敗ではview/key/借用を保持、全成功後put。page丸め区間の重複は§2で拒否し、要求byte範囲の借用を予約に紐付ける。
- tile変更は対象と重なるlive viewがあればEBUSY（同一予約でも非重複は可）。tile有効中の新view mapは許すが、viewの生存中はそのtileを変更/clearしない。内容のlinear/tiled変換は行わない。
- 予約はdisplayのfront/hold/snapshot保護に加えてこの検査を受ける。既存のscan_discard/release_allからも必ず中央releaseを通す。

## 4. tileの生成、初期所有、失敗保持（F5 §6への追記）

```c
/* feature5.h: GF5内のslot状態。既存tile_liveだけで空きを決めない。 */
enum gf5_tile_owner { GF_TILE_FREE, GF_TILE_OWNED, GF_TILE_UNKNOWN };
struct gf5_tile_slot {
    enum gf5_tile_owner state;
    u32 raw_addr, raw_limit, raw_pitch;
    struct gf_vram_borrow loan[2]; u32 current; /* nodeは移動せず役割を反転 */
};
i32 gf5_tiles_init(struct gf5 *); /* devinit後、gf2_start前、全12枠ゼロprog */
i32 gf5_tiles_retire(struct gf5 *); /* 受付閉・窓閉・表示利用終了後 */
```

| 手順 | 契約 | 手本・外れる理由 |
|---|---|---|
| pitch検査 | p>0、a=max(1024, floor_pow2(p/4))、p%a==0。aは整数shift、p&(a-1)で整列検査。呼び手のpを勝手に切上げない | X`nv_accel_common.c:94-100` の生成固定点。これはソフト契約で、NV44A最小条件の断定ではない |
| サイズ検査 | base 64KiB整列、要求bytesを64*pに切上げ、全域が同じ予約内・VRAM内。u64加算/乗算＋既存tile_remainder、libgcc除算なし。範囲外EINVAL | N`nouveau_bo.c:178-188`。丸め結果を予約済み範囲と照合するのはOS32の責任 |
| 初回初期化 | devinit/F1完了後・gf2_start前・予約/受付前、GR無投入で12枠全部にlimit=0,pitch=0,addr=0をPFBへprogしてflush。全成功後だけ全枠FREEを公開し、gf2_startがゼロ値をGRへ転記。firmware値を取り込まない | N`nvkm/subdev/fb/base.c:201-202`、`fb/nv44.c:40-46`。ゼロのソフト状態を全枠progする手本に合わせる |
| 初期化失敗 | 部分成功をFREEとして公開せずUNKNOWN/受付閉、起動を中止。借用なし・GR停止の初期化再試行では全12枠を再progし、完了後だけ進む | 同上。失敗しうるOS32 I/Oのため完了を一括公開 |
| 新規/置換 | FREE/OWNEDだけ。境界検査→新借用get（旧は保持）→drain/pause→PFB limit,pitch,addr,flush→GR pitch,limit,addr→unpause→受付復帰。全成功で旧put、新を現行へ | N`nvkm/subdev/fb/nv44.c:40-46`、N`nvkm/engine/gr/nv44.c:31-46`。PFB/GRを同一停止区間に置く既定差分を維持 |
| 途中失敗 | 新旧借用とraw/要求値を保持、UNKNOWN、FAULT/accepting=false。slotも範囲も再利用不可。失敗段/rc記録 | N`nouveau_bo.c:75-103` の完了まで再利用不可を、部分I/O不明状態へ拡張 |
| clear | 稼働中はview閉鎖を検査し、新規/置換と同じdrain/pause→PFB→GR disable→unpauseを使う。全成功だけ旧/新put→FREE、失敗はUNKNOWN/借用保持 | N`nouveau_bo.c:121-127`、`fb/nv44.c:40-46`。再利用前の完了を保証 |
| retire | 通常stopの `service.c:472-477`、窓close・GF2 OFF・quiesce成功後、release_allより前に全OWNED/UNKNOWNを処理。GR停止済みを前提にdrain_pauseを呼ばず、PFB limit,pitch,addr=0→flush→GR pitch,limit,addr=0を直接書く。各枠全成功だけ両借用put→FREE。失敗枠はUNKNOWN/借用を残しrelease_allへ進まない | N`nouveau_bo.c:75-103,121-145` のBO寿命に対応。停止済みなので稼働用drainを省き、OFFだけでdescriptor消失と扱わない |
| 再入場/recover | OWNEDはleaveをまたがない。retire失敗はcleanup_pendingで再enterを拒否し停止状態で再試行。成功した次enterではdevinit後に全枠ゼロprog。稼働セッション内のF5 save/restart/restoreは同一予約matchと再program完了まで借用を保持し、node/ポインタをコピーしない | N`nouveau_bo.c:75-103`、O`state.c:181-219`。内部recoverと表示leaveを区別 |

- F2確定設計 `docs/decisions/realhw/GF_FEAT2_DESIGN.md:150,153` はPFB維持の理由を「既存表示設定のsnapshot転記」としている。実機観測を理由とする記載はこの決定箇所にない。PM決定「手本どおり全部」によりこの既存表示維持の差分を撤回し、`gr.c:87` は初期化済みのゼロPFBをGRへ転記する。FOREIGN・除外区間・firmware衝突による起動拒否は追加しない。
- gf5_service_initはbindingだけ。initは `display/service.c:381-384` のfoundation_start後かつgf2_start前へ結線する。生存する借用をゼロ初期化で消さない。
- F5 saved imageにはslot状態/rawの値表を追加。UNKNOWNのsave拒否。保存値から終了済みの予約を復活させず、通常leaveでretireする前にF5保存処理を終了させる。
- EINVAL=-501、EBUSY=-505、I/O=-507、状態=-509。timeout等の既存source_rcを失わない。

## 5. 保存像の終端破棄と停止（F4 A3-caseへの追記）

```c
/* display/api.h: ctx所有、停止の確認とメモリ借用解除を分離 */
bool cleanup_pending; /* ctx所有。回収開始前に立て、全後始末成功後だけ解除 */
i32 gf4_diagnostic_abandon(struct gf4_ctx *, const struct gf4_saved *);
void gf4_service_cleanup_reset(struct gf4_ctx *); /* release_all成功・診断借用終了後だけ */
/* restore_ops末尾へabandon。通常opsは公開しない。 */
```

| 呼出し/状態 | 手順・所有 | 手本と差分 |
|---|---|---|
| save/診断中 | kernel static保存像はA3所有。ctxはそのpointerを借りる。restore成功後のsaved_releaseは既定どおり。保存像をheap freeする処理はない | O`display/save.c:363-402`、F4設計:569,577 |
| restore失敗→leave | 元restore rcを保存。受付閉→窓close→GPU停止→表示quiesce→98帰還。各段のrcを別記し、元rcを返す | N`dispnv04/disp.c:57-96` のflip/IRQ停止後unmap/unpin。OS32は98帰還をbackendで行う |
| 終了確認 | `service.c:472-476` のX3に従い、quiesce成功・F2 OFF・窓closeを確認して既存l->engaged=false。abandonは `!l->engaged && c->ch->state==GF2_OFF && !c->ready` を要求。新たな終了フラグは作らない | N`dispnv04/disp.c:189-207` の停止後破棄。ready=false単独では終了証拠にならない |
| abandon | A3が管理slotを毎回再読して呼ぶ。pointer一致、上記終了証拠、F5保存中でないことを検査。cleanup_pending=true→tile retire完了→saved_releaseで借用終了→saved_closed解除→scan_release_all→共通リセットhelper | N`dispnv04/disp.c:195-207` の無条件破棄に対応。差分: OS32では解除が失敗しうるので停止確認と回収再試行を分離 |
| 部分cleanup失敗 | 借用解除済みなら保存像は復元不可としてA3に記録、残予約はctx nodeが所有。cleanup_pendingと管理slotを残してcleanup再試行（saved_release済みなら再解除を飛ばす）。service.c:364の既存条件より前にcleanup_pendingを検査しGF4_EBUSYを返す。unloadも拒否。成功していないreleaseをゼロクリアしない | N`nouveau_bo.c:136-145` の寿命終端。OS32では失敗しうる解放を再試行可能にする |
| 終端成功 | 残予約なし・借用なしと共通リセット完了後だけcleanup_pending=false。abandon成功時だけA3の像/activeとdisplay_cui_owner/display_sessionをゼロにし管理slot閉、次enter可。元restore失敗は成功に塗り替えない | F4 A3-caseへの明示追記。復元結果と回収結果は別 |
| 二重shutdown/再enter | 完了済みcleanupは副作用なし。enterは残借用/UNKNOWN/cleanup pendingがあればEBUSY、解消後だけ開始。通常leaveで保存像がなければabandon不要 | N`dispnv04/disp.c:57-96,189-214` に対応する停止/破棄境界 |

- 共通リセットhelperをservice.cに置き、通常stopとabandonのrelease_all成功後に呼ぶ。`:479-490` のhead image、hold、saved_closed、hold_saved.version、s->scan/test、scan_bankをすべてリセットする。cleanup_pendingは通常stopでもretire/release開始前に立て、診断保存像が残る間・途中失敗時は維持する。abandonから呼ぶ内部宣言はapi.hに置く。
- 管理slotはmoduleとctxが生存する間、restore可能またはcleanup pendingなら公開する。停止直後のready=falseでもabandonは呼べるがrestoreは従来どおり拒否。
- 手本 N`dispnv04/disp.c:195-207` のrestoreはvoidで、vfree(disp)は成否によらず無条件。PM決定により最終復元失敗像も終端破棄する。OS32では解除自体が失敗しうるため、完全停止の証拠と再試行用所有を保持する。
- `saved_release` 自体はpointer照合と借用解除だけの既存意味を維持。新abandonが終了確認を担い、通常の稼働中には呼べない。
- 98帰還処理は失敗しても試み、帰還rcを記録。管理口を古いpointerでキャッシュしない。F5の像をA3が破棄する口にはしない。

## 6. ファイルごとの変更一覧（1機能・1ファイル＝1段）

| 段 | ファイル | 機能 / 依存・並列 |
|---|---|---|
| D1 | mm/pgalloc.h | 動的map型/上限/helper宣言。先行 |
| D2 | mm/paging.h | device map/unmap宣言。D1と並列可 |
| D3 | hwdev/gf/vram_view.h | 借用型/view key宣言。先行 |
| D4 | hwdev/gf/accel.h | EBUSY追加。D3と並列可 |
| D5 | hwdev/gf/feature5.h | slot状態/旧新node/init/retire/保存値表。D3後 |
| D6 | hwdev/gf/display/api.h | cleanup_pending/abandon/共通リセット/管理ops宣言 |
| M1 | mm/pgalloc_resource.inc | 固定配列と登録照合/静的変更拒否。D1後 |
| M2 | mm/paging_map.inc | 全検査後のPTE/台帳commit。M1後 |
| M3 | mm/paging.c | 厳密な集合監査。M1後、M2と並列可 |
| M4 | mm/pgalloc_surface.inc | 集合helperによる照会。M1後、M3と並列可 |
| M4a | drivers/video/geforce/geforce_module.h | rid/owner照会宣言。M5前 |
| M4b | drivers/video/geforce/geforce_platform.c | 予約成功時のrid保存と範囲照会。M4a後、M5前 |
| M5 | hwdev/gf/instmem_map.c | production adapter結線。M2/M4b後 |
| V1 | hwdev/gf/vram_view.c | 借用・予約解除の中央管理。D3/D4後 |
| T1 | hwdev/gf/tile.c | pitch/init/transaction/retire。D5後、V1と並列可 |
| T2 | hwdev/gf/state.c | slot値表の保存復元、借用保持。T1後 |
| S1 | hwdev/gf/display/save.c | 終端abandonと元rc保持。D6/V1/T1後 |
| S2 | hwdev/gf/display/service.c | init順序、quiesce後のretire、共通リセット、pendingによるenter拒否、管理slot寿命。T1/S1後 |
| S3 | kdebug/gf_display_test.c | 失敗後cleanup、abandon成功時だけCUI owner/sessionをゼロ化。S1後、S2と並列可 |
| H1 | tools/tests/test_gf_command.py | 実MM/production map結合、変異登録。M5後 |
| H2 | tools/tests/test_gf_display.py | 停止失敗/回収/再enter結合、変異登録。S2/S3後 |
| H3 | tools/tests/test_gf5.py | tile寿命/init/保存復元、変異登録。T2後 |
| H4 | tools/tests/gf1_instmem_map_host.c | production adapterのrid照会・map/unmap結合。M5後 |
| H5 | tools/tests/test_gf1_instmem_map.py | H4のCHECK入力/変異登録。H4後 |
| I1 | tools/hwdev_layout.json | struct gf4_ctx (:1041)/struct gf5 (:1264)と保存値表を最終配置へ追随。D5/D6/T2/S2後、統合担当 |

- 宣言段でfake APIも凍結: 第n回read/write/map/unmap失敗、書いた後のI/O失敗、全slot初期値、台帳/借用件数観測。既存試験登録にH5を含めて追随する。
- 各試験fixtureの編集は対応H段の所有。CHECK入力、必要なstub/全欄走査/sizeof期待値をその段で追随し、試験番号を推測で追加しない。
- MM固定表1024B、借用nodeはi386で40B（key24+off/bytes8+kind4+next4、実sizeofを正とする）を宣言段で確定。12slot×2node、raw/state、保存値表増分をmodule上限へ合算する。
- 内部宣言のみでKAPIは不変。module公開境界の実レイアウトを変更する場合は既存版/生成器/fixtureの追随を統合担当へ集約し、手生成しない。

## 7. 受入 → 試験と変異

| 受入 | host / 実機 | runtime REDにする変異（各1行） |
|---|---|---|
| A1 複数窓で監査0 | 実paging/ledgerとproduction adapterでDISPLAY＋BAR2＋push＋離れた2view、間はNP。実機enter→hello_gfx→leave | dynamic集合照会を無効化→開窓後監査非0 |
| A2 監査強度維持 | 未登録の穴1頁present、USER/WB/異physも個別注入して非0。AS/lease監査も通す | 穴を包含区間で許可→不正present検出試験失敗 |
| A3 map容量/失敗保持 | 64件上限・重複・owner違い・unmap preflight失敗で台帳/PTE/out不変、再試行成功 | unmap失敗時に登録削除→件数/再試行照合失敗 |
| A4 pitch生成範囲 | 0/1/1023/1024/2048/8192/9216、丸めoverflow/予約越境。実機は合格pitchだけ転送照合 | pitch整列条件を常に真→9216受理で失敗 |
| A5 借用中release禁止 | 実V0＋tile/view、scan_discardを通しEBUSY、unmap/clear後解除 | releaseのborrow検査をfalse→予約再利用試験失敗 |
| A6 部分tile書込み失敗 | 各PFB/GR write、unpause、admission失敗注入。旧/新双方release不可、停止後retire再試行成功で可 | fault時new_loanをput→新予約の保持照合失敗 |
| A7 初期台帳 | 本番startupでdevinit後に非ゼロtileを置き、gf2_start前の全12枠ゼロprogとGR転記を検査。各write/flush失敗は受付不可、再試行成功。実機12枠値を採取 | 最終slotのprogを省略→12枠ゼロ照合失敗 |
| A8 復元失敗後の終端 | 本番管理slot経由でrestore失敗→stop→abandon→次enter。元rcと段別rcを確認。実機98帰還と再enter | engagedの検査を削除して終了証拠成立→quiesce失敗時abandon拒否試験失敗 |
| A9 回収失敗再試行 | 窓close/tile retire/予約releaseの各失敗、saved_release後release_all失敗でもpendingでenter拒否、module保持・二重shutdown・旧pointer拒否・全リセット照合。復元成功対照も実施 | release失敗時node消去→再試行と残予約照合失敗 |
| A10 F5回帰 | セッション内save/restart/restore前後でOWNED値と予約key一致、UNKNOWN保存拒否。tileをclearせずleave→retire→scan解放→次enterも検査 | restartでslotをゼロ化→借用とslot値照合失敗 |

- hostはlibcなし/qemu-i386、実物MM/結線を使う試験を含める。期待値fixtureは出典/版付きでrepoへ置き、CIからRを読まない。
- 実装後は対象ビルド（MM変更につきkernel、関連module/userland）と触った試験の--mutate、lint。変異anchorは一意、コンパイル失敗をREDと数えない。全mutation/check-changedは回さない。
- 今回はread-only設計のためpreflight --fix-fonts、ビルド、host/実機試験は未実施。コード/フォント/生成物を変更していない。実機受入済みとはしない。

## 8. 未決

- ユーザー判断0件。PM決定により、最終復元失敗のA3保存像は完全停止後に破棄する。停止後は復元経路がなく、保持しても回復できる表示を失わない。F4 A3-case:577の成功復元後だけのreleaseに§5の終端回収を追加し、元restore失敗は残す。通常稼働中の破棄は許さない。

反論: 0件。

## 確定 (2026-11-06、Opus の第 2 回で条件付き Approve、PM が反映)
本文の §4 (112・115・117 行) と A10 は、下の 3 点で補う。
- R2-1 (P2、§4 :112): retire の書き込みが届く前提の根拠。quiesce の中の foundation_stop は engine_stop.c:7-23 で FIFO・GR・MPEG を PMC で一度落として戻す (GR はリセット済み)。master.c:40-45 は PCI COMMAND を 0x0102 にして bus master だけ止め、メモリの decode は残す (BAR0 への書き込みは届く)。master_disable が decode まで止めるように変わったら retire が黙って消えるので、A10 の実機の項目に「retire の後に PFB 100600+i*10 を読み返して 0」を足す。
- R2-2 (P3、§4 :117、PM 決定): F5 が保存中・SAVED のまま leave したときは、S2 の stop の冒頭で F5 の free を呼んでから retire する (EBUSY で断らない)。理由: leave は失敗の後でも必ず 98 に戻る (gf-cui 直し 5 の方針)、保存像は表示の寿命に結び付く (手本 dispnv04/disp.c:195-207 は無条件に破棄)。
- R2-3 (P3、§4 :115): gr.c の PFB→GR の転記は残す。「ゼロの PFB を転記する」のは enter の初回だけで、gf2_recover (command.c:181-190) が GR を作り直すときに OWNED の値を PFB から GR へ戻すのもこの転記。
