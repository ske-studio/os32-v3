# .osri ビューアー (ユーザーアプリ) — 詳細設計

> 状態: **現行 (2026-11-04)** — 出所: ~/os32-tmp/run/osri-viewer/ (astra 起草 r1〜r3、Opus レビュー 2 回、PM が確定)。段の状態は票 TASK_OSRI_VIEWER が持つ。実装は GeForce の後。

## 1. 実物の確認

### SDK とアプリから届く範囲

| 項目 | 現状と根拠 |
|---|---|
| SDK 版 | KAPI v79、関数枠300。`sdk/kapi.json:3`、共有定数は `sdk/include/os32/os32_kapi_shared.h:42`。 |
| 起動契約 | `main(argc, argv, KernelAPI *api)`、OS32X v4・60B、最低 API 版・ヒープ指定。`sdk/README.md:39`。 |
| 描画初期化 | `gfx_init` / `gfx_init_200` / `gfx_shutdown`。`sdk/kapi.json:73`。 |
| 転送 | `gfx_present`、`gfx_present_rect`、dirty 登録/転送、nosync。`sdk/kapi.json:90,698,776`。 |
| 注意 | ライブラリの `gfx_present()` は dirty 登録、実転送は `api->gfx_present_dirty()`。`sdk/README.md:63`。 |
| 色 | `gfx_set_palette` / `gfx_get_palette`、`gfx_lease_palette`。`sdk/kapi.json:744,755,1357`。 |
| BB・能力 | `gfx_get_framebuffer`、`gfx_screen_info`、`gfx_stats`。`sdk/kapi.json:768,1324,1351`。 |
| 描画補助 | `gfx_hardware_scroll`、`gfx_hw_fill_rect`、`gfx_hw_blit`。`sdk/kapi.json:691,1330,1335`。 |
| 所有・面 | `gfx_screen_owner`、`surface_query/lease/unlease`、`gfx_surface_lease`。`sdk/kapi.json:1465,1760,1775,1798,1822`。 |
| ラスタ | `gfx_present_raster(void *table)` は公開済み。`sdk/kapi.json:796`。 |
| ファイル | `sys_open` / `sys_close` / `sys_read` / `sys_stat`。`sdk/kapi.json:617,626,634,724`。 |
| ファイル選択補助 | `sys_ls_window(path, skip, out)` あり。初版は一覧 UI を作らずパス引数を使う。`sdk/kapi.json:1830`。 |
| 入力 | `kbd_trygetchar`、`kbd_trygetkey` 等。`sdk/kapi.json:95,245`。 |
| 無い入口 | `sdk/kapi.json` 全 API に OSRI 読込・v2 plan 作成・B31 再生・OSRI セッションの公開入口はない。 |
| 型と画像ライブラリ | `GFX_Surface` は4面のオフスクリーン型で OSRI デコーダーではない。`sdk/include/os32/os32_kapi_shared.h:605`。 |

- `os32api.h:14` は共有ヘッダを取り込む入口。生成 KAPI 定義を手編集して追加しない。
- BB 公開型は幅・高さ・pitch・4面ポインタ。`sdk/include/os32/os32_kapi_shared.h:306`。面の公開型があっても、カーネル専用 `gfx_kernel_fb` と同一視して渡せない。
- 公開ラスタ表は最大200件、各行1件、行間隔2以上、既定予算は4行1件。
- IF=0・同期失敗・予算違反は静止提示。戻り値が void なので失敗理由は得られない。
- 以上の契約は `sdk/include/os32/os32_kapi_shared.h:623`。v2 の6384件表とは別物。
- wrapper は USER 入力表の全域を検査して既存描画へ渡す。`kapi/kapi_generated.c:1281`。
- 既存利用例: `userland/lib/gfx/draw/gfx_raster.c:20,42,51` が表構築と KAPI 呼出を提供。
- `userland/tests/gfx_demo200.c:47,102,150` は surface→sprite、初期化、dirty 転送の例。
- `userland/` の OSRI 検索ではデコーダー利用例は見つからず、画像用の実装を流用できるとはしない。

