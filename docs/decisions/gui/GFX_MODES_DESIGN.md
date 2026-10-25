# GFX_MODES — 色数と解像度を形式とドライバの能力の表で持つ — 設計

> 状態: **現行 (2026-10-26)** — 確定 (astra 起草・Opus 2 往復・PM が条件 B1〜B4 を反映)。段の状態は票 TASK_GFX_MODES が持つ。
> 出所: ~/os32-tmp/run/gfx-modes/DESIGN.final.md を 2026-10-26 に写した (往復の版と REVIEW はそこに残る)。本文の r2 の見出しと『r1→r2 の変更』は往復の記録として残す。

# GFX_MODES G2 以降 詳細設計 r2
## r1→r2 の変更
- A1: 起動予約・面の型板を維持し、Cirrus の表は登録後のモード選択時だけ参照する。
- A2: 実施した grep の照合先を段ごとに列挙し、試験修正を含めて直列に統合する。
- A3: G2 のモジュール ABI 自動更新・3か所の機械語差・kernel/cirrus.mod の対配布を受入に加える。
- A4: gfx_current_height を現在高さの正典にし、shutdown で selected_mode も default に戻す。
- A5: G6 を選択経路の G6a と公開形状・容量照合の G6b に分け、既存 host fixture を延ばす。
- A6: 新規試験を tools/tests に置き、新規検査の checks.d/check_map.d 登録も差分に含める。
- A7: Backend 依存 inline を gfx_hal.h の定義後に一本化し、gfx_mode.h を Cirrus contracts に追加する。
- A8: legacy200 の選択条件を planar かつ height==GFX_HEIGHT_200 とコードで固定する。
- A9: gfx.h の定義と追加指定の7使用箇所を確認し、固定値の一覧・保持理由を補う。
- A10: ゲスト ID の登録と C480 の節目延期を計画に入れ、持越し台帳の行案を記す。
- A11: G5 の変異をモード表の pitch に変更し、既存 STATIC_ASSERT だけで止まる変異を避ける。
- A12: 未使用の容量別名だけを作る G1 を削除し、必要なら G6b の使用箇所と同時に追加する。
- 対象: /home/hight/os32-v3、2026-10-26 の指定範囲を読み取り確認。実装は行っていない。
- G0 の形式 bpp・パレット項目数マクロを利用する。以下の識別子は新規 API 案、G0 のマクロ名は実装時に合わせる。
- 単一利用者・単一タスク・協調切替。目的はハード故障・実装バグで固まらず、壊さず、CUI に戻れること。
- 9821 上の標準 16 色経路は残す。9801 互換、高解像度、直接色の描画実装、権限分離の追加は範囲外。
- C11・整数演算・既存 P2V 系変換を使う。SDK は GNU89 互換、KAPI は追記のみ。archive は未参照。

