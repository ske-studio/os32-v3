# PCM 合成器（ソフト OPNA）詳細設計 r3

> 状態: **現行 (2026-11-24、sol 第 2 回レビュー後に PM 確定)**。実施状況は 作業票 TASK_SYNTH_OPNA を参照。

## r2→r3 の変更

2026-11-09 のユーザー決定「HAL側に寄せる…カーネルに直にミキサーを実装するのは反対」を反映した。[AV_HAL_DESIGN](AV_HAL_DESIGN.md) §7〜9 と以下の分担を共有する。

| r2 の節・段 | r3 の置き換え | 担当 |
|---|---|---|
| §2 分担 | userland/lib/snd/fmvoice.c が OPNA を voice として提供する。mix.c が各入力の周波数・音量・パンを整数で処理し、飽和加算後の 1 本だけを snd2_stream_write に渡す。カーネルは装置列挙・能力・PCM stream・位置・DSP を担当する。 | L1・L2 |
| §3 サービスの口 | synth_open/write/read/pump/wake_irq/finish/abort を廃止。adapter の fmv_new・fmv_write・fmv_read・fmv_status・fmv_render・fmv_reset・fmv_csm が段 D の FFI を呼ぶ。 | L2 |
| §4 リンク・障害 | SDK の共有ライブラリの再配置規則で libos32snd にリンクする。最終の到達命令で整数・386 命令制約を検査し、panic の終端はアプリの異常終了とする。 | L5 |
| §5 private ABI | opna_timer_write・opna_timer_count・opna_apply_csm を段 A の opna.h に残す。Count は次期限と 32767 µs の小さい方まで分割する。V86 の C 模型は Rust の timer_write/count を呼ばず、CSM_KEYON を受けた L2 が fmv_csm から opna_apply_csm を一度呼ぶ。単体 oracle は opna_count で timer と音側の結果を比較できる。 | A・L2・V1 |
| §5 時計・所有、確定 C1/C2 | D+6144 と owned submit を廃止し、下記の stream の空きと入力時刻を使う。PCM の所有はアプリの stream 1 本に集約し、voice・SOFT chip・MIDI が共有する。最後の利用者が閉じる。token および attach/push/finish/abort/status/detach の内部口を削除。 | L2・L3 |
| §5 timer IRQ、確定 C3 | IRQ は起床の印だけ立てる。guest 復帰前の通常文脈で C 模型の Count、CSM の FIFO 追加、PIC 経由の仮想 IRQ12 判定を行う。V0 で PCM IRQ との共存を資料確認し、CS4231 I20/I21 の次期限→PIT one-shot の順。双方不可なら 100 Hz 制限をユーザーに諮る。 | V0・V1 |
| §6 装置選択、段 F | MML と旧 snd_engine の SOFT 経路を廃止（AV §7-6）。FM 検出を V86 の FM_SOFT の既定に使う。snd.fm=auto/hw/soft/none と chip 能力・空きに従う。既定要求 OPN では OPN/OPNA 実機を HW で取得し、無ければ PCM を取れる Pentium 以上で SOFT。選択不能・BUSY は FM 無し（読み FF、書き捨て）。HW の取得者だけが実ポートを使い、往復でリセットする。 | V2・chip 選択側 |
| §6 trap・bank、段 G | FM_SOFT の間は 0188h–018Fh と A460h を bitmap で拒否し、観測より先に捕捉。幅 2/4 は連続 byte 順、bank 別 latch、奇数 port は FF/書き捨て。A460h は 70h＋bit0、bit0=0 は bank1 を隠す。実 IRQ12 は開かない。アプリが FIFO を drain して voice に渡す。 | V1・V2・V86 アプリ |
| §7 大きさ・移植性 | 合成器のカーネル常駐量は 0、合成で IRQ stack を使わない。state・RAM・波形・scratch はアプリのメモリとして見積もる。PC-98 固有の trap は platform/pc98/legacy/ に置く。 | L2・V2 |
| §8 段 D のビルド | i686-os32-mod.json と build/synth-module.mk の計画をユーザー用 Rust staticlib target と build/os32snd.mk に置換。core/compiler_builtins も同じ target。C 規則は profile の char/enum 旗、order-only の ABI_CONTRACT_ORDER、対象 object の一覧登録を継承し、check-type-contract を通す。 | L5 |
| §8 段 E | 段を削除。synth_service・synth_samples・synth_module・synth_body・pcm_source.inc・CS4231 への専用接点を設けない。L2 がユーザーの波形 cache をアプリ文脈で検査して読み、アプリ寿命で保持する。リズム素材は同梱せず、未指定ならリズムだけ無音。 | L2 |
| §9 受入、段 H | synth.mod の load/unload・内部 token・専用 PCM の占有試験を外す。元 C++ との差分を残し、以下の adapter・V86 の試験を加える。 | H・AV §13 |

