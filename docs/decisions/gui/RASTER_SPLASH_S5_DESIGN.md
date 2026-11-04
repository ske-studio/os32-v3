# ラスタスプラッシュ S5 — 常用の可変 OUT (v2) と enc2・B31

> 状態: **現行 (2026-11-04)** — 出所: ~/os32-tmp/run/raster-s5/ (astra 起草 r1〜r4、Opus レビュー 2 回、PM が確定)。段の状態は票 TASK_RASTER_SPLASH が持つ。

実行者: Codex。指定モデルIDはgpt-6-astra、実行環境から厳密なIDは確認不能。設計のみ、実装・ビルド・配備は未実施。
M=`/home/hight/os32-v3`、レビュー照合基点 `9335e72a`。以下のリポジトリ相対パスはM。W=`/home/hight/wt/os32-v3/osri-enc2-impl`。
ユーザー決定「N7でマルチスレッド化してください」「ブートスプラッシュ側もN7で最適化してください」とデフォルトパレット約2秒の演出を採用する。
シングルタスク・協調切替、PEGC/EGC不使用、公開ラスタKAPI不変。既定enc2/B31/band1、OBSも同じ31上限。唯一の仕様未決はU1。

## 1. 現状と根拠
| 実物の食い違い | 本設計での扱い |
|---|---|
| `platform/pc98/splash_image.c:31` がcount≠0を拒否、`init/boot_splash.c`がこの入口を呼ぶ | 動的画像の所有・準備・再生を結線する。旧再生器が常用で稼働中とはしない。 |
| `gfx/gfx_osri.h`・`.c`は通常1197件/行3番号/昇順、OBSだけ6384件/行16番号/任意順・同値可 | v2の形式検査・RAM型を共通化し、ビルドマクロで意味を変えない。 |
| `drivers/video/pc98/splash_raster_image.*`の旧核は6 OUT/認定表依存、差分準備・可変フレームはOBS専用 | S4核を共通部品へ移し、旧核を削除。上限置換だけで済ませない。 |
| `tools/img2osri.py`の旧既定は5 OUT、enc2部品・並列化は別作業木 | 受入済みenc2-mtを固定して取り込み、既定を結線する。 |
| PL正典の毎行N件・旧固定列契約とS4の空行可/同色省略が不一致 | 正典の該当契約を置換し、歴史的観測結果と採用仕様を分ける。 |
根拠: `/home/hight/os32-tmp/run/obs-s4/{report.md,RESULT.md}`、`osri-enc2-impl/report3.md`、`splash-img2/out/comparison.csv`（後二者も同run配下）。
S4はRa266/31k・8枚・最大実OUT28・各351frames/drop0。47は旧設計の上限・ホスト比較範囲であり、実OUT47の実機検証済みとはしない。