## 1. 実物の確認
| # | 票から想定しやすい形と実物の差 | 根拠と設計への反映 |
|---|---|---|
| D1 | PEGC の 400/480 を選べるように読めるが、現実の描画 init は 480 のみ | gfx/backend_pegc.c:907,943,959。400 は include/pegc.h:186 の定数と復帰タイミングであり、256 色の選択経路ではない |
| D2 | 範囲の 400/480 以外に既存 200 ライン API がある | gfx/gfx_core.c:975,982,991,993。packed は 200 要求でも 480、planar は 200、面間隔は 400 のまま。退行させない |
| D3 | 面・リースを毎回表示サイズで確保するわけではない | gfx/gfx_core.c:254,320,442 が probe 前に候補を予約。exec/lease.c:88,109 は台帳の npages を貸す。選択寸法と予約容量を分ける |
- 食い違いは上記 **3 件**。D2/D3 は票の省略を補う点であり、既存コードの不具合認定ではない。
- G0 前の確認値を以下に残す。並行実装による変更は食い違いに数えない。
- 直書きの集計単位は「指定ファイル内の幅・高さ・bpp・パレット項目数の数値マクロ定義」。コメント・タイミング列・DAC 最大値を除く。
| ファイル:行 | 定義（数値直書き） | 件数 |
|---|---|---:|
| gfx/backend_cirrus.c:72,73,74,101 | WIDTH=640 / HEIGHT=480 / BPP=8 / PAL_COUNT=256 | 4 |
| include/pegc.h:184,185,186,187,211 | WIDTH=640 / HEIGHT_480=480 / HEIGHT_400=400 / BPP=8 / PALETTE_COUNT=256 | 5 |
| include/memmap.h:147,148 | BB8_WIDTH=640 / BB8_HEIGHT=480 | 2 |
| gfx/gfx.h:16,17,23 | WIDTH=640 / HEIGHT=400 / HEIGHT_200=200 | 3 |
| 合計 | G0 が上記パレット 2 定義を形式名へ寄せた後は 12 件 | 14 |
- 別途 bpp の別名 1 件: gfx/backend_pc98.c:43 の PC98_BPP=GVRAM_PLANES。
- gfx/gfx.h:18–24 の BPL=80/WPL=40/PLANE_SZ=32000/PLANE_SZ_200=16000 は収納・転送定数として残す。上の寸法定義14件とは別に4件。以下もリポジトリ全体の件数ではない。
| 固定寸法を使用する部位 | file:line（同種の連続処理は一まとまり） |
|---|---|
| 標準 16 色 query / shutdown / clip / pitch | gfx/backend_pc98.c:52,79,93,100,161 |
| PEGC 診断縞 / clear / 高さ / query / present / 復帰 / pitch | gfx/backend_pegc.c:873,921,943,958,1001,1138,1172,1194 |
| Cirrus サイズ・offset / setup / clear / 高さ / query | gfx/backend_cirrus.c:75,79,80,311,336,360,376 |
| Cirrus clip / present / fill / blit / 復帰 / pitch | gfx/backend_cirrus.c:398,418,460,485,560,585 |
| core 初期値 / 未初期化 FB / CLIENT 型板 / DISPLAY 型板 | gfx/gfx_core.c:33,187,313,320,326,333,350,358 |
| core 予約・clear / 400 init / 200 init | gfx/gfx_core.c:442,445,943,949,958,991,995,1004 |
| VRAM dirty clip / merge / 全画面 present | gfx/gfx_vram.c:88,106,149,311：全モード640幅なので今回は残す。高解像度を実装する段で変更 |
| planar ハードスクロール倍率 | gfx/gfx_scroll.c:35：400/gfx_current_height は200倍表示のHW規則として残す |
| 起動面の自己試験 | kdebug/kselftest.c:912：MEM_GFX_BB8_* の予約形状照合なので残す |
| Cirrus 識別時の窓容量判定 | gfx/cirrus_identify.c:44：モジュール前の予約上限判定として残す |
- 使用箇所の一覧は10部位群・49起点。追加7起点は今回変更しない。gfx.h の宣言コメントは G6b で現在の400/480/200を説明する。
- 関連根拠: docs/05_drivers.md:131–133 も PEGC/Cirrus は 480 のみと記載。
- exec/surface_query.h:11 は format/width/height/pitch/planes を持つが bpp は持たない。lease.c 自身に幅・高さ・bpp の決め打ちは無い。
- GfxBackend の定義だけは指定コードから参照する境界として include/gfx_hal.h:35–80 を追加確認した。

## 2. 形式の層
| 形式 | bpp | 色の意味 | パレット項目数 | 論理 1 行 / 面数 |
|---|---:|---|---:|---|
| GFX_FMT_PLANAR4 (=0) | G0 の 4 | indexed | G0 の 16 | (width+7)/8 bytes **各プレーン**、4 面 |
| GFX_FMT_PACKED8 (=1) | G0 の 8 | indexed | G0 の 256 | width bytes、1 面 |
| 将来 RGB565（今回未定義） | 16 | direct | 0 | width×2 bytes、1 面 |
| 将来 RGB888（今回未定義） | 24 | direct | 0 | width×3 bytes、1 面 |
- bpp/8 の一律計算は禁止。PLANAR4 の 640 幅は 80 bytes×4、320 bytes の単一面ではない。
- カーネル専用 include/gfx_format.h に G0 を残して以下を追加する。テーブル/関数は static inline でビルド登録を増やさない。
```c
typedef struct GfxFormatDesc {
    u8 bpp, indexed, planes, bits_per_plane;
    u16 palette_count;
} GfxFormatDesc;
static inline int gfx_format_desc(u8 fmt, GfxFormatDesc *out);
static inline int gfx_format_row(u8 fmt, u32 width, u32 *row_bytes);
/* 既知形式を switch で解く。未知形式・0幅・計算超過は 0、成功は 1。 */
```
- G0 の値を初期化子に用いる。indexed の意味と収納方式を分け、palette_count=1<<bpp を汎用規則にしない。
- row は最小行幅。実 pitch は mode.pitch を採り、row 以下でないこと・必要な装置アラインを検査する。
- include/gfx_mode.h は形式・モードの内部橋渡し。SDK 形式番号を参照し、数値の別定義を作らない。
- SDK 側 os32_kapi_shared.h:315–330、GFX_CAP_*、GFX_ScreenInfo、gfx_screen_info の入口は今回変更しない。ABI 世代も据え置く。
- 将来 16/24bpp を追加する際は SDK の形式番号を末尾追加し、形式記述に収納ビット数・成分配置の記述を追加する。
- SDK/KAPI を変える段では生成元 sdk/kapi.json に従い世代を上げ、生成物の手編集をせず make clean → make all。
- 形式の追加だけで既存ドライバが直接色を描けるとはしない。対応ドライバのモード・HW 設定・描画/転送経路を別途実装してから申告する。
- 高解像度も同様に対応タイミングと容量が必要。表への未対応行の追加だけで選択可能にはしない。