### OSRI と再生器の現在地

| 部品 | 実装と到達性 |
|---|---|
| ファイル形式 | 64Bヘッダ、32B RGB444初期色、128000Bの4面、8Bイベント。`gfx/gfx_osri.h:7,29`。 |
| 読込 | `osri_load(path, gfx_kernel_fb *, OsriImage *)`、VFSからBBへ読込。カーネル内部のみ。`gfx/gfx_osri.c:28,44,76`。 |
| 検証 | 寸法・予約欄・正確な長さ・band・RGB444・順序・CRC・末尾余剰を検査。`gfx/gfx_osri.c:54,60,85,107`。 |
| 現行通常 | v1≤200、v2≤1197・行3番号。行内番号昇順、同値拒否。`gfx/gfx_osri.h:15`、`gfx/gfx_osri.c:87`。 |
| 現行OBS | v2≤6384・行16番号、行内任意順・同値可、番号重複拒否。`gfx/gfx_osri.c:9,55,89,97`。 |
| 旧通常核 | `pl_prepare/pl_frame`、最大6 OUTの型・認定方式。`drivers/video/pc98/splash_raster_image.h:13,19`。 |
| OBS準備 | `pl_obs_prepare`、band1/v2専用、差分成分をG/R/B順に列化。`drivers/video/pc98/splash_raster_image.c:769`。 |
| OBS制限 | 現物の上限47 OUT、格納64、plan52008B。`drivers/video/pc98/splash_raster_image.h:25`。 |
| HBLANK核 | activeで先頭番号を選び、H立上りで列を実行、VSYNCならDROP。`drivers/video/pc98/splash_raster_image.c:939`（pl_obs_frame、919はobs_edge）。 |
| IF区間 | VBLANK待ちはIF開放、その後CLI、初期色・可視行走査、全出口で復元。`drivers/video/pc98/splash_raster_image.c:939`。 |
| 既存公開ラスタ | dirty転送/flip後 `pc98_splash_raster_table` へ。`gfx/gfx_vram.c:311,333`。OSRI核ではない。 |

- `gfx/gfx_vram.c:305` は目標行直前〜HBLANKの4 OUTだけ保護し、保護間にIF開放を置く方式。
- `RASTER_LINE_US` は実装から削除済みで、`tools/tests/cpu_calibrate_host.c:165` のコメントだけに残る。古い120µs待ちを現在の根拠にしない。
- 読込失敗でもBBには部分画素が入り得る（`gfx/gfx_osri.c:76,107`）。検査完了前の転送は禁止する。
- B31は「HBLANK内に全OUTが収まる」保証ではない。enc2は可視域への色変更のはみ出しも評価する。
- 根拠: `docs/decisions/gui/RASTER_SPLASH_ENC2_DESIGN.md:58,139`。生成時の探索はホスト専用（同:94）。

### S5 後との差分（未実装扱いの境界）

| S5確定事項 | ビューアーへの影響と根拠 |
|---|---|
| v2検査の共通化 | 通常/OBSとも6384件・行16番号、任意順・同値、band1/2/4。`docs/decisions/gui/RASTER_SPLASH_S5_DESIGN.md:21`。 |
| B31共通核 | `pl_v2_prepare/frame`、64B行・25608B plan、旧核撤去。S5設計:30,63。 |
| OUT再計算 | `C + max(M-1,0)`、先頭番号先行OUTを除き31以下。ファイルにBの欄はない。S5設計:26。 |
| 常用所有 | load→prepare→転送→約1秒preview→再生→cleanup。S5設計:38。 |
| 期限 | tickとフレームモデルの大きい方、DROPも課金、tick補正なし。S5設計:44,141。 |
| 段 | S5a=形式/準備/核、S5b=常用所有/期限、S5c=生成、S5d=配布/実機。S5設計:137。 |