## 2. 形式・準備・共有再生
- v2配置は64Bヘッダ＋初期色32B＋4面128000B＋8Bイベント。CRC計算時はオフセット48のCRC欄を0化、予約欄0、正確な長さを検査する。版3・成分マスクは追加しない。
- v2は総6384件・行16番号まで、行1..399の非減少、同一行番号一意、番号範囲・RGB444・CRC・band整列を検査する。行内はファイルのπ順、同値イベントを受理する。
- band=1/2/4を読込・準備で受理し、間の空行も走査する。生成既定は1、enc2でband≠1はCLIエラー。OBSのband==1専用制約を一般化する。
- v1は200件・番号昇順・同値拒否・空画像band=4等の既存検査を維持し、動的画像は共通準備器へ渡す。v2の行16番号制約をv1形式検査へ混ぜない。
- Bはファイルに記録されない。準備器が初期パレットから全400行を一度走査し、実OUT=C+max(M−1,0)を再計算する。Cは変更G/R/B成分数、Mは実際に変わる番号数。
- 初期64 OUTと各行先頭番号の先行OUTは費用に含めない。同色は持越し、成分はG/R/B順。各イベントの番号切替＋成分を追記する前に累積≤31と容量を確認する。
- 0 OUT行はfirst=0/count=0。準備器自身も範囲・順序・重複・末尾を検査し、全行成功までVRAMへ転送しない。32以上はplanを使わず解放し、常用はロゴ、OBSはstatus=prepareで次画像へ。
- 旧5/6 OUT画像・固定画像を受理する。B47生成物も実OUT≤31なら受理、超過なら拒否。新原画N7/N8は同一バイト列・最大25、既存S4の最大28も範囲内。将来のB47再生は保証しない。
- S4 prepareを新`splash_raster_plan.c`、frame/edge/展開列を新`splash_raster_play.c`へ移す（いずれも`drivers/video/pc98/`）。通常/OBSが同じ関数をリンクし、元のpl_obs_*本体は削除する。
- 手順はIF=1で新VBLANK→CLI→初期64 OUT→F原点→active0+yのlow/high辺→表示中の先頭番号→H立上りから列実行。空行も辺を数え、次フレームは初期色を復元する。
- 追加INなしでVSYNCを判定し途中ならDROP。各waitは1000000 pollでNO_SYNC、全出口で入口IFを復元。可視ループへ探索・時計・診断を追加しない。
- emitは0..31を手書き展開し32..47の型・列・関数表要素を残さない。生成器・生成ファイル・生成用make規則は作らず、B変更時は静的検査でビルドを止める。
- OBSはN順・TVRAMラベル・SPLO・5秒/枚・1秒previewの駆動を維持する。通常の固定画像は既存IF開放経路でplanを確保しない。公開table/内蔵ロゴの同期規則は不変。
- 旧pl_prepare/pl_frame・認定表・qualified・専用PlPlan/PlProfileと不要な補助処理を削除する。DIAGの独立probe_pl_prepareが使うPlRowと、通常/OBSのpl_previewおよび必要な補助だけを残す。
- 旧pl_frame専用のktime_splash_sample/settle・宣言・専用状態/算術/試験/登録も参照を確認して削除する。一般のktime/IRQ時間処理は維持。U1補正あり案も旧認定表への依存を復活させない。

## 3. 所有・演出・期限・失敗
- 常用所有者はimageと動的planをheapへ確保し、画素は既存BBを借用する。load→prepare→全色黒→128000B転送1回→pl_preview 1回→再生→黒→解放。巨大配列をstackへ置かない。
- ユーザー決定 (2026-11-04): 「24.8kHzでもバッチリでした。ただ、表示時間が短いです。デフォルトパレットの時間を倍に。ラスタの時間を3倍にしたい。」
- pl_previewはIF開放でデフォルトパレットを約2秒表示する。意図は演出で、VRAMに書かれたデータがデフォルトパレットで見えた後、行ごとのパレットの再生で突然カラフルになる (ユーザー指定 2026-11-04、実機で最初の約0.4秒が既定の16色に見えたのはこの演出で、崩れではない)。転送後・動的再生前に必ず1回呼び、フレーム内で重複実行しない。準備時点でpreview分が残らない場合は転送前にFALLBACKとする。
- 画像経路の入口の8秒枠からload/prepare/転送/previewの実経過を引く。preview同期取得の端数も課金し、期限超過なら追加再生しない。previewの有限poll・失敗出口を維持する。
- missing/invalid/memory/prepare失敗は未転送でFALLBACK。同期未判定・IF=0はNO_SYNC。転送後のpreview/同期停止は黒へ戻してNO_SYNC。全DROPも表示成功にしない。
- 呼出元の内蔵ロゴ分岐、両ページ消去→gfx_shutdown→palette_init→tvram_clearを維持する。全経路で所有物を解放し、転送済みの部分失敗からも復帰する。
- 動的再生開始時の残りをR µs、開始後のtick経過をE µs、消費済みフレーム数をn、P=total_lines×line_num_us、D=line_denとする。OK/DROPをともに1回消費、NO_SYNCで即終了する。
- 次フレームを許す条件は `max((u64)E*D, (u64)n*P) + P <= (u64)R*D`。周期の除算・floorを使わず、tick停滞でも有限に止まる。同期取得の余分な1周期は許容し、厳密な壁時計上限とは称さない。
- Pの計算前にu64へ拡張する。31k/24kの許可済み同期値・8秒枠・nの上限を検証し積と和のoverflowを防ぐ。µs化も既存tick周期の整数乗算を使い、新たな64bit除算を入れない。
- 消費はtick経過とモデル経過の大きい方とし二重加算しない。残りを超えないtick数を3秒相当の有界範囲で乗算比較して求め、`*end_tick=現在tick+残tick`を全出口で返す。fallbackで3秒を再起算しない。
- previewの待ち回数計算・残時間変換も除算補助を増やさない方式へ点検する。通常リンクのmap/未解決参照/逆アセンブルでスプラッシュ由来の`__udivdi3`を除去し、libgcc依存を新設しない。