## r1→r2 の変更
- R1: 音声の供給時計を DMA 再生済み位置 D + 6144 frame に変更し、staging に最低 2048 frame の補充余裕を持つ。
- R2: 0188h status・reg FFh ID の検出手順と auto/hw/soft/none 設定を追加し、Ra266 の判定を実機受入へ移す。
- R3: 次のタイマ期限で起きる方式を推奨し、100Hz 制限を選ぶ場合の機能低下をユーザー判断に残す。
- R4: 浮動小数点の全対象とプリスケーラ別生成キーを列挙し、Rust の float_arithmetic を deny にする。
- R5: SOFT V86 で A460h の86 ID・拡張マスクを仮想化する。
- R6: kai/np2 の原文、THIRD_PARTY.md、README.OS32、配布像での置き場を追加する。
- R7: snd の BGM/SE 開始・終端に source を取得・解放し、通常文脈 producer と保持 owner を P8 に揃える。
- R8: Count の各 delta を next_event と 32767µs 以下に分割する。
- R9: usefmgen を PCM 比較の前提から除外し、入力列を鳴らすゲスト試験と tools/checks の CHECK 宣言を指定する。
- R10: 起動時の最悪発音ベンチマークと遅い CPU の NOSYS 判定を削除する。
- R11: ホスト oracle の SUPPORT_FMGEN と最小 compiler.h shim を追加する。

## 1. 実物の確認

元の fmgen との一致は同じ入力列の PCM・status・read で判定する。NP21/W では CS4231 の DMA 出力を採取する。効果音の PCM はユーザーの明示実行時に生成する。

## 2. 分担・移植範囲・条件

Rust は FM 6ch×4 operator、SSG 3ch、リズム 6ch、ADPCM-B、レジスタ、タイマを持つ。I/O・VFS・DMA・IRQ 登録・メモリ確保は持たない。
N `sound/fmgen/` の `fmgen_fmgen{,inl}.{cpp,h}`、`fmgen_psg.*`、`fmgen_fmtimer.*`、`fmgen_opna.*` の OPNBase/OPNABase/OPNA と必要な型を移植する。OPM/OPNB、保存像、FileIO、C++ wrapper は製品に入れない。
元の関数→Rust 関数の対応表・元ファイルの SHA-256・NP21/W revision を `PORT.md` に固定する。ホストの比較対象はこの快照とする。
fmgen 由来部分を MIT に付け替えない。cisc の著作権・出所・Rust 化/確保除去/表生成/入出力変更を明示する。
`fmgen_readme.txt`・`fmgen_readme_kai.txt`・`fmgen_readme_np21w.txt` と N `LICENSES/fmgen/fmgen_readme_np2.txt` をバイト単位で無改変添付する。配布像の配置は §8。NP2/NP21/W の改変記録も保存する。商用組込みは既決の条件どおり作者合意が必要。
別の GPL/LGPL 音源を取り込まず、許可リストで入力ソースを排除する仕組みは作らない。
初版は OPNA 入力クロック **7,987,200 Hz**・出力 **44,100 Hz**・補間指定 false・各部 0 dB。N `win9x/ext/opna.cpp` の倍クロック契約を保つ。
`OPNABase::SetRate`（N `fmgen_opna.cpp`）内部の /2 を二重に適用しない。レジスタの prescaler 変更は移植範囲。
任意の外部レート変更は初版の口に足さない。現在の snd は OPN 3ch のまま、全 6ch は直接レジスタ入力と V86 で提供する。

## 3. 状態・C ABI・演算

