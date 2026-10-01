# TASK_T2_APPBAND — T2: アプリ帯 + lease 窓 (設計票)

> 状態: **設計中 (2026-10-01)** — ユーザーが着手を承認 (2026-10-01)。設計を記述した段階、独立レビューは未実施。実装・ゲスト検証は未着手。D1〜D36 を変更せず、P1 の 3 番目の票 T2 を T2a〜T2h に分ける (§5)。
>
> 発行: GPT-6 / Codex (設計者)。調査基点 `b5cd920` (`wt/t2-design`、T1 受入完了後の main HEAD)。以下の `file:line` はこの基点。今回の作業はこの worktree の文書だけ、commit / push・配備・NP21/W・NHD・ini・Windows 側の操作なし。
> 決定の正典: [TASK_MEMMAP_V3](TASK_MEMMAP_V3.md) §0・§2-2・§2-3 ⑥・§3-5・§6 T2・§7・§8-4。位置づけ: [V3_PLAN](V3_PLAN.md) P1 / P7。引継ぎ: [TASK_T1_LEDGER](TASK_T1_LEDGER.md) §1-2・§4-1-R〜§4-6-N。B1: [FEP_BOUNDARY](../settings/FEP_BOUNDARY.md) §4 (D31 で T2 に移管)。
> 番地の正典は `include/memmap.h`、実装後の地図は [02_memory](../../02_memory.md) §2-1 (生成)。本票は T2 の実装契約・段・受入を持ち、上位の決定本文を置き換えない。[C1]〜[C5]・[ABI1]〜[ABI3] は [CONSTRAINTS](../../CONSTRAINTS.md)、状態行は [POLICY_DEV](../../POLICY_DEV.md) §8。

## 1. 範囲と引継ぎ

### 1-1. TASK_MEMMAP_V3 §6 T2 の対応表

| # | T2 の項目 | 担当段 | 契約 / 受入 |
|---|---|---|---|
| 1 | lease 契約を実装より先に確定 | 本設計 → 独立レビュー、T2b | §3。SURFACE 参照と AS 仮想番地を分離 |
| 2 | アプリ帯 0x80000000、exec 0x80100000、lease 窓 0xF0000000、物理定数から分離 | T2b・T2c | §4-1、非恒等・複数 AS / PDE |
| 3 | claim / EXEC_DYN_RESERVE / DEVICE_FLOOR / `--cpl0` / `ring3_band_ram_top` 撤去 | T2c | 旧形式を入口前に拒否、物理を AS owner で配る |
| 4 | exec の共有 USER 写像 (SHM・トランポリン以外) と paging の共有 PT 書換え経路撤去 | T2e | §3-3、master・無関係 AS が不変 |
| 5 | shlib を池の実ページ + AS ごとの text RO / data 私有写像へ | T2c | §4-2、data 分離、text owner=shlib を回収しない |
| 6 | 既定 heap 最小化、可変スタック (OS32X ヘッダ) | T2c (stack)・T2f (heap) | §4-1・§4-4、既定 256KB と指定 512KB |
| 7 | 検査 3 段 (master / AS / 毎起動) と lease 回帰 (d) | T2b で部品、T2e で全適用、T2h | §5-2。V86 セッション外 |
| 8 | app.ld / app_sys.ld / shlib.ld / mkshlib / Rust stub / CRT | T2c、T2e、T2f | §2・§4-6。app_sys の住所変更自体は T3 |
| 9 | 全再ビルド・旧形式拒否・4 種の世代識別 (D7 / D35、P7 と共有) | T2c で拒否基盤、各 ABI 段、T2h | §4-6。常駐シェル・旧 `.o` も例外なし |
| 10 | BB を gshell 所有 + lease に (b) | T2e | §3-4。T1 の移譲を継承、3 backend 描画 / present |
| 11 | R1 の移譲 / 回収 2 段化、panic 化、IRQ broker の深さ統一 | T2a | §4-3、純ループ・PCM の STOP と再 open |
| 12 | `mem_map` / `mem_unmap`、`_sbrk` 末尾 map、64KB 閾値 | T2f | §4-4、伸長 → unmap → 終了で owner 0 |
| 13 | 池不足時だけ trim 通知、起動予約なし | T2g (予約なしは T2c から) | §4-5、池 0 でも閉じる / 次アプリ起動 |
| 14 | R3-e: lease 先頭 PT 1 枚事前確保、追加 PT と全体巻き戻し | T2b、T2c、T2f | PT 枯渇 / lease 表満杯 / 途中失敗で全不変 |
| 15 | FEP_BOUNDARY B1: 新帯の `ring3_ptr_ok`、共通 bounded copy、overflow | T2d (帯判定の切替は T2c・T2e) | §4-7、page 末 NUL / 次 page NP / 未終端、SQLite 進入 0 |
| 16 | R5: AS 終了後ゼロ、永続 owner は別勘定 | T2a 以降全段、T2h | lease / shlib / maps / PD の順、取り残しを隠さない |

### 1-2. T1 の実装結果の上に載せるもの

- T1a は 8MB も model / ONLINE。物理の登録上限は 2GB、`MEM_POOL_BASE` は **現時点 0x400000**。0x500000 に上げるのはシェルが動く T3。T2 の仮想定数を物理検査に流用しない。
- T1b の L2 owner、永続 owner、`ledger_reclaim_owner` / retire、全 IRQ / 全例外の `kctx_*_depth`、8 語 jmpbuf を継承。T1 の `ledger_irq_ops` / `ledger_exc_ops` は計数だけで、STOP と fault で増えるのが観測済み。T2a が両方をゼロにする。
- T1c の DMA API と物理記述子を維持。池の返却と固定 DMA プール内の返却を混同しない。PCM の IRQ 解除は T2a の通常文脈で行う。
- T1d の `ledger_reserve_set` は検証済み resource の span ごとの包含検査・一括 commit・永久保持。RAW PCI BAR は予約権限にならない。lease は DEVICE の予約を解除しない。
- T1e の `ledger_surface_create/find/transfer` と gfx 識別 → 予約 → 写像 → BB を継承。選択中 CLIENT の boot → gshell 移譲は RAM なら L2 ごと、固定 RAM は区間も、MMIO は SURFACE だけ。GUI 終了でも backing を保持する。auto では Cirrus 選択時にも PEGC の予備 BB 300KB がある。
- **8MB + PEGC の修正を取り消す順序**: T1 は `ring3_band_set` の私有上端を `min(帯上端, sys_usable_mem_end())` にし、BB が私有 PTE を上書きする 74 ページの漏れを閉じた。T2c で私有帯を高位へ移した後にこの物理上端依存を外し、T2e で BB の旧共有写像を lease に替える。先に切り詰めだけ外す段は作らない。
- T1f の [C5] を維持。恒等の backing は P2V / P2V_IO、PD/PT の物理は V2P、アプリ / lease の仮想は **AS / SURFACE による翻訳**。`as_va_to_pa` は現状 USER+RW 用であり、B1 の RO 入力にはそのまま使えない。

T1 の残件は消さない: PCM STOP → 再 open は実機で未確認、`cirrus-off` + `GFX=cirrus` 未実施、T1f 実機未実施、HostDrv の番地解釈 24 件未確認。前 3 件は T2a / T2e / T2h の回帰でも追う。HostDrv は例外一覧を継承し、T2 の非恒等ポインタが渡る経路を別に監査する (§6)。

### 1-3. 後続との境界