## 4. 定数・型・内部API
[C4] `docs/POLICY_DEV.md` §2に従いSubsystemの新`gfx/osri_profile.h`を既定B＝ゲスト上限の唯一の正典とする。公開KAPIのShared層は変更せず、poll回数等はLocal層。
`tools/osri_profile.py`が指定整数マクロを厳密に読み、欠落・重複・不正値は失敗、31へのfallbackなし。enc2入口・CLI・資産検査がimportする。比較上限は同モジュールだけに`OSRI_COMPARE_OUT_LIMIT = 47`と定義する。
```c
/* gfx/osri_profile.h */
#define OSRI_DEFAULT_OUT_BUDGET 31u
/* gfx_osri.h: OSRI_COLORS=16, GFX_HEIGHT=400, OSRI_V1_MAX_EVENTS=200 */
#define OSRI_BUDGET_ROW_EVENTS ((OSRI_DEFAULT_OUT_BUDGET + 1u) / 2u)
#define OSRI_IMAGE_ROW_EVENTS \
    ((OSRI_BUDGET_ROW_EVENTS < OSRI_COLORS) ? OSRI_BUDGET_ROW_EVENTS : OSRI_COLORS)
#define OSRI_IMAGE_V2_EVENTS ((GFX_HEIGHT - 1u) * OSRI_IMAGE_ROW_EVENTS)
#define OSRI_IMAGE_EVENTS \
    ((OSRI_IMAGE_V2_EVENTS > OSRI_V1_MAX_EVENTS) ? OSRI_IMAGE_V2_EVENTS : OSRI_V1_MAX_EVENTS)
/* splash_raster_play.h: image/planは所有者のheap、関数は借用 */
struct PlV2Row {
    u8 first, count;
    u8 port[OSRI_DEFAULT_OUT_BUDGET], value[OSRI_DEFAULT_OUT_BUDGET];
};
struct PlV2Plan { struct PlV2Row row[GFX_HEIGHT]; u32 max_outs, total_outs; };
STATIC_ASSERT(OSRI_DEFAULT_OUT_BUDGET == 31u, osri_default_budget_b31);
STATIC_ASSERT(sizeof(struct PlV2Row) == 64u, pl_v2_row_size);
STATIC_ASSERT(sizeof(struct PlV2Plan) == 25608u, pl_v2_plan_size);
STATIC_ASSERT(OSRI_IMAGE_EVENTS == 6384u, osri_image_events);
STATIC_ASSERT(sizeof(struct OsriImage) == 51110u, osri_image_size);
int pl_v2_prepare(const struct OsriImage *image, struct PlV2Plan *plan); /* 0/-1 */
int pl_v2_frame(struct SplashRaster *s, const struct OsriImage *image,
                const struct PlV2Plan *plan); /* OK=0, DROP=1, NO_SYNC=-1 */
/* play.c: 手書きemit表の定義直後 */
STATIC_ASSERT(sizeof(emit_table) / sizeof(emit_table[0]) == OSRI_DEFAULT_OUT_BUDGET + 1u, pl_emit_table_len);
/* include/platform_splash.h: try_image_until宣言は削除 */
int platform_splash_try_image(const char *path, u32 *end_tick);
/* init/boot_splash.c: 既存cleanup/内蔵ロゴ分岐へ接続 */
int rc = platform_splash_try_image(SYS_SPLASH_IMAGE, &end_tick);
```
1成分変更M番号の最小費用2M−1からB31では16番号・6384件となる。OsriEvent=8B、count=u16と添字幅も静的検査する。同値は0 OUTなのでBから一般のv2形式上限は導かず、形式検査は16/6384を維持する。将来Bを下げる場合は容量・同値互換を再設計する。
旧PL_V2_OUT_LIMIT/PL_OBS_OUT_LIMIT・OBS別image型・47列作業配列・image全コピーは置かない。行初期化は64B。ホストAPIはenc2-mt確定入口をそのまま共有し、探索/前処理/packの別実装を作らない。

