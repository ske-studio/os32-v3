# ラスタパレット画像スプラッシュ — 確定設計

> 出所: ~/os32-tmp/run/raster-splash/DESIGN.final.md (ユーザーの草案 2026-11-01 を出発点に astra 起草 r1〜r3、Opus レビュー 2 回)。
> 状態: **現行 (2026-11-01)** — 段の状態は票 TASK_RASTER_SPLASH が持つ。

## r2→r3 の変更
- N1: VSYNC立下りを原点とし、高期間の幅から非表示全体型/VS型を判別してactive0を選ぶ。
- N2: 共通再生状態をsplash_raster所有へ移し、gfx_initごとに遅延判定・保持して公開KAPIからも使う。
- N3: IF=1の窓を各保護区間後に1行以上確保し、2行未満のイベント間隔を予算で拒否、立下り捕捉と遅れの破棄条件を定める。
- N4: ロゴの基準をテーブルの行位置へ改め、下端までの帯・流れる向き・3秒間のcommits差±10%で受入する。
- N5: 既存コメント・SDK・KAPI説明の更新を§7の段の依頼へ注記する。
- N6: docs/designとtest_src_layoutの登録をB2の直列作業へ加える注記を§7へ置く。
## r1→r2 の変更
- R1: 目標の1行前から更新までを短くIF=0にし、lateは次のHBLANKへ繰越して数え、率で受入する。
- R2: ファイルのtiming_profileを予約領域へ替え、24/31kHzを起動時判定で吸収する。
- R3: 09A8hはpegc.hのD0読出定数を再利用し、054Ch bit5は記録だけに使う。
- R4: 8フレーム照合・モード別有理数周期・SYNC由来の位相を定義し、ラスタのcpu_delay_usを廃止する。
- R5: 8行縞を−4〜+4行ずらした副尺を追加し、カメラの判別限界を記録する。
- R6: 実機24kHzはユーザー立会いの延期試験とし、段0の実機受入は31kHzだけにする。
- R7: NP21/Wで24kHzのHBLANK経路を明示的に受入する。
- R8: 原画・配布の未決を解消し、生成模様・権利の問題の無い画像を使う。
- U120: 既存ロゴとgfx_present_rasterも共通の絶対行位置待ちへ移す専用段を追加する。
- UIMG: 誤差拡散と256色版を並べたPNGを採り、厳密な走査模擬・色誤差レポートは初版から外す。
- UPAR: GeForce加速と並行し、共有ファイルの変更とエミュレータ操作はPMが直列化する。
- 状態: 第3稿・最終往復、2026-11-01。r2に `REVIEW.r2.md` のN1〜N6と `BRIEF_r3.md` を反映。実装・実測・配備は未実施。
- 目的: 内蔵 OS32 ロゴ＋虹色スクロールを残し、640×400・4bpp・RGB444 の外部静止画を追加する。
- 前提: PC-9821、シングルタスク・協調切替、整数演算、CPU による VRAM 転送とパレット I/O。公開 KAPI と通常 GUI の選択・初期化順は維持する。
- 主対象はRa266の既定31kHz。24kHzも起動時に吸収し、実機でPEGCを切った24kHz起動だけは9801相当構成の確認対象に含める。実機24kHzの合格は受入の必須条件にしない。1行最大16色で4096色同時表示は要求しない。

## 1. 実物の確認

| 草案の記述 | 実物・設計への反映 |
|---|---|
| §2: パレット1、4行、100件 | 一致。`init/boot_splash.c:15,29,44,177`。行は0,4,…,396。 |
| §2: 4プレーンへ描画 | 一致。`platform/pc98/splash.c:30,40`。借用 BB を使用する。 |
| §2: 120µs待ちで再生 | 差異 D1: palette-only と flip 経路は `line` を読まず、各イベント後に120µs待つ。`gfx/gfx_vram.c:421,464`。不等間隔・同一行複数件を現APIへ渡すだけでは再生できない。 |
| 同上、初回と継続の扱い | 差異 D2: non-flip の dirty 初回は400行各々で待つ（最低48ms）、以後は100件各々（最低12ms）。`gfx/gfx_vram.c:446,460,464`。I/O・転送・IRQ時間は別に加算される。 |
| §2: 最大200件 | 一致。ただし SDK の1件は8B、コメントは2行単位推奨。`sdk/include/os32/os32_kapi_shared.h:623`。200は容量上限であり時間予算ではない。 |
| §2: 標準 planar 強制 | 一致。希望値を保存→PC98選択→init→希望値復元。`platform/pc98/splash.c:13`。 |
| §2: 終了時復帰 | 一致。両ページ消去→shutdown→既定パレット→テキスト消去。過去の任意パレットの保存復元ではない。`init/boot_splash.c:253`。 |
| §6: ルートから画像を開く | 実現可能。mount は `init/kernel.c:176`、IRQ有効化・services は234、splash は242。呼出位置を動かさない。 |
| §9: 標準出力モードが未決 | 差異 D3: 31kHz既定は決定済み。`docs/decisions/realhw/RA266.md:48` に09a8=81の観測。未確定なのは実行時判定と位相。 |

