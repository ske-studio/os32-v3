# #18　表示提供者が選ぶ surface lease の束

出所: PM 確定版 `DESIGN.final.md`（2026-11-19）。

> 状態: **現行 (2026-11-19)** — 確定設計。

## r1→r2 の変更

- R1 既存の提供者インターフェースを使い、列挙と件数照合を共通化。
- R2 変異を共通側と各機種の判定関数へ移し、PC-98 の真偽表を fixture で検証。
- R3 段5・7 を一括で取り込み、変異の目印を同時に更新。
- R4 QEMU は取得前の INVAL と再取得、巻き戻しは host で検証。
- R5 段9 で PPC の後半失敗の受入を host の複数件試験へ移す。

## 1. 実物と統合判断

|経路|現在の実装|
|---|---|
|入口|`exec/surface_query.c` の `surface_api_bundle` の surface_api_bundle は map 方針を先に調べ、role から query と同じ source を作って束へ渡す。|
|PC-98 の順序|`platform/pc98/gfx_surfaces.c` の `display_provider_display_records` は PC98 に GVRAM_PLANES、`display_provider_display_first` は各面の先頭 PFN を返す。`gfx/gfx_core_surface.inc` の `gfx_selected_source` はその順に生きた ledger レコードを探し、sid と世代を組にする。|
|4 面の固定箇所|`exec/lease.c` の `surface_lease_bundle` は DISPLAY・PC98・source と引数がともに SURFACE_QUERY_MAX の場合だけ通す。同関数の入力コピーも常に配列全体。`docs/KAPI_SPEC.md` §3-3 の契約は query 順の全 4 面一括。|
|PowerMac|`platform/powermac/display_provider.c`  の `display_provider_boot_reserve` は OF_FB の RAM shadow を CLIENT・WB・RW で 1 件登録。同じ関数は実 FB を DISPLAY・MMIO・UC・NONE で登録。`gfx/gfx_core_surface.inc` の `gfx_selected_source` で NONE は既に query 元から除外される。|
|取得と取消|`exec/lease.c` の `surface_lease_bundle` で bridge を開始し、IRQ 保存下で選択中 backend・権限・全 ref を照合。同ファイルの `lease_acquire` は全件を準備して paging_lease_map を一度呼ぶ。`surface_lease_bundle` は結果の一括 copyout に失敗すると発行分を解放する。|

1. 同じ役目は本線の surface_api_bundle にある。`PPC_KAPI_FILL_DESIGN.md` の公開表と実装順 の 5c も同じ入口を指定する。
2. 機種差は貸せる role・backend・面の組。query の列挙、認可、取得、取消は両機種で同じ実装を使う。
3. 5c の所有者・世代の公開順と整合する。AC-DISPLAY-1 は隣接する表示能力の契約。束の原子性と件数差は両機種の互換項目として先に追記し、§5 の同一手順で検証する。

## 2. 内部の口

`include/display_provider.h` の u32 display_provider_display_records(u32 ledger_backend)、u32 display_provider_display_first(u32 plane) と並べて宣言する。

```c
int display_provider_bundle_ok(u32 role, u32 backend);
```

- 結果は既存の `exec/surface_query.h` の source（role、backend、ready、count、query 順の refs）。呼出側のスタックに置く。提供者は sid や世代を保存せず、選択中の表示状態と ledger を正本とする。
- surface_api_bundle は従来の方針判定（禁止なら NOSYS）の直後、通常文脈で gfx_surface_source(role,out) を一度呼ぶ。既存の起動済み・ready・貸出権限の検査と query 順を使い、列挙の誤りをそのまま返す。列挙から取得・公開の間に協調切替や装置コールバックを挟まない。
- 同じ共通関数で display_provider_bundle_ok(role,out->backend) が偽なら INVAL。件数の照合はしない (out->count は gfx_selected_source が display_provider_display_records(backend) から決めるので必ず一致する。共通のソースは GVRAM_PLANES など機種のヘッダの定数を読まない — 確定 N1)。通過した source を本体へ渡す。
- 共通側は期待値をカーネルの source から受け取り、ユーザーの refs は照合対象として別にコピーする。単独 lease の PC98 DISPLAY 制限は現状どおり。

```c
/* surface_lease_bundle の入口。既存の文脈・NULL 検査の後 */
if (!count || count > SURFACE_QUERY_MAX || count != source->count)
    return OS32_ERR_INVAL;
/* caller 検査の後、コピーする入力長は count * sizeof(refs[0])。
   出力はゼロ初期化済みの結果構造体全体を preflight / copyout。 */
if (source->role == LEDGER_ROLE_DISPLAY)
    exec_surface_bridge_begin(access);
```

|条件|共通側の結果・状態|
|---|---|
|空、上限超過、件数不一致|入力を読む前に INVAL。欠落・重複・順序違い・別の生きた束も INVAL。|
|世代不一致|surface_query_refs（`exec/surface_query.c`）が全件の形を検査し、後方の不正 sid・重複・生きた別面を古い世代より先に INVAL とする。世代だけが古ければ STALE。取得時にも再検査。|
|準備・map 失敗|既存の一括取得と誤り変換を使用。枠・token 不足は FULL、ページ表メモリ不足は NOSPC。先行面を公開せず、次 token を進めない。|
|copyout 失敗|INVAL。全 token の release を試し、全成功なら slot の全バイトと次 token を復元。release 失敗は件数を記録し、残存 lease を終了時 revoke に残す。|

## 3. 部品の大きさ

- x86 と 5c 後の PPC の各像に role・backend の比較関数を 1 個ずつ置く。列挙と提供件数の照合は共通の surface_api_bundle に 1 本。
- 永続 RAM・ヒープ増分 0。既存 source と固定長作業配列を再利用する。ユーザー像、KAPI 表の配置・版、公開構造体のサイズは現行どおり。