## 5. ホスト生成・資産・配布
- 取り込み元はWのPACK3 `1700cadc`を基準としたenc2-mt受入済みコミット。`/home/hight/os32-tmp/run/enc2-mt/PACK.md`の完了コミット・試験報告をそろえて固定し、進行中作業木をコピーしない。
- `osri_enc2_native.py`・`osri_enc2_core.c`とplan/tree/spec/timing/dither/pack/decode等の必要依存・1枚生成入口を一体で取り込む。比較器の8通り生成を常用から起動しない。
- `python3 tools/img2osri.py INPUT -o OUTPUT.osri`でenc2/B31/band1を1本出力。明示形は`--encoder enc2 --outs 31 --band 1 --threads N`、threads既定は論理CPU数、1は従来単一スレッド経路。
- 候補評価だけを並列化し候補番号順で集約する。全Bで従来単一スレッドとパレット・順序・J・索引・CRCを含む全バイト一致を要求する。黒合成・縮小・640×400中央配置・RGB444・時刻モデル・符号付き整数FS1回を継承し、FS後にπを変えない。
- 明示B=15/23/31/47を残し、画像による自動B/方式変更はしない。`--hblank-outs 5/6`または`--encoder pl-b`は旧バイト互換、`--obs-colors`は明示観測用。新方式との混用、`--outs`と旧`--obs-outs`の二重指定は拒否する。
- greedy-orderは比較器専用で常用CLIには提供しない。r1のgreedy共有モジュール新設は行わない。再生成はPython/Pillow/NumPy＋Cホストコア/POSIX threadsを必要とし、ゲスト・配布済み資産には依存を持ち込まない。
- 既定原画は`/home/hight/os32-tmp/run/splash-img2/src/sfw_01321_rgb.png`、SHA256=`a5399f3e19b4b03c80b37e43afa93e634e85893185ba88c174d7e5b5b04d196d`。原画待ちはない。N02は旧互換fixtureに残す。
- 期待出力は同runの`out/enc2-b31.osri`、SHA256=`afc3ceb70e9b7dc35115f9cc27b56b41ac05ae8c9a8fabdc84aa4f39dd760bab`、CRC=`16769bea`、131888B/474件/実OUT最大25・総1181。B47出力と同一バイト列。
- 新`tools/raster_splash_asset.py`はINPUT/OUTPUT必須、共通入口で一時生成→独立復号・全行OUT≤31検査→既存基準との全バイト照合→rename。不一致の由来を調査し、期待値を無条件更新しない。既存出力置換は明示指定、原画は不変。
- manifestに原画SHA・確定生成器コミット・引数・threads・ライブラリ版・出力SHA/CRC/bytes/events/OUT最大総数を保存。資産は`assets/splash/splash.osri`と同ディレクトリのmanifest。OSはsidecarを読まない。
- 生成は明示ホスト操作とし、通常makeで取得・再変換しない。配布の画像 `/boot/splash.osri` は手元で道具 (tools/raster_splash_asset.py) が作り、`assets/splash/` に置く。公開リポジトリには入れない (ユーザー決定 2026-11-04「リポジトリに入れない」、.gitignore)。手元にあれば普通の /boot の配備の集合 (HDD の配備・hsync) に入れて更新のたびに上書きし、無ければ配らない (起動はロゴ)。起動は `/boot/user.osri` を先に探し、あればそれを、無ければ `/boot/splash.osri` を表示する (ユーザー決定 2026-11-04「user.osriがある場合そちらを優先」)。user.osri は配備に入れないので、ユーザーが置いた画像は更新で消えない。起動FD/MINIMALには追加しない。
- 差替えは再生成・検査→時刻モデルpreview→常用対応カーネルを候補媒体へ先行配置→画像配置→NP21/W/実機受入→PMによる既存画像退避・差替え→像内読戻しSHA照合。B47実機良好の既報を常用A8完了の代用にしない。
- ロールバックは退避.osriを復元。旧カーネルへ戻す場合は画像も旧受理範囲へ戻す。新ファイルの旧カーネルでの再生は保証せず、拒否時は内蔵ロゴへ戻す。