| 後続 | T2 が渡すもの | T2 では行わないこと |
|---|---|---|
| T3 カーネル帯 | 高位アプリと物理池の分離、サイズ実測 | カーネル 3MB 化、shell 0x400000、KERNEL_SLACK、DMA 0x3E0000、固定 PT の画像外化。ただし X15 は §6-1 |
| T4 SQLite | B1 の共通 copy、R1 通常文脈回収、世代検査部品 | モジュールローダ、MEMSYS5 512KB / FEP 予約、F2 / F3、B3 の旧 db_exec/prepare 結線・copy-before-finalize、B4 の engine in-flight fail-stop |
| T5a FEP | B1 の API と失敗時未公開バッファ | B2 の staging → finalize → copyout、RESIDENT 接続 2 本 |
| T5b / T6b | 物理 staging / bundle 定数を仮想定数から切り離す | 起動順・ローダ区間表・同梱・508KiB 解除。T2 の圧縮画像は現上限以内 |
| T5c | owner 回収と D35 検査部品 | ドライバモジュール化、スプラッシュのアプリ化。lease の借用契約だけ先に用意 |
| T7a / T7b | planar のポインタを SURFACE 起点に、低位 USER を撤去 | planar BB の物理移動、低位解放、OpenType、共有グリフキャッシュの選定。暫定 Unicode RO lease は T7 で撤去 / 更新 |
| P4 / Trident | 私有 lease API、台帳由来の属性 | BAR sizing、装置同一性再確認、probe / enable の GUI 境界への移動。T1 の起動時順序を維持 |
| P6 | R1 の安全点・非再入 trim 配送境界 | 自動番犬の方式、長い KAPI の取消期限・最大応答時間の確定、アプリごとの x87 保存復元。T2 の合格を P6 完了とは扱わない |
| P7 | T2c の4世代検査、コンパイル単位の識別、混在試験 | 全 KAPI の整理を独自に広げない。fork 時の並替えは P7 と一組でのみ行う |
| サーフェス層 / P8 / P9 | (b) gshell の CLIENT と全画面 lease | (c) 私有面 + WM 合成、Video HAL、SDL、音・入力の層 |

## 2. 現状調査 (実コード、基点 b5cd920)

`0x500000` は exec のロード位置で、**現アプリ PDE の開始は 0x400000** (先頭 1MB が shlib)。定数の置換だけでは動かない。

| 場所 (file:line) | 現状と T2 での処置 |
|---|---|
| `include/memmap.h:352`、`:365`、`:397`、`:416`、`:449`、`:462`、`:472` | APP 0x400000 / 最大2 PDE、DEVICE_FLOOR 0xF00000、SYSTEM_SPACE がその別名、POOL 0x400000、exec=SHLIB_END、固定スタック256KB。SYSTEM_SPACE を独立した物理定数にしてから DEVICE_FLOOR を消す |
| `sdk/link/app.ld:8`、`sdk/link/app_sys.ld:5`、`sdk/link/shlib.ld:20`・`:64` | exec / shell / shlib の直書き。app / shlib を高位へ、app_sys は世代 stamp のみ (住所は T3) |
| `tools/mkshlib.py:48`・`:224`・`:277`、`sdk/rust/os32api/src/gui/stub.rs:64` | shlib 0x00400000 とヘッダ / entry / data の検算・bind。mkshlib の OS32X 包装も変更 |
| `sdk/mkos32x.py:99`・`:145`・`:158`、`exec/os32x_hdr.c:22`、`sdk/include/os32/os32_kapi_shared.h:163`・`:213` | FORCE_CPL0、ELF から load_addr、v3 ヘッダ48B。現在は version>=3、data offset、min API 片方向。未知形式も通る |
| `sdk/crt/crt0.asm:15`、`sdk/crt/crt0_c.c:8`・`:14`、`sdk/crt/syscalls.c:162` | BSS / 引数積み・CRT の kapi 名と stamp・固定 sbrk 上限。スタック実サイズ / 各単位 stamp / 実行中 map へ |
| `sdk/kapi_rust_gen.py:45` | 未知の非ポインタ型を u32 にする fallback。D35 の生成失敗へ |
| `exec/exec.c:301`・`:728`、`exec/appslot.h:113` | 物理上端で band_top を切り詰め、ring3_ptr_ok は低位 VRAM も許す。AppSlot の stack_top / band_top と固定 RING3_USTACK_SIZE を分離 |
| `exec/exec.c:964`・`:990`・`:1013`・`:1039` | exec_child_claim / ring3_band_ram_top / CPL0 claim/release。T2c で撤去 |
| `exec/exec.c:1869`・`:1928`・`:1949`・`:2034` | RAM 上端で PDE 枚数、空きの半分を exec_heap、sbrk の二段先取り、3 領域 map。仮想容量と実ページ供給を分離 |
| `exec/exec.c:2049`・`:2052`・`:2056`・`:2060`・`:2067`・`:2074`・`:2079` | VRAM / SHM / フォント / Unicode / planar BB / 選択 BB / trampoline を USER 化。最終形では SHM・trampoline のみ master に事前作成、他は削除または lease |
| `exec/exec.c:1182`・`:1214` | BB の恒等 USER と領域3本の teardown。可変 map と lease の台帳を回収する方式へ |
| `kernel/paging.h:181`・`:183`、`kernel/paging.c:108`・`:121`・`:127`・`:132` | APP_BAND_PDE=1、PT 配列2本、bootstrap PT 数 / DEVICE_FLOOR / 32MB 未満の ASSERT。高位・疎な私有 PT の ASSERT へ |
| `kernel/paging.c:686`・`:714`・`:755` | RAM 量で仮想帯を制限、master の同帯 PT 必須、identity PTE 複写。高位 PT はゼロから作る |
| `kernel/paging.c:805`・`:820`・`:826`・`:845` | app 外なら共有 page_tables を書き、keep_cache は宛先 PTE から読む。通常 API では拒否し SURFACE 属性を指定 |
| `kernel/paging.c:415`、`kernel/pgalloc.c:174`・`:197`・`:894`、`kernel/memory_boot.c:28`・`:165`・`:333`、`kernel/sys.c:72`・`:122` | **物理 metadata / workspace / 最低量 / BB 凍結まで APP/EXEC 定数に依存**。高位化の前に物理専用の境界へ。T1 の FIXED/ARENA_TOP 経路を維持する |
| `kernel/shlib.c:76`・`:97`・`:214`・`:224`・`:258`・`:284`・`:317` | 1MB 全域 claim、末尾の data 原本、master USER text、連続 data 複製、detach で先に free。ページ単位の池 + 私有 PT、unmap/TLB 後 free へ |
| `exec/exec.c:1619`・`:1476`、`exec/appslot.c:609`・`:643`、`kernel/pgalloc.c:348`、`kernel/irq.c:23` | fault/STOP は深さを下げる前に exec_exit の回収。計数と irq_in_irq は別。T2a で移譲 / 通常回収へ |
| `exec/ring3_str.c:18`・`:36`・`:42`・`:63`、`kernel/paging.c:622`、`kapi/kapi_db.c:210` | ring3_str は返却文字列と RW 判定。db_user_str_copy は1 byteごとだが帯中心の検査。共通 copy は caller PD と RO/RW を区別する |
| `gfx/gfx_core.c:23`・`:30`・`:119`・`:130`・`:135`・`:439` | planar の bb_b/r/g/i 固定初期値、fb は kernel ポインタをそのまま返す。SURFACE 由来の kernel pointer と AS lease VA を分離 |
| `gfx/gfx_core.c:183`・`:297`、`kernel/pgalloc.h:173`、`kernel/pgalloc.c:821`・`:854` | T1 の SURFACE は24B・8本、gen/lease_count は8bit、create は boot 限定。T2 で寿命・属性・ページ占有を強化 |
| `userland/lib/gfx/libos32gfx_core.c:25`、`userland/gshell/src/lib.rs:138`・`:446`・`:644`、`userland/gshell/src/handler.rs:358` | attach で返った fb を保存、WM 復帰で gfx_init、OP_WAIT 内にも WM がある。再 init の revoke と再 attach、trim の安全な配送点が必要 |
| `lib/utf8.c:32`・`:65`、`userland/lib/gfx/text/gfx_kcg.c:32`・`:50`・`:98` | Unicode 表の低位直読は実在 (lib/utf8.c はユーザー側にもリンク)。字形は kcg_read_* KAPI で取得。USER を消すだけでは日本語が fault |
| `userland/lib/gfx/text/lconsole.c:297`、`userland/lib/filer/filer_draw.c:33` | lconsole はローカル配列から gfx 描画、filer は tvram_* KAPI。名前やコメントと異なり、この2経路に TVRAM 物理直書きは無い (U20 の部分調査) |
| `userland/lib/rt/dbgserial.c:287` | 0x500000〜0x500FFF のアドレス判定。高位化の監査対象に含める (U8) |