- 上表の「S5設計」は `docs/decisions/gui/RASTER_SPLASH_S5_DESIGN.md` の略記。
- 票 `docs/tasks/gui/TASK_RASTER_SPLASH.md:3,24` は実装中・固定画像経路実装済みを記載。S5a/bの完了証拠にはしない。
- PL末尾はVSYNCによるDROPとenc2への参照を追記済み。`docs/decisions/gui/RASTER_SPLASH_PL_DESIGN.md:553,558`。
- ENC2本文の観測専用/47本とS5の常用/B31は段が異なる。後続仕様はS5を採る。
- 実物との食い違いは3件として記録: D1=通常v2検査の旧制限、D2=旧核/OBS47とS5共通B31、D3=RASTER_LINE_US不在。
- D1/D2は移行中の差であり不具合断定ではない。スプラッシュ結線先など未読箇所を追加の確認済み差として数えない。

## 2. ビューアーにできることの選択肢

| 案 | 表示できる範囲 | 費用・制約 |
|---|---|---|
| (a) 既存KAPIのみ | 固定16色OSRI、または行パレット復元後に全体16色へ減色した静止画 | アプリ側に形式検査/復号が必要。enc2の再現ではない。 |
| (a補) 既存ラスタ表 | 表の行間隔・件数・予算を満たす疎な画像のみ | v1の全画像も無条件には渡せない。v2表の詰替えだけでは不可。 |
| (b) カーネル再生KAPI追加 | S5が受理する固定画像・動的v1/v2、実OUT≤31 | 忠実な全画面再生。可視区間はIRQ停止、他アプリとの並行動作なし。 |
| (c) GUI内の静止プレビュー | ホストで減色した別画像を既存GUI描画で表示 | 変換物の管理が増える。初版の独自形式ビューアーの主経路にはしない。 |

- **(b)を推奨**。CPL=3にI/O命令を渡さず、既存S5核をHALの内部で共有する。
- アプリから毎行 `gfx_set_palette` を呼んでも同期・呼出費用を満たせず、代替案にならない。
- 動的表示中はフレームごとにIFを開けても、同期KAPIから戻るまでアプリ本体は進まない。
- 動的画像の初期枠は既定30秒、起動引数で最大120秒。Esc以外のキーで締切をmax(現在の締切, 今+30秒)へ更新する。入力が続く限り総表示時間の上限はない。固定画像はEscまで無期限。
- 可視中の入力はキーボードbufferに保持し、VSYNCのIF開放時にkbd_peekkeyで確認。Escは1回取り出して即取消、他キーも1回取り出して延長する。abort_req（CTRL+STOP）も取消。
- 可視ループでは入力を読まない。境界で有限件数を処理し、固定画像では他キーは消費のみ。sfsセッション中・pcm_open中はBUSY、LANの取りこぼしは許す（PM決定）。
- 正常同期時のEsc応答は概ね次のフレーム境界。IRQ配送・キーバッファ溢れを含む実測が必要。
- 同期停止時は有限pollで抜ける。時間枠は厳密な壁時計の上限ではなく、モデルとtickによる再生上限。
- ファイルI/Oの既存待ち時間までこのAPIが保証するとはしない。読込・準備・転送の実経過は枠から引く。

## 3. 推奨案の形

### 公開契約（提案。既存APIではない）

```c
/* C89/GNU89 compatible; sdk/kapi.jsonから関数表へ生成 */
int gfx_osri_show(const char *path, u32 duration_ms);
/* 1 <= duration_ms <= 120000。動的画像の初期枠。固定画像では有効値を検査後、時間枠を適用しない。 */
```

| 戻り値 | 提案する意味 |
|---|---|
| 0 / 1 | 動的時間枠終了 / Escまたはabort_reqで取消（サービスのcleanup済み） |
| OS32_ERR_INVAL (-9) | 時間値不正、空/長すぎるpath、CRC不一致・形式不正 |
| OS32_ERR_NOTFOUND (-2) / OS32_ERR_IO (-1) | ファイルなし / 読込失敗・短読 |
| OS32_ERR_NOSPC (-4) | image/plan/path等の確保失敗 |
| OS32_ERR_NOSYS (-10) | 対象形式外・B31準備不可、現在の表示経路または同期能力が非対応 |
| OS32_ERR_BUSY (-17) | GUI中の所有者不一致・gfx未開始、再入、sfsセッション中・pcm_open中 |
| OS32_ERR_AGAIN (-14) | 同期停止・全DROP・読込等で期限切れ・動的OKフレームなしで枠終了 |