## 6. 容量と準備時間
| 対象 | 上限・見積もり・測定条件 |
|---|---|
| ファイル | 399×16=6384件、128096+6384×8=179168B。新資産131888B。画像＋退避は最大358336B＋manifest/FS端数（新資産2枚なら263776B）。 |
| heap payload | image51110B＋plan25608B=76718B（約74.9KiB）、128KiB枠の残り54354B。r2の89518Bから12800B/14.3%減。管理領域・丸めは別途実測。 |
| 行/plan/BB | 行96→64B、plan38408→25608B（33.3%減）。既存BB128000B込み204718B、BB追加確保なし。imageは6384件を保持し新画像474件へ固定しない。 |
| text/kernel | 通常text/rodata増分は暫定32KiB枠、旧核削除の減分を先取りしない。emit縮小の圧縮量は未測定。508KiB=520192B上限をmap/圧縮像で確認。 |
| 媒体 | 両FDの空き≥64KiBを`tools/tests/test_packages.py`で実測、割れたら重複削減し基準を緩めない。kdebugへ機能追加0B・FD除外維持。同像からpaired moduleを再生成しCRCを混在させない。 |
ゲストprepare秒数はr2/r3/r4とも未測定。過去の通常238734B/OBS249359Bも今回の実測値ではない。ホストenc2探索時間・S4 preview間隔をprepare時間に代用しない。
同入力の簡略モデルはT2=A+38408/R、T4=A+25608/R秒（Rは初期化B/s、Aは検証・差分生成）。仮定R=1000000なら差−0.012800秒、10倍なら−0.001280秒でありRa266実測ではない。
新画像474イベント・1181 OUTは不変。最悪OUT総数は399×47=18753から399×31=12369へ減るが、400行走査・6384件検証は残り、全体時間が31/47になるとはしない。
同一Ra266/同入力でr2相当とr4のprepare入口〜出口をIF開放下で計時し、新画像・6384件・32超過入力を複数回、中央値/最大/分解能を秒で報告する。load/prepare/VRAM転送/preview/再生を別計測し、短いprepareは診断でのみ反復する。見積もり値を期限計算には使わない。