`EXEC_DYN_RESERVE` は `exec/exec.c:176` の256ページの定義と `:972`・`:1751` のCPL=0用レイアウト計算に残る。claimと一緒にT2cで撤去し、残存検査を行う。`tools/mkos32x.py` ではなく **`sdk/mkos32x.py`**、stub は **`sdk/rust/os32api/src/gui/stub.rs`** が現実の場所。

## 3. lease の契約 (T2 の設計として固定する案)

以下を独立レビューの対象とする。物理を直接指定する公開 map、旧 `{virt, phys, size}`、共有 PT を昇格する案は採らない (X3)。

### 3-1. 記述子・容量・公開 API

**記述子は SURFACE 台帳への参照 `{sid, generation}`**。物理番地も kernel pointer も利用者へ公開しない。内部 SURFACE は owner、backing、ページ範囲、backend/role、geometry、プレーン配置、最大権限、cache、generation、lease_count を持つ。`gen` は u32、参照数は u16 へ拡張する。世代は再定義時に増やし、周回時はその slot を再利用不能にする (同じ boot で古い参照を復活させない)。owner ID 再利用と独立に判定する。

初期上限は **SURFACE 16本、lease 8本/AS、mem extent 32本/AS、アプリ PT 64本、lease PT 56本**。PT の配列は物理ポインタの控えだけで、未使用要素は0。lease 表は AS の固定配列で、実行中に増設しない。SURFACE は 1 本が連続したページ区間を占有し、planar BB の plane はその内側の byte offset で表す。互いに不連続な表示 VRAM 4 plane は4本の SURFACE を1操作でまとめて lease する (8本の上限の内側)。MMIO の余白を跨ぐ巨大な連続 SURFACE を作らない。

公開 ABI の形 (名前は実装時に KAPI 正典に登録、構造体は C89 互換、u32 欄で固定):

```c
/* 物理とカーネルポインタを含まない参照 */
typedef struct { u32 sid, generation; } OS32_SurfaceRef;
/* query は geometry/plane offset/許可権限を含む値の写しも返す */
int gfx_surface_query(u32 role, OS32_SurfaceDesc *out);
/* 成功時だけ out に {token, base, bytes, planes[4]} を返す */
int surface_lease(const OS32_SurfaceRef *ref, u32 access, OS32_LeaseView *out);
int surface_unlease(u32 token);
```

- query は **選択中 backend の、呼出元に許可した role だけ** (Unicode表はbackend非依存のシステムRO roleとして別判定)。単なる sid の知識は権限にならず、lease 時も caller / role / generation を照合する。`access` は RO / RW だけ、PWT/PCD や任意の物理は渡させない。ユーザー指定 AS は受け取らず、保存済み caller AS に貸す。
- token は boot 内の単調 u32 (0無効)、AS に紐付く。周回時は新規貸与を拒否し、再 boot まで再利用しない。別 AS の token、二重解除、revoke 済み token は負の誤りで無変更。
- **lease 結果の VA は記述子とは別**の `OS32_LeaseView` に返す。同じ参照でも AS ごとに異なる VA になり得る。同じ AS の明示 lease 呼出しは別 token。SDK の attach は保持中 token を再利用し、繰返し query だけで参照数を増やさない。
- out / ref は B1 の checked copy で kernel 内に確定し、出力範囲を事前検査する。失敗時は out・PTE・refcount・owner 会計がすべて元のまま。誤りは INVAL (参照/権限/範囲)、NOMEM (物理/PT)、FULL (固定表)、旧世代は INVAL と診断理由で区別する。実際の `OS32_ERR_*` への対応を生成時に固定する。
- `gfx_get_framebuffer` のソース互換は SDK の薄い橋で保つ。CPL=3 には lease view の plane VA を返す。新しい失敗可能な attach を SDK 内で使い、失敗を無視して NULL 面へ描かない。古い void API のカーネル入口を残す場合は失敗を明示的なアプリ終了に変換する。trusted kernel / WM には内部 `gfx_kernel_framebuffer` で恒等のポインタを返す。公開 query の出力と内部 framebuffer の型を流用しない。
- API は `sdk/kapi.json` から C/Rust とラッパを生成 ([C3][ABI1])、既存 slot は通常追記 ([ABI2])。P7 が D35 の1回の整理を実施するときだけ、世代を切る同じ成果物集合で統合する。T2 が独自にスロットを並べ替えない。各追加で version と KAPI_VERSION を揃え全再ビルド ([ABI3])。

### 3-2. ページ占有・属性・貸与トランザクション

1. SURFACE 登録時に `[first, first+npages)` の全ページの backing / owner または DEVICE の検証済み資源と写像範囲を検査する。geometry の乗算 / offset+長さ / 丸めの overflow を加算前に拒否し、plane が span 内に収まることを検査。ページ端の未使用 byte は同じ SURFACE の占有として扱い、別用途・別 owner・表示面を同居させない。RAM の padding は公開前にゼロ。DEVICE の decode と実際に貸す面の範囲は別。
2. cache は backing 台帳が正: RAM=WB、MMIO/VRAM=UC (既存 PCD/PWT の表現に統一)。SURFACE の cache は根拠との一致を検査する写しであり、caller が上書きできない。kernel alias・全 AS alias が一致しなければ貸与を拒否。新しい PTE の値から `keep_cache` で継承しない。
3. 通常文脈で caller owner を固定し、slot / lease VA の空き / 全 plane の権限 / 出力先を先に検査。先頭 PT は AS 生成で1枚事前確保 (R3-e)。足りない PT は AS owner でページ単位に準備し、ゼロ化。4 plane の束は **全本数**の slot / PT を先に用意する。
4. 準備中の PTE は AS に公開しない。新規 PT への下書き、既存 PT への変更予定を走査可能に保持し、最後の短い IRQ 保存区間で PTE・lease 表・refcount を一括公開。準備中に WM / callback / AS 切替を行わない。失敗で新規 PT と今回の予約を戻す。既存 PT / 既存 lease / master / 他 AS / 永続 reservation は変更しない。
5. active AS の TLB は CR3 再ロードで反映 (386 下限のため invlpg を前提にしない)。非 active AS は再開前の CR3 ロードが必須。private PT を共有したり、global TLB にして保持する最適化は導入しない。成功後にのみ出力値を公開する。

### 3-3. 解除・revoke・隔離

解除順は **当該 AS の PTE を NP → TLB 反映 → lease 表を失効・参照数減算 → owner が返却済みかつ参照0なら backing 回収**。非 active AS は現在の CR3 が当該 AS でないことと次回ロードを保証してから参照を落とす。先頭 lease PT は AS 終了まで残し、追加 PT は空になれば同じ順で返す。DEVICE の予約と kernel alias は永久保持。

owner が返却を要求した面は新規貸与を拒否する状態にし、全 lease が消えるまで backing を保持する。通常 AS の終了では全 lease を先に revoke し、shlib data / private map / PT / PD の回収を行う。gshell の永続 CLIENT は AS 回収の対象外。片方だけの revoke は他 AS の表・PTE・TLB・参照を変えない。

**「2 AS に貸して互いに見えない」**は、未貸与面と lease 操作の波及を防ぐ意味。同じ面を A/B に明示的に貸せば画素は共有する。試験では S を A だけ、T を B だけ、U を両方に貸し、未貸与 S/T の拒否、U の画素共有、A の U revoke 後も B が使えることを分けて測る。

通常 map / unmap API は private PT の所有確認と対象仮想帯の確認を行い、**共有 PT を書く要求を無変更で拒否**。master を書く `paging_set_page` / `paging_map_range` / `paging_map_phys` も監査し、live AS 後の USER 昇格を禁止する。SHM / trampoline の共有 USER は最初の AS より前に専用の初期化口で作る。V86 は専用 session 口だけが低位 USER を変更し、正常・異常脱出で復元する。V86 中に通常 AS の生成・実行・地図検査をしない (CUI の呼出元 AS は停止中)。

### 3-4. gfx・再 init・過渡的な低位データ