正典ヘッダ `lib/os32_opna/opna.h`。公開 KAPI ではなく内部 C ABI。C の固定幅型と Rust の repr(C) を寸法・offset で両側照合する。

```c
/* OPNA_STATE_BYTES/ALIGN は生成ヘッダに固定、C が確保して Rust が借りる。 */
int opna_init(void *state, unsigned int bytes, unsigned char *adpcm,
              unsigned int adpcm_bytes); /* 0x40000 bytes、初期化時だけ */
int opna_reset(void *state);             /* reg/位相/timer reset、RAM 内容は保持 */
int opna_rhythm(void *state, unsigned int ch, const unsigned char *s16le,
                unsigned int frames, unsigned int source_rate);
int opna_write(void *state, unsigned int reg, unsigned int value);
int opna_read(void *state, unsigned int reg, unsigned int *value);
int opna_status(void *state, unsigned int bank, unsigned int *value);
int opna_count(void *state, unsigned int delta_us, unsigned int *irq_level);
int opna_next_event(void *state, unsigned int *delta_us); /* 無しは UINT_MAX */
int opna_render(void *state, unsigned char *out_s16le, unsigned int frames);
```

`state` は C がロード時に確保した固定サイズ・固定 alignment の領域、ADPCM RAM は別の 256 KiB。init は in-place に書き、大きい一時構造体をスタックに作らない。
波形は C が所有する読み取り専用領域で、reset を跨いで有効、停止・callback 解除の後にだけ解放。Rust がポインタを保つ範囲はこの契約に限定する。
アプリ文脈の L2 が RIFF/WAVE の fmt/data chunk を境界検査し、mono PCM16 の6本を C の確保領域に読む。奇数 chunk の padding を処理し、範囲外・短い read は INVAL/IO。元 FileIO の固定 offset や過大確保は移植しない。
`opna_rhythm` は停止中のみ。6 本が揃うまで rhythm-ready=0、未提供はリズムだけ無音。PCM mono 16bit LE、rate>0、位置の固定小数点が溢れない frames 上限を検査する。
レジスタは bank 込み 0x000–0x1ff、値は 0–255。範囲外は INVAL、範囲内の未使用レジスタは元 fmgen の無視/読み値を保つ。0x100 の ADPCM data port の副作用も移植する。
reset の RAM 保持は元実装に合わせる。新規 init 時の RAM は C がゼロ化する。reset で波形を読み直さない。
render は 0–256 frame、出力は上書き、0 frame は何もしない。内部 i32 L/R scratch は state 内の 512 要素。C は有効な `4*frames` bytes を渡す。
N `fmgen_fmgen.h` は i32 Sample、`fmgen_fmgeninl.h` は加算。FM→SSG→ADPCM→rhythm の順を保持し、全部を加算した後にだけ [-32768,32767] へ飽和し LE 化する。
`opna_count` は元 Timer::Count を移植、render 自体は timer を進めない。C/Rust の双方で各呼出しを `delta_us <= min(next_event,32767)` に分割する（イベント無しも32767以下）。期限到達を処理してから次の期限を取得し、0を返す未処理イベントで空回りしない。u32 の長い差分を int32 へ直接 cast しない。timer A/B status・clear・CSM の副作用、ADPCM EOS/BRDY/mask を保持する。
回り込みは明示 `wrapping_*`、符号付き右 shift・切捨て・中間幅を元処理に合わせる。除算 0・shift 範囲・添字範囲は入口または固定型で解消する。
C++ の未定義動作が比較で露見したら、該当式と採用する幅を PORT.md に記録して両側を同じ定義に直す。差を許容誤差で隠さない。
返値は 0/既存 `OS32_ERR_INVAL`・`NOSYS`・`IO`、メモリ不足は `PCM_ERR_NOMEM`、占有中は `PCM_ERR_BUSY`。整数値は既存ヘッダから使い、新しい errno を乱造しない。
不正な値は状態を変更せず返す。FFI の有効領域・非 alias・排他的呼出しは C の責務で、任意ポインタのアクセス可能性検査はしない。

## 4. 表・ビルド・障害