## 7. ファイル割当て・受入・実装段
| 所有ファイル | 変更内容 |
|---|---|
| +`gfx/osri_profile.h`、`gfx/gfx_osri.h`・`.c` | B正典・共通RAM型・v1維持/v2共通検査。宣言と本体は各ファイルで分担。 |
| +`drivers/video/pc98/splash_raster_play.h`・`splash_raster_plan.c`・`splash_raster_play.c`（後二者も同ディレクトリ） | 型/API・追記前検査・S4走査/0..31手書き展開。 |
| `drivers/video/pc98/splash_raster_image.*`、`kernel/time/ktime.*`・`time_splash_math.*` | 旧核/認定/専用時間処理削除、PlRow/preview保持、参照と登録を整理。 |
| `platform/pc98/splash_image.c`・`splash_obs.c`、`include/platform_splash.h`、`init/boot_splash.c` | 常用所有・残時間・preview、OBS共通化、引数変更とtry_image_until削除、呼出結線。 |
| +`tools/osri_profile.py`、enc2依存群、`tools/img2osri.py`、+`tools/raster_splash_asset.py` | 正典読取/比較47、受入済み並列入口、既定結線、1枚検査・manifest。 |
| `build/kernel.mk`・`build/packages.yaml`、手元の資産 2 ファイル (.gitignore)、PL/ENC2正典・TASK_RASTER_SPLASH | PM直列で新.c/削除登録・/boot の配備の集合への splash.osri の追加・FD除外・契約/段状態を更新。emit生成規則は追加しない。 |
| `tools/tests/test_osri.py`・`splash_obs_host.c`・`test_splash_obs.py`・+`test_splash_v2.py` | 形式・共通I/O模型・OBS駆動・通常所有/時間/CLIを検査。 |
| `tools/tests/splash_raster_host.c`と起動スクリプト/CHECK、`time_splash_math_host.c`と対応登録 | 旧核/認定/専用時間APIの試験・stub・mutation anchorを削除。有効なpreview/IF/同期/cleanup試験を共通核・通常所有試験へ移す。DIAG probe試験は残す。 |
1機能1ファイル、同一ファイル/宣言/結線/登録/生成物は直列。大きい旧再生ファイルへ新機能を追記しない。
| 受入 | 試験と対応変異（正常GREEN→変異RED→復元GREEN） |
|---|---|
| A1 形式 | v1 200/201・昇順/同値、v2 1198/6384/6385・任意順/重複/同値・CRC/短読/余剰/band。旧上限復活・seen/CRC削除を検出。 |
| A2 準備 | 全400行first/count/port/valueを独立復号と照合。16番号1成分=31許可、1成分追加32拒否、47/48拒否、同値16件=0、空行/持越し/7成分/v1超過/canary。47復帰・等号拒否・追記後検査を検出。 |
| A3 共有走査 | 独立時計で先行番号low/G-R-B順/空行/H再武装/VSYNC DROP/poll停止/IF全出口/次周初期復元。通常OBS列一致、32..47未リンク。先行/H再武装/VSYNC/IF削除を検出。 |
| A4 所有 | 非0件が再生、固定互換、32超過で転送0/全解放/常用ロゴ・OBS prepare、転送後失敗cleanup、try_image_until参照0。count拒否再導入・free欠落を検出。 |
| A5 期間 | 31/24k、残り0/preview不足/読込超過/全DROP/IF=0/NO_SYNC/tick停滞/tickのwrap/fallback残時間、preview1回と経過控除。preview削除・無課金・DROP無課金・3秒再設定を検出。64bit除算補助/新libgcc依存なしをリンクでも確認。 |
| A6 生成/定数 | 無指定＝明示B31＝基準SHA、1本出力・新資産474/25/1181・旧5/6互換・引数競合。小fixture/指定2原画B31 threads=1/2/4/16一致、全Bの従来一致・core試験・完了順集約/候補欠落変異・1/4/8/16時間表をenc2-mtから受領。既定5/B23・threads誤渡し・OBSだけ47・旧47配列を検出。正典変更時はPython値連動とCのB==31によるビルド拒否を確認。 |
| A7 配布/容量 | 生成→候補媒体→読戻しSHA、既存画像保持・FD除外・heap実使用/prepare秒数・通常OBS map/508KiB/FD空き/paired CRC。上書き・FD資産混入を検出。 |
| A8 表示/時刻 | PMがNP21/W通常起動→約2秒preview→動的表示終了→shell、Ra266/31k写真/serial、画像なし/CRC破損ロゴを確認。ノートの外部時計とserial時刻差で§8の遅れを測る。24kは機会時に別記し、host合格を実機合格としない。 |
S5a=形式/準備/共有再生・旧核撤去（A1–3）、S5b=常用所有/preview/期限結線（A4–5、U1に応じた条件は§8）、S5c=ホスト既定化（A6）、S5d=資産/配布/実機（A7–8）。
S5cはS5aと独立だがenc2-mt受入コミット固定が関門。S5dは原画待ちなし、S5b/c受入後。S5aの型/読込/再生は一体受入し半更新を配備しない。各段の受入は10項未満。
実装者は該当像ビルド・check-changed MUTATE=0・変更試験--mutate・CHECK lintをcheck_slot経由で実行。CHECK入力に正典ヘッダ/Python読取器/新旧変更ソースを登録し、旧anchorを更新。OBS「通常text不変」は共通列一致へ置換。PMが統合ビルド/媒体/ゲスト受入を行う。