- 通常 GUI には **CLIENT** を貸し、DISPLAY / VRAM は貸さない。CLIENT は (b) の共有 BB、アプリ私有面ではない。Cirrus の CLIENT は MMIO backing でも表示領域とページで分離した面なので許可し、DISPLAY と区別する。通常 GUI から禁止する「VRAM」は低位 VRAM / デバイス窓の直アクセスと DISPLAY lease を含み、正当に貸与した CLIENT alias は含めない。
- DISPLAY (planar / PEGC / Cirrus) は GFX 宣言 + 現在の全画面所有者にだけ。T1 の Cirrus DISPLAY `perm_max=NONE` は T2e で「全画面授権時のみ RW」にする。TVRAM は CUI 全画面所有者へ `[0xA0000,0xA4000)` だけ。通常 GUI へは拒否。backend 変更や所有権剥奪で先に revoke。U20 の未監査アプリには物理直書きを残さず KAPI か TVRAM lease に直す。
- kernel の bb_b/r/g/i と bb[] は選択 CLIENT の backing + plane offset から毎回設定する。planar の plane stride は **pitch × height** (400行なら32000B) で、ページ境界ごとに勝手に丸めない。128KB の占有と有効画素範囲は別。packed は plane0 のみ。kernel は P2V/P2V_IO、アプリは lease_base+offset。
- T2 の planar backing は低位固定のまま (移動は T7a)。それでも固定初期値への依存を消し、再 init / 200行・400行の変更で pointer と geometry が一致するようにする。PEGC の連続 BB は起動時確保を維持。gshell への移譲前の CUI 全画面 / スプラッシュは boot owner の面を借り、終了で lease だけ返す。
- 再 init は **新規貸与停止 → 全関連 lease revoke → TLB → refcount0 → backend 初期化/geometry更新 → generation更新 → 面公開**。失敗時は失効した旧 token を復活させず、予約済み fallback の面を新世代として公開する。GUI → CUI → GUI でも RAM backing の確保し直しは不要。SDK は保存済み framebuffer を捨て、resume / attach 時に再取得してから描く。生ポインタの失効を魔法で検出できるとはしない: 旧 mapping は NP、旧 token での操作は拒否、後の VA 再利用は通常の解放後ポインタと同じ禁止契約。
- **Unicode の移行穴を閉じる**: `lib/utf8.c` のユーザー用ビルドは、低位固定表でなく起動時取得した **RO lease** のポインタを使う (CRT と shlib 初期化の両方)。kernel 版は恒等のまま。表128KBは kernel 所有の固定 RAM SURFACE とし、T7 の新表 / 廃止までの橋とする。これは共有グリフキャッシュ U21 の採用ではない。字形は既存 `kcg_read_*` を用い、フォント低位全域の USER を消す。apps/game を含め直読が残るなら同じ移行段で直す。

## 4. exec / 仮想メモリの設計

### 4-1. 配置と可変スタック

| 仮想帯 | T2 の方針 |
|---|---|
| 0x00000000〜0x7FFFFFFF | 恒等 supervisor、USER は SHM / trampoline だけ (V86 session 例外)。物理 RAM 上限 2GB |
| 0x80000000〜0x800FFFFF | shlib の予約仮想1MB、text RO共有・data AS私有 |
| 0x80100000〜 | exec image + BSS → libc heap、下から伸長 |
| 0x88000000〜 | exec_heap の予約起点、上向き伸長。既定64KBだけ map。仮想予約は物理を先取りしない |
| 0x90000000 の直下 | 可変スタック (既定256KB) と直下1ページNP guard。大きな map は guard より下から下向き、exec_heap と衝突したら ENOMEM |
| 0x90000000〜0xEFFFFFFF | T2 では未使用 (アプリ帯上限64 PDE=256MB)。勝手に master MMIO を継承しない |
| 0xF0000000〜0xFDFFFFFF | AS 私有 lease 窓、先頭4MBのPTだけ先取り |
| 0xFE000000〜 | 既存 device / ROM 窓、sup+PCD 等の台帳属性、AS は master のまま共有 |

上記 heap の分け目は T2 の仮想配置方針として `memmap.h` に置き、リンカ / Rust の写しは生成か一致検査で管理 [C4]。2MB は私有総量の受入例で上限ではない。64 PDE 全部の PT は確保せず、image・heap最小・stack が触る分だけ作る。metadata / workspace は T1 の物理配置を維持する独立定数 (旧12MB境界など) にして、0x90000000 との比較をさせない。将来の T3 で物理配置方針を改める。

OS32X の新形式に `stack_size` (byte、0=256KB) を追加し、0以外はページへ切り上げ、最低16KB、最大は仮想配置に収まる量とする。負数相当・加算/切上げ overflow・image/heap/guard との重複を拒否。指定量は起動時に全 map し、demand paging / stack 自動成長はしない。argv / alignment / entry frame が stack 内に収まることも先に検査。AppSlot は `stack_base, stack_size, stack_top` を保存し、全 kill / park / resume / teardown でその値を使う。

`heap_size==0` は exec_heap 64KB、libc sbrk の初期追加量は1ページ (BSS末尾の端数は利用可)。指定 heap は切り上げた指定量 (最低64KB)。旧「空きの半分」・sbrk 先取り二段を撤去。初期物理不足なら完全に巻き戻して起動拒否し、起動用の予約は設けない。CPL=0 は常駐シェルだけとし、通常アプリのフラグで昇格できない。

### 4-2. shlib と loader

shlib の **仮想**1MB は予約したまま、物理1MB claim を消す。ヘッダ・text/rodata・data原本は owner=shlib の実ページ数をページごとに確保し、VFSからページ単位で読み、検証完了まで公開しない。物理連続を要求しない。entry は shlib の実行範囲内、data 範囲・ページ数・OS32X と内側 shlib の世代の一致を検査。原本は恒等 supervisor、AS の text alias だけ RO+USER、data/BSS 複製は AS owner。途中失敗は原本 / attach の準備済みページを全返却する。

master の高位アプリ PDE は **空**。CPL=0 の gshell は現コードでは shlib stub 呼出しを確認しなかった (自身の描画実装 + libos32gfx)。T2 では常駐シェルは静的リンクを契約とし、ビルド検査で高位 shlib への参照を拒否する。shell を走らせるために master に shlib USER を残さない。将来 shell が shlib を使うなら専用 AS の設計が別途必要。

アプリロードも仮想と物理を区別する。master のまま高位 VA へ vfs_read / memcpy しない。AS のページを翻訳し恒等 backing にページ単位で読み込む。argv / image / shlib data のコピーも同じ helper。実行開始直前だけ CR3 を AS へ切替。子の終了では parent CR3 / heap state を正しく復元して WM callback を呼び、master のまま親の高位 heap に触らない。

### 4-3. R1: 移譲 → 通常文脈の回収

T2a は高位化より先に旧配置で着地できる。

1. IRQ の要求は `abort_req` と理由だけ。EOI 済みかつ被割込み CS.RPL=3 の脱出点で状態を ABORT_PENDING にし、対象ID/終了種別/復帰点を控えて longjmp する。**FD・PCM・shlib・lease・AS・池・kfree をここで触らない**。カーネルの KAPI 実行中は要求を残し、安全点まで移譲しない。
2. exec_launch の setjmp 着地 (exec_run / start / resume からの全入口) は trusted stack / CR3 / owner を整え、jmpbuf の深さ復元を確認し **IF=1 に戻してから**既存 `exec_exit` 相当の共通回収へ。再 longjmp で同じ pending を処理しないよう先に一度だけ消費する。正常 sys_exit / WM exec_kill は通常文脈からこの回収へ直行する。
3. #PF/#GP だけでなく #DE/#UD 等、**従来 app-kill と分類していた全例外**も FAULT_PENDING の移譲 → 着地回収にする。kernel の障害を一律 app-kill に変えない。B4 の engine fail-stop は T4 の境界で、SQLite の途中を安全に巻き戻せると T2 では主張しない。
4. 回収は対象 owner を明示: 装置停止・IRQ解除・FD等の利用終了、lease revoke、shlib detach (PTE消去/TLB→data free)、全 private extent / stack / image free、PT/PD破棄、owner pages=0 / retire。WM通知は parent の文脈で行う。既存 cleanup の実順と依存を T2a の trace 試験で固定する。永続 boot/gshell/shlib/DEVICE owner は返さない。
5. 深さは全 IRQ / 全例外の `kctx_*_depth` に一本化。broker の `irq_register/unregister` も全 IRQ 深さを見る。`irq_in_irq` を別の真実として残さず、診断互換が必要なら同じ深さの読み口にする。**経路の移譲が完了してから** `ledger_note` の違反を panic に変える。IF=0 は深さの代用にしない (通常の短い IRQ 保存区間は許可)。boot の broker 自己診断による violations と STOP による差分を区別する。