## 4. ファイル単位の段

|段|ファイル|変更|
|---|---|---|
|1|docs/decisions/ARCH_COMPAT_CONTRACT.md|束の原子性、機種別件数、共通試験を記載。|
|2|include/display_provider.h|既存の提供者インターフェースに display_provider_bundle_ok の宣言を追加。|
|3|新 platform/pc98/display_bundle.c (と build/platform/pc98.mk の 1 行)|`display_provider_bundle_ok` の PC-98 の定義 (DISPLAY かつ PC98 のときだけ真) を単独で組める小さなファイルに置く (確定 N3 (a))。|
|3b|新 platform/powermac/display_bundle.c (と build/platform/powermac.mk の 1 行)|PowerMac の定義 (CLIENT かつ OF_FB のときだけ真)。FEATURE_EXEC の PPC 像は surface_query.c を既に組み `--no-undefined` で link するので、本線の段 2〜4 と同じ取り込みに入れる (確定 N2)。|
|4|exec/surface_query.c と、surface_query.c を組む host 試験のつなぎ (surface_query_host.c・public_surface_host.c・gfx_kernel_fb_host.c・memmap_audit_host.c など、grep で全部)|bundle のみ新しい口へ接続。つなぎには display_provider_bundle_ok の定義を足す (確定 N2b)。ここまでは x86 の 4 面制限が本体にも残る。段 2・3・3b・4 は同じ取り込みの単位。|
|5・7（一括）|exec/lease.c、tools/tests/test_surface_bundle.py|固定条件と入力長を一般化し、bridge を DISPLAY に限定。同時に旧条件を指す変異の目印と試験の依存を更新。|
|6|tools/tests/surface_bundle_host.c|公開入口で後端の拒否を、本体で可変件数と復元を検証。|
|8（PPC PM・5c）|PowerPC の公開表|段 3b の定義は本線が入れる。5c では gfx_surface_lease の束を公開する配線だけ。|
|9（本線）|docs/KAPI_SPEC.md、docs/decisions/ppc/PPC_KAPI_FILL_DESIGN.md の受入表|前者に PPC の role・件数を併記。後者の「束の後半失敗で先頭も未取得」を host の複数件 source の受入に移す。文書変更を PowerPC PM に知らせる。|

## 5. 受入と変異

- host では test_surface_lease.py と test_surface_bundle.py の通常・変異を実行。4 面と 1 件で query → bundle → 全 token の unlease、世代更新後の旧 ref は STALE。1 件入力は次ページが読めない境界に置き、未使用 views はゼロを確認。
- 4 面の後半で map を失敗させ、PTE・slot・lease_count・次 token・出力の前後一致を検査。1 件では取得後の copyout 失敗を注入し同じ復元を検査。複数件途中の失敗は host の複数件 source で再現する。
- 両機種の display_bundle.c を test_surface_bundle.py の TARGET_SRC と `extra_sources` に足し、role と backend の組合せごとの真偽表と照合する (gfx_surfaces.c・display_provider.c は加えない)。変異は各 display_bundle.c の 1 行 (制限の除去) と、surface_query.c の `display_provider_bundle_ok(` の呼び出しの除去 (確定 N3)。
- 変異の目印の置き換え (確定 N4): 旧 `count != SURFACE_QUERY_MAX)`・`source->backend != LEDGER_SF_PC98 ||`・`source->role != LEDGER_ROLE_DISPLAY ||` → 新 surface_query.c の `display_provider_bundle_ok(` の呼び出し、lease.c の `count != source->count` とコピー長 `count * sizeof`。
- 変異はsurface_query.c の提供者判定・提供件数照合の除去、各機種の 1 行からの role/backend 制限除去、件数一致検査除去、入力長の 4 件固定、世代検査除去、取消 1 件省略。各変異に対応する試験が落ちること。CLIENT 取得は bridge 呼出数 0。
- NP21/W は PC98 DISPLAY の query 順で 4 token を取得・解放し、順序交換と 3 件を INVAL とする。QEMU PowerMac G4 は OF_FB CLIENT の 1 token を取得・解放、DISPLAY は INVAL。QEMU は書ける出力先で件数の不一致を与え、入力を読む前の INVAL と、その後の正しい CLIENT 1 面での再取得を確認。取得後の巻き戻しは host だけで検証する。両方で再 query 後の世代と失敗後の再取得を確認。
- x86 build_compare は surface_api_bundle、surface_lease_bundle、新設後端に命令差を見込む。lease_acquire・lease_release・paging の処理差は調査対象。リンク移動と命令変更を区別して確認する。

## 6. 未決

- 0 件。PowerMac の初回公開は登録済み CLIENT 1 件とする。

反論なし。

## 確定 (2026-11-19、claude-opus-5-5 の第 2 回、PM が反映)
- N1 (P1): 共通の surface_query.c から件数の照合と GVRAM_PLANES の参照を消し、display_provider_bundle_ok だけで判定 (§2)。
- N2: PowerMac の定義 (段 3b) を本線の段 2〜4 と同じ取り込みに入れる。PPC の 5c は公開の配線だけ (§4)。
- N2b: surface_query.c を組む host 試験のつなぎに定義を足す (§4 段 4)。
- N3: 案 (a) — 両機種の 1 行を単独で組める platform/<機種>/display_bundle.c に切り出し、試験の TARGET_SRC と extra_sources に足して真偽表と変異で守る (§4 段 3・3b、§5)。
- N4: 変異の目印の置き換えを §5 に書いた。

確定後の追記: 5c の QEMU の受入を件数の不一致の INVAL に (2026-11-19、PowerPC の PM の提案、本線 PM 決定、理由は wrapper の出力先の検査)