## 8. PIT の tick の遅れ (U1、ユーザー決定 2026-11-04「補正しない (推奨)」)
補正しない。tick_count へフレームの推定値を足さず、遅れは許して受入の記録に数字を残す。
| 項目 | 内容 |
|---|---|
| コード | S4 の核のまま、§3 の有限フレーム/残時間の制御。tick_count へフレームの推定値を加算しない。旧 ktime_splash_* は削除。 |
| 期限と失敗 | preview は IF を開けて、経過を実 tick で差し引く。動的区間は tick とモデルの大きい方。全部の出口で残時間を返す。 |
| 受入 | A5 は tick が止まっても有限に終わる。A8 で壁時計との差を記録する。 |
| 段 | S5a・S5c は進めてよい。S5b を受け入れてから S5d へ。 |
未実測の概算: 31kで周期約14.2ms・IF=0約13ms・100Hz tickなら、1フレーム約1.4 tickに対し記録約1 tick。動的再生3秒なら遅れ約0.9秒、24kなら約1.3秒とレビューは推定している。IRQ位相・同期待ちで変わる値で保証ではない。
本設計は画像経路の8秒内に約2秒previewを含み、動的再生は最大6秒とする。実際の動的時間を併記し、3秒すべてがCLI区間であるかのように扱わない。
A8では外部ノートの単調時計でserial受信時刻を付け、load前/preview前後/再生前後/shellでゲストtickを出す。外部経過−ゲスト経過を画像なし基準との差でも比較し、31k/可能なら24kを複数回測る。serial遅延・測定分解能・中央値/最大を記録し、可視ループ内へログを入れない。
enc2-mt完了コミット固定・容量/準備秒数実測・実機受入は作業関門であり、新たな仕様未決ではない。

反論: 0件（F1〜F9をすべて採用）。

## 確定 (2026-11-04、Opus 第 2 回の条件付き Approve、PM が反映)
- G1: libgcc の受入はこの段で足す・変える関数に限る (この段の関数に 64bit の割り算を足さない)。preview_rounds (splash_raster_image.c:502-507) は今も割り算をしているので、残すこの段で割り算を外す。platform/pc98/splash.c の `__divdi3`・`__udivdi3` はこの段の受入に入れない (libgcc を外す別の段)。§3 の該当の文はこれに読み替える。
- G2: §4 の STATIC_ASSERT は実物のマクロ (include/types.h:57) の 2 引数の形に直した。
- G3: time_splash_math を消すときに参照を直すのは tools/tests/test_time_splash_math.py、tools/tests/test_dep_dir_b_timer.py、tools/checks/test_run.py、check-select-host.py、build/kernel.mk の 5 つ (登録の整理は PM が直列で行う)。
- G4: 残す pl_preview は、内部の preview・preview_vsync の引数から PlProfile と deadline を外し、有限の poll だけにする。
- §8: U1 はユーザー決定で補正しない案だけにした。