- 専用の負値は追加しない。loaderの0/-1だけでは分類できないため、内部に理由を返す補助口を足し、既存boot呼出の契約は維持する。
- USER不正アドレスは既存B1のfault/kill契約。pathはNULまで上限付きコピーし、保持するUSERポインタは0本。
- G1確認: `sdk/gen_kapi.py:166,225,535,562` のinは範囲検査、target:trueはtargetへ委譲し、引数はそのまま渡す。文字列コピーは生成しない。
- `kapi/kapi_strings.c:17,89` のコピー表も手登録。今回は登録せず手書きkapi_osri.cをtargetにし、in=[{arg:"path",target:true}]・out:"none"を指定する。
- 入口でcallerを捕捉しVFS_MAX_PATH内をcopy_caller_bytesでNULまでコピー。不正範囲/桁溢れはB1、正常範囲の非終端/空はINVAL、確保失敗はNOSPC。全域一括の先読みはしない。
- 現在末尾 `sys_ver_info` の後へ1枠追加、他変更が先行しなければv80。`sdk/kapi.json:1949`。
- v79基準の追加slotは255。実装時に最新末尾・容量を再確認し、版衝突時は次の版を採る。
- JSON/共有定数を同時版上げ、生成C/Rust/初期化を再生成、アプリ要求版も更新する。
- ABI作業時はclean→全体再ビルドが必要。今回は行わない。`docs/KAPI_SPEC.md:44`。

### アプリとカーネルの境目

```text
アプリ（mkos32x --gfx付き）: osriview path [seconds] を検査（省略=30、指定=1..120）、不正ならusageで終了。
  voidのgfx_initを呼んだ後にgfx_osri_show(path, seconds*1000)。開始・所有の判定はshowのBUSYに任せる。成否にかかわらずgfx_shutdown、結果を表示して終了。
カーネル: USER path copy → 既存所有/開始・BUSY検査 → HAL経路hook → load/prepare/転送
  → HAL内共通再生制御(pl_v2_frame) → 色/TVRAM/両ページcleanup → free（CUI/GUI復帰はアプリ）。
```