ホスト C++ 生成器が元の MakeTable、MakeTable2、PSG::SetVolume、倍率/レート設定の結果を整数配列として出す。sin/log/pow はホストだけで使う。
対象は sine/log/EG、PM/AM、TL、detune/multiple、PSG 振幅・ノイズ等。clock/rate/prescaler ごとの依存係数も焼き、キーごとの表長と hash を manifest に載せる。
OPNA で到達する倍率設定を列挙して全件比較する。OPM 専用表は不要。整数式で置換できる式も、元 C++ の丸め結果との全件一致を条件とする。
ゲストで音量の pow 計算はしない。初版の部品 gain は 0 dB 固定、レジスタ TL/level は通常どおり、全体音量は既存 PCM volume を使う。
クレートは `#![no_std]`・`#![deny(clippy::float_arithmetic)]`、alloc/外部クレートなし、staticlib。固定 nightly、core/compiler_builtins も同一 target、`cpu=i386`、static relocation、SSE/MMX 不使用。
ユーザー用 target の core/compiler_builtins を使い、libos32snd の最終リンク後に到達命令の整数・386 制約を検査する。panic はアプリの異常終了とする。

浮動小数点の監査台帳は `lib/os32_opna/PORT.md` に次の 元関数 と生成キーを全件記録する（N `sound/fmgen/` 基準）。ソースhash変更時は再抽出する。

| 元の箇所 | ホストで焼く鍵・処置 |
|---|---|
| fmgen_fmtimer.cpp SetTimerBase（有効な #if 1） | clock/2、prescaler p=0/1/2 のFM分周6/3/2、さらに/12、timer_step |
| fmgen_psg.cpp SetClock | clock/2、p別SSG分周4/2/1、44100Hz、tone/env/noise各shift |
| fmgen_psg.cpp SetVolume | 0dB、全振幅段；clock非依存 |
| fmgen_fmgen.cpp MakeTable | type・LFO深さ・位相の全組合せ；clock非依存 |
| fmgen_fmgen.cpp SetRatio | p=0/1/2で得る全ratio、dt2=0..3とmultiple全値 |
| fmgen_fmgen.cpp Operator表 | TL/sine/logの全index；clock非依存 |
| fmgen_fmgen.cpp Channel4::MakeTable | kftable全indexを焼く。OPM専用でも共有初期化からの到達を除去確認するまで省かない |
| fmgen_opna.cpp SetVolumeFM | 0dBの整数gain |
| fmgen_opna.cpp MakeTable2 | TL全index；clock非依存 |
| fmgen_opna.cpp adplbase | 入力7987200/2、44100Hz。p非依存（SetRateの式どおり） |
| fmgen_opna.cpp SetVolumeADPCM | 0dBの整数gain |
| fmgen_opna.cpp | OPNB専用で製品から除外。oracleに含む場合はホストのみ |

pは0/1/2、レジスタ2Dh/2Eh/2Fhによる選択。すべての表にキー・長さ・hash・元式の丸めを保存し、ゲストでの再計算を不要にする。clippyに加えfloat型/定数と最終命令の検査も行う。

## 5. DMA 時計・PCM 接続・所有者（R1/R7/R8/R10）

sndl_pump は前回の部分 write の残りを先に送り、空きが無ければ戻る。pos の frame 数から空き cap − (written − played) を取り、最大 1024 frame ずつ各 voice を引く。fmvoice はイベント境界とコア上限 256 frame でさらに分割する。無入力は無音。受理分だけ written を進め、残りは混合済み scratch に保持する。初回 IDLE で cap を満たしてから start。DRAIN は voice 完了まで pump して閉じ、ABORT は即時に閉じる。

今回生成する N frame の先頭を F_r、前回・今回の pump 時刻を u_p・u_n（sys_time_now の µs）とする。N>0 で今回に属するイベント e の適用位置は次式で決める。

    f(e) = F_r + clamp((at_us − u_p) × N / max(u_n − u_p, 1), 0, N−1)

時計差は u32 の wrap を考慮し、積と商は 64 bit 整数。先に未来の入力を次回へ残し、遅着は F_r、同じ frame は FIFO 順。初回は u_p=u_n として到着済み入力を F_r に置く。N=0 は式を評価しない。F_r は未送信 scratch も含む生成済み前線で、render した frame 数だけ進む。部分 write・pause・underrun・epoch 変更で巻き戻さない。