P6 に渡す前提: KAPI 更新途中の AS 切替なし、owner 明示、trim は安全点、x87 は別要件。純 CPL=3 loop と KAPI 連打の強制停止の受入を持つが、kernel 自身の無限待ちの救済や停止時間上限の保証はしない。

### 4-4. R2: map / unmap と allocator

```c
void *mem_map(u32 bytes, void *hint, u32 flags);
int mem_unmap(void *base, u32 bytes);
```

伸長用の公開口はこの2本だけ。`flags` は既定0 (hint優先・空いていなければ別の穴) と EXACT (希望位置が取れなければ失敗)、TOPDOWN (大塊用) の定義済み組だけ。保護属性・物理・owner は指定させない。全て匿名ゼロ済み RAM、USER+RW、AS owner。size0、非ページ整列hint/base、32bit overflow、帯外 / guard / image / shlib / lease / SHM との衝突、未知 flag を拒否。bytes は map では安全にページ切上げ、unmap ではページ倍数必須。失敗の map は NULL、unmap は負値。失敗で既存写像と heap limit を動かさない。

- 物理は1ページずつ。VA探索・extent slot・必要PT・全物理を用意し、公開直前まで未完 map を呼出元に見せない。既存 PTE を上書きする MAP_FIXED は持たない。準備途中のページは未公開 PT の PTE 等で追跡し、失敗時に全件返す (巨大なスタック上 PFN 配列は作らない)。
- unmap は **呼出元の mem_map extent と allocator用初期heap extent** だけ。image / stack / shlib / lease を混ぜた範囲は全拒否。部分解除を許可し、分割で記録が足りなければ無変更で FULL。NP→TLB→owner free。隣接した同種 extent は併合し、伸長ごとに32本を消費しない。ポインタ再利用後の二重freeは allocator の責任で、カーネルは記録と owner を照合する。
- `_sbrk` は signed incr / pointer overflow を検査し、現 break より先が必要ならページ末尾を hint に EXACT map。**_sbrk は非連続の結果を連続と偽って返さない**。続きが取れない場合、上の malloc が flags=0 の別 extent を新しい arena として扱う。newlib の MORECORE と map arena の接続を SDK 内で実装し、_sbrk 単体は −1 / ENOMEM。負の incr は break を下げるだけで、ページ返却は allocator が末尾の生存参照なしを確認した trim の時に行う。
- `malloc` / `calloc` / `realloc` / `free` と Rust allocator の対応を揃える。**要求64KB以上**は TOPDOWN map、free で即unmap。headerを足した結果で閾値を変えない。overflow の calloc、realloc失敗で旧内容保持、大小を跨ぐreallocを試験する。64KB未満は heap、完全に空いた末尾ページを trim 可能にする。
- 既存 `exec_heap_alloc` / mem_alloc も共通の map 実体で伸長し、大塊は同閾値。shell の固定heapは従来のままで、poolを使い切っても閉じる操作が動く。user heap のメタ情報を kernel が無検証で辿って他 owner を返さない。KHeap の部分trim/複数arenaが必要なら明示的な検証・境界を足す (汎用 KHEAP を池へ伸長しない)。allocator の具体的な実装選択 (TLSFを含む) は T2f の実装設計で実サイズと試験から決める。
- R5: extent の回収後 owner 0。`ledger_reclaim_owner` の最後の掃除だけで漏れを隠さず、`exec_as_leftover_pages==0` を要求する。起動失敗でも PD/PT・lease・shlib data を含め全返却する。

### 4-5. D24: trim 配送は非同期

**mem_map の失敗時には要求 bit / epoch を記録して NULL を返し、KAPI の内部で WM を回さない**。通知は「物理池不足」のときだけ (VA断片化・固定表満杯・引数不正では出さない)。back GUI の AppSlot に1 bitを合成して保持し、追加メモリを使わない。CUIと終了中のASは対象外。

SDK の GUI イベントループが、安全な既存 park / resume の配送で trim event を受け、malloc の末尾空きページと未使用map arenaを返す。任意キャッシュ解放 hook は allocator が操作中でないときだけ1回呼び、再入bitで二重配送を防ぐ。応答しないアプリを待たない。front の SDK は map失敗後、allocatorの操作を終えて安全に yield できる GUI 文脈なら **1巡だけ yield → 1回再試行**し、できない文脈ならそのまま ENOMEM。retry のための新しいメモリ KAPI は作らない。

trim 専用の要求・配送・完了を別々に記録し、KAPI 全体の更新中を示す状態と `ring3_wm_depth` を混同しない。syscall出口でも未完の caller wrapper / allocator がある間は他ASを実行させず、既存の安全な park に戻してから配送する。これは P6 の「KAPI内でASを切り替えない」の実装要件。pool 0 のときも bit・イベントの受け皿・閉じる/STOP経路は既確保。起動予約は無し (D25)。

### 4-6. D35 / P7: 世代と全再ビルド

4つを別の欄として持ち、相互の代用品にしない。

| 識別 | 検査 |
|---|---|
| OS32X 形式版 | 新版の完全なヘッダを読めたこと、既知の**完全一致**。未知の上位版、短いヘッダ、未知flagを拒否 |
| KAPI ABI 世代 | 呼出規約・slot / data配置を識別、完全一致。同じ世代の中だけ min_api_ver <= kernel機能版 |
| メモリ配置世代 | high app / lease / stack契約を識別、完全一致。T3 の shell 再配置時も改訂 |
| shlib プロトコル | GUI/shlib 呼出規約とentry配置の世代を完全一致。依存しないバイナリは明示の「依存なし」、依存があるのに0は拒否 |

T2c で OS32X 次版 (現v3の次) に3世代欄と stack_size を追加する。具体的な世代値は P7 と共通の正典から生成し、番号を別々のツールに手書きしない。image種別ごとに load_addr と entry範囲を検査し、**shellも `!is_shell` の外で照合**、load_addr=0 の警告続行を廃止。全拒否は entry を呼ぶ前、できるだけAS確保前。shlib は公開前。常駐shell不一致は起動停止と再構築案内で、旧shellから更新する想定を置かない。

各 C コンパイル単位へ世代付き参照と note、Rust の各 crate / codegen object へ同等の識別をビルドから強制付与。CRT1個のstampだけで済ませない。リンクに使った object / archive member の欠落・旧値・混在を入力検査し、KEEPした最終ELFの印・旧シンボル・配置と包装時に再照合する。未使用memberと実際に取り込んだmemberを区別し、vendor newlib等の世代非依存部品はビルド台帳で列挙する。asm CRT・LTOも対象。**新SDKで旧.oを包む**テストを必須にする。Rust生成器の未知非ポインタ型は生成失敗、構造体戻り値は追加せず int/u32 + checked out pointer (EAX戻り) に揃える。

各 ABI 変更後は `make clean` → `make all`、さらに **clean-external を明示して apps/game を再ビルド** (`make clean` だけでは消えない)。kernel・loader・SDK・CRT・ライブラリ・shell・shlib・その段で存在するモジュール・in-tree・apps/game のビルドID / 4世代 / hash を一組のmanifestにする。private submoduleを取得できなければ完全な成果物とは報告しない。

v2戻り先とv3の成果物/配備先を分離し、PMが停止中に一組を更新してから起動する [D1][D2][V1]。既存 settings/辞書/ユーザーデータを保つ。T2の今回作業では配備しない。P7との共有受入は **旧アプリ / 旧shell / 旧shlib / 新SDK+旧.o / v2 SDK製apps/game / HostDrvとNHD食違い / 未知形式版**。入口直前の到達カウンタ0で拒否を確認し、単にクラッシュしたことを拒否の証拠にしない。旧.kcgfontの廃止はT7a、モジュール世代の実ローダ適用はT4以降。