- 起動→引数で1ファイル選択→全画面表示→Esc/期限/失敗→gfx_shutdownで元の環境へ復帰→終了。別ファイルは再実行する。
- 引数なしはusageを出す。ファイル一覧・次画像・拡縮・回転・PNG/JPEG対応は初版に加えない。
- GUI中も--gfx宣言で既存の全画面取得を許す。gfx_init→gfx_kapi_claim→appslot_gfx_claimの門を通り、GUIウィンドウ内には表示しない。
- showはgfx開始済みを必須とし、con_sink_is_enabled()のときだけ呼出元==appslot_gfx_owner()も検査する。不成立はBUSY。CUIでは所有者がWMのままでも許可する。
- gfx_shutdownと同じGUI限定の所有照合を使う（gfx/gfx_core.c:431）。static gfx_startedには内部読み口を1つ足す。showはgfx_init/shutdownを呼ばず、所有者・開始状態を変更しない。
- HALに現在の表示経路のOSRI hookを1つ置く。pc98 GDC 640×400 planar4の31k/24kに対応し、GeForce/PEGC480/200ラインはNOSYS。gfx/からpl_v2_*を直接呼ばない。
- HAL hookの署名案は `int (*osri_show)(const struct OsriImage *, const struct PlV2Plan *, run_ctl *)`。固定画像のplanはNULL、run_ctlは時間・取消状態を持つ内部型。hook欠如はNOSYS。
- パレットは全16色を使うため、GUIの部分leaseで済ませない。TVRAM文字重畳も表示中は隠し、終了時に復元する。
- loadは既存BBを借用し、全検査・prepare成功後にのみVRAMへ転送。巨大配列はheapに置く。
- 未転送の失敗では表示を変えず、BBの部分更新/dirty状態を破棄する。転送後は黒→両ページ整理→表示中の色/TVRAM復元。CUI/GUIへの復帰はアプリのgfx_shutdownが担う。
- S5のboot所有関数そのものを呼ばない。bootのロゴfallback・3秒枠・1秒previewはboot固有である。
- ビューアーは画像初期色から開始しpreview演出を省く。固定画像はIF開放で待ち、Esc/abort_reqを確認する。期限・有限反復上限は設けない。
- S5aの `pl_v2_prepare/frame` をそのまま共有し、S5bの有限時間計算を共通部品へ抽出する。
- 共通制御に時間枠と取消callbackを渡す。bootは取消なし・3秒、viewerの動的画像は初期1〜120秒（既定30秒）・入力で延長し総表示時間の上限なし。OUT列は増やさない。
- 動的次周の条件は `max(E*D, n*P)+P <= R*D`。OK/DROP双方でnを増やし、NO_SYNCは即失敗。
- 式のE/Rはµs、P/Dは周期。初回Rは初期枠からload/prepare/転送時間を引いた残予算、E/nは現在の原点からの経過/フレーム数。初期処理で枠を使い切ればAGAIN。
- キー処理時はmax(E*D,n*P)から現在の残予算を求め、R=max(残予算,30秒)とし、E=0・n=0・tick原点=今へ置き直す。これは締切max(現在の締切,今+30秒)であり、30秒を加算しない。
- 延長のたびに置き直すためRは常に120秒以下。表示成功の有無はセッション全体で保持し、E/nと一緒には消さない。
- Pの計算と式の乗算前にu64へ拡張する。S5の乗算比較による有界探索をRの範囲に合わせて使い、残予算の変換で時間を水増ししない。入力は期限判定より先に処理する。
- tick wrapは原点との差分で扱い、64bit除算を追加しない。原点ごとのRでE/nを有界に保つ。tick停止時も追加入力がなければnの累積で有限に終わる。
- 全出口でIFとHAL状態を復元、fd/heapを解放する。永続ハンドルは設けずアプリ終了後に資源を残さない。
- tick補正はしない。延長を続ける動的表示ではbootより遅れが累積するため、外部時計との差を受入に残す。

## 4. 大きさの見積もり

| 対象 | 増分の概算（未ビルド・未実測） |
|---|---|
| アプリ | C 80〜150行、固有text/rodata 2〜6KiB、スタック/小バッファ4〜8KiB。CRT込み.binは別途測定。 |
| kernel・S5後の追加 | HAL所有/入口/時間制御の整理で250〜450行、text/rodata 6〜12KiB程度。核の複製なし。 |
| 動的一時heap | image51110 + plan25608 = 76718B、path等は別途約1KiB＋allocator管理領域。 |
| 固定画像一時heap | 現行loader型を使うならimage51110B、plan不要。最適化のため別型は作らない。 |
| BB | 既存128000Bを借用、追加画素バッファ0B。BB込み動的payloadは204718B。 |
| 入力ファイル | 最大179168B、全ファイルの別コピーは作らずVFSから読む。 |
| S5自身 | plan/型変更は上記増分と別。S5設計:102のkernel暫定32KiB枠を二重に計上しない。 |

- CUI文字状態の保存領域が既存復帰機構で不足する場合は実装前に再見積もりする。GPU画素全退避は採らない。
- kernel508KiB上限・媒体空き・実heap・読込/prepare/転送の秒数は実装段で測る。推定サイズを合格証拠にしない。

## 5. ファイルごとの変更と段・受入