V86 セッションは WRITE と CSM_KEYON を seq・at_us 付きの同じ 512 件 FIFO に積む。半分以上なら OUT 完了後にアプリへ戻る。命令全体を積めない場合は OUT の実行前に戻り、drain→F_r に順次反映→空きがあれば pump の後に再実行する。満杯時の CSM は模型に pending を残す。音源 I/O のないループでも slice でアプリへ戻る。

bank0 status は BUSY=0 と模型の timer flags を即答。bank1 の音由来の status は shadow_seq == pushed_seq の時だけ影を使う。影が古い status と状態依存の data（ADPCM RAM 等）は IOREAD にする。保留中は AL/AX/EAX と EIP を保存し、要求した byte 位置を q[] で渡す。アプリは先行イベントを全件 drain して F_r に順に反映し、render せず fmv_status/read を実行する。影と shadow_seq を更新し、次の run で reply を同じ IN に一度だけ適用する。これで PCM 満杯でも読みが完了する。end・STOP・異常終了は次期限の解除、pending/仮想 IRQ と FIFO の消去、bitmap 復元を共用する。

### ネイティブ SOFT chip の同期 read と pump

L2 が chip ごとに timer_state、audio_state と二つの消費位置を所有する。read と pump は同じ chip 上で直列に実行する。open 時に tm_us を現在時刻に置く。受理した WRITE は元の at_us と受付順を保持し、時刻順・同時刻は受付順でタイマ側に保留する。時刻比較は u32 の wrap をまたぐ差で行う。

1. read(r) と pump(u_n) は共通の期限処理を呼ぶ。指定時刻までの各 WRITE について、タイマ側の適用時刻 t を max(at_us, tm_us) に丸める。旧状態で t まで Count し、各期限で生じた CSM_KEYON をその期限の時刻で音声列の末尾に積む。t と期限が一致すれば CSM がその WRITE に先行する。次に timer 所管のビットだけ opna_timer_write で反映し、音声側に元の at_us の WRITE を引き渡してタイマ側の消費位置を進める。最後に指定時刻までの残りの期限を処理し tm_us を更新する。未来の WRITE は保留し、同じ時刻までの再読では Count の経過も新たな発火もゼロになる。
2. 音声列はこの引渡し順を保つ。各項の frame は AV_HAL_DESIGN §7-4 の式で求め、遅着で先行項より前になる場合は先行項の適用位置まで繰り上げる。pump は境界まで render して WRITE の音声部分または opna_apply_csm を一度実行し、音声側の消費位置を進める。音声側の WRITE は timer の load/clear・Count・CSM 発火を繰り返さない。部分 write の再送対象は混合済みの bytes だけである。
3. timer status の同期 read はタイマ側だけを消費する。音声状態に依存する read では、読みに先行する音声列を F_r で順に消費してから fmv_read/status で値を返す。この場合も render は不要で、ADPCM データの読取りは要求ごとに一度だけ行う。pump は残った音声列から再開する。V86 では V1 の C 模型がタイマ側を所有し、L2 は受け取った WRITE・CSM_KEYON の音声側だけを処理する。

### L2 のリズム波形

停止中にアプリ文脈で mono PCM16 LE の WAV 6 本を読む。RIFF の form 種別が WAVE であることを確認し、異なる場合は INVAL を返す。RIFF の宣言長と実ファイル長の内側で fmt/data のヘッダ・本文を検査し、未知 chunk も奇数長なら pad 1 byte を含めて次へ進む。形式、正の rate、偶数の data 長、固定小数点位置に収まる frames を確かめる。構造・範囲違反は INVAL、読取り失敗・短い read は IO。検査済みの全 6 本を揃えて opna_rhythm に渡す。欠損時は rhythm-ready=0 とし、他の音源は継続する。C 所有の波形は reset を跨いで保持し、voice の停止と参照解除の後に解放する。

## 6. snd・装置選択・V86（R2/R3/R5/R7）

V86 の FM 選択は snd.fm=auto/hw/soft/none と chip の能力・空きに従う。既定の OPN 要求は実機 OPN/OPNA を優先し、PCM が使える Pentium 以上では SOFT に移れる。取得不能時は読み FF・書き捨てとする。HW の取得者だけが実ポートを使い、取得・返却時にリセットする。