### 4-7. FEP_BOUNDARY B1

`exec/ring3_str.[ch]` に `copy_caller_cstr` / `check_caller_write_range` / `copy_to_caller` を共通化し、`kapi_db.c:210` の db_user_str_copy を移す。callerの出自をsyscall入口に保存し、WMがtrustedとして呼ぶ経路を明示する。現在CPLやCR3==masterやポインタの低さでtrustedと推測しない。

`ring3_ptr_ok` は NULL (個別APIが判定)、新app/shlib/stack/SHMと**現在のASに存在する許可済みlease**を早期分類する。これだけで読み書き可能とはしない。B1 の文字列・DB/FEPのデータコピーは通常RAMとSHMを対象とし、MMIO/VRAM lease を拒否 (一般のgfxポインタと別の方針)。RO RAM lease / shlib rodata は入力可、出力不可。

- `[start,start+len)` は `len > limit-start` 等で **加算前**に検査。NULL非ゼロ長・件数積・page丸め overflow・帯跨ぎを拒否。保存したcaller PDと実行時CR3の一致、PD/PT frameが管理下でpresent、PDEのPS拒否、PDE/PTEのPRESENT|USER、出力は両方RWを確認。T1fのRW専用walkはread/writeモードを分離し、既存出力ガードを弱めない。
- NULまで **1バイトずつ検証してから読む**。page末NULなら次pageを触らない。容量はNUL込み、未終端/長過ぎは失敗し切り捨てない。未完成bufferはSQLite/VFSへ渡さない。trustedでも容量とNULは確認する。
- 短いwalk/copyだけEFLAGS保存→cli→入口IF復元、全エラー出口を同じ規約にする。allocation / callback / GUI pump / SQLite / VFSは区間外。copyoutは全範囲検証後に書き、通常の範囲エラーで部分出力しない。IF=0のI/O facade拒否はT4/T5aの責務。
- T2d は **既にdb_user_str_copyを使う入口**を共通関数に置換し、正常と不正の両方を実callerで試験する。旧db_exec/db_prepareの全結線と旧stmt finalize前コピーはT4 B3、FEP facade全体はT5a B2。T2の受入「SQLite進入中の#PFにならない」はB1を通る対象入口についての保証で、未移行の全APIへの保証としない。

## 5. 段の分割と受入

### 5-1. 単独着地の単位

以下は**将来の実装・試験計画**で、今回実行した結果ではない。各段は前段までの正常起動を保ち、実ソースILP32ホスト試験でRED→GREENを記録する。追加試験は tests inventory / check_map / build に登録し、起動用guestは自層deploy.yamlへ [V2]。変異は写しの木で行い、コンパイル失敗を実行時REDに数えない (意図したASSERT拒否は別集計)。各段の `make check-changed`、PM着地の `make all` + `make check` を行う。

| 段 / 依存 | 主な変更ファイル (実装時) | kselftest・ホスト・変異の受入 | NP21/W / 実機の受入 |
|---|---|---|---|
| **T2a R1** / T1 | exec/exec.c、appslot.[ch]、kernel/isr_stub.asm・isr_handlers.c・setjmp.asm・irq.[ch]・pgalloc.c、PCM回収の呼出境界 | 移譲traceでfree/cleanup=0、着地IF=1/depth0・対象ID/親復元・二重回収なし、全例外と全longjmp入口。kselftest通常深さ0/owner往復、panicはホスト捕捉と別故障起動。変異: IRQ中teardown、IF復帰削除、brokerの旧深さ、pending再消費 | 8/17MB旧配置でfaulttest pf/gp/de/ud/loop/kloop、STOP後free baseline。Ra266 PCM STOP→tick進行→再open/再生、irq_ctx_violations差分0。V86往復 |
| **T2b 私有lease基盤** / a | kernel/paging.[ch]・pgalloc.[ch]、新 exec/lease.[ch]、kselftest、gfxのSURFACE型板 | 公開callerはまだ切替えず合成ASで新lease窓だけを試す。先頭PT1枚、追加PT、属性/ページ端/世代/権限/8本満杯/複数plane一括。master+他ASのPTE全比較。変異: sharedPT書込、PCD落とし、TLB前free、途中rollback欠落、世代無視。kselftest S/T/Uの2AS往復 | 17MBの既存GUI/CUI/V86回帰、新boot selftest fail0。旧USER経路はまだ利用中なので最終地図検査とは数えない |
| **T2c 高位配置と形式切替** / b | memmap.h、paging・pgalloc・memory_boot・sys・shlib、exec/appslot、OS32X共有ヘッダ・os32x_hdr、sdk/crt、sdk/link/3本、sdk/mkos32x.py、tools/mkshlib.py、Rust stub/shlib、生成器・build・app.conf・cpl0_probe撤去 | 物理定数切離し、疎PT、可変stack、shlib page供給、D35の全入口。kselftest高位AS/owner、hostで断片化pool・2PDE超・shell load照合・旧.o混入・起動失敗全段。変異: RAM上限でVA判定、固定stack teardown、master高位写像、未知形式受入 | 全再ビルド一式で8/17MB CUI/GUI/入れ子/park/fault、256/512KB stack。旧CPL0/旧shell/旧shlib拒否。**T2c〜T2dは旧低位共有USERを継続** (旧gfx consumerの回帰を保つ)。最終T2保護の受入はT2e |
| **T2d B1 共通copy** / c | exec/ring3_str.[ch]・exec.c、kernel/paging.[ch]、kapi/kapi_db.cの既存checked入口、関連host | §4-7。kselftest RO入力/RW出力、hostでcaller/master相違・page末NUL・次NP・未終端・全overflow・PS・MMIO・IF両値・失敗out不変。変異:masterで検査、RW条件欠落、NUL後先読み、先にstrlen、無条件sti | 未終端/跨ぎ入力KAPIが失敗しSQLite entry0、既存正常DB/FEP・GUI描画。帯外dispatcher killとcopy失敗を区別 |
| **T2e gfx/低位USER切替** / d | gfx/gfx_core.c・backend3本、exec/exec.c・lease、paging共有経路撤去、sdk/kapi.json・生成物、userland/lib/gfx、lib/utf8.cユーザー版、CRT・Rust shlib初期化、gshell lib/handler、kselftest | §3のAPI/BB再取得/Unicode RO lease、SHM/tramp事前作成、3段検査全適用。hostは実backend選択→fb出力まで (T1eの模擬選択だけで済ませない)、予約/登録/写像失敗・fallback/reinit。変異:旧bb pointer、plane offset誤り、revoke他AS、余白別owner露出、無条件VRAM授権 | 8MB planar/PEGC、17MB planar/PEGC/Cirrusで描画/present/日本語、GFX全画面とGUI、通常GUIのVRAM kill、片方revoke、GUI→CUI→GUI、cirrus-off強制指定fallback、V86後復元 |
| **T2f map/allocator** / e | 新exec/appmem.[ch]、exec/exec_heap.[ch]・exec.c・appslot、paging、sdk/kapi.json・CRT syscalls・malloc接続・Rust allocator、kselftest、memコマンド | §4-4。kselftest map/unmap/owner0、hostで各ページ/PT/extent不足注入、partial unmap、hint衝突・非連続arena、64KB境界3値、calloc/realloc、可変stack残存。変異:別owner free、zero省略、失敗heap上端更新、初期reserve追加 | 8/17MB伸長→unmap→終了、物理枯渇/VA断片化を区別、pool0でもSTOP/閉じる→再起動。複数ASで片方free後もう片方が内容保持 |
| **T2g trim** / f | exec/appslot・appmem、安全点、GUI proto/SDKイベントループ・allocator、gshell handler/WM、関連host | §4-5。固定bit合成・backだけ配送・再入拒否・一巡一再試行・未応答/終了AS。kselftest要求管理の無確保性、hostでA map途中にB hookが入らないtrace。変異:mem_map中pump、常時trim、CUI配送、無制限retry | 8MBでbackが末尾を返しfrontの再試行成功、返せなければENOMEM、trim配送中STOPでもowner0・WM生存。pool0から閉じる経路 |
| **T2h 統合受入** / g | guest試験・deploy.yaml・host検査の結線・本票の結果追記・正典の実装説明/生成地図 | 全段のkselftestとhost/変異、§5-2地図、D35混在セット、[C5]走査、target sizeofとsize予算。新機能の一括追加段にはしない | §5-3の構成表、R5、旧新混在、T1回帰の残件を結果付きで閉じるか理由付き残件に。未検証をPASSとしない |

