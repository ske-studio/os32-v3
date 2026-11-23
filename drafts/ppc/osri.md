# gfx_osri_show の PowerPC 提供案 r2

## r1→r2 の変更

- P3・排他の短絡順を gate → mount → PCM に固定した。
- P1・常駐 /host を表示競合から外し、PC-98 の mount 判定を保存した。
- P2・PCM と gate は既存の契約を使い、機種別ヘッダを platform_*.h に揃えた。
- P1・動的画像のキー延長式、入力経路、両機種の初回転送と残時間判定の順を定めた。
- P2・固定 CLUT 全体から選んだ物理索引を直接転送する。
- P2・OF が bind 世代を所有し、再 bind 時の書込みと bridge の返却を世代で区別する。
- P2・共通デコーダの受理範囲と PC-98 固有の行出力予算を公開契約に記す。
- P1・run の配置を固定し、PPC 専用の面受渡しを x86 共通部の変更より先に用意する。
- P1・EXEC／USERSHELL ごとの実リンクと wrapper／実体の検査を区別した。
- P2・不在に戻した像も起動し、通常 slot の戻り値を観測する。
完了の基準の案　両機種の通常の gfx_osri_show が同じ入力検査と排他を通り、PowerMac の表示・中止・復帰をゲストで確認し、PC-98 の生成物を基点とバイト比較できること。
HAL 中心期の決まりに沿うか　沿う。既存の表示後端に OSRI を結線し、音と serial は現に必要な占有照会だけを契約化する（docs/POLICY_DEV.md §1）。

## 1. 実物の確認

|調査の前提との食い違い・補足（3 件）|本線の根拠|設計への反映|
|---|---|---|
|対象リストの定義場所は select.mk から移っている|build/select.mk:28、build/sources/common.mk:11|像の選択式とソースの所属を分けて扱う|
|PLANAR4 の検査は再生の入口だけではない|gfx/gfx_osri_view.c:32、gfx/gfx_osri.c:30|読込み先にも連続した 4 面が必要。表示先だけ INDEX8 に置換するとロードで失敗する|
|「PCM の再生」は OSRI の処理にはない|gfx/gfx_osri_view.c:28、gfx/gfx_osri.c:65|音声トラックも書込みもない。閉じているかだけで排他する|

統合の判断は次の 3 点。
1. kapi/kapi_osri.c:9 の入口をそのまま束縛する。呼出元・時間・パスのコピー・fault 処理を共用する。
2. ファイルの平面形式を保持する。検証済み画像を既存の GfxBackend.osri_show に渡す所で表示方式を分ける。
3. 表示と競合する serial 利用および閉じていない PCM は BUSY。サービスの実在と現在の使用状態を分ける。

- 5c の不在は命令セットの制約ではない。固定面・OF hook 不在・状態照会の未分離が理由だった（PPC_KAPI_GFX_REST_DESIGN.md:45,68）。
- 今回は OSRI の既存 slot 一本だけを対象にする。5c 当時の不在数を現在の期待値に流用しない。
- [HW1] に従い、機種選択は像が選ぶ実装で行う。共通入口に機種判定を足さない。

## 2. 依存の一覧

|依存|現在の呼び先／データ|共通側の契約と担当|
|---|---|---|
|呼出元|caller_access_get、copy_caller_bytes、ring3_fault_kill|既存 KAPI 入口。空文字・過長・読めないポインタを従来通り区別|
|所有者|gfx_is_started、con_sink_is_enabled、appslot_gfx_owner|開始済み・表示中の再入・端末所有者を view で検査|
|PCM|pcm_state() != PCM_ST_CLOSED|カーネル音 HAL の占有照会。装置検出や再生中だけの照会では不足|
|serial|serial_gate_active() と機種別 mount 判定|gate → 機種の競合 mount → PCM の順に短絡する。UART の存在や送信 FIFO は無関係|
|表示面|gfx_kernel_framebuffer、gfx_kernel_fb.format と pitch|CLIENT の実形式・寸法・行間隔を取得|
|入力面|osri_load_checked(path, dst, image)|ファイルの規格通り 640×400、80 byte/行、4 面を要求|
|画像検証|VFS、LE 読取り、CRC32、イベント順・重複・末尾検査|同じデコーダを両像で使う|
|表示|GfxBackend.osri_show|PC-98 は GDC・ラスタ、OF は固定 CLUT への変換と既存の転送|
|中止|gfx_osri_poll_abort、後端の ESC|abort_req・stop_pending は共通、ESC は既存の入力経路|
|同期・復帰|v1_bridge_begin/end、dirty_queue、prev_dirty|CLIENT/DISPLAY の書込み区間と失敗時を含む後始末は view が所有|
|文字|PC-98 はテキスト属性を一時退避|OF は既存端末面・既存フォントの再描画を利用|