FM_SOFT の間は 0188h–018Fh と A460h を捕捉し、幅 2/4 を連続 byte 順で処理する。bank 別 latch を持ち、奇数 port は FF・書き捨て。A460h は 70h＋bit0、bit0=0 で bank1 を隠す。実 IRQ12 は開かず、guest 復帰前の通常文脈で timer・CSM と仮想 IRQ12 を処理する。

## 7. 大きさ・移植性

合成器のカーネル常駐量は 0。アプリが state、256 KiB の ADPCM RAM、波形、scratch を所有する。合成は IRQ stack を使わない。PC-98 固有の trap は platform/pc98/legacy/ に置く。共有像とアプリ別の頁予算は [SNDL_LOAD_DESIGN](SNDL_LOAD_DESIGN.md) §3 を参照。

## 8. ファイルごとの変更一覧・実装順

- 段 A は lib/os32_opna/opna.h、PORT.md、fmgen の説明書 4 原文、README.OS32、THIRD_PARTY.md。opna.h が内部 C ABI の正典で、公開 KAPI にしない。opna_init/reset/rhythm/write/read/status/count/next_event/render の型・返値・副作用と private timer/CSM の宣言を保つ。固定寸法・alignment の state、256 KiB の ADPCM RAM とリズム波形を C が所有し、Rust が借りる。reset は RAM と波形を保つ。
- 段 B は元 C++ による gen_opna_tables と生成 const、state.rs。入力 7,987,200 Hz、出力 44,100 Hz、補間 false、各部 0 dB。prescaler 3 値ごとの表・長さ・hash・丸めを照合する。浮動小数点を要する生成はホストで完了する。
- 段 C は operator・channel・psg・adpcm・rhythm・timer。FM 6ch×4 operator、SSG 3ch、リズム 6 音、ADPCM-B、timer A/B・CSM の整数移植を保つ。段 D の registers.rs、lib.rs の FFI と混合、Cargo の staticlib も保つ。render は 0〜256 frame、FM→SSG→ADPCM→rhythm を i32 で加算して最後に飽和し、s16le stereo で返す。render は timer を進めない。
- 製品は no_std・alloc 無し・整数のみ。クレートに `#![deny(clippy::float_arithmetic)]` を置き、Clippy・float 型/定数の検査・最終リンク後の命令検査を行う。PORT.md に原関数と Rust 関数、NP21/W revision、元ファイルの SHA-256、整数幅と丸めを記録する。原文は ~/np21w-src/src/sound/fmgen/ の fmgen_*.cpp と fmgen_*.h。OPNBase/OPNABase/OPNA と依存部分を移植し、FileIO・wrapper・OPM/OPNB は製品から除く。
- fmgen の条件（出所、フリー配布、改変の明示、原文添付）を引き継ぐ。cisc の著作権と NP2/NP21/W の改変履歴を保ち、商用組込みは作者合意を要する。上記ディレクトリの fmgen_readme.txt、fmgen_readme_kai.txt、fmgen_readme_np21w.txt と ~/np21w-src/src/LICENSES/fmgen/fmgen_readme_np2.txt を無改変で lib/os32_opna/ に置く。README.OS32 に由来と Rust 化・確保除去・表生成・入出力変更・timer 分離を記し、THIRD_PARTY.md から案内する。像作成 manifest に配置を登録し、配布像の /sys/licenses/fmgen/ に 4 原文と README.OS32 を収め、原文の hash を照合する。

コアの後は L2 が音源を voice に接続し、L3 が L1 の混合結果を PCM stream に送る。L5 の共有像をアプリから呼ぶ契約は [SNDL_LOAD_DESIGN](SNDL_LOAD_DESIGN.md) に従う。

## 9. 受入 → 試験・変異