## 3. ドライバの能力の表と HAL 境界
```c
/* include/gfx_mode.h: 純データ。タイミングのバイト列は装置側に残す。 */
typedef struct GfxMode {
    u16 width, height;
    u8 format, dac_bits, hw_mode, reserved;
    u32 pitch;                 /* 1プレーンまたは packed 1行の実バイト数 */
} GfxMode;
/* include/gfx_hal.h: GfxBackend の prepare の後に追記（内部 ABI）。 */
/* const GfxMode *modes;
   u16 mode_count, default_mode;
   const GfxMode *selected_mode; */
/* gfx_hal.h の構造体定義後だけに置く inline（mode.h には置かない）。 */
static inline const GfxMode *gfx_backend_mode(const GfxBackend *be);
static inline const GfxMode *gfx_mode_pick(const GfxBackend *be, int legacy200);
static inline int gfx_mode_layout(const GfxMode *m, u32 *plane_bytes,
                                  u32 *total_bytes);
/* gfx_core.c のみ: probe/prepare の順序は既存選択関数に残す。 */
static int gfx_select_mode(GfxBackend *be, int legacy200);
/* gfx_mode_pick の legacy200 分岐。表の範囲を検査済みとして示す。 */
if (legacy200) {
    for (u16 i = 0; i < be->mode_count; ++i)
        if (be->modes[i].format == GFX_FMT_PLANAR4 &&
            be->modes[i].height == GFX_HEIGHT_200) return &be->modes[i];
}
return &be->modes[be->default_mode];
```
- 循環 include を避けるため、GfxMode と layout は gfx_mode.h、Backend を使う inline は gfx_hal.h の構造体定義後だけに置く。tools/prototypes/cirrus_imports.def の contracts に include/gfx_mode.h を追加する。
- GfxBackend は既に非 const の実体。g_backend の const ポインタは維持し、選択関数へ実体を渡して selected_mode だけ更新する。
- selected_mode は静的表内のポインタ。動的確保なし。init(void)/query/prepare の関数型は変更しない。
- query の幅・形式・bpp は表と形式記述から、高さだけは gfx_current_height から埋める。動的 flags・リース規則は維持する。
```c
/* 各 backend_*.c が所有。PC98_WIDTH 等は既存定数の例示。 */
static const GfxMode pc98_modes[] = {
    {GFX_WIDTH, GFX_HEIGHT,     GFX_FMT_PLANAR4, 4, 0, 0, GFX_BPL},
    {GFX_WIDTH, GFX_HEIGHT_200, GFX_FMT_PLANAR4, 4, 1, 0, GFX_BPL}
};
static const GfxMode pegc_modes[] = {
    {PEGC_WIDTH, PEGC_HEIGHT_480, GFX_FMT_PACKED8, 8, 0, 0, PEGC_PITCH}
};
/* Cirrus も既存 WIDTH/HEIGHT/PITCH を宣言時だけ使う 480 行を 1 件。DAC=6。 */
/* 既存初期化子末尾: .modes=pegc_modes, .mode_count=ARRAY_COUNT(pegc_modes),
                     .default_mode=0, .selected_mode=0 */
```
- ARRAY_COUNT は例示。既存に無ければ sizeof 配列/要素を用い、別の共通ファイルを変更しない。
- hw_mode はドライバ内のタイミング ID。画素寸法から GDC SYNC 列を合成しない。今のレジスタ書込み順・待ち上限は維持。
- DAC 幅は物理出力の値。アプリの RGB 0..15、既存の色キューブ・階調・変換丸めを変更しない。
- 宣言以外の clip・転送 pitch・clear 範囲と query の幅は gfx_backend_mode() から得る。init 時に mode->height を現在高さへ反映し、query の高さは必ず gfx_current_height を読む。
- gfx_current_height が現在高さの唯一の正典。全ドライバの init は選択行の height をここへ書く。shutdown は400へ戻し selected_mode も default に戻す。
- selected_mode は設定用の行であり shutdown 後の現在高さではない。gfx_vram/gfx_scroll と CirrusModuleContext の高さポインタも維持する。
| 条件 | 選択規則（現状維持） |
|---|---|
| GFX=pc98 | probe を呼ばず標準 16 色、gfx_init は 400、gfx_init_200 は 200 |
| GFX=pegc / cirrus | 指定だけ probe。失敗・未リンクは直接 pc98、もう一方へ試し直さない |
| auto / 不正 pref | Cirrus → PEGC → pc98 の最初の成功 |
| PEGC 成功 | 640×480 PACKED8、gfx_init_200 でも同じ。起動時 24/31kHz による 400 選択は無い |
| Cirrus 成功 | 640×480 PACKED8、gfx_init_200 でも同じ |
| init 後 probe 失敗 | 既存の cleanup を経て pc98。要求が legacy200 なら 200、それ以外 400 |
| prepare | 選択だけ先行、PEGC は同期を変えない。Cirrus の既存 init→shutdown の時機も維持 |
- モード選択は実体の probe 成功後・init/prepare の前。boot reserve は候補表を参照せず、今の容量定数と型板を使う。
- kernel/kernel.c:590 の予約、:640 の prepare は :642 の cirrus_module_load より先。未登録 Cirrus（kernel/cirrus_module.c:16）は modes=NULL のまま skip し、probe/enable を追加しない。
- 登録後の graphics 境界だけで Cirrus の表を読む。G6b はその選択点で既存予約容量を照合し、未登録表への参照を一切行わない。
- gfx_mode_pick は通常 default、legacy200 は planar の既存 200 行だけ選ぶ。面積最大・色数最大の探索はしない。
- 未知形式/空表/容量不足はハード設定前に不成立とし、pc98 へ戻す。pc98 自体の資源不足は描画未開始の既存経路へ。
- **(確定の B1 で差し替え)** query の高さは `gfx_started ? gfx_current_height : gfx_backend_mode(be)->height`。pc98 は 200→shutdown→query=400、packed (PEGC・Cirrus) は GUI を抜けた後も init の前も 480 (今と同じ)。他の format/flags は従来の backend 問合せのまま。
- GFX_CAP は PC98 の PAGE_FLIP の実状態、PEGC の TEXT_OVERLAY、Cirrus の HW_FILL/HW_BLT を維持する。
- パレットリースは PC98 mask=0xFF7E、packed は first=16/count=240、reserved はゼロ。G5 のアプリ契約を変えない。