T2b の新APIは内部のみ、T2c の旧共有USERは既存機能を保つ過渡状態で、T2e 完了まで最終隔離の合格を宣言しない。T2c の形式・配置・shlib・CRT切替は相互依存する **最小の同時更新単位**。準備のhost検証はファイル群ごとに分けられるが、半分だけの成果物を配備しない。T2c以降もABI変更のたびに世代/機能版と成果物を一式で揃える。

**heapの切替はT2fで一括**: T2c〜T2eでは実行中の伸長がまだ無いので、初期量を64KB/1ページへ縮めない。T1の起動時予算計算を物理専用の暫定helperとして残し、旧配置のcode/stack/guardを引いた容量から得たsbrk/exec_heapの「byte数」だけを新しい仮想予約へ写す (256MBの仮想余白を半分にして物理を先取りしてはいけない)。高位VAをこの予算helperへ入力しない。T2fでmap・allocatorと最小初期量を同時に有効化し、この暫定helperと旧二段先取りを撤去する。これは旧バイナリ互換層ではなく、各段の起動を維持する実装順序である。

### 5-2. 検査3段と lease 回帰 (d)

| 検査 | 具体的な期待値 |
|---|---|
| (a) master | 台帳からRAM / FIXED / DEVICEを引き、低位〜池上端と高位窓を走査。USERはSHM/trampolineだけ。登録MMIOはsup+PCD、15〜16MBの未登録部分はNP、APP/LEASE PDEは空 |
| (b) AS生成/変更後 | PDE0〜511はmasterと同一。512〜959は当該AS ownerのPTEだけ (shlib textはshlib owner ROのみ例外)、未使用帯はNP。960〜1015は当該ASの有効leaseと全page一致。1016以後の共有窓はmasterの権限/物理を維持 |
| (c) 毎起動 | gfx予約/写像、exec trampoline初期化が済んだ後のpost-exec地点で(a)+合成ASの(b)+S/T/U隔離を実行し全部返す。probe前の候補検査とprobe後の選択面検査を区別し、GUI移譲後にもledger_selfcheck。V86中は止め、終了時復元後に(a) |
| (d) lease | 物理対応・全aliasのcache一致、masterと第三ASのPDE/PTE(権限も)不変、3backendで実描画/present、PT/lease表枯渇と途中失敗全rollback、片側revoke、終了/起動失敗時回収、再initのgenerationと旧token拒否 |

比較対象はPTEの物理番号だけでなく P/U/RW/PCD/PWT と owner/参照数。失敗時のbyte比較から診断カウンタの増分だけを除き、会計変化を診断として免除しない。変異で境界を1ページずらすケースはリンクASSERT・地図検査・実動作試験のどれが止めたか記録する。

### 5-3. 8MB / 17MB / 64MB と観測方法

T1 §4-0 と同じ道具を使う。実施は後日PM/テスター、今回の設計作業で環境を変えない。

| 構成 | 手段 | T2 の受入 |
|---|---|---|
| NP21/W 8MB | `np21w_ini_live.py` の既存 `ram-8mb` (ExMemory=7)、planar/PEGC | kselftest/地図fail0、GUI + 私有総量2MBの3配分 (下)、起動描画終了×20、pool戻り、FEP1語、fault/STOP、枯渇から閉じる/再起動、trim |
| NP21/W 17MB | `ram-15mb` (名前は15だが ExMemory=16、検出17408KiB) | 同じ項目 + 15〜16MB非配布、2枚以上のアプリPDE、planar/PEGC/Cirrus、全画面/GUI/lease拒否、V86復元 |
| Ra266 64MB | 実機、`rshell_serial.py` とbootlog/mem/kselftest | 同じ基礎 + 検証済みPCI窓sup+PCD、PEGC、CS4231再生中STOP→再open/再生、pool/owner戻り。RAW BARを検証済みとして数えない |
| ホスト | T1の実ソースILP32足場 (test_memory_boot / pgalloc_model / paging_bounds / app_band_pde / app_bb_overlap / gfx_boot / ledger) を拡張 | 8/17/64MB合成地図、12MB付近FIXED/ARENA_TOP境界、疎PT/枯渇/overflow/変異。実機の代替とはしない |

2MBの3配分は上位票 §7 と同じ: (i) code1500 + shlib data40 + stack256 + heap252、(ii) code/bss64 + shlib data40 + stack512 + heap1432、(iii) code200 + shlib data40 + stack256 + exec_heap1552 (すべてKiB、計2048)。再ビルド後の shlib data が40KiBと異なる場合は **実測量を内数にして heap を調整**し、総量2048KiBを維持する。heapにはsbrk初期ページも含め、PD/PT・共有text・lease backingは別勘定。これは最終P1の容量条件でもある。T2で現固定帯のため不成立なら実測不足量を記録してT3のSLACK解放へ渡し、T2で「2MB合格」とは書かない。

各回で新しい kernel.elf / kernel.map のnmから番地を引き、NP21/Wは `/api/mem?space=phys` で `used_pages` / owner表 / kctx深さ / ledger診断 / leftover / lease数を前後比較、`gui_gate.py` と screenshot で見た目、`tools/guest_tests.py` で既存回帰。実機は `/api/mem` がないので `mem` のowner表示とselftest/trace出力を使う。既存の `mem` 表示に足りない lease/世代/owner 0 の読み口はT2hまでに追加する。

最低記録: commit/build ID・Image CRCとサイズ・構成・実コマンド・rc・before/after free/owner/lease・表示結果・失敗/skip。`fault_kill_count` はSTOPでも増える現挙動を前提に終了種別も読む。NP21/WのPCM無しをPASSにせず実機で音を確認。32/128MB presetは補助で64MBの代わりにしない。64MBちょうどのNP21/Wが必要なら別タスク+[D2]。配備手順は [08_build](../../08_build.md) §8-4 と [POLICY_DEBUG](../../POLICY_DEBUG.md) §5、停止/起動は `tools/np21w_ctl.py`。ここで操作承認を取得したことにはしない。

最終P1のうちT4以降にしか存在しない条件 (必須モジュール/MINIMAL、512KB DB poolとFEP優先予約、FD同梱、OpenType) は後続へ明示して渡す。T2では現SQLite/FEPを有効にした通常構成で検証する。

## 6. サイズ予算・リスク・未確認

### 6-1. カーネル本体の予算と X15

基準はT1f実測: `.text`316,942B、`.data`32,759B、`.bss`254,948B、`__bss_start=0x156000`、`__bss_end=0x1943E4`、ASSERT上限0x195000まで **3,100B**。`.got.plt`末尾から`.bss`前まで **2,524B**。両者は加算できない。text/dataが2,524Bを超えて増えるだけでもbss開始が4KB進んで失敗する。

以下は**設計時の概算、未ビルド** (正味の削除効果を楽観して予算に入れない)。

| 項目 | text/data 増分の見込み | 固定データ / その他 |
|---|---|---|
| T2a R1 | 0.5〜1.5KiB | pending等 <128B |
| T2b/T2e lease・属性・検査 | 3〜6KiB | SURFACE拡張・表、各AS8lease |
| T2c 配置 / shlib / D35 | 2〜5KiB、旧claim/先取りの削除は別に実測 | PD/PTは池。全64枚先取りなし |
| T2d B1 | 1〜2KiB | 大きなcopy bufferの新設なし |
| T2f/T2g map / trim / 回収 | 3〜6KiB | 各AS32extent、要求bit。SDK allocatorの大部分はuserland |
| kselftest/診断の追加 | 1〜3KiB | 本番同梱、削って帳尻を合わせない |