| ファイル（新規は+、宣言は対応.h） | 1ファイルの責務 |
|---|---|
| +`userland/osriview/main.c` | 引数、KAPI呼出、結果表示のみ。 |
| +`gfx/gfx_osri_view.c` / +`.h` | 同期サービスの所有・読込・復帰、USER入口から渡されたkernel pathを使う。 |
| `gfx/gfx_core.c` / 対応.h | static gfx_startedの内部読み口を追加。GUI限定の所有照合をサービスで使用する。 |
| +`kapi/kapi_osri.c` | 手書きtarget。USER文字列の検証/コピーと理由分類、呼出元を保持してサービスへ渡す。 |
| HALの現表示経路interface/pc98実装（GeForce後に配置確定） | OSRI hook・能力判定と共通runへの結線。非対応経路はNOSYS。 |
| +`drivers/video/pc98/splash_raster_run.c` / +`.h` | 有限時間制御と取消境界。S5bの同種計算をここへ移し二重実装しない。 |
| `platform/pc98/splash_image.c` | S5bの呼出を共通runへ結線、bootの演出/復帰責務は維持。 |
| `gfx/gfx_osri.c`、S5のplan/play | 核を再利用しloaderに内部エラー理由を追加。公開KAPIのためにOBSマクロ分岐を復活させない。 |
| `sdk/kapi.json`、`sdk/include/os32/os32_kapi_shared.h` | 末尾1枠、既存戻り値を使用、版。生成物は生成器のみ。 |
| `build/app.conf`、`build/kernel.mk`、`userland/deploy.yaml` | 最低API版、--gfx、ソース登録、アプリ配置。登録変更は直列。 |
| +`tools/tests/test_osri_view.py` / +`osri_view_host.c` | 契約・所有・終了のhost試験と変異、CHECK入力登録。 |
| `docs/KAPI_SPEC.md`、`sdk/README.md` | 公開契約・時間/延長・全画面所有・起動例を記載。 |

- V0: S5a/b受入を前提確認。S5dの画面品質結果も参照し、未受入の核をviewerで正当化しない。
- V1: 共通run抽出と所有/復帰を実装、boot回帰を通す。V2: KAPIとアプリ、ABI再生成/cleanビルド。
- V3: host/変異・選択検査・CHECK lint、NP21/W、Ra266の31k/24kで表示と入力/時刻を受入。同期モードごとに結果を記録する。

| 受入 | 試験と変異（正常GREEN→実行時RED→復元GREEN） |
|---|---|
| A1 ABI/USER | GNU89ヘッダ、版/末尾不変、pathの跨ぎ/非終端/不正USER範囲。コピー省略・版不一致を検出。 |
| A2 形式/予算 | v1/v2、固定/動的、CRC/短読/余剰、6384/6385、31許可/32拒否。旧1197制限・CRC省略変異。 |
| A3 核共有 | boot/OBS/viewerで同一imageから同一OUT列、空行/同値/任意順。順序変更・重複核を検出。 |
| A4 期限/取消 | 0/120001拒否、既定30秒/引数120秒、締切max(現締切,今+30秒)、連打/リピートでも積み上げなし、120秒超・71.6分超の延長、固定無期限、tick停滞/wrap、全DROP/同期停止、Esc/abort_req。置き直し忘れ・足し算の積み上がり・二重消費・取消無視変異。 |
| A5 所有/復帰 | CUIでWM所有のままinit→show成功、GUI全画面許可、未init/GUI非所有者拒否、31k/24k許可、非対応表示経路/hook欠如拒否、sfs/pcm BUSY、確保/読込/転送後失敗。IF/fd/heap/色/文字/両ページ・アプリshutdown、所有/経路検査省略変異。 |
| A6 実画面 | 既定画像・最大イベント・静止画像、繰返し起動、Escの保持/短押し。ちらつき・筋・色を写真で比較。 |
| A7 時間/容量 | 31k/24k別に外部時計で初期30/120秒と延長継続時の表示期間・Esc最大応答・tick遅れ・buffer溢れ、LAN取りこぼし（許容）の実測、heap/map/媒体。host合格と実機合格を分ける。 |

## 6. 決定と実装関門

- PM決定は確定: 動的初期枠は既定30秒・引数で120秒まで。Esc以外の入力で締切max(現在の締切,最後のキーから30秒)、総表示時間の上限なし。更新ごとにE/n/原点を置き直す。Esc即取消、固定画像はEscまで無期限。
- 実装はGeForceの後。S5a/b完了、GeForce後のHAL配置・所有/セッション検査の接続・KAPI末尾、応答/容量実測を実装関門とする。今回は設計だけを更新する。
- 反論: なし（0件）。