## 4. 選択モードと容量を分けて大きさを決める
- フレームバッファの表示幅は mode、高さは gfx_current_height、row/pitch は形式＋mode。物理先頭・窓・キャッシュ属性は現行ドライバ/台帳を使う。
- packed 使用量 = pitch×height、planar 論理使用量 = pitch×height×4。全乗算は u32、容量との比較前に超過検査。
- PC98 CLIENT の収納間隔は既存 32,000 bytes、4 面合計 128,000 bytes、予約は 32 頁=128 KiB。
- 200 モードでも plane_offset={0,32000,64000,96000} と予約 32 頁を維持。200 の使用量 64,000 bytes と混同しない。
- PC98 DISPLAY は非連続 4 記録・各 32 KiB のまま。表示端以降の VRAM padding は clear しない。
- PEGC CLIENT は候補識別時に 75 頁=300 KiB を予約、DISPLAY は既存リニア窓内の 300 KiB。
- Cirrus は表示 offset=0、CLIENT=0x4B000、pattern=0x96000 を維持。各面 300 KiB、主記憶 BB を新設しない。
- **起動時の予約と面の型板は変更しない**。gfx_core.c:313–359,442–445 は memmap 等の現行容量定数を使い続ける。
- 登録済みの mode を選ぶ時だけ、計算した pitch×height×planes と既存 CLIENT/DISPLAY の容量を照合する。PC98 は固定 plane_offset の最終端でも照合する。
- init のたびに追加確保・移動しない。型板を表から生成する変更、boot の候補表最大値計算は範囲外。
- gfx_kernel_framebuffer は選択後に mode の幅と現在高さを使い、未開始時は boot PC98 の既存寸法を維持する。
- SURFACE の PC98 200 時の収納高さ 400 は維持し、公開 framebuffer の高さ 200 と区別する（gfx_core.c:192–199）。
- lease は既存の台帳 npages と plane_offset をそのまま使用。exec/lease.c と exec/surface_query.h は変更不要。
- 再 init の既存 revoke→bind→完了処理と面世代を維持する。モードポインタを更新しただけで古いリースを再利用しない。
- MEM_GFX_BB8_HEIGHT=480 は「現在の高さ」でなく「現行予約の上限」の互換名として残す。
- 未使用の CAPACITY 別名は作らない。G6b は既存 MEM_GFX_BB8_SIZE と台帳容量を使う。別名が実装上必要な場合だけ同段で定義と使用を同時に追加し、数値は保持する。
- memmap は番地・予約上限の正典、mode は表示能力の正典。静的上限を実行時選択値に置き換えない。
- gen_memmap.py:113–119 は memmap.h の include を外して cpp で解くため、容量式を gfx_format.h 等への依存にしない。
- gen_memmap.py は変更不要。--check で地図・重なりを確認し、生成物を実装の別正典にしない。
- **現在の番地配置は変更しない。配置世代・KAPI 世代は据え置き**。成果物ハッシュ/build ID は通常どおり再生成する。
- GfxBackend の変更は Cirrus モジュール ABI を変える。contracts の文字列 FNV により CIRRUS_MODULE_ABI_GEN は自動更新（tools/gen_sqlite_imports.py:41–44）。kernel と cirrus.mod を同じビルドの対で配る。
- 将来、上限超過や Cirrus 面 offset を変更する段だけ配置世代・地図・生成 manifest を同時更新する（今回は行わない）。