- 差異は上表の3件（D1・D2は草案の現行説明への訂正、D3は前提更新）。未測定値は差異に数えない。
- 現行はテキストGDCのVSYNC入りを待ってからグラフィックGDCの抜けを待つ（`gfx/gfx_vram.c:418,420`）。抜け＝可視行0とはまだ証明されていない。
- `cpu_delay_us()` は校正済みループの整数待ちで、IRQ処理時間を補償しない（`kernel/time/cpu_calibrate.c:134`）。校正は起動初期に実施済み（`init/kernel_boot_cpu.inc:68`）。

## 2. 段0: 計測の設計

| 根拠 | 採用する情報・限界 |
|---|---|
| PC9800Bible `docs/hw/PC9800Bible/2-7_グラフィック.md:230` | 0A8h=番号、0AAh=G、0ACh=R、0AEh=B。既存の4 OUT順を保つ。 |
| UNDOCUMENTED `docs/hw/undocumented/io_disp.md:24,765` | 0060h/00A0h の bit5=VSYNC、bit6=HORIZONTAL BLANK。水平同期信号そのものとの同一視はしない。 |
| `include/pegc.h:95,106,115,247` | 09A8hは `PEGC_HSYNC_PORT` / `PEGC_HSYNC_READ_HF` でD0だけ読む。書込用MASKは使わない。054Ch bit5は既存BIOS定数とP2Vで診断記録だけ。 |
| 周期・位相の正典 | `include/pegc.h:325-335` の400行SYNCと `docs/hw/undocumented/io_disp.md:1875` の公称周波数を組にする。周期のµs比は31k=`100000/3147`、24k=`100000/2483`、4行は約127.1/161.1µs。 |
| NEC Technical Databook Hardware (1993) | 手元の `docs/hw/` に該当資料を確認できず、未照合。メーカー資料のタイミング表を実装前のHW3資料ゲートでPMが補う。 |
| BSD参照 | [FreeBSD releng/10.4 sys/pc98/cbus/gdc.c](https://github.com/freebsd/freebsd-src/blob/releng/10.4/sys/pc98/cbus/gdc.c#L655):655,685 はVSYNCの抜け→入り、831は16色パレット設定。冒頭はBSD 3条項。同期・I/Oの参考に限り、描画コマンドは採らない。 |

- 観測口は診断ビルド専用 `platform/pc98/splash_probe.c`。通常起動と同じ begin 後に呼び、診断終了後は共通終了処理へ戻す。新規KAPI・常駐IRQハンドラは作らない。
- 時計は既存 `sys_time_now()`（`kernel/time/ktime.c:56`、PIT補間µs）。読出費用・1µs量子化・逆行補正回数を測る。ラスタの待ちに `cpu_delay_us()` は使わず、絶対時刻とステータスを照合する。
- 全画面CLIは禁止。IF=0は目標の1行前から対象HBLANKの同一行更新終了まで、最長2行周期（31k約64µs/24k約81µs）。各保護区間後はIF=1を1行周期以上確保する（入口IF=0では動的再生しない）。立下り捕捉は§5の1行以内だけを保護する。最大値と1フレーム合計を記録する。

| 測るもの | 手順・記録 |
|---|---|
| 起動条件 | SHA・媒体CRC・機種・09A8h生値/D0・054Ch bit5・表示高・flip・PIT分周・シリアル速度。begin前後の値を比較する。 |
| VSYNC起点 | begin後、00A0h bit5の新しい立上りを9回採り8周期をsys_time_nowで測る。立下りも記録し、周期用の立上りと位相用の立下りを分ける。診断では32周期のmin/median/maxと0060hとの差も採る。 |
| 行周期/HBLANK幅 | bit6の0→1→0を高速ポーリング。32/64周期の総時間を整数比で求め、幅は単発測定の観測誤差込みで下限を出す。IRQを含む長い区間は欠落候補として別集計。 |
| パレットI/O | 同じ値で単一OUTを8/16回、色1件の4 OUTを1/4/8組実施。空ループ・時計費用を別測定し、組数に対する傾きと単発上限を記録。長い組は分割しIRQ窓を設ける。 |
| 初期16色 | VSYNC入りで16件を設定し、終了時刻・bit5・次の可視開始までの余裕を記録。16色全体をCLIで囲まない。 |
| IRQによるずれ | 無負荷/シリアル受信負荷の予定→開始/終了差、late件数/率、遅延行数、最大IF=0・フレーム合計、tick差、OE/FE/PE/ring overflow差と番号付き受信列の欠番を採る。 |
| 現行120µsとの比較 | flip有無、dirty初回/継続を分け、同じ像で初回と後続の所要・最終帯位置を比較。120を127や161へ置換する試験とは分ける。 |

- 計測像: 左端に固定白の16行ごとの目盛、中央にパレット1だけの広い縦長矩形、右端に固定16色タイル。画面上下は太い固定枠にする。
- 中央の赤/緑を4行または1行ごとに変え、カメラ用に32行帯と上/中/下の太い印も置く。副尺は固定色の8行縞を9列へ−4〜+4行ずらして並べ、動的帯と揃う列を選ぶ（周期8で両端は同位相なので粗い枠で区別）。
- VSYNC立下りから最初の帯までを1行ずつ掃引する。beginでbit5の高期間を測り、総行数−400（31k=49、24k=40行）に近ければ `active0_lines=0`、VS幅（31k=2、24k=8行）に近ければ `active0_lines=P8>>2`（VBP:31k=34、24k=25）とする。8周期の高期間平均を行周期へ換算し±1行以内を「近い」とし、どちらにも合わなければdynamic_ok=0。NP21/Wの非表示全体型では立下りが可視行0になる。実機は選んだ位相を副尺・HBLANK番号で確かめる。立上りを原点に表示するログには実測VSYNC幅を加える。
- カメラは表示有無・大崩れ・帯の大まかな位置だけを判定する（`docs/decisions/realhw/RA266.md:94`）。1行位置・色差・細い水平乱れをカメラ合格だけで保証しない。
- プローブはRAMへ固定長記録し、測定区間終了後にシリアルへ出す。走査中のprintf・ファイル読み込み・カメラ要求は禁止。溢れた記録は失敗扱い。
- 記録例: `SPLR phase=... frame=... y=... target_us=... begin_us=... end_us=... stat=... late=...`。集計はn、min、median、p99、max、欠落候補数、時計エラー数を残す。
- PMはノートのセッションへ31kHz診断起動・受信負荷を依頼し、ログとカメラ画像名を対応させる。実機24/31kHzの物理切替と復帰はユーザーの立会い時だけ。PMのソフト操作では切り替えない。
- NP21/Wは24kHzの周期判定・bit6によるHBLANK待ち・ポート順・両VSYNC辺・timeout・復帰・planar配置・初回/継続を先行確認する。bit6模擬は `~/np21w-src/src/io/gdc.c:761`。実機I/O予算の証拠にはしない。
- 段0成果物は31kHzの位相補正・hblank_min_us・誤差E・1件上限W・IRQ遅延J・許容件数・late率・IF=0最大/合計。周期num/denは定数で、実測値をそのまま周期にしない。24kHzはSYNC由来の既定値をNP21/Wで確認する。
- 採用ゲート: `m*W + E + 判定費用 < hblank_min_us`、初期16色は可視開始前に完了。31kHzは実測で採用、24kHzは初版4行1件をNP21/Wで確認して採用し実機予算は未確認と明記。複数件は各モードの確認済み上限まで。

## 3. `.osri` 形式 v1

- カーネル内部形式。静止画1枚、非圧縮、little endian。下記は配置の説明でありファイルを構造体へ直接castしない。サイズとoffsetofを静的検査する。
```c
struct OsriHeader {                    /* 64 bytes */
    u8 magic[4];                      /* "OSRI" */
    u16 version, header_bytes;        /* 1, 64 */
    u16 width, height;                /* 640, 400 */
    u8 bpp, color_format;             /* 4, 1 = RGB444 */
    u16 flags;                        /* 0 */
    u32 file_bytes, palette_off;      /* 128096 + 8*n, 64 */
    u32 pixels_off, pixels_bytes;     /* 96, 128000 */
    u32 events_off, event_count;      /* 128096, 0..200 */
    u16 event_bytes, reserved_profile; /* 8, 0; mode is not in file */
    u16 band_lines, reserved0;        /* 1/2/4, 0 */
    u32 file_crc32;                   /* offset 48 */
    u32 reserved[3];                  /* all zero */
};
struct OsriEvent {                    /* 8 bytes */
    u16 line;                        /* 1..399; visible line */
    u8 index, reserved0;              /* 0..15, 0 */
    u16 rgb444, reserved1;            /* 0x0RGB, 0 */
};
```
- 初期パレットは16個のu16（0x0RGB、上位4bit=0）。画素はB/R/G/Iの順に各32,000B、各行80B、左端がbit7。色番号のbit0/1/2/3を各プレーンへ入れる。
- 初期パレットが可視行0の値。イベントはその行の開始前に完了すべき差分で、同じ行はindex昇順、同じ(line,index)の重複を拒否する。lineはband_linesの倍数。
- 行0の変更は初期パレットへ畳む。段2の0,4,…,396の比較列は「初期の1色＋99差分」になる。毎フレームの初期16色は別負荷として必ず数える。
- reserved_profileは常に0。n=0ならband=4、n>0でも走査モードを格納しない。同じファイルを24/31kHzで使い、行/フレームの件数上限は起動時に選んだカーネルの予算で検査する。
- 整合性: loaderでmagic/version/固定値/予約0、正確な全長と固定offset、n<=200、範囲・整列・重複・同値再設定を検査。画像制御側で行/フレーム予算をcommit前に検査する。異なるイベント行の間隔は2行以上とし、1行間隔を予算違反で拒否する（band=1自体は形式上許すが、間隔の緩和は段4の実測後だけ）。同一行の複数件は一つの保護区間の予算で検査する。未知フラグは拒否しファイルの走査モード照合は設けない。
- nを上限検査してから `128096+8*n` を計算し、短い読み・途中EOF・末尾余分も拒否。検査済みになるまでVRAMへ提示しない。暗号署名や複雑な回復形式は追加しない。
- CRCはヘッダー48..51を0としてファイル全体をIEEE CRC-32。Pythonは `zlib.crc32`、Cは `lib/crc32_core.inc:13` のinit/update/finalを再利用し、finalは最後の1回だけ。
- `lib/crc32.c:14` の一括版と同値であることを既知ベクトル・分割読みで照合する。CRCの新実装やゲストへのzlib追加は不要。
- SDKの `GFX_RasterPalEntry` はr/g/b別バイト・pad2のRAM形式（上記`:623`）。`.osri` の8Bと同サイズでもバイナリ互換にせず、SDKヘッダー・世代・KAPIを変更しない。
- 既存MGXは画像用、VBZはパス描画用でイベント列を持たない（`tools/img2mgx.py:51,528`、`tools/img2vbz.py:1160`）。枠を流用せずPIL・struct・CRCの既存手段を使う。

## 4. ホスト変換

- 置き場は `tools/img2osri.py` 1本。PillowでPNG等を読み、RGBへ合成、縦横比維持の縮小＋黒余白で640×400にする。透明部分は黒へ合成する。
- CLI案: `input.png -o splash.osri --mode fixed|A|B|C --budget <json> --events 100 --per-line 1 --band 4 --fixed-colors 8 --dither fs --preview preview.png`。budgetは変換の件数上限だけでファイルにモードを埋めない。
- fixedは全画面16色、n=0。初版動的出力は4行・1件/境界・100件以下（格納上限200と区別）。両モードに配る像は両者の採用予算の小さい方を使い、実測前の出力は診断用として扱う。
- RGB888→444は各成分 `(v*15+127)/255` の整数丸め。PILの量子化色も444へ丸め同色を統合。パレット履歴を確定後、各行で有効な16色へ整数Floyd–Steinberg誤差拡散（右7・左下3・下5・右下1、分母16、左→右）して画素を決める。符号付き誤差は0方向へ除算、画像外は捨て、行間は持ち越し、最近色の同点はindex順とする。

| 最適化 | 初版の具体的な処理 |
|---|---|
| A:16色全可変 | 初期行/帯をPILで最大16色へ量子化。次の帯は前パレットを維持し、量子化候補色への1スロット置換を全候補で評価、二乗誤差を最も減らす変更を予算まで採る。 |
| B:共通色＋可変 | 全画面量子化で得た色を使用数降順・同数はRGB順でk色固定し、残り16-kだけAの置換対象にする。kはCLI指定、初版既定8。 |
| C:帯で共有 | A/Bの評価範囲を4行の帯へ広げ、同じパレットで全画素を再割当て。初版既定はB＋C（4行・最大1件/境界）。2/1行は段4で有効化。 |

- 変更は誤差が改善する時だけ採り、同値設定を出さない。indexの選択は誤差→index→RGBの順で同点処理する。探索は貪欲法で、最適解を保証しない。
- 更新予算に達したら現在のパレットで残りを量子化する。完成画像からイベントだけを削る処理は禁止。パレット履歴と誤差拡散後の画素を一組で出力する。
- プレビューは出力済み`.osri`を読み直し、各行のパレットでディザ込み画素を復号する。同じリサイズ済み原画をPillowで256色に減色・ディザした像と横に並べ、ラベル付きPNGを1枚出す。厳密な走査時刻・実機色の模擬はしない。
- 初版に色誤差レポート・比較専用ツールは作らない。変換完了時は件数・最大同一行件数・予算・CRC・Pillow版とオプションだけを記録する。画質目標は256色版と見比べて遜色ない程度で、数値閾値は置かない。
- 400×16色の全パレット表はホストの比較用だけ。全6,400色更新をゲスト形式へ機械的に詰めない。

## 5. OS32再生と部品の境目

| 部品 | 責任・状態の所有 |
|---|---|
| `init/boot_splash.c` | 全体の開始/終了、3秒期限、内蔵ロゴ。begin後に画像経路を試し、失敗時はロゴを描き直す。装置・プレーンの詳細を持ち込まない。 |
| `platform/pc98/splash_image.c`（新規） | 借用BBと共通再生状態を使い、画像記述だけを所有。読み込み→実行予算検査→commit→再生を制御し、戻る時にFD・画像記述を解放。共通状態はロゴへのfallbackまで保持。 |
| `gfx/gfx_osri.c`（新規） | VFS読み込み・形式/CRC検査。受け取ったBBへ分割読み込み。ポートI/O・表示・時間待ちは行わない。 |
| `drivers/video/pc98/splash_raster.c`（新規） | 24/31kHz判定・有理数周期・VSYNC/HBLANK待ち・パレット再生を画像と既存ロゴで共有。FS/VRAM転送は知らず、共通再生状態も本部品が所有する。各gfx_initで未判定に戻し、最初の利用時に8周期・位相を判定してそのgfx_initの寿命中保持、gfx_shutdownで失効する。splash外の公開gfx_present_rasterも同じ状態を使う。 |
| `gfx/gfx_vram.c`（既存） | 画像はnosyncの初回転送だけ。既存gfx_present_rasterのflip/dirty処理も転送→共通再生へ分離し、RASTER_LINE_US・相対delay・無期限VSYNC待ちを除去。公開KAPIの型/番号は維持。 |
| `platform/pc98/splash.c`（既存） | begin成功後にsplash_rasterの非公開accessorから共通再生状態を借り、初回の8周期判定を済ませる。clear_pagesは共通状態の寿命を変えない。BBは既存gfx_kernel_framebufferで借りる。 |

```c
/* 非公開ヘッダー。呼出し契約を先に固定する。 */
struct OsriImage { u16 pal[16], count, band;
                   struct OsriEvent event[200]; };
struct SplashRaster { u32 frame, late, mode, dynamic_ok; int last_result;
                      u32 line_num_us, line_den, active0_lines; };
int osri_load(const char *path, struct gfx_kernel_fb *dst,
              struct OsriImage *image); /* 成功0、失敗<0、必ずclose */
int pc98_splash_raster_begin(struct SplashRaster *s); /* accessorの初回利用時に8周期・位相判定 */
int pc98_splash_raster_frame(struct SplashRaster *s,
                            const struct OsriImage *image);
int platform_splash_try_image(const char *path, u32 end_tick);
/* begin/frame/tableは0またはNO_SYNCをlast_resultにも記録。try_imageはDONE/FALLBACK/NO_SYNC。 */
int pc98_splash_raster_table(struct SplashRaster *s, const GFX_RasterPalTable *t);
struct SplashRaster *pc98_splash_raster_state(void); /* 再生器所有、初回遅延判定、gfx_init〜gfx_shutdown */
void pc98_splash_raster_reset(void); /* gfx_initで未判定化、gfx_shutdownで失効させる内部フック */
```
- 初回のopenは現行boot_splash内、begin後・root mount/IRQ有効化後。パスは `SYS_SPLASH_IMAGE` の `/boot/splash.osri`。その前に共通再生器で8周期を判定し、結果を画像/ロゴで共有する。Host Serviceや遅延mountを待たない。
- begin後はパレットを黒へ揃える。`osri_load` は64Bヘッダー→32Bパレット→4プレーン→イベントを順に読み、4KiB以下の単位で既存BBへ直接格納しCRCを逐次更新する。
- 短い正のreadは残量を読み足す。失敗なら全BBをロゴで上書きしてから提示。固定サイズのBB各プレーン・pitch・高さを確認してから読み込む。`OsriImage`だけ小容量確保し、大きなスタック配列・128KBの一括ヒープ確保は避ける。
- 全検査完了後に全画面dirtyを1回登録し `gfx_present_nosync()` を1回呼ぶ。準備中は黒、最初の正常VSYNCで初期色を出す。flip後の表示ページを固定し、以後present/flipを呼ばない。
- prev_dirtyは次回presentのための既存状態として残るが、画像再生中は参照されない。終了/ロゴ再描画を含め両ページを確認する。継続フレームのVRAM書込量は0。
- 起動判定: D0が選ぶ周期と8周期平均/総行数を±1%で照合する。SYNCの総行数は31k=449（約14.27ms）、24k=440（約17.72ms）。照合は64bit整数の交差積で行い、不一致はdynamic_ok=0としn=0画像のみ使用、動的画像/ロゴは静止ロゴへ。bit5高期間からのactive0選択は§2に従い、どちらの幅にも合わない場合もdynamic_ok=0。beginがBIOSのSYNCを保つ前提も段0で検証する。
- 各フレームで新しいVSYNC入りに初期16色を設定し、抜け時刻t_endを捕捉。起動判定の周期・高期間と直近のVSYNC入りから立下りを予測し、その1行前までIF=1、予測した立下りの1行前から立下り捕捉までだけIF=0とする。捕捉窓は最大1行、窓内に捕捉できなければIFを戻して当該フレームを捨てる。立下り捕捉が予測から半行以上遅れたフレームも、以降のイベントを適用せず捨てて数え、次の新しいVSYNCから回復する。行yの可視開始は `t_end + floor((active0_lines+y)*line_num_us/line_den)`、更新はその直前HBLANK。64bit整数で絶対位置を計算し、イベント間隔も行差から導く。
- 入口IF=0では動的再生せず、静止提示して入口IFを維持する。目標の1行前までIF=1で待ち、irq_save後に対象HBLANKの新しい辺を待つ。同一行の全4 OUT群まで保護してIF=1へ戻す。1区間は2行周期以内、立下り捕捉を含む各保護区間の後にIF=1を連続1行周期以上確保し、VRAM転送はこの区間へ入れない。初期16色はIF=1のまま設定し、立下り捕捉前に完了する予算で検査する（IRQ側からパレットを書かない前提を確認する）。画像と公開tableの予算検査は異なるイベント行の間隔2行未満を拒否する。遅れや繰越で窓が確保できない時は窓を優先し、後続を繰り越す。間隔の緩和は段4の実測後に限る。
- IRQ等で目標を逃したらlateを数え、IF=1の窓を確保した次のHBLANKへ順序を保って適用する。残余に収まらない分も繰越し、後続と重なった同一indexは最新の予定値へ畳む（未適用件数も記録）。絶対目標はずらさず、次フレームの初期色で回復する。
- HBLANK待ちが2行以内に完了しなければIFを戻してlateとして数え、IF=1を1行以上確保した次の窓へ繰越す。可視区間を越えた未適用分は数えて捨て次フレームへ。lateだけでは再生停止しない。同期待ちはVSYNCの100msタイムアウトでNO_SYNC、時計異常時は有限ポーリング上限で終了する。
- 固定画像（n=0）は初期色を1回設定した後、終了期限までIRQを有効にして待つ。画像経路の全再試行を含め3秒の共通期限を延ばさない。
- NORMALだけに画像を配り固定パスにあれば使用する（PM決定）。欠落・破損・予算超過・動的判定不可は理由を1行だけログして内蔵ロゴへ。FD/MINIMALは画像を含めない。begin自体の失敗は従来どおりCUIへ。
- 再生中はVSYNCタイムアウトで画像経路を終了しNO_SYNC静止ロゴへ。late率は試験の採否基準に用い、通常再生中の停止条件にはしない。故障時も3秒の共通期限を延長しない。
- 共通再生器の結果を非公開の状態照会でboot_splashへ返す。NO_SYNC/dynamic_ok=0ならロゴをnosync提示して有限期限で終了する。時計も使えなければ共通終了へ直行。公開gfx_present_rasterのvoid署名は変更しない。
- 既存ロゴ移行L0: gfx_present_rasterは全dirty転送（non-flipも一括）とflipを先に完了し、次のVSYNCから共通table再生へ。SDKのline=0は初期色、以後はline順の絶対目標。ロゴの100件/4行・形・色順・フレームごとのoffsetは維持し、初回だけ長い経路を解消する。位置の基準は旧120µs再生の見た目ではなくテーブルの意図とし、行yの色をエントリ⌊y/4⌋に合わせて400行全体へ広げる。splash外もgfx_initごとに判定した共通状態で再生し、判定不可/NO_SYNC/予算違反は転送済み像を静止提示してvoidで戻る。3秒間のcommitsを移行前後で数え、スクロール速度の差を±10%以内に保つ。
- 共通終了は成功/失敗とも一箇所: 画像記述解放→両ページ消去→`gfx_shutdown()`（共通再生状態失効）→`palette_init()`→`tvram_clear()`。保存したIFとGUI希望値を維持し、画像用状態を次回GUIへ残さない。

## 6. 部品の大きさ・配置

| 対象 | 設計上の量・方針 |
|---|---|
| ファイル | 固定128,096B、n=100で128,896B、n=200で129,696B。圧縮なし。 |
| RAM | 既存BB128,000Bを借用。追加は画像記述1,636B＋ヘッダー/CRC/共通再生状態。診断リングは診断ビルドのみ。 |
| カーネル | パーサ・再生器のコード増分は未測定。内蔵ロゴ維持、画像の埋込みなし。`size`/mapとvmkernel.lz4実サイズの前後差を記録する。 |
| ヒープ | `include/memmap.h:45` は192KiB。全画像を別途確保しない理由。4KiB単位のreadはBBへの直読みであり別4KiB配列は不要。 |
| 配布像 | PM決定: 自作・生成等の権利の問題の無い原画を `assets/splash/` →build/outのOSRI→deploy.yamlの `[data]` →NORMAL。固定パス `/boot/splash.osri`。 |
| 起動FD/MINIMAL | PM決定: 画像を入れず内蔵ロゴ。`build/packages.yaml:69,85,113` の分類を維持。共通再生器のカーネル増分はFDにも乗るので2HD/1.44MBの空きを実測する。 |

- `build/packages.yaml:117` には2HDの空き64KB未満を受けた除外経緯がある。現在の空きは未測定で、128KB画像を追加できるとは扱わない。
- `.osri` は拡張子4文字でFAT 8.3に収まらない。将来FDへ入れるならrenameと読込パス・install逆対応が別途必要。初版のFDは欠落としてロゴへ戻る。
- 原画の追加選定は待たず、自作模様・生成画像を使う。変換依存・再生成方法を固定し、欠けた入力を黙って別画像へ置き換えない。

## 7. ファイルごとの実装票

- 各行を「1小段＝主変更1ファイル＝1実装者」にする。共有宣言・登録は先行する専用小段で直列に確定し、機能実装者が競合編集しない。各票の受入は10項以内。

| 小段 / 草案段 | 主ファイル・責任 | 依存/並列 |
|---|---|---|
| H0 / 0 | `gfx/gfx_osri.h` 新規、形式・画像記述・load宣言の正典 | 先行固定 |
| H1 / 0 | `drivers/video/pc98/splash_raster.h` 新規、共通状態/モード別num・den/予算/宣言 | H0後 |
| H2 / 0 | `include/platform_splash.h`、try_image/診断の宣言・結果値（再生状態accessorはH1） | H1後 |
| P2 / 0 | `platform/pc98/splash.c`、begin時に再生器所有の状態を借用し初回判定 | H1/R1後、H1にaccessor宣言 |
| P0 / 0 | `platform/pc98/splash_probe.c` 新規、§2の診断1機能 | H1/P2後、ホスト変換と並列 |
| F1 / 1 | `gfx/gfx_osri.c` 新規、読込・CRC・形式検査 | H0後、変換/再生器と並列 |
| T1 / 1→3→4 | `tools/img2osri.py` 新規、fixed→A/B/C→細分化 | H0後、P0待ちは採用予算の確定だけ |
| R1 / 0→2 | `drivers/video/pc98/splash_raster.c` 新規、状態所有/gfx_initごとの遅延判定→ロゴ/固定色→差分再生 | H1後、31k動的採用は段0測定後 |
| L0 / 0→2 | `gfx/gfx_vram.c`、120µsと旧3再生経路を共通table呼出へ置換、gfx_init/shutdownの状態リセットを接続、VRAM/flip/dirtyの責任は維持 | R1後、B1と直列 |
| P1 / 1 | `platform/pc98/splash_image.c` 新規、所有・表示制御 | F1/R1結合 |
| B1 / 0,1 | `init/boot_splash.c`、probe→8周期判定・画像選択・共通期限・静止fallback、既存虹色を共通再生へ | R1/L0後、P1結合 |
| C1 / 1 | `include/config.h`、SYS_SPLASH_IMAGE定義 | パスを単一正典へ |
| B2 / 0,1 | `build/kernel.mk`、各ソース登録/診断限定フックの定義 | PMが小段ごと直列化 |
| T2 / 1,2 | `tools/tests/test_osri.py` 新規、形式/変換のCHECKと変異 | H0後、F1/T1と並列 |
| T3 / 0,1,2 | `tools/tests/splash_raster_host.c` 新規、両モード・周期照合・IRQ/late/復帰の偽物 | H1後、R1/L0と並列 |
| T4 / 1,2 | `tools/tests/test_splash_raster.py` 新規、T3の実行/CHECK/変異 | T3契約確定後 |
| T5 / 1 | `tools/tests/test_dep_dir_d_splash.py`、既存ロゴ対照を維持し画像分岐を追加 | B1後、既存目印を移動確認 |
| A1 / 0→4 | `tools/accept/raster_splash.py` 新規、証拠集約台本 | host試験後、PMが実行 |
| D1 / 3 | `build/assets.mk`、権利の問題の無い試験原画の変換ルール | T1後、原画選定待ちなし |
| D2 / 3 | `userland/deploy.yaml`、dataで配布登録 | D1後、packages.yamlは変更不要 |

- N5 段の依頼の注記: L0で `gfx/gfx_vram.c:362-382` の注記、SDKの「2ライン単位推奨」（`os32_kapi_shared.h:620`）、KAPI説明を新しい行位置・間隔・同期失敗時の契約に合わせる。型・番号は維持し、生成物の説明は正典から再生成する。
- N6 段の依頼の注記: GF_ACCELと共有する `build/kernel.mk`・`gfx/gfx_vram.c`・`init/boot_splash.c`・`include/gfx_hal.h`・`tools/tests/test_src_layout.py`・`docs/design` はPMが直列化し、新規ファイルの `docs/design` と `test_src_layout` への登録もB2の一覧に含める。
- Shared層のKAPI定義は維持。Subsystem層はpegc.hのポート/SYNC、再生ヘッダーの派生周期/位相/予算、OSRIヘッダーとconfig.h。Local層は計測回数等。[C4]に従い新旧ロゴ・画像で周期定数を複製しない。
- GeForce加速と並行するが、gfx_vram.c・共通ヘッダー・build登録はPMが所有を調整し直列化。NP21/W操作は同時に1人。
- 段3/4はT1とR1の後続票で、同じファイルの前段完了後に出す。メーカー資料不足・未実測の時間値はPM観測ゲートで管理する。

## 8. 受入・試験・変異

| 草案段 | Host | NP21/W | Ra266＋カメラ / 次段への条件 |
|---|---|---|---|
| 0:測定＋既存ロゴ移行 | D0のみ・8周期±1%・bit5高期間の両型/不一致・有限待ち・line尊重・120µs復活を検出、splash外KAPIとgfx_init再判定 | 24kHzのHBLANK経路・ロゴ初回/継続/flip両方、31kHzも同じ模様。画面画像で行yの色＝エントリ⌊y/4⌋を行単位で照合、3秒間のcommitsは移行前後±10% | 31kHzの位相/予算/IRQ負荷、副尺とロゴの形・虹色・帯が下端まで届く・下の平らな帯が消える・流れる向き・3秒後復帰をカメラ確認。速度は3秒間のcommitsを移行前後±10%で照合。24k実機は延期。 |
| 1:固定画像 | 正常/短読/CRC不一致/サイズ溢れ/余剰、B/R/G/I・bit順、FD closeと失敗後ロゴ | 欠落/壊れ/正常、両ページ、終了→CUI→通常GUI、再起動 | 固定16色タイル・画像・復帰を撮影。表示有無はカメラ、CRC/選択はシリアル。 |
| 2:4行1件 | 不等間隔/同一行/最終行・2行未満間隔の拒否、IRQ遅延後の繰越/畳込/立下り捕捉が半行以上遅れたフレーム破棄/次フレーム回復、連続IF=1窓、初期色復元、継続VRAM書込0 | 同じファイルを24/31kHzで4行99差分＋初期色、D0/周期不一致でもn=0は表示、初回〜定常 | 31kHzで300フレーム、late率≤1%（各予定件を一度だけ数えたlate件数/総予定件数）、適用遅延≤1行、未適用・予算超過・受信欠落0。IF=0各区間≤2行（立下り捕捉≤1行）、各区間後の連続IF=1≥1行、合計と破棄フレーム数を記録。立下り捕捉が半行以上遅れたフレームの破棄も確認し、300フレーム受入では破棄0。通常3秒も確認。 |
| 3:複数色 | A/B/Cの復号一致・誤差拡散・固定色不変・予算内・決定性 | 各モードの採用上限内でm件/行と総件数を増やす | 256色版との並列PNGと大崩れなしを確認。16初期色込みで段2と同じlate/IRQ基準。 |
| 4:2/1行実験 | band検査と位相ずれ/欠落の失敗検出 | 行単位の画像比較・ポート時刻を確認 | 同じ負荷試験。カメラで判別不能な1行精度/細い乱れは未確認と記録し、既定化しない。 |

- PMが `docs/tasks/DEFERRED_TESTS.md` に `SPLASH-24K-RH` を登録する: ユーザー立会いでPEGCを切った24kHz起動→D0/8周期/副尺/画像/内蔵ロゴ/終了を観察→ユーザーが31kHzへ復帰。段0〜4の受入条件にはせず、未確認を合格と記録しない。
- 変異: CRC無視・上限除去・plane/bit交換・line無視・120µs相対待ち復活・24k拒否・D1混入・周期照合省略・初期色省略・IF長時間保持/復元省略・late即停止・timeout無効・再present・close欠落を各々実行時REDにする。
- 無限待ち変異は偽I/Oの回数上限で異常終了させ、コンパイル成功だけをREDと数えない。既存ロゴ・backend希望値・復帰試験を正常対照として毎回残す。
- 既存 `tools/tests/test_boot_splash_native.py:2` と `test_dep_dir_d_splash.py:2` のCHECK/fixture/変異目印への影響を点検する。必要なCハーネスは専用小段へ分ける。
- 実装時は依頼SHAのpreflight、触った像のbuild、`make -k check-changed BASE=<SHA> MUTATE=0`、追加/変更試験の`--mutate`、checks lintを行う。最後の変更後のログだけを採る。
- PMは取り込みで全体build/変異・5静的検査と媒体容量/配備集合を確認し、NHD配備は停止→配備→再起動、実機はノート経由。画像CRC・kernel CRC・カメラ画像名をRESULTへまとめる。

## 9. 残る未決
- ユーザー判断が要る未決は0件。原画と配布・選択は決定済み（§4・§6）。
- PMの観測事項: begin後SYNCの保持、8周期照合、VSYNC抜け→可視行0の位相、I/O/IRQ予算・late率・容量実数・メーカー資料照合。24kHz実機は§8の立会い項目に残す。
- 段4の細かい色/位置は副尺でも判別不能なら未確認とする。追加観測なしで保証へ読み替えず、既定化は実測可能な範囲まで。

## 反論
- 0件。r2のR4起点への反論はN1を受けて撤回し、bit5高期間の両型を判別する設計へ修正した。

## 確定 (2026-11-01)
- r3 を確定版とする。Opus の第 2 回 (条件付き) の N1〜N4 は r3 で反映、PM が本文で確認 (dynamic_ok・2 行未満の拒否・テーブルの行位置の基準・±10%)。N5・N6 は §7 の段の依頼の注記。
- ユーザーの決定・指示は USER_DECISIONS の 6 件 (並行で進める・24.8kHz の吸収・24kHz の実機は立会いのついで・120µs はマジックナンバー・ディザで 256 色並み・プレビューは PNG で足りる)。未決 2 (配布) は PM 決定 (NORMAL だけ、無い・壊れていれば内蔵のロゴ)。