- コアは固定 revision の元 C++ を SUPPORT_FMGEN と host 専用 compiler.h shim でビルドする。同じ時計・波形・適用 frame 列で FM 全 algorithm、SSG、6 音、ADPCM RAM/repeat/EOS、timer/CSM の全 PCM frame・status・read を完全一致させる。全表・prescaler、1/17/256 frame 分割、32767/32768 µs、wrap、ABI 寸法・align、無効入力の状態不変、左右・飽和・LE を検査する。
- 時刻は N=4、区間 100〜200 µs で 99/100/150/200 µs が先頭から 0/0/2/3、201 µs が次回になることを確認する。同 frame の逆順、clamp 除去、epoch での前線 reset を検出する。
- pump は初回満杯で一度だけ start。空き 0・部分受付・再開時の出力を一括受付と byte 比較し、再 render、重複、欠落を検出する。NP21/W の固定入力試験ではゲストと元 C++ に同じ manifest（指定 frame・レジスタ列・ADPCM パターン・自作 6 波形）を渡す。実時刻入力試験ではゲストが実際に適用した frame・イベント順を記録して元 C++ に渡す。両試験で SOFT を明示し、既知の左右マーカと frames_total で DMA 採取区間を揃え、欠落ゼロ・同一長の s16le の md5 一致を確認する。低遅延設定の実機で pump 間隔 10 ms 以内と underrun の有無を測る。
- V86 は半満・満杯での OUT 再実行と CSM pending を含む seq 列を照合する。幅 2/4、A460h の 70→71→70、SOFT の実 I/O ゼロ、無 I/O の slice 復帰、終了経路の復元を確認する。timer は NB=230 の 10 秒の IRQ12 回数と CSM 順を oracle と比較し、IF/STI・IMR・EOI・clear を検査する。
- ADPCM 開始直後の古い影を拒否し、PCM 満杯でも IOREAD が往復することを確認する。幅 4 の IN で同期対象 byte と即答 byte が正しい位置に戻り、RAM 読みの副作用と EIP の前進が各一度であることを検査する。影の seq 比較除去・reply の再利用を変異で落とす。

- ネイティブ chip では timer A の CSM 期限と同時刻に clear/停止を書き、後続に ADPCM RAM 書きと未来の WRITE を置く。PCM 満杯で timer status を二度読み、tm_us より古い WRITE を追加して再読し、空きを戻して pump を二度呼ぶ。timer の load/clear は各一度、期限の CSM は同時刻の停止より先に一度だけ音声へ届く。遅着で tm_us は戻らず、未来の WRITE は期限前に現れない。音声側の RAM 書込み・アドレス増分も一度だけであることをイベント列と RAM 内容で照合する。途中で音声依存 read を挟んでも、そこで消費した接頭部を pump が再適用しないことを確認する。
- L2 に奇数長の未知 chunk と pad を持つ自作 WAV 6 本を渡し、全音の PCM を比較する。1 本欠損では rhythm のみ無音。RIFF 外へ伸びる chunk、pad 欠落、WAVE 以外の form、非 mono/16bit は INVAL、読取り途中の短い read は IO とし、失敗後に半端な波形が参照されないことを検査する。
- host oracle は固定した元の fmgen 翻訳単位を `-DSUPPORT_FMGEN -Itools/tests/opna_host` でビルドする。compiler.h shim は標準の stdint/stdio/string、必要な型・呼出規約・MAX_PATH に限り、fmgen_types.h の型を二重定義しない。必要な識別子を PORT.md に記録し、host の ulong 幅を static assert と比較で確認する。FileIO の未解決参照に必要な場合だけ元の fmgen_file.cpp を host にリンクする。
- tools/checks/check_opna.py の CHECK に Rust/C・ABI ヘッダ・表生成元と生成物・oracle/shim・guest runner と manifest・関連試験と変異を宣言し、runner の lint と入力変更時の選択を確認する。
- 配布像から /sys/licenses/fmgen/ の 4 原文を読み戻し、それぞれ段 A の移植元 SHA-256 と一致すること、README.OS32 も収録されることを検査する。manifest の登録漏れ、原文の改行変換は不合格とする。

## 10. 決定（ユーザー判断 3 件）

2026-11-05 に推奨案を採用した。リズム素材は同梱せず、OPN だけの機械は HW の OPN を既定にする。V86 の起床源は V0 で PCM IRQ と CS4231 I20/I21 の共存を資料確認し、使用できなければ PIT one-shot を検証する。双方不可なら 100 Hz 制限をユーザーに諮る。

## 11. 反論

r3 のレビューで、同期 read と pump の二重適用、リズム波形の検査、比較元の再現、配布像の原文照合を §5・§9 に反映した。