## 5. 大きさの見積もり
| 増分 | 見積もり / 制限 |
|---|---|
| モード表 | sizeof(GfxMode)=12 想定×4 件 = 48 bytes |
| Backend 追記 | 32bit で 12 bytes×3 = 36 bytes |
| 形式記述・inline のコード | 約 0.5–2 KiB、定数伝播と共有の程度で変動 |
| HAL 選択・形状計算・容量照合 | 約 1–3 KiB、既存の定数代入を置き換える |
| カーネル像増加の予算 | 合計 **2–6 KiB**。8 KiB 超なら重複 inline/検査を調べる |
| RAM/VRAM の追加予約 | **0 KiB**。表・状態以外の BB、lease、ページ表は増やさない |
| 起動 FD | カーネル payload 約 2–6 KiB 増、セクタ丸め/圧縮後はビルドで実測 |
- 現在の kernel.map・FD 残量は指定範囲外で未測定。収まるとの断定はしない。
- 各段で同条件ビルドの size/セクション/生成 FD 使用量を保存し、既存像上限・FD 容量検査の成功を受入条件にする。
- バッファを配列としてカーネル像へ埋め込まない。動的メモリの増設やマスター像への書込みも不要。

## 6. ファイル別変更一覧
- 段番号はレビューとの対応のため G2/G3/G4/G5/G6a/G6b を保持。旧G1は削除、全6段。以下の行数は追加＋削除、試験・登録も含む。
| 段 | 本体ファイル（変更、明記したものだけ新規） | 本体差分目安 |
|---|---|---:|
| G2 | include/gfx_format.h、include/gfx_hal.h、新規 include/gfx_mode.h、tools/prototypes/cirrus_imports.def | 45–65 |
| G3 | gfx/backend_pc98.c | 35–50 |
| G4 | gfx/backend_pegc.c | 55–75 |
| G5 | gfx/backend_cirrus.c | 65–85 |
| G6a | gfx/gfx_core.c（選択・init/init_200/prepare への結線だけ） | 60–80 |
| G6b | gfx/gfx_core.c（FB/query・容量照合）、gfx/gfx.h、gfx/gfx_internal.h、docs/05_drivers.md | 65–85 |
- G6b の include/memmap.h は別名が必要な場合のみ0–5行。型板・予約関数はどの段も変更しない。
- 試験・登録・照合先の全リストは§7に記す。新規試験は tools/tests/*_host.c と test_*.py。新規検査は build/checks.d/<名>.mk と tools/check_map.d/<名>.yaml の対も追加する。
- 既存検査の拡張は既存入口を使い、新しい gfx_modes_hal_host.c は作らない。SDK・KAPI・exec・gen_memmap.py・アプリ・GUI・Makefile は変更しない。

## 7. 段の分け方と受入
- 小さい段から **G2→G3→G4→G5→G6a→G6b**。実装＋候補検査は各1回・2時間以内。全段を直列統合し、共有試験の同時編集を避ける。
- PMのA2/A5決定に伴い、test_gfx_bounds.py 等と gfx_core.c は段間で共有する。非重複の旧条件はこの分割には適用せず、直前の合格版を次段の基準にする。
- 原則sol、G6a/G6bの初期化順・容量照合は難所としてastra。本体.cは各1本、追加/変更host.cも各段3本以下を目安とする。
- 共通候補検査: make all、当該検査をMUTATE=0、check_select.py --lint。PM統合でcheck-changed、節目で全体検査。ゲスト待ちは実装者の2時間に含めない。
- 以下の照合集合は `rg -l -F '<本体ファイル>' tools/tests/`（grep -rlnと同義）の実結果。パスはすべて tools/tests/ 相対、空集合も明記する。
| 集合 | 検索した本体 → 照合先（変更するか必ず確認し、既存検査は候補として実行） |
|---|---|
| F | include/gfx_format.h、include/gfx_mode.h、tools/prototypes/cirrus_imports.def → 該当なし |
| H | include/gfx_hal.h、gfx/gfx_internal.h → boot_splash_native_tdd.md |
| P | gfx/backend_pc98.c → boot_splash_native_host.c、test_unicode_surface.py、test_gfx_attach.py、test_gfx_kernel_fb.py |
| E | gfx/backend_pegc.c → pegc_hostshim/io.h、pegc_mode_tdd.md、test_unicode_surface.py、test_gfx_attach.py、test_shm_user.py、test_gfx_kernel_fb.py、test_gfx_bounds.py、k6_ram_tdd.md、pegc_mode_host.c、test_pegc_mode.py、test_gfx_boot.py |
| C | gfx/backend_cirrus.c → test_cirrus_module.py、test_unicode_surface.py、cirrus_win_tdd.md、cirrus_win_host.c、test_gfx_attach.py、test_gfx_kernel_fb.py、test_cirrus_win.py、k6_ram_tdd.md、test_gfx_bounds.py、test_gfx_boot.py |
| K | gfx/gfx_core.c → test_shutdown_probe.py、k5b_gshell_tdd.md、boot_splash_native_host.c、gfx_boot_host.c、test_unicode_surface.py、test_shm_user.py、test_gfx_attach.py、test_kapi_bounds.py、test_gfx_kernel_fb.py、test_memmap_audit.py、test_display_cleanup.py、t8_tdd.md、boot_splash_native_tdd.md、test_gfx_reattach.py、test_gfx_reinit.py、test_gfx_boot.py、test_kcg_module.py |
| G | gfx/gfx.h → t8_tdd.md、r1_recovery.py、kcg_display_host.c |
| M | include/memmap.h → multiapp_impl_host.c、guest_acceptance.yaml、test_appmem_map.py、test_appmem.py、cirrus_win_tdd.md、test_fep_boundary.py、test_memmap_gen.py、test_owner_reclaim.py、test_memory_boot.py、test_c_dialect.py、test_np21w_gui.py、test_hdd_stage2.py、test_stack_entry.py、multiapp_model_host.c、test_vmkernel_lz4.py、test_t3c2.py、memmap_tdd.md、k6_ram_tdd.md、test_vmkernel_layout.py、test_vk32_crc.py、test_layout_defs.py、test_boot_named_constants.py |
| B | kernel/cirrus_module.c → cirrus_module_host.c；kernel/cirrus_body.c → test_cirrus_module.py、cirrus_module_host.c（本体ソースは変更せず再ビルド） |
- 集合は「全件編集」の意味ではない。変異のold文字列やstubが壊れる行だけ修正し、count(old)==1を維持する。markdownは記載照合、host.cは対応runner、手動項目はゲスト受入へ回す。
- 下表の test_*.py/*_host.c は tools/tests/ 相対、guest_acceptance.yaml も同所、DEFERRED_TESTS は docs/tasks/。行数/時間は本体・試験差分と照合集合の候補検査を含む見積もり。新しい不具合が出て枠を超す場合は未合格候補としてPMへ返し、無断で巨大段にしない。
| 段 | 触るファイル（§6の本体＋試験・登録）と照合先 | 行数 / 時間・依存 |
|---|---|---|
| G2 | include/gfx_format.h、include/gfx_hal.h、新規 include/gfx_mode.h、tools/prototypes/cirrus_imports.def；新規 tools/tests/gfx_modes_format_host.c、test_gfx_modes_format.py、build/checks.d/gfx-modes-format.mk、tools/check_map.d/gfx-modes-format.yaml；既存 test_cirrus_module.py/cirrus_module_host.c は必要なstubだけ更新。照合 F/H/B | 90–120行 / 45–65分。G0後、並列なし |
| G3 | gfx/backend_pc98.c；test_gfx_kernel_fb.py、gfx_kernel_fb_host.c；照合 P/H（高さ・shutdownの期待値と必要なstubだけ） | 100–130行 / 55–75分。G2後、並列なし |
| G4 | gfx/backend_pegc.c；test_gfx_bounds.py、test_pegc_mode.py、pegc_mode_host.c、test_gfx_kernel_fb.py；照合 E/P。guest_acceptance.yaml に§7末尾の4IDを登録、DEFERRED_TESTS文案も記録 | 125–155行 / 65–85分。G3後、G5と直列 |
| G5 | gfx/backend_cirrus.c；test_gfx_bounds.py、test_cirrus_win.py、cirrus_win_host.c、test_gfx_kernel_fb.py；照合 C/E/B | 145–175行 / 75–95分。G4後、並列なし |
| G6a | gfx/gfx_core.c；test_gfx_kernel_fb.py、gfx_kernel_fb_host.c、test_gfx_attach.py、gfx_attach_host.c、test_gfx_reattach.py、gfx_reattach_host.c；照合 K/P/E/C | 160–190行 / 85–110分。G5後、並列なし |
| G6b | gfx/gfx_core.c、gfx/gfx.h、gfx/gfx_internal.h、docs/05_drivers.md（必要時include/memmap.h）；test_gfx_kernel_fb.py、gfx_kernel_fb_host.c、test_gfx_boot.py、gfx_boot_host.c；照合 K/G/H、memmap別名を足す場合だけ M。docs/05_drivers.md の更新も含む | 175–205行 / 95–120分。G6a後、並列なし |
- G2以降のcontracts変更ごとに生成ヘッダ・manifest・cirrus.modを生成し直す（手編集しない）。表の追加前は末尾ゼロ、G3～G5は未設定selected_modeをdefaultとして動くので単独ビルド可能。
| 段 | 機械語・host受入 | 段のゲスト / 変異1行 |
|---|---|---|
| G2 | 関数本体の差は cirrus_module_load のABI即値、cirrus_detach の構造体ゼロ化、cirrus_module_init の構造体コピーの3か所だけ許容。objdump -drと生成ABIを照合し、データサイズ増を別記。形式・行幅・超過とモジュール対を試験 | ゲスト省略可 / PLANAR4行式をwidth/2にしhost失敗 |
| G3 | 変わる。200/400 query、init→shutdown→query=400、flip、mask、reserved、端を実fixtureで照合 | N16 / shutdownの高さ復元を200にしhost失敗 |
| G4 | 変わる。I/O列は旧版一致、clear/転送端、DAC出力、prepareが同期を書かない。boundsのPEGC変異文字列を更新しcount==1 | P480/P400 / モード表の高さを400にし480期待値で失敗 |
| G5 | 変わる。setup引数、BLT端、CPU代替、失敗時リレー復帰。boundsのCirrus変異文字列を更新しcount==1 | N16/P480/P400でfallback回帰、C480は節目 / 表のpitchを641にし引数・容量のhost照合で失敗 |
| G6a | 変わる。auto/強制/不正pref、probe/init/prepare失敗、未登録Cirrus、legacy200、再init。実coreを呼び順序・回数・attach/reattachを確認 | N16/P480/P400 / 強制Cirrus失敗をPEGCへ落とし選択試験失敗 |
| G6b | 変わる。FB/query/現在高さ、容量超過でHW設定前にfallback、200固定面間隔、lease世代、起動型板の不変を確認。gen_memmap.py --check、像/FD増分も実測 | N16/P480/P400 / 容量超過の判定を無効化し不成立モードのhost試験失敗 |
- G2の差分位置の根拠: kernel/cirrus_module.c:53,79 と kernel/cirrus_body.c:6。load内はABI即値とcandidateのコピー長を同じ関数の許容差として記録する。
- 予約行列はPC98常時32頁、PEGC候補時75頁、Cirrus CLIENT/DISPLAY各75頁。旧型板・起動順・MEM_GFX_BB8_SIZEのclearは不変なのでtest_gfx_boot.py:270の変異文字列も原則維持する。
- G6aは選択と現在高さ/selected_mode更新だけ。G6bでFB/queryの一貫性・容量照合を仕上げる。G6aの試験文字列をG6bで再変更する場合も同じ試験を再実行する。
- 初期化失敗で既存cleanupが表示を戻すこと、reinit_pendingが残らず古い参照が復活しないことを確認。権限分離の仕様は追加しない。
- G4で tools/tests/guest_acceptance.yaml に manual 項目 gfx-modes-n16 / gfx-modes-p480 / gfx-modes-p400 / gfx-modes-c480 を登録する。下の短名はその別名で、既存IDを仮定しない。
- 登録項目は gate/contract/prereq/entry/observe/expect/evidence を既存形式に合わせ、contractはTASK_GFX_MODES、entryは下の手動手順。生成/整合検査を回し、未実行を合格にしない。
| 短名（登録ID末尾） | PMがNP21/Wで見るもの |
|---|---|
| N16 (n16) | 9821＋GFX=pc98。640×400/4/PLANAR4、全16色、四隅、flip。既存200アプリ→shutdown→query=400、再入場 |
| P480 (p480) | 64MB、GFX=pegc。640×480/8/PACKED8、下端479、256色、TVRAM合成、CUI復帰・再入場。query の高さは復帰後も 480 (確定の B1) |
| P400 (p400) | 400ラインCUIからPEGCに入ると480、終了で元の同期へ戻る。256色400描画を合格条件にしない。通常構成で実行、別周波数構成の追加は節目へ |
| C480 (c480) | **段ごとには行わずv3最終段の構成別関門**。GD5430有効、対のkernel/cirrus.modで480/256色、HW fill/blit、CPU代替、終了/CTRL+STOP後リレー・再入場 |
- G4で docs/tasks/DEFERRED_TESTS.md §2へ加える行案: `| GFX-MODES-C480 | gfx-modes-c480（対のkernel/cirrus.mod、480下端、BLT/CPU代替、リレー復帰・再入場）、別周波数のP480/P400 | 構成変更を段ごとに行わずまとめるPM決定。未実行は未合格 | v3最終段・H-4の構成別関門でPMが採証 |`。
- 各通常ゲスト条件で既存描画アプリ・GUIの色リース復元・画面取得・再起動なしの往復を確認。既存バイナリの入口は変更しない。
- 既存試験/道具の期待値はPM決定のshutdown後高さ以外は保つ。memmap地図、画面情報表示、スクリーンショット寸法を比較する。

## 8. 未決・反論
- 未決0件。U1はPM決定で閉じる：PEGC 256色400は表に載せず、400 CUIから480への出入り・復帰を試験する。
- 反論0件。A1～A12と末尾のPM決定を採用する。

## 確定 (2026-10-26、Opus 5.5 第 2 回の条件付き承認、PM が反映)

- B1 (P2、§3 の高さの式を差し替え): query の高さは **`gfx_started ? gfx_current_height : gfx_backend_mode(be)->height`**。shutdown で selected_mode を default に戻すので、pc98 は 400、packed (PEGC・Cirrus) は 480 を返し、今の振る舞い (GUI を抜けた後・gfx_init の前も 480) と同じになる。§3 の「PM 決定により shutdown の後の query の高さは 400 にする (packed の 480 からも変える)」は取り消す — PM 決定 (b) は A4 (pc98 の 200 → 400) だけ。
- B2 (P2、§7 の G2): 「関数本体の差は 3 か所だけ」は、つないだ後の kernel.elf ではなく**段ごとの `.o` (再配置が記号で出る) の `objdump -dr`** で比べる。kernel.elf では gfx_backend_pc98・gfx_backend_pegc が 12B ずつ大きくなり .data の番地がずれて、多くの関数の即値が変わる。
- B3 (P3、§7 の G4): guest_acceptance.yaml への 4 つの ID (N16・P480・P400・C480) の登録と DEFERRED_TESTS の C480 の行は、**G2 の前に PM の文書のコミットで入れる** (G4 から外す)。G6b の docs/05_drivers.md と gfx/gfx.h の注記も、G6b が 2 時間に収まらないときは PM の文書のコミットへ回してよい。
- B4 (P3、§7 の G6b): 変異「容量超過の判定を無効にする」を落とすため、**host の fixture が `be->modes` を容量を超える試験用の表に差し替える** (GfxBackend は非 const)。
- A9 の表の行番号: gfx/gfx.h の実物は 15・16・22 行 (設計の 16・17・23 から 1 つずれる、害なし)。

## ユーザー指示の追記 (2026-10-26)

「高解像度時の窓は作るが高解像度版の実装はないで」。この設計の範囲は窓まで: 形式の層に形式を足せる (§2)、ドライバの表にモードを足せる (§3)、HAL が表から選んで画面・FB・query の大きさを決める (§3・G6a/G6b)、予約に収まらないモードは選ぶときに断って今のモードへ戻る (G6b の容量の照合)。起動時の予約とバックバッファの大きさ (480 ライン固定、memmap) は変えない — 広げるのは高解像度版の実装の側で、番地の配置と配置の世代が変わるので別の票にする。

## G2 の実装で PM が決めたこと (2026-10-26)

- `build/cirrus-module.mk` の Cirrus の ABI の値の生成の依存一覧に `include/gfx_mode.h` を足す (G2 の触ってよいファイルに加える)。生成器は contracts の本文から値を出すので、この依存が無いと gfx_mode.h だけを変えたときに値が作り直されない (G2 の実装者の「設計の欠け」の報告)。
- G2 で `gfx/backend_pc98.c`・`backend_pegc.c`・`backend_cirrus.c` の既存の初期化子に、新しい末尾の欄を明示の 0 (NULL) で書き足す (G2 の触ってよいファイルに加える、この 3 ファイルでは初期化子の追記だけ)。host の `-Wextra -Werror` が省いた欄を落とすため。製品の像はゼロ初期化で同じ。表の中身は G3〜G5 が入れる (段は直列なので衝突しない)。
- **G3 の実装で PM が決めたこと (2026-10-26)**: 確定の B1 の高さの式 (`gfx_started ? gfx_current_height : gfx_backend_mode(be)->height`) は gfx_core.c の static な `gfx_started` を使うので、**入れるのは G6b (gfx_core.c の問い合わせ)**。G3〜G5 のバックエンドは表を申告し、init で選んだ行の height を `gfx_current_height` に書くところまでで、各バックエンドの問い合わせの高さは今の形 (`gfx_current_height` などを読む既存の式) のまま変えない。G3 の host 試験の「init→shutdown→query=400」は、今の振る舞いのまま成り立つことを確かめる形にする。