## 3. 共通入口と機種別の状態

KAPI は時間・パス・呼出元の検査を保存する。view の入口は次の式を同じ順で評価し、次に hook と framebuffer を検査する。占有の確認から active の設定までは yield しない。

```c
if (!gfx_is_started() || osri_active ||
    (con_sink_is_enabled() && caller != appslot_gfx_owner()) ||
    serial_gate_active() || platform_osri_mount_busy() || platform_osri_pcm_busy())
    return OS32_ERR_BUSY;
```

|像|gate（serial_diag.h）|mount の inline 式|PCM の inline 式|
|---|---|---|---|
|PC-98|実状態|serialfs_is_mounted()|pcm_state() != PCM_ST_CLOSED（pcm_contract.h）|
|PowerMac EXEC|実状態|0（常駐 /host と OF 表示は併用可能）|0（現行像は PCM 不在）|

PPC の gate は /host の有無によらず診断転送の占有を検出する。新たな排他的マウントや PCM 提供者が入る段でその実状態を結線する。FAULTED・停止途中も占有に含む。既存 PCM の open/write/close や音源・tick は変更しない。TASK_AV_HAL.md のカーネル一流、ミキサはライブラリという境界を保つ。

### 入力面の受渡し

```c
struct OsriViewRun { int caller; u32 start, duration_ms; }; /* 現行定義 */
int (*osri_show)(const struct OsriImage *, struct OsriViewRun *);
/* PPC view のみ。decoded は同期呼出し中のローカル記述子。 */
rc = osri_load_checked(path, &decoded, image);
if (!rc) {
    session.decoded = &decoded;
    rc = g_backend->osri_show(image, &run);
    session.decoded = 0;
}
```

|経路|準備|所有・解放|
|---|---|---|
|PC-98|既存 view が gfx_kernel_framebuffer で寸法・pitch・PLANAR4 を検査して CLIENT を借用|現在の begin/end、失敗時を含む四面クリア、dirty 両列の初期化を保つ|
|PPC EXEC|同じ入口検査を持つ PPC 用 view を選択し、PACKED8 の有効域を確認。active を確保して開始 tick を記録し、128000 byte を確保。先頭から 32000 byte ごとに planes を置き 640×400、pitch 80 の PLANAR4 とする|PPC 内部アクセサは active かつ公開中だけ const 記述子を返す。全出口で公開解除後に private 面・image を解放し最後に active を落とす。shadow は消去しない|

形式・hook 非対応は NOSYS、確保失敗は NOSPC。VFS と CRC を含む読込みは共通。PPC の poll_abort も既存の appslot 判定を使う。共通の検査式・poll 本体は platform ヘッダ経由の inline／同一ソース片とし、対象像はビルド時に選ぶ。

## 4. 公開する表示・時間の契約

- 両機種で osri_load_checked が v1/v2 の寸法、オフセット、band、順序、同一行の重複、予約値、CRC と EOF を全て検査してから表示する。ファイルとして不正なら INVAL、短読は IO。
- 両方で表示できる動的画像は共通検査に合格し PC-98 の pl_v2_prepare の行予算にも収まるもの（対応表示モード・同期が前提）。変更成分の OUT と二つ目以降の変更索引の OUT を数える。v2 同色更新は 0。合法でも行予算／port 配列容量超過なら PC-98 は表示前に NOSYS、OF は同じイベント列を描く。
- OF は protected_rows 直下の左端に等倍 640×400 を置く。有効領域不足または固定 CLUT 不在なら NOSYS。各行の直前にその行のイベントを順に適用し、平面の上位 bit から B/R/G/I を重み 1/2/4/8 で合成する。
- 初期 16 色と更新項目ごとに RGB444 の各成分を 17 倍し、固定 CLUT 全 256 色との RGB8 の二乗距離の和を最小にする物理索引を選ぶ。同距離は小さい索引。GDC のビーム同期は OF の条件に含めない。
- OF は private な物理 INDEX8 矩形を準備し、専用の転送関数で各行 640 byte を display pitch に従って整列した native word で書き、present_bytes とメモリバリアを通常転送と揃える。scan_index は通さない。復帰時だけ of_fb_copy で現行の論理 shadow（shadow_pitch）を再転送する。保護帯・padding・矩形外を保持し、端末文字も shadow から復元する。

