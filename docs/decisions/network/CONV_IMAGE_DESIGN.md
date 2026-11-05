# CONV_IMAGE_DESIGN — os32.conv.image (ホストで画像を OS32 の資源に合わせた形式へ変換して返す)

> 出所: ~/os32-tmp/run/conv-image/DESIGN.final.md (astra 起草 r1→r2、Opus レビュー 2 回、G1〜G5 は PM が反映)。
> 状態: **現行 (2026-11-05)** — 段の状態は票 TASK_NET_HOSTSVC が持つ。実装はまだ。

## r1→r2 の変更
- F1: Cirrus の現状と wt/cirrus-gray の修正を明記し、全装置共通の既定パレットを前提化。
- F2: index_mask、16色の予約0/7、packed8のfirst=16・colors=240とlease手順を追加。
- F3: 全画像SDKをhost_open/status経路に統一し、生statusと上限エラー詳細を保持。
- F4: MEDIANCUT・kmeans=0を固定し、対象indexと透明を扱うnumpy製FSを定義。
- F5: LGY-98の100～300KB/sによる128000B・307200Bの所要秒数を追加。
- F6: READのパレット省略を既定にし、palette_bytes=0と再送指定を定義。
- F7: 静的OSRIのversion=2・band=4・flags=0を明記。
- F8: fileの絶対パスと相対パスのagent cwd基準を明記。
- F9: 自前上限を設定の既定にし、根拠と失敗の報告を追加。
- F10: wildcard収集のためB4を削除し、B2に未実装の共通S1依存を追加。
- F11: 受入表の冒頭で全変異の実行時GREEN→REDを必須化。
- PM: 初版は静的OSRIで確定し、未決を0件に変更。
状態: 設計中。2026-11-05。NET_HOSTSVC_DESIGN の要求行・KAPI・LIST・世代・HOSTDOWN に従う。
## 1. 実物の確認
| 実物（この木を確認） | 設計への反映／食い違い |
|---|---|
| `tools/host_agent.py:794–795,808–829` は GET の第2語を取得先にするだけ | **差異1**: `GET --as text\|mgx` は未実装。手本にできるのは GET/非同期取得の境界のみ。旧GETへ変換オプションを黙って足さない。 |
| `tools/img2mgx.py:11,52,480–536`、`userland/lib/mgx/libos32mgx.h:25` | **差異2**: 冒頭の「32B・1bpp」と違い実装は48B、1～4面、16段階グレー。既存 encode/decode を使う。 |
| `gfx/gfx_osri.h:8–20,29–37`、`gfx/gfx_osri.c:30–36,65–95` | **差異3**: 任意サイズ形式ではない。640×400、4面128000B、RGB444、CRC、イベント。lib/にOSRI読込はなくgfx/が所有。 |
| `tools/img2osri.py:2,23–32,204` | 既定enc2はラスタ色更新。入力は黒合成・中央配置。直接コピー用の静的16色出力と区別する。 |
| `gfx/palette.c:15–33` | 16色は index=B+2R+4G+8I、暗7/明15、8も黒。VGA標準色とは異なる。 |
| `drivers/video/pc98/backend_pegc.c:330–355`、`drivers/video/geforce/backend_geforce.c:344–365` | 256色は下位16システム色、16～231はRGB順6³立方体(0,51,…255)、232～255は灰8+10k。 |
| `drivers/video/cirrus/backend_cirrus.c:112–113,271–272` | CirrusもPACKED8。現状の灰は6bitの2+2kでPEGCと不一致。PMの別修正 `wt/cirrus-gray` でPEGCへ統一することが本設計の前提。全装置で共通の論理既定色を使い、DAC精度差はバックエンドに従う。 |
| `docs/05_drivers.md:123–141`、`drivers/video/geforce/backend_geforce.c:543` | **差異4**: 文書の3バックエンド表にはGeForceがないが実装はPACKED8を持つ。drivers/videoにTrident実装なし。機種名でなく面形式で要求する。 |
| `docs/05_drivers.md:131,143`、`docs/decisions/gui/OSRI_VIEWER_DESIGN.md:14–20` | 面間隔32000Bと現在の高さは別。CPL3はクライアント面を借りHALでpresent、表示VRAMの物理番地を渡さない。 |
| `docs/decisions/network/NET_HOSTSVC_DESIGN.md`「アプリの口」「サービスの一覧」 | 共通基盤は確定設計だがこの画像サービスは別設計枠。実装済みとは扱わない。 |
## 2. 要求の形
- 名前は `os32.conv.image`。os32. は OS32 標準、ske-studio が定めた。LISTは提供可能時だけ `os32.conv.image 1 <世代内id> builtin` を追加する。
- 要求行は短いASCII、画像指定は本文へ置く。URL・空白入りパス・固定256色が要求行の上限を超えない。パースはshell非経由。
```text
os32.conv.image CONVERT
os32.conv.image PREPARE
os32.conv.image READ <generation> <token> rows <y> <h> [palette=0|1]
os32.conv.image READ <generation> <token> tile <x> <y> <w> <h> [palette=0|1]
os32.conv.image READ <generation> <token> bytes <offset> <length>
os32.conv.image RELEASE <generation> <token>
# CONVERT/PREPARE本文 = json_bytes:u32LE + UTF-8 JSON + source_bytes
{"src":"body","format":"raw","layout":"packed8","width":320,"height":200,
 "fit":"contain","palette":"os32-256","colors":256,"max_bytes":65536}
```
- CONVERTは完成物を1応答、PREPAREは同じ変換をホストに保持しメタデータだけ返す。READは再変換せず取り出す。RELEASE成功は空本文。
- JSON長の設定既定上限16384B（超過413）、未知キー・重複キー・不整合な組合せは400。入力長は転送層の宣言長と一致させ、src=bodyではJSON直後の全bytesを画像とする。
| JSONキー | 値／省略時の既定（v1） |
|---|---|
| src / source | `file\|url\|body`、src必須。fileはホストの絶対パス、相対パスはagentのcwd基準。urlはhttp(s) URLをsourceに指定。bodyではsource禁止。ソース許可リストは作らない。 |
| format / layout | `raw` / `planar4` が既定。format=`osri\|mgx\|raw`、rawのlayout=`planar4\|packed8\|plane1`。plane1は1面の生ビット列。 |
| width / height | 出力キャンバス各640/400。各1以上、上限4096は設定の既定。MGXは640×400以下、OSRIは640×400固定。rawは画面より大きくてもよい。 |
| fit / upscale / anchor | `contain\|cover\|stretch` / false / `[500,500]` (各0～1000)。余白はbackground。coverは中央切抜き、anchorで位置を指定。 |
| resample / pixel_aspect | `lanczos\|nearest` / `[1,1]`。resample既定lanczos、pixel_aspectは出力画素の物理横:縦、各1～4096。 |
| colors | raw planar4/osriは16、packed8はos32-256で256、optimized/fixedで240、plane1は2、mgxは16が既定。2～形式上限、mgxのみ2/4/8/16。色数はfirstからの候補窓の長さ（予約色を含む）、実使用数はその上限。 |
| palette / fixed | `os32-16\|os32-256\|optimized\|fixed\|gray`。既定はplanar4/osri=os32-16、packed8=os32-256、plane1/mgx=gray。fixedはRGB三整数の配列。 |
| palette_bits / first | 各成分4または8bit、既定4 / 開始index=0、ただしpacked8のoptimized/fixedは16。fixed値は0～(2^bits−1)。最適化色は指定精度へ丸めてから量子化する。 |
| index_mask | 変更を許すindex。16色はu16整数、既定65406（0xFF7E=1～6・8～15）。packed8はu32整数8個の配列、bit iは要素i/32のbit i%32、既定は16～255（先頭0xFFFF0000、残り0xFFFFFFFF）。SDKはu32[8]で保持。optimized/fixedの窓と交差させる。 |
| dither / background | `none\|fs`、既定none / RGB888 `[0,0,0]`。fsはFloyd–Steinberg。MGXも自動網点化せず指定通り。 |
| alpha / threshold / transparent_index | `flatten\|index` (既定flatten) / 128 (0～255) / 省略。indexではtransparent_index必須、alpha<thresholdを透明にする。 |
| stride / order | rawのみ、既定0=最小行長 / `top\|bottom` (既定top)。strideは最小以上、上限65536は設定の既定、paddingは0。plane間隔はstride×返却高さ。 |
| codec | mgxのみ `stored\|zlib`、既定stored。zlibは圧縮して小さくなる場合のみ使用。raw/osriは無圧縮。 |
| osri_mode | osriのみ `static` が既定。ラスタenc2は初版では受理しない（400）。既存OSRI v2のイベント0で出力する。 |
| max_bytes / max_pixels | 完成物（外側80Bと外側パレット含む）上限、既定1MiB / 入力展開画素上限、既定32Mi画素。超過413、黙った縮小・減色はしない。 |
- サーバ既定上限は入力64MiB、入力32Mi画素、出力16Mi画素/64MiB、処理120秒。設定で変更可、要求値との小さい方を使う。運用上限は設定層、wire定数は公開契約層、作業バッファは実装層に置く。
- JSON 16384B: 固定256色等を収めるための設定の既定。超過413、JSON構文不正は400。
- 入力64MiB・32Mi画素、出力16Mi画素/64MiB、寸法4096・stride65536、max_bytes既定1MiB: メモリ/転送量の運用設定の既定で、物理制約ではない。超過413。形式固有寸法・整数幅は契約上限。
- cache 256MiB: ディスク使用量を抑える設定の既定。未pinをLRU排出し、必要量を確保できなければ503。
- pin 8件: 同時保持量を抑える設定の既定。超過503、既存pinは追い出さない。
- 無使用60秒: 放棄token回収の設定の既定。失効後READは410、RELEASEの冪等性は維持。
- 処理120秒: worker占有時間を抑える設定の既定。超過504で終了し一時物を掃除、SDKはTIMEOUTへ対応付ける。
- 上限起因の失敗本文はUTF-8 JSON `{ "error":"limit", "limit":"max_pixels", "configured":33554432, "requested":40000000 }` の形（判明値）。SDKは構造化詳細を保存し、アプリはユーザーに上限名・設定値・要求値を報告、黙った再試行はしない。
## 3. 応答とそのまま写す境界
```text
# link RESPONSE は既存status/length。200の本文だけが下記。全整数LE、構造体cast禁止。
# 80B = magic[4]="OCI1" + u32×19（次の順序）
version=1, format, width, height, x, y, w, h, stride, planes,
order, palette_bits, palette_first, palette_count, transparent_index,
palette_bytes, payload_bytes, total_bytes, token
# format: 1=planar4,2=packed8,3=plane1,4=osri,5=mgx; order:0=top,1=bottom
# transparent_index=0xffffffff は不透明。token=0 は保持なし。
# 続き: palette[palette_bytes] + payload[payload_bytes]。末尾余剰禁止。
```
- width/heightは完成キャンバス、x/y/w/hは切出し領域。CONVERTは全域、PREPAREは全域のstride/planesを返しpayload_bytes=0。total_bytesは完成ファイル／全画素部のみの長さ。
- ファイル形式の外側stride=0,planes=0,order=0,palette_bits/count/first/bytes=0、transparent_index=0xffffffff。rawのplanesは4/1/1。全体raw長はstride×height×planes。
- PREPAREは実パレットも返す。READ rows/tileは同じindexを保持し、palette省略または0ならpalette_bytes=0（count/first/bitsは元の値）、1なら実パレットを再送する。SDKはPREPAREのパレットをtokenに結び付けて保持する。bytesはosri/mgx専用、x/y=0,w/h=全域、palette_bytes=0、payloadは指定範囲、offsetは要求側で管理。
- tokenはホスト世代内の非零u32、世代内再利用なし。SDKはLISTのgenerationと組で保持。再起動・消去は410、SDKでSTALE。READ/RELEASEは要求のgenerationも検査し、不一致は410。RELEASE再実行は同世代なら成功。
| 形式 | payloadとパレット／描画先 |
|---|---|
| raw planar4 | B,R,G,Iの順に4面。各行MSBが左、最後の余剰bitは0。各面stride×h。RGBパレットは別領域。面へ行コピー後present。 |
| raw packed8 | 1byte=index、各行stride。Cirrus/PEGC/GeForce等のPACKED8クライアント面へ行コピー。Tridentも将来同じ能力を公開すれば同経路。 |
| raw plane1 | MSB左の1面。2色のindexは0/1固定。受け手が選んだ1面へコピーする用途、他の3面は受け手が初期化済みであること。 |
| osri | 既存64Bヘッダ+32B RGB444+4×32000B、version=2、band=4、flags=0、event_count=0、計128096B（gfx/gfx_osri.c:62–72）。CRC等は既存仕様通り。外側paletteは無し。保存後既存OSRI読込／表示口へ渡す。 |
| mgx | 既存48Bヘッダ（32～47はグレー0～15）+1～4面、stored/zlib。既存encodeを利用しgraycode=Falseで番号を維持。外側palette無し。既存MGX decode/blitへ。 |
- rawパレットはindex昇順RGB3byte（4bit値も各1byte）、送付時count×3B、省略READは0B。os32既定256色はF1修正後の共通論理RGB888を返し、再設定不要。DACのビット精度による丸めはバックエンドの責務。固定／最適化は現在のKAPIで設定可能な4bitを使う。
- os32-256選択時はpalette_bits省略で8へ、明示4は400。fixed/optimizedの8は将来の8bit設定口向けで初版400。osri/planar4は4bit、mgxはgrayの4bitのみ。互換不能な指定は400。
- ファイル形式は復号／再生の費用が残る。**色変換も展開も不要**な口はrawまたはMGX storedの面コピー。圧縮MGXを無変換転送と称しない。
- SDK転送補助は既存surface_query/leaseからpitch・面間隔を得る。コピー先=p[p]+(dst_y+y)*pitch+byte_x。受信のplane間隔を32000と決め打ちしない。
- planarのコピー先xは8画素境界、右端非整列は面の右端のみ許可。任意xを要する呼出しはホストで余白を含む領域に変換する。packed8は任意x。SDKはVRAMレジスタを操作しない。
- パレット変更はindex_maskと候補窓の交差の連続区間ごとに既存leaseを取得（16色既定は1～6と8～15、packed8は16～255）。全区間を取得してから設定/描画し、途中失敗は取得済みleaseを返して描画前に失敗する。0/7は変更せず既定色で量子化する。共有GUIではos32既定または借りた範囲のfixed/optimizedを要求する。
## 4. 減色・パレット・透過
- os32-16/256は§1の表と順序を厳密再現。起動後に変更された色は「既定」ではないため、アプリが現在の色をfixedで送る。
- first＋colorsは16/256以下。既定パレットはその連続範囲のみ使用し変更しない。optimized/fixedで16色のmaskに0/7、packed8で0～15が立つ指定は400。候補窓内でmask外の色は既定値のまま候補に残す。fixedはcolors件必須でmask外の値が既定と違えば400。
- optimizedは不透明画素をRGBとしてPillow `quantize(colors=N, method=MEDIANCUT, kmeans=0, dither=NONE)` で選色する（Nは変更可能index集合からtransparent_indexを除いた要素数）。N=0なら選色を省略。選んだ色はindex昇順へ配置、4bit精度へ丸めてから、変更しない既定色も含む候補へ全画素を再割当。同距離は小index。
- rawの外側パレットは候補窓を連続して返すのでmaskの穴も既定色で含める。Infoに要求index_maskを保持し、SDKはmask内だけ設定する。静的OSRIの内部16色も0/7を既定色で維持し、表示側は同じlease制約に従う。
- grayは均等グレー（4bitで0～15）を使う。MGXは常にグレースケール、first=0、colors=2^bpp、透明なし。RGB固定色やos32既定は400。
- fsは全キャンバスの左→右、上→下で7/16,3/16,5/16,1/16を拡散。noneは最近色。タイルごとに最適化・誤差初期化しない。PillowのFSは使わずnumpyで実装する。tools/img2mgx.pyのquantize_to_levelsを手本に、指定精度をRGB888へ展開した候補集合で距離と誤差を求め、透明への誤差は捨て、境界で再配分しない。OS32には数値変換を置かない。
- flattenは背景へalpha合成後に縮放。indexは透明画素をreserved indexにし、そこへ誤差を渡さず不透明画素もそのindexを使わない。transparent_indexは候補窓内で必須、残りの候補数はcolors−1。
- indexモードはrawのみ、threshold以上は背景合成済み色として量子化。縮放はpremultiplied alphaで行い縁の黒ずみを防ぐ。
- 透過indexのコピーだけでは背景は透けない。既存の透過blitで使うか、既知背景へflattenを選ぶ。直接面コピーの既定はflatten。
## 5. 大きさ・分割・帯域・時間
- EXIF向きを正規化、複数フレームは先頭。sRGBを基準としICCがあればPillow ImageCmsで変換、タグなしはsRGB。変換不能プロファイルは422。
- pixel_aspect=a:bなら入力Wi×Hiの表示比に対し出力格子の目標比はWi*b:Hi*a。containはmin、coverはmaxの倍率、stretchは各軸独立。upscale=falseは各倍率を1で上限化。
- 結果寸法はfloor、最低1画素。余剰／切出し差×anchor/1000もfloor。coverで拡大禁止のため足りない辺は背景で埋める。指定キャンバス寸法は常に維持する。
- PREPARE後、READ rowsは全幅のy/h、tileは任意矩形。rawだけ許可、planarのxは8境界（wの端は0bit埋め）。全画像の減色結果から切り出すので分割有無で画素一致。
- tileのstrideは指定stride（0ならtile最小長）を用いる。order=bottomは切出し内部の行順のみ反転、x/yは常に左上座標。plane1も同様。
- OSRI/MGXはREAD bytesで4096B等に区切りファイルへ保存する。任意byte位置を画像のタイルと解釈しない。rawの転送既定も4096B程度、80B（palette=1時のみパレットも）を含めcap内に収める。
- 読出しはランダムアクセス・再要求可。同じtoken/範囲で同じbytes。範囲外416、切詰め成功なし。1行がcapを超す場合はtileへ分ける。
- 640×400 planar4=128000B、640×480 packed8=307200B、16色palette=48B、256色=768B。時間見積りは転送bytes/実測有効bytes毎秒＋変換時間＋要求往復回数。
- LGY-98の100～300KB/s（REVIEW.r1.md F5が引用するHOST_SERVICES_PLAN.md:118）での目安。KB=1000B、画素のみ、変換・往復・ヘッダ・PREPAREパレット分は別途加算。
| 画素量 | 100KB/s | 300KB/s |
|---|---|---|
| 128000B（640×400 planar4） | 1.280秒 | 0.427秒 |
| 307200B（640×480 packed8） | 3.072秒 | 1.024秒 |
- 低速時に色数やサイズを勝手に変えない。アプリがmax_bytes・寸法・codecを変えて再要求する。キャッシュ命中でも回線転送量は減らない。
- 子プロセス変換中も親agentがSTATUSへPROCESSINGを返す。1000ms無応答はHOSTDOWN、SDK全体期限はTIMEOUT。READごとの期限と全体deadlineを区別し残り時間を渡す。
- 通知メールボックスへ画素・進捗を流さない。初版に専用NOTIFYは不要。ALIVE世代変更でSDK token/LIST cacheを捨て、利用者の指示なしに再変換しない。
## 6. ホスト実装・SDK
```python
# tools/host_conv_image.py: agentは入口へ委譲、workerは純粋変換関数を呼ぶ
start(rid, verb, args, body_spool, generation)  # Jobまたは応答
poll(job, now)                                # 未完/完了(status, body_reader, size)
convert(spec, source_file)                    # Artifact(path, metadata, palette)
# READはseek可能なspoolを既存_answer(reader=...)相当へ接続する。
```
- host_agent.pyはサービス分岐、本文完了hook、poll/close、LIST登録だけ。既存GETのsubprocess方式を手本にPython workerへ渡す。入力/出力はdisk spool、主loopでPILを呼ばない。
- 子のstdout/stderrを無制限にためず、小さい完了情報とspoolを受ける。close/期限切れはworker終了と一時物掃除。HTTPは既存取得経路相当を再利用、fileは直接open、入力allowlistなし。
- キャッシュkey=入力bytesのSHA256＋正規化全設定＋変換器版＋Pillow版。URLも毎回取得して同一性を判定し、URLだけでは再利用しない。fileも開いた内容のhashで判定。
- ディスクcache既定256MiB LRU。PREPARE最大8件をpin、READ/発行から無使用60秒で解除、超過503。pin中のものを追い出さない。tokenは完成変換のsnapshot、後の元ファイル変更で揺れない。
```c
/* 公開C89。新KAPI不要。以下はSDK提案、整数型は既存SDKのu32等。 */
typedef struct Os32ImageRequest Os32ImageRequest; /* §2各項目を保持、initで既定 */
typedef struct Os32ImageInfo Os32ImageInfo;       /* §3をdecode、wire castなし */
int os32_host_conv_image(const Os32ImageRequest *r, const void *src, u32 n,
                         void *out, u32 cap, Os32ImageInfo *info, u32 timeout);
int os32_host_image_prepare(const Os32ImageRequest *r, const void *src, u32 n,
                            Os32ImageInfo *info, u32 timeout);
int os32_host_image_read(const Os32ImageInfo *image, u32 x, u32 y, u32 w, u32 h,
                         void *out, u32 cap, Os32ImageInfo *part, u32 timeout);
int os32_host_image_bytes(Os32ImageInfo *image, u32 off, u32 n,
                          void *out, u32 cap, u32 timeout);
int os32_host_image_release(Os32ImageInfo *image, u32 timeout);
```
- 全画像要求はhost_open/write/status/read/closeを直接使う（sdk/include/os32/os32_kapi_generated.h:217–221）。host_callはstatusを返さないため使わない。host_statusで生statusを取得してからエラー対応付けし、全終了経路でcloseする。出力capには80B＋paletteを含む、成功は応答byte数。prepare/releaseは0成功。Infoはpalette[768]・index_mask・generation・raw_status・上限詳細も持ち、READはpart、bytes/releaseはimageへ結果を保存する。応答未取得時raw_status=0。寿命は呼出し元所有。
- 全入力を同じserializerから逐次host_writeへ流し、JSONと入力を連続した巨大領域に複製しない。待ちはyield、同一全体期限、追加メモリはJSONと小転送bufferのみ。
- SDK errors: 400/416/422→INVAL、413→NOSPC、404→IO（ホスト入力不在、詳細は生status）、410→STALE、503→IO、502→IO、504→TIMEOUT。生statusもInfoへ保存。未完AGAIN、HOSTDOWN/TIMEOUTは共通層の値をそのまま返す。
- 壊れた長さ／積のoverflow／応答format不一致はIO。先にヘッダと必要量を検査し、cap不足をNOSPCにする。転送中失敗は部分出力を有効画像と扱わない。
## 7. ファイルごとの変更（各行＝1段＝1ファイル＝1実装者）
| 段 | ファイル | 所有する関数・状態／依存 |
|---|---|---|
| A1 | 新 tools/host_image_contract.py | JSON検証・OCI1 encode・定数、他のホスト段の前提 |
| A2 | 新 tools/host_image_palette.py | 既定色・固定/最適化・量子化、A1 |
| A3 | 新 tools/host_image_geometry.py | 向き・ICC・alpha・縮放、A1 |
| A4 | 新 tools/host_image_encode.py | raw/静的OSRI pack・MGX既存encode adapter、A2/A3 |
| A5 | 新 tools/host_image_store.py | spool/cache/token/切出し、A1/A4 |
| A6 | 新 tools/host_conv_image.py | worker取得・変換・job状態、A1～A5 |
| A7 | 改 tools/host_agent.py | dispatch/body/poll/LIST、A6と共通host基盤完成後 |
| B1 | 新 userland/lib/host/libos32host_image.h | §2/3完全構造体・初期化・SDK宣言（共通構造体を集める例外） |
| B2 | 新 userland/lib/host/host_image.c | serializer・応答検査・全SDK関数、B1/A1およびNET_HOSTSVCの未実装S1（host_call.c）の完了が前提。共通期限/エラー契約を揃え、画像通信はKAPI直呼出し |
| B3 | 新 userland/lib/host/host_image_copy.c | 面コピー/palette lease補助、B2、既存HALのみ |
| B5 | 新 sdk/rust/os32api/src/host_image.rs | C相当の用途別API、共通host.rs上、B1と契約一致 |
| B6 | 改 sdk/rust/os32api/src/lib.rs | module公開、B5 |
| T1 | 新 tools/tests/test_host_image.py | host golden/実行時変異、自身のCHECKに全入力、A1～A7 |
| T2 | 新 tools/tests/host_image_sdk.c | C89とSDK fake transport/面guard検証、B1～B3 |
| T3 | 新 tools/tests/test_host_image_sdk.py | T2ビルド・qemu実行・変異・CHECK、T2 |
| T4 | 新 userland/tests/host_image_test.c | NP21/W/実機で取得→表示→復帰、B2/B3 |
| D1 | 改 docs/decisions/network/NET_HOSTSVC_DESIGN.md | 採用後にサービス境界・受入結果を反映、全段後 |
- B4は不要: build/libs.mk:62がuserland/lib/host/*.cをwildcard収集する。
- 1ファイル約500行以内。既存巨大CLIを複製せず公開関数をimportする。新テスト/guest像のビルド登録が別ファイルなら各1段を追加し、同一ファイル段は直列。今回は設計のみで上記ファイルは変更しない。
## 8. 受入 → 試験・変異・段の順
全変異は同じ試験を変更前に実行してGREEN、1行変異後に実行してRED、復元後GREENを確認する。ビルド失敗だけをREDに数えない。
| 受入 | 試験／1行変異（実行時GREEN→REDを必須） |
|---|---|
| 全形式を既存側で読める | host: OSRI既存loader・MGX既存decoderとgolden比較／plane順を反転する。 |
| 色と透明を要求通りにする | host: 16/256既定・固定・最適化・fs/none・透明境界・0/7保持とmaskの穴・packed8 lease／暗色7を8へ変える。 |
| 分割が一括と同じ | host: 全行/非8倍幅/左右tile/bottom/padding・fs連続性・READパレット省略/再送／切出し時に再減色する。 |
| 寸法と上限を厳守 | host: contain/cover/stretch・非正方画素・拡大禁止・max_bytes境界／倍率の上限1を外す。 |
| 低メモリで逐次転送できる | SDK: 413/410/416/504の生statusと対応エラー・上限詳細の保存・4KiB cap・面間隔32000・200行・guardと総byte数／plane間隔を現在高さ×pitchへ変える。 |
| 世代と通信断を区別する | host/SDK: PROCESSING>1秒、無応答、全体期限、再起動、cache pin/失効／旧generation検査を除く。 |
| 既存経路を壊さない | host: GET/TIME/LIST、8要求併走・ALIVE継続／workerを主loopで同期実行する。 |
| 画面に同じ絵が出る | NP21/W: PC98の色票・斜線・余白・MGX/OSRI・CUI復帰／presentを省く変異で画面hash不一致。 |
| 実機の色と時間が合う | 実機: PC98/Cirrus/PEGC/GeForceの利用可能機で色票、転送量、変換ms/転送ms/RAM測定／RGBのR/Bを交換する。Tridentは実装後の別受入。 |
- 順序=A1/B1契約→A2～A6→A7→B2/B3/B5/B6→T1～T4→NP21/W→実機→D1。テスト作成と対象段は対応付け、各変異anchorは一意・行移動時追従。
- 実装時は依頼BASEでpreflight、触った像、check-changed MUTATE=0、対象変異、lint。統合の全build/全変異はPM。NHD停止・配備と実機物理操作は既存D1/D2手順。
- 今回は読み取りと本票作成だけ。ビルド・preflight（fonts変更を伴う）・試験・git操作は実施しない。
## 9. 未決（ユーザー判断）
- 0件。PM決定（2026-11-05）: 初版は静的OSRI。ラスタenc2は別設計・別段とする。
反論: 0件。

## 確定 (2026-11-05、Opus 第 2 回、PM が反映 — 本文と食い違う所はこの節を正とする)
- G1 (P2) §3 のパレット変更: SDK は gfx_screen_info の lease_mask/first/count で全区間を先に検査し、全部通るときだけ区間ごとに gfx_lease_palette を呼ぶ。lease に取得・返却の概念は無い (gfx/gfx_core_api.inc:102-128 は範囲を検査して即座に色を書くだけ、返却の API も無い)。「取得済みの lease を返す」の記述は無効。
- G2 (P2) §2 index_mask・§4・§3 osri: mask の既定は今のまま (16 色 0xFF7E、packed8 16〜255) とし、0/7・0〜15 を含む mask も受理する (400 にしない — 自前の制限だった)。lease が断る index は、全画面のときだけ SDK が gfx_set_palette (gfx_core_api.inc:49-57、どの index でも書ける) で書く。静的 OSRI に 0/7 の保持は課さない。
- G3 (P3) §4 optimized: 不透明画素を 1×n の RGB 画像に詰めて MEDIANCUT で quantize する。
- G4 (P3) §6・§7 B2: B2 の前提は「NET_HOSTSVC の TIMEOUT/HOSTDOWN の番号の確保」。画像の SDK は status が要るので KAPI (host_open/host_status) を直接使い、NET_HOSTSVC の「用途別 API は host_call の上へ」の例外として NET_HOSTSVC 側の文書に 1 行足す (段 D1)。
- G5 (P3) §1 Cirrus の行: 4e1c9bfd で 8+10k に統一済み (Cirrus は 6bit の最近傍で ±2)。