合計 **約10.5〜23.5KiB**、削除前の概算。現予算2.5KiBを大きく超える可能性が高い。`AppSlot`に大きな固定表を直に足すとBSSも超えるため、AS制御ブロックは起動/AS生成時に固定KHEAPから確保し、実行中は固定容量とする。失敗すれば起動を拒否、終了で返す。暫定目標: PT控え480B + lease8×32B + extent32×16B +制御128B = **1,376B/AS以下** (最大4通常ASで5,504B)。SURFACEはplane offsetを含む **64B以下×16=1,024B**、T1のowner/region/resource表2,816B、AppSlot等を含む所有権/写像管理の合計 **16KiB以下**をtarget sizeofで検査する (U1/U9)。extentは `{base,end,kind,flags}`、PFNはPTEを正とし二重の巨大配列を置かない。lease32Bはref/token/base/end/perm等を持ち、plane値はSURFACEから計算する。PD/PTページの実占有はこの16KiBとは別に計上。KHEAP192KBの他顧客と同居するピークも測る。

**X15 の扱い**: T2aから各段でreadelf -SW / nm -S / kernel.map / sizeを保存し、text/data・bss・bss前余白・ASSERT残り・圧縮サイズを測る。残り1.5KiB未満、または次段の最小実装が入らない実測が出たら、その段の着地前にPMからユーザーへ **「T3の固定PT画像外化だけを前倒し」** を具体差分と測定で上げる。順序変更なので自動承認と解釈しない。前倒しする場合は予約・写像・初期化・リンカ/地図検査を一組にし、T1の8MB metadata固定域や現スタックと重ならない位置を別途設計する。最終T3の0x3F1000を現shell帯へ無条件に置かない。診断削除・ASSERT緩和・無断のカーネル帯拡張で回避しない。**本票は前倒し済みを前提としない**。

### 6-2. リスクと未確認の台帳

| ID | リスク / 未確認 | 閉じる段・証拠 |
|---|---|---|
| R1 | IRQ移譲時のEOI・スタック・全longjmp経路、再入cleanup | T2a、asm実行試験+guest全fault/STOP。PCMは実機証拠が要る |
| R2 | 0x80000000以上をsigned intで扱う隠れた比較 (U8)、HostDrvが非恒等user bufferを物理と誤認 | T2c/T2h、SDK・lib・apps/gameも検索し、VFSのkernel bufferとの境界で翻訳。HostDrv24件の意味は未確認、該当経路をゲストで試す |
| R3 | metadata / BB探索の物理条件へ新APP定数が混ざり8MB全停止 | T2c、T1の8/12境界/17/64の足場で物理配置が維持されること |
| R4 | Cirrus CLIENTとDISPLAY混同、planar geometry変更、ページ端露出、fallback後古いfb | T2e、実backendからSDK描画まで通す。T1eの模擬選択の証拠だけでは足りない |
| R5 | 8bit世代の周回・slot/token再利用・参照数overflow | T2b、u32世代/周回拒否、別AS tokenと二重free。生ポインタの使用期限は§3-4 |
| R6 | static表/KHEAP/コード予算不足 (U1/U9/U15) | §6-1の各段実測。gshellは現shell帯のimage+二heap+stackのピークを測り、BB実ページを二重加算しない |
| R7 | newlib MORECOREが非連続を受けない、exec_heapのtrimで生存データ破壊 | T2fで実allocatorを試す。模型mallocだけでは受入不可。TLSF採否は未確定 |
| R8 | trim時にWMを同期で回してcurrent ownerを取り違える | T2g、要求記録と配送のtrace、再入・未応答・STOP、無確保性 |
| R9 | source互換bridgeがlease失敗を隠す、Unicode低位直読が残る、TVRAM直書きU20 | T2e全再ビルド/日本語/再attach。確認済みlconsole/filer以外とprivate apps/gameは未監査 |
| R10 | D35のstampがCRTだけ / 欠落objectを見逃す、最終ELFに古いslotが残る | T2c/P7、旧.o/旧archive/asm/Rust/LTOを含む混在試験。private submodule不在は未完 |
| R11 | master空の高位shlibをshell/WMが呼ぶ、子終了のparent heapが別CR3 | T2cの静的参照禁止と親子/WM回帰。将来のshell shlib利用は本契約外 |
| R12 | PCI BARが高位app/lease仮想と衝突 (上位U10) | 恒等に置ける2GB未満のみ直写し。高位BARはdevice窓へ別名、実装未対応なら予約しても公開拒否、重ねて写さない。sizing/汎用配置はP4 |
| R13 | T2受入とP1/P6受入の混同 | §1-3/§5-3。x87・長いKAPI期限・モジュール・MINIMAL・最終8MB余量は未確認として担当へ |

## 7. 独立レビューに突き合わせる論点

対象は本票と基点 `b5cd920` の §2 掲載ファイル。要旨は「高位ASとSURFACE leaseの契約を具体化し、T1のR1/所有権を接続する」。判定基準は [ROLES](../agents/ROLES.md) §5: **到達可能な反例を伴う現契約違反**をブロックとして示す。設計者による本票の整合確認は独立レビューの代用ではない。

1. **X3 / ページ占有** — 連続SURFACE・不連続planar4面の一括操作・Cirrus CLIENTの例外・cache一致で、未貸与物理や表示面を露出する反例がないか。通常map拒否だけでなくmaster変更口も閉じるか。
2. **lease寿命** — prepare/commit/rollback、active/非active TLB、片側revoke、全終了経路、owner返却待ち、再init世代とtoken周回、SDK再attachの契約が整合するか。
3. **R1 / T1 B1** — EOI後移譲→IF=1回収の全復帰点、全例外、broker深さ統一とpanic化の順、PCM停止/IRQ解除/再openが成立するか。
4. **R2 / R3-e** — heap希望VAと非連続arena、partial unmapと固定extent表、疎PT/先頭leasePT、全rollback、起動予約なしが矛盾しないか。
5. **trim / P6 X5** — 非同期要求→安全なpark配送→一回再試行でKAPI/allocator再入を避けられるか。`ring3_wm_depth`に排他の意味を持たせていないか。
6. **B1 / D31** — caller出自、RO入力/RW出力、overflow、page末NUL、IF復元、MMIO拒否、T4 B3/B4・T5a B2との保証範囲の境界。
7. **D35 / X6** — 4世代の独立性、shellも入口前拒否、各コンパイル単位の欠落検出、旧新混在試験、T2途中段/P7の機能版と世代の使分け。
8. **T1引継ぎと段の切替** — 8MB BB漏れ修正を外す時点、物理metadata定数の残留、shellは静的リンク、低位Unicodeの橋、T2cの過渡共有USER→T2e全面撤去に回帰不能な中間状態がないか。
9. **容量 / X15** — 16KiB管理予算・KHEAPピーク・2.5KiBのリンク予算、X15を要する判断時点と前倒し時の衝突。概算を測定済みの数字として扱っていないか。
10. **受入の証拠** — 3 backend描画、S/T/Uの意味、8/17/64MB・実機PCM・P7混在の試験が実装の穴を検出するか。最終P1の容量条件をT2だけで達成済みと誤認しないか。

既知の未確認は §6-2。コード・ビルド・ホスト挙動試験・変異・NP21/W・実機は今回未実施。独立レビューをPMが依頼する前提で本票を渡す (本セッションは設計役)。

## 8. ユーザー判断と今回の文書検査

既決D番号の変更を求める点はない。追加のユーザー判断が必要になるのは、**サイズ不足の実測を受けたX15の順序変更**、NP21/Wの64MB preset追加が必要な場合、各受入時の[D2]操作/実機物理操作。lease/API/trim/段の案は本票の独立レビューに出し、争点が3ラリーで決着しなければユーザーへ上げる。今はこれらの追加操作を承認済みとは扱わない。

今回の完了条件は文書検査のみ:

```text
make check-docs-links check-docs-orphans check-docs-status check-tests-inventory check-constraints < /dev/null
```

**2026-10-01 実行結果: rc=0**。文書リンク (0 Errors)、orphan (0件)、状態行、試験一覧の最新性、制約17件の整合がすべて通った。`git diff --check` も rc=0。将来の実装受入は §5 と分けて記録する。