```c
/* 時刻は同一単位の経過時間に正規化。tick 差は unsigned、換算は 64 bit。 */
elapsed_us = (u64)(u32)(now_tick - run.start) * tick_us;
deadline_us = (u64)run.duration_ms * 1000;
if (dynamic && non_esc_key)
    deadline_us = max(deadline_us, now_us + (u64)GFX_OSRI_SHOW_EXTEND_MS * 1000);
```

now_us は開始からの経過時刻（連続待機中は tick 差分を累積）。GUI は raw make のみ、CUI は既存の文字入力を非ブロックで読み、Esc・abort_req・STOP で CANCELLED。固定絵は期限なし。動的画像の一度も表示できない枠は AGAIN、有効フレーム後の期限終了は DONE。

|動的画像|初回転送と残時間判定|待機|
|---|---|---|
|PC-98|現行どおり plan・文字退避・黒化・初回 present の後に prefix を測り remaining=max(0,budget−prefix)。転送前には残時間を判定しない。残り 0 も通常の復帰を通って AGAIN|pl_run_extend と good フレームの判定を維持|
|OF|読み込み・色変換後、初回書込み直前に経過を比較し期限済みなら AGAIN。転送完了時にも判定し、枠内に表示できなければ復帰して AGAIN|表示後は有限回のキー poll と協調待機。上式で延長し期限で復帰|

### OF の bind と復帰

backend_of_fb.c に live と独立の世代カウンタを置く。unbind の入口で旧世代を無効化し、bind 成功時に新世代を発行する。同一アドレスへの再 bind も別世代。カウンタは再初期化せず、周回で借用中の値を再利用しない。転送直前、VFS・待機からの復帰時、後始末前に live と取得済み世代の一致を確認する。照合から書込み完了まで切替を許さない。
PPC は private 面へのロード中・待機中に bridge を保持しない。初回表示と復帰それぞれの短い非 yield 区間で同一世代の CLIENT 読取り・DISPLAY 書込みを begin し、成功した分だけ逆順に end する。unbind も同じ区間と直列化する。dirty_queue・prev_dirty は消費しない。失敗時は書込み済みかつ同世代なら復帰、世代違いなら新旧両面への復帰を省き IO を返す。

## 5. 実装順とリンク

```make
# build/platform/powermac.mk の EXEC 群にのみ追加
C_PLATFORM_EXEC += gfx/gfx_osri.c platform/powermac/osri_view.c kapi/kapi_osri.c
```

|像|EXEC／USERSHELL|最終 ELF に求める構成|
|---|---|---|
|powermac、kboot-sh、kboot-link-sh|1／1|共通 decoder・KAPI、PPC view、OF hook、常駐用 serialfs|
|kboot-shell|1／0|同じ OSRI 経路、serialfs 本体への参照なし|
|kboot-core、kboot-link|0／0|OSRI 三本・OF 後端・serialfs 本体なし|
|pc98|既存選択|pending の三本と後端、オブジェクト順を保存|

|段|変更単位・目安|段を通す条件|
|---|---|---|
|1|各機種の platform_osri.h を一枚ずつ追加（各約 10 行）。§3 の式を inline 化|未使用の段から x86 基点比較|
|2|gfx/gfx_osri_view.c の既存式を inline に差替え、共用する入口判定と poll を内部ヘッダへ抽出（約 20 行）|各変更で x86 生成 C、像、配布物が byte 一致するまで進めない|
|3|platform/powermac/osri_view.c と内部アクセサ宣言（約 70 行）。§3 の配置・寿命で固定|x86 はこの TU を選択しない|
|4|OF 後端に世代照合・物理転送・復帰・hook を追加（約 110 行）|3 とインターフェースを合わせ host 検証|
|5|PPC EXEC の選択式、sdk/kapi_target_ppc.py、binding 検査を同一統合段で更新|全行の像を clean link、map/nm で上表と照合|
|6|KAPI_SPEC §4-16 と host/guest probe・変異を各担当ファイルごとに反映|§6 の実行結果を保存|

生成物は正規生成器で更新する。PPC の不在集合と EXPECTED_ABSENT_SLOTS から gfx_osri_show を除き、verify の通常 wrapper 比較集合へ個別追加する。実体名が slot 名と異なるので PUBLISHED_GFX をそのまま拡張しない。

```python
DISPLAY_PROVIDERS |= {'kapi_osri_show'}  # missing と unexpected の両方に使われる集合
```

生成 wrapper の未定義参照は kapi_osri_show を必須かつ許容、最終 ELF では定義済みとし、OF 表の hook 到達まで確かめる。gfx_hardware_scroll の不在を保つ。工数は入口分離 1 日、OF と復帰 1–2 日、ゲスト・変異・比較 1–2 日。

## 6. 受入 → 試験と変異

各行の変異は対応する観測が赤になることまで確認し、変異を戻した結果を採録する。

|受入|試験|変異|
|---|---|---|
|入口の互換|両像の通常ラッパー経由で空パス・時間 0・最大超過・不正ユーザー番地を検査|時間上限比較を反転|
|排他の優先順|未開始・再入・所有者違い・PCM 各非 CLOSED を BUSY、hook なしと重ねても BUSY|占有を RUNNING のみに狭める|
|serial 使用中|PC-98 の gate/mount 四通りと呼出し順を記録。PPC は /host 常駐で表示成功、gate 中は BUSY|OR の片側を除去|
|同一デコーダ|v1/v2、同一行の更新、CRC・短読・余分な末尾を両形式の入力面で検査|CRC 比較を除去|
|平面変換|4 plane の単独 bit とバイト境界、隣接行で同索引を別色にする入力を画素比較|bit 順反転／イベントを一行遅延|
|OF 転送|scan_index の像外に唯一の完全一致を置き、物理 VRAM を検査。非一致 pitch、保護帯、同距離色を host で比較|物理索引を論理索引として再転送|
|保持・中止|固定絵を期限越えで観測後 ESC、イベント付きで期限・abort・stop を独立に観測|固定絵にも期限を適用|
|資源の復帰|確保失敗・読込み失敗・中止後に再表示し、端末文字、両 bridge、面の番兵を観測|packed 面を平面として消去|
|無音機|PCM 未登録で正しい画像を guest の通常 KAPI から表示|音の存在を必須化|
|公開集合|ppc-kapi-binding と ppc-kapi-table で通常 wrapper → kapi_osri_show、残る gfx_hardware_scroll は欠如|不在に戻して再生成・リンクした像を集合検査とは独立に起動。同一の有効画像で通常 wrapper が DONE から NOSYS に変わることをゲストで記録|
|§9・生成物|同じツール・設定・時刻で基点と各段を clean build、x86 の生成 C、ロード像・配布物を byte 比較|x86 のリンク順を交換|
|KAPI 指紋|正規生成器を二度実行し slot・引数幅・関数表順・版・容量・公開ヘッダの指紋を基点比較|リンク可能な同じ型の slot 入替えを指紋比較で検出し、ゲストの通常呼出しの結果も照合|

- §9 の証跡には基点 revision、像、比較対象、差分、実行した試験と変異の結果を対応づける。
- PPC の束縛に由来する生成差分のみを記録する。既存 slot の提供で ABI 指紋が動いた場合は原因を調べる。
- 既存 osri-view-host、OF FB の pitch・protected-band・復帰の変異と、層検査を両像で追従させる。
- 設計時点では試験・生成物比較は未実施。公開の判定は実装段の結果に基づく。

- 同じ v2 fixture の行 OUT 数を予算内／超過にし、両像の DONE／DONE と NOSYS／DONE（PC-98／OF）を観測する。
- GUI make・break と CUI キーを初期期限直前に与え、非 Esc で延びた期限、Esc 即時終了、初回枠切れ時の機種別転送回数を記録する。延長無効化と PC-98 初回転送前の早期 return を変異とする。
- 同じ面アドレスで待機中に unbind→bind し、新世代の番兵・dirty 両列と bridge 残数を検査する。live だけの判定に戻すと赤になることを確認する。

## 決定

PM 決定を §4 の公開契約に採用。未決事項はない。

## 確定 (2026-11-23、sol 第 2 回の条件付き承認、PM が反映)
本文と食い違う所はこの節を正とする。PM 決定 (未決 1 件): OF の固定の CLUT の全体から最近色を選び、色差の尺度とラスタの制約の差を公開の契約に書く。
- **R2-P1 kboot-shell の Esc**: kboot-shell は USERSHELL=0 で input_boot.c がリンクされず、固定の絵を Esc で閉じる道が無い。この像では既存の CLI 端末の FIFO を非ブロックで読み、通常の KAPI で固定の絵を表示して Esc で戻れることを受入で確かめる。
- **R2-P1 世代の取得の時点**: 最初の yield より前に有効域と bind の世代を取得する。ロード中に同じ番地へ再 bind された時は IO を返し、新しい世代の面と dirty を保つ — この場合を試験に足す。
- **R2-P2 OSRI 中の重複の転送**: OSRI の表示中は、通常の present と V2 の flip が同じ矩形を上書きしないよう後端で保留し、終了の時に有効な表示元から反映する。待機中の通常の present と V2 の flip を受入に足す。
- **R2-P2 待機の順**: 初回の転送の後の待機では、入力と取消を期限の判定より先に処理する (境界でキュー済みの非 Esc キーの延長を落とさない)。期限の境での延長と Esc を試験する。
