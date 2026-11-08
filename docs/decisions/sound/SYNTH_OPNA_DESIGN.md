# PCM 合成器（ソフト OPNA）詳細設計 r2

> 出所: ~/os32-tmp/run/synth-design/DESIGN.final.md (astra 起草 r1〜r2、Opus レビュー 2 回、PM が確定)
> 状態: **現行 (2026-11-05)** — 段の状態は票 TASK_SYNTH_OPNA が持つ。

> 作成: Codex（実行モデル ID は環境から確認できない）。パスの基点は `/home/hight/os32-v3/`、移植元 N は `/home/hight/np21w-src/src/`。

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
採用・移植・Rust は決定済み。「fmgenは採用です」「fmgenは無理に手本でなく移植で大丈夫です」「PCMの合成機は先に作れるのでは？」に従い、純粋合成器を先行する。
`V3_PLAN.md:70-71` の合成器と効果音を区別する。後者の PCM 生成はユーザーの明示実行時だけで、起動時生成を入れない。
以下の食い違い **6 件**を接続設計に反映する（機能が未実装の箇所は不具合とは扱わない）。

| ID | 票・想定と実物 | 設計への反映 |
|---|---|---|
| D1 | `RUST_VS_C11.md:67`（N1）の NP21/W 生 frame 比較は、そのまま FM 比較にはならない。N `win9x/aidebug/aidebug_api.cpp:843-889` は CS4231 DMA の採取 | 元 C++ と Rust の比較と、CS4231 転送比較を分ける |
| D2 | NP21/W が常に fmgen とは限らない。N `win9x/ext/opna.cpp:142-182,392-430` は設定・能力別に分岐、API `:772` が `usefmgen` を返す | FM 直接出力の実装識別に限る。CS4231 DMA 比較の前提にしない。稼働値は未確認 |
| D3 | `RUST_VS_C11.md` §1-3 は描画 256 frame を想定するが、`pcm_cs4231.h:119-125` は半分 2048・リング 4096・ステージング 4096 frame、`.c:151-164,385` はコピーだけ | 256 frame は計算の単位。DMA 半分を変更せず、既存ステージングへ供給する |
| D4 | V86 提供は未接続。`platform/pc98/legacy/v86_io.c:97-99` は 0188h–018Fh を素通し、`:387-434` にソフト OPNA の処理なし、`v86.c:500-503` は実 IRQ を開く | ソフト選択時だけ trap、仮想 IRQ12 を接続 |
| D5 | 「表を const 化」だけでは alloc/浮動小数点は消えない。N `fmgen_opna.cpp:1436,1485-1568` は ADPCM RAM と外部 WAV を確保、`:540`・`fmgen_fmgen.cpp:262-266`・`fmgen_psg.cpp:89-91` は設定時の浮動小数点 | RAM は C 所有、WAV は通常文脈で準備、設定用係数もホスト生成 |
| D6 | `RUST_VS_C11.md` §1-3 の「panic を無音化」と §3-2 の「panic→kpanic」は異なる。abort panic は通常の戻り値に戻せない | 回復可能な障害は Result→無音、実 panic は診断付き停止。unwind/longjmp による復帰はしない |

`TASK_PCM_CS4231.md:255-258` の E4 は合格、E3 に未実施項目、E5 は未、E6 は部分。合成器先行の妨げではないが、PCM 全体の受入完了とは記さない。
`drivers/sound/fm/fm.c:54-86` の `opn_write_checked` は実ポート・BUSY 待ち専用。`snd_engine.c:4,207-210` は tick と既存 FM 関数で発音する。
`fm_module.h:24-35` の snd API、`pcm_module.h:10-20` の PCM API は既存の公開面を維持する。

## 2. 分担・移植範囲・条件
経路: `snd / V86 → C の時刻・所有者制御 → Rust OPNA → s16le stereo → PCM staging → CS4231 DMA`。
Rust は FM 6ch×4 operator、SSG 3ch、リズム 6ch、ADPCM-B、レジスタ、タイマを持つ。I/O・VFS・DMA・IRQ 登録・メモリ確保は持たない。
N `sound/fmgen/` の `fmgen_fmgen{,inl}.{cpp,h}`、`fmgen_psg.*`、`fmgen_fmtimer.*`、`fmgen_opna.*` の OPNBase/OPNABase/OPNA と必要な型を移植する。OPM/OPNB、保存像、FileIO、C++ wrapper は製品に入れない。
元の関数→Rust 関数の対応表・元ファイルの SHA-256・NP21/W revision を `PORT.md` に固定する。ホストの比較対象はこの快照とする。
fmgen 由来部分を MIT に付け替えない。cisc の著作権・出所・Rust 化/確保除去/表生成/入出力変更を明示する。
`fmgen_readme.txt`・`fmgen_readme_kai.txt`・`fmgen_readme_np21w.txt` と N `LICENSES/fmgen/fmgen_readme_np2.txt` をバイト単位で無改変添付する。配布像の配置は §8。NP2/NP21/W の改変記録も保存する。商用組込みは既決の条件どおり作者合意が必要。
別の GPL/LGPL 音源を取り込まず、許可リストで入力ソースを排除する仕組みは作らない。
初版は OPNA 入力クロック **7,987,200 Hz**・出力 **44,100 Hz**・補間指定 false・各部 0 dB。N `win9x/ext/opna.cpp:146` の倍クロック契約を保つ。
`OPNABase::SetRate`（N `fmgen_opna.cpp:534-547`）内部の /2 を二重に適用しない。レジスタの prescaler 変更は移植範囲。
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

`state` は C がロード時に確保した固定サイズ・固定 alignment の領域（見積り 16–32 KiB）、ADPCM RAM は別の 256 KiB。init は in-place に書き、大きい一時構造体をスタックに作らない。
波形は C が所有する読み取り専用領域で、reset を跨いで有効、停止・callback 解除の後にだけ解放。Rust がポインタを保つ範囲はこの契約に限定する。
通常文脈の `synth_samples_load(dir)` が RIFF/WAVE の fmt/data chunk を境界検査し、mono PCM16 の6本を C の確保領域に読む。奇数 chunk の padding を処理し、範囲外・短い read は INVAL/IO。元 FileIO の固定 offset や過大確保は移植しない。
`opna_rhythm` は停止中のみ。6 本が揃うまで rhythm-ready=0、未提供はリズムだけ無音。PCM mono 16bit LE、rate>0、位置の固定小数点が溢れない frames 上限を検査する。
レジスタは bank 込み 0x000–0x1ff、値は 0–255。範囲外は INVAL、範囲内の未使用レジスタは元 fmgen の無視/読み値を保つ。0x100 の ADPCM data port の副作用も移植する。
reset の RAM 保持は元実装に合わせる。新規 init 時の RAM は C がゼロ化する。reset で波形を読み直さない。
render は 0–256 frame、出力は上書き、0 frame は何もしない。内部 i32 L/R scratch は state 内の 512 要素。C は有効な `4*frames` bytes を渡す。
N `fmgen_fmgen.h:21` は i32 Sample、`fmgen_fmgeninl.h:246-252` は加算。FM→SSG→ADPCM→rhythm の順を保持し、全部を加算した後にだけ [-32768,32767] へ飽和し LE 化する。
`opna_count` は元 Timer::Count を移植、render 自体は timer を進めない。C/Rust の双方で各呼出しを `delta_us <= min(next_event,32767)` に分割する（イベント無しも32767以下）。期限到達を処理してから次の期限を取得し、0を返す未処理イベントで空回りしない。u32 の長い差分を int32 へ直接 cast しない。timer A/B status・clear・CSM の副作用、ADPCM EOS/BRDY/mask を保持する。
回り込みは明示 `wrapping_*`、符号付き右 shift・切捨て・中間幅を元処理に合わせる。除算 0・shift 範囲・添字範囲は入口または固定型で解消する。
C++ の未定義動作が比較で露見したら、該当式と採用する幅を PORT.md に記録して両側を同じ定義に直す。差を許容誤差で隠さない。
返値は 0/既存 `OS32_ERR_INVAL`・`NOSYS`・`IO`、メモリ不足は `PCM_ERR_NOMEM`、占有中は `PCM_ERR_BUSY`。整数値は既存ヘッダから使い、新しい errno を乱造しない。
不正な値は状態を変更せず返す。sourceのFIFO満杯等は§5の別の実行時fault契約に従う。FFI の有効領域・非 alias・排他的呼出しは C の責務で、任意ポインタのアクセス可能性検査はしない。

## 4. 表・ビルド・障害
ホスト C++ 生成器が元の MakeTable、MakeTable2、PSG::SetVolume、倍率/レート設定の結果を整数配列として出す。sin/log/pow はホストだけで使う。
対象は sine/log/EG、PM/AM、TL、detune/multiple、PSG 振幅・ノイズ等。clock/rate/prescaler ごとの依存係数も焼き、キーごとの表長と hash を manifest に載せる。
OPNA で到達する倍率設定を列挙して全件比較する。OPM 専用表は不要。整数式で置換できる式も、元 C++ の丸め結果との全件一致を条件とする。
ゲストで音量の pow 計算はしない。初版の部品 gain は 0 dB 固定、レジスタ TL/level は通常どおり、全体音量は既存 PCM volume を使う。
クレートは `#![no_std]`・`#![deny(clippy::float_arithmetic)]`、alloc/外部クレートなし、staticlib。固定 nightly、core/compiler_builtins も同一 target、`cpu=i386`、static relocation、SSE/MMX 不使用。
最終 `synth.elf` の実行命令を検査し、386 外の bswap/cmov/cmpxchg/xadd 等を拒否、到達コードの x87/SSE/MMX も拒否する。JSON 指定だけで合格にしない。
最終再配置はローダが扱う `R_386_32/PC32` の範囲にする。compiler_builtins と libgcc の提供関数を map で固定し、このモジュールでは Rust builtins 採用を明記する。
通常エラーは C サービスが fault を 1 回計数して以後無音、通常文脈で停止する。panic は診断 C 関数へ渡して kpanic 相当、無言 loop と IRQ 越し unwind は禁止。
panic handler は最終リンク単位に 1 個。既存 os32_lz4 の handler をこのモジュールに混ぜない。Rust 票 §3-3 の既存部品修正は別作業として状態を確認する。

浮動小数点の監査台帳は `lib/os32_opna/PORT.md` に次の file:line と生成キーを全件記録する（N `sound/fmgen/` 基準）。ソースhash変更時は再抽出する。

| 元の箇所 | ホストで焼く鍵・処置 |
|---|---|
| fmgen_fmtimer.cpp:137 SetTimerBase（有効な #if 1） | clock/2、prescaler p=0/1/2 のFM分周6/3/2、さらに/12、timer_step |
| fmgen_psg.cpp:46-48 SetClock | clock/2、p別SSG分周4/2/1、44100Hz、tone/env/noise各shift |
| fmgen_psg.cpp:89-91 SetVolume | 0dB、全振幅段；clock非依存 |
| fmgen_fmgen.cpp:193-218 MakeTable | type・LFO深さ・位相の全組合せ；clock非依存 |
| fmgen_fmgen.cpp:262-266 SetRatio | p=0/1/2で得る全ratio、dt2=0..3とmultiple全値 |
| fmgen_fmgen.cpp:355,371-375 Operator表 | TL/sine/logの全index；clock非依存 |
| fmgen_fmgen.cpp:831 Channel4::MakeTable | kftable全indexを焼く。OPM専用でも共有初期化からの到達を除去確認するまで省かない |
| fmgen_opna.cpp:156 SetVolumeFM | 0dBの整数gain |
| fmgen_opna.cpp:490 MakeTable2 | TL全index；clock非依存 |
| fmgen_opna.cpp:540 adplbase | 入力7987200/2、44100Hz。p非依存（SetRateの式どおり） |
| fmgen_opna.cpp:1708 SetVolumeADPCM | 0dBの整数gain |
| fmgen_opna.cpp:1846,2105 | OPNB専用で製品から除外。oracleに含む場合はホストのみ |

pは0/1/2、レジスタ2Dh/2Eh/2Fhによる選択。すべての表にキー・長さ・hash・元式の丸めを保存し、ゲストでの再計算を不要にする。clippyに加えfloat型/定数と最終命令の検査も行う。

## 5. DMA 時計・PCM 接続・所有者（R1/R7/R8/R10）
新規 synth.mod は Rust staticlib と C サービス、カーネルは薄い facade。単一 OPNA・PCM 排他で、ミキサは追加しない。
**P8 との1行契約: synth の通常文脈 worker が保持した資源 owner と世代付き handle で 44100Hz/s16le stereo の生成済み列を submit し、PCM 側が受付 byte 数を返す（frame=byte/4）；start/finish/abort/status は同じ owner・handle を検査し、IRQ は DMA 補充と起床要求だけを行う。**
参照時点で P8 `DESIGN.r2.md` は未作成のため r1 §3 のみを読んだ。そこでは名称が `pcm_stream_start/finish/close/status`、内部供給は `pcm_stream_submit_owned`。PM の4口 `pcm_start/finish/abort/status` はこれらの private adapter 名とする（abort は close の破棄停止）。既存 static pcm_start と公開 pcm_status のシグネチャを流用せず、private API 表で名前空間を分ける。P8 の将来 r2 で別名になっても停止・所有者の意味を変えない。

```c
int synth_open(int owner);                /* 通常文脈、PCM open と同じ排他 */
int synth_write(unsigned int reg, unsigned int value, unsigned int now_us);
int synth_read(unsigned int bank, unsigned int reg, unsigned int now_us,
               unsigned int *value);
void synth_pump(void);                    /* 通常文脈、生成と owned submit */
void synth_wake_irq(unsigned int now_us);  /* timer/status と要処理フラグのみ */
int synth_finish(int owner);              /* 入力停止、尾を生成し非同期 drain */
int synth_abort(int owner);               /* 入力停止、未再生分を破棄 */
```

P8 の P=公開済み frame、C=DMAへ転送済み frame は維持する。**C は再生済み位置ではない。** PCM private status に D=DMA が実際に消費した累積 frame と position_valid/epoch を追加する。既存 DMA 位置観測から wrap を積算し、resync/restart/位置喪失で epoch を変える。合成器で残カウンタを別読出ししない。P/C/D は u32 差分、差は2^31未満、epoch を跨ぐ差分は禁止。公開 P8 status の consumed を D に読み替えない。

音声の生成目標は **Ptarget=D+6144**。DMA 4096 frame + staging の先行 2048 frame（約46.44ms）が根拠で、staging 容量4096を超えない。µs×44100 を累積して供給量を決めない。開始前に4096 frameのゼロを submit し start が両半面へ移した直後、通常文脈でさらに2048 frameを生成・submit してから guest/演奏処理へ戻る。開始時キーオフならこの2048もゼロになるが、これは空の staging と区別する。
D が進んだ分だけ不足を最大256 frameずつ生成し、PCM の空きまで公開する。補充前 staging は2048以上を目標とし、半面転送後の worker 起床・補充を次の半面期限（46.44ms）より前に完了させる。例えば通常10msごとの pump なら最大約441 frameずつ補充するが、個数の正典は常に D。±50ppm/±100ppm の発振差でも同じ先行量を維持し、音程は実 DAC clock に従う。
生成済み未公開の最大256 frameを scratch に保ち、部分受付の残りを先に再送する。P は受付分のみ進める。未受付を再 render したり音を捨てたりしない。位置無効・epoch変更・追いつけず underrun が出た場合は fault として通常文脈 abort、明示的な再開始まで黙って resync しない。

レジスタ操作と timer は単調化した sys_time_now の µs（PCM と同じ原点、u32 wrap差分）で時刻づけする。ただし既に生成した音を上書きしない。各入力は受付時のPCM position snapshotから適用frameを `max(D_at_input+6144, G)` と定め、`(now_us, sequence, reg, value, apply_frame)` の固定長 FIFO（4096件）に積む。Gは未公開scratchを含む生成済みfrontierでありPとは区別する。pump は次のapply_frameまでを生成してから順序どおり書込みを適用し、そこを跨いで256 frameをまとめない。時刻順と同frame内のsequence順を保存する。初期化列だけは開始前に適用し、開始後の入力を先頭へ遡及させない。通常の聞こえる遅延は概ね139.3ms（6144/44100）、pump 間隔による揺れが加わる。r1 の「未来を先読みしない」「0–1 sample の実時間忠実度」は撤回する。リアルタイム入力の sample 精度ではなく、確定した適用 frame 列に対する合成一致を保証する。
ライブの timer A/B・制御 status は µs で進め、レジスタ受理前にも Count する。IRQ 文脈では timer の小さい制御状態だけを更新し、CSM 発音イベントを同じ FIFO に積む。render 側への CSM 副作用は pump で順序どおり適用する。timer 部と音声部を分け、timer Count を render 側で二重実行しない。§3のABIにprivateな `opna_timer_write(timer_state,reg,value)`・`opna_timer_count(timer_state,delta,&event)` と `opna_apply_csm(audio_state)` を追加し、live timer_stateは別領域、音声側のopna_writeではtimer Count/CSMを発火させない。単体oracle用opna_countはこの分離部品を同じ順で結ぶwrapperとする。IRQ1回の期限処理数にも固定上限（64件）を設け、超過はfault要求として次の通常文脈へ渡す。音声由来 ADPCM EOS/BRDY は生成 frontier で成立した状態を報告し、物理再生済み位置の完了を意味しない。ADPCM RAM 読み等の同期要求は通常文脈 pump で先行入力を反映して返す。oracle にもこの Count/event/Mix 順の adapter を用意する。
FIFO は確保済み、満杯はイベントを落とさず IO fault/停止要求。IRQ と producer の排他は短い irq_save で FIFO・timer snapshot・PCM reserve/publish だけを守る。Rust render・コピー・WAV I/O は IF=1 の通常文脈、worker の再入は禁止。timer IRQ中に synthesis state を直接変更しない。

pump は通常のサービス巡回、snd の安全な復帰点、V86 のホスト復帰 trampoline から呼ぶ。IRQ は work_pending を立て、IRQ epilogue 後に通常文脈 trampoline を必ず通してから guest へ復帰する。V86 がI/Oを全く発行しない busy loop でもこの経路が動くことを受入条件とする。起床から完了までの遅延を測り、協調タスクが長時間譲らなければ保証外として診断する。IRQ に合成を戻すことで穴埋めしない。
停止は新規入力停止→wake 登録解除→通常文脈で worker の非実行を確認→finish または abort→status で停止証明→handle解放→状態/波形解放。停止失敗は PCM 既存隔離規則に従い、参照中 module は unload しない。起動時負荷試験・CPU による NOSYS 判定は入れず、性能は §9 で受け入れる。

## 6. snd・装置選択・V86（R2/R3/R5/R7）
`fm.c` の実ポート関数を fm_hw.c へ分離し、backend HW/SOFT/NONE へ dispatch する。現状は検出関数がなく opn_init が無条件に書くため、新規 probe を通常文脈の初期化時、演奏を止めて1回行う。
probe は0188h statusを複数回読み、連続FFhなら無し候補、BUSYは上限付きで待つ。未接続以外の安定 status を得たら0188hへFFhを address として選択し、018Ahから ID を複数回読む。ID=01h を OPNA、非01hで OPNらしい status を OPN候補とする（NP21/W `cbus/board86.c:73-75` は01h、26Kは非SSGで latch を返す）。IDだけで floating bus を OPN と認めない。不確かな機種は静音初期化内で timer B の短い単発を開始し、status bit1 の立上りと clear を上限付きで照合して OPN を確定する。元の timer状態を壊すため、稼働中にはprobeしない。タイマ制御を停止/clearしてから選択したbackendを初期化する。FFh固定・BUSY固定・検証失敗は absent/unknown を区別して記録する。

設定 `fm.backend=auto|hw|soft|none` を通常文脈の設定口と診断コマンドに追加し、演奏停止中にのみ変更する。auto は OPNAならHW、無しかつPCM有りならSOFT、両方無しならNONE。OPNだけの既定は §10(b) 待ち（推奨HW、26Kの既存sndを維持）。unknown は明示診断し、PCM有りならSOFT、無ければNONE。hw強制はprobeで確定したOPN/OPNAだけ、soft強制は実FMが有ってもPCMを選び、PCM無しならNOSYS。noneは無音。選択後は演奏中に自動fallbackしない。強制softなら実FMのprobe自体も省略でき、SOFTセッション中は実OPNA I/Oゼロ。NP21/W SNDboard=0x64 の比較では必ずsoftを明示する。
Ra266は資料間でFM有無が矛盾しており断定しない。HW-BASICで0188h status、FFh ID、必要ならtimer応答を記録して §9 で確定する。

snd の MML・focus・owner_exit・persist・channel借用は維持する。SOFTのBGM/SE最初の開始要求で通常文脈からsynth_openし、初期化列とbank0 pan=L/Rを投入してから開始する。tick IRQからopenしない。終了通知は解放要求のみ、BGM/SE/借用chが全て終了して有限の release tail（最大2秒、以後key-off/quiet）を通常文脈で生成後finishする。status DONEでhandleを閉じる。新しい演奏がDRAININGに重なれば旧sourceの終了またはabortを完了してから再取得する。
PCMアプリが先に取得していればsnd開始はBUSYで無音、勝手に奪わない。snd再生中はPCM側がBUSY。アイドルではsourceを開かない。SEがBGMに重なる場合だけ同じsnd sourceを共有する。SOFT sndの利用権ownerはsndサービスの保持ownerで、曲/SEの依頼ownerと参照数は別管理。依頼owner終了時はその演奏だけ終了、persist移譲は既存規則、最後の依頼が消えたら解放する。常駐サービスownerを理由にPCMを永久占有しない。V86はbeginでsndを通常文脈で休止・解放してguest資源ownerで取得、終了時sndを復元する。
GUI/起動音の追加は別票。PCM効果音はユーザーが明示生成したキャッシュのみ、欠損時無音、起動時生成なし。

V86のSOFT beginは0188h–018FhとA460hをtrapにし、observe/gcap passより先に処理する。bank別latch、0188/018A=bank0、018C/018E=bank1、奇数はFF読み/捨て、幅2/4は連続byteとして分解する。BUSY=0、timer statusはライブCount後の値、非音源GPIO=FF。実IRQ12は開かず、仮想timer levelを既存PICへ渡す。
A460hは86 board IDの固定上位bitと保持bit0を返す。仮想DIP=0の既定ではID上位70h（`pcm86io.c:329` の式）、初期bit0=0で70h、bit0=1を書いた後は71h。書込みはbit0だけを保持し、0なら拡張bankをマスク（read FF/write無視）、1なら有効化する。元 `pcm86io.c:38-44,213-216` の契約に合わせる。bank0 FFh IDは01h。HW時は従来のI/O経路を変更しない。86 PCM FIFO自体は提供せず、対象はOPNAドライバであることを明記する。

**R3 の推奨方式は CS4231 I20/I21 のタイマを使う次期限起床**。timerレジスタ変更・発火・clearごとに opna_next_event から絶対期限を再計算し、装置の分解能へ切上げてarm、IRQで実経過をCountしてpendingを更新する。長い期限は分割、遅れた場合は境界ごとにCountしてstatus/CSMを保つ。実IRQと仮想IRQ12を同一視しない。CS4231のtimer enable/ackとPCM IRQ要因の共存は、実装前にvendor manualと許諾された実装で確認してから追加する。未確認のレジスタ値をこの設計で断定しない。使えない装置ではPIT one-shotを代替候補とするが、既存100Hz時刻/スケジューラの期限も維持する変更が必要で、黙って切り替えない。
ユーザーが §10(c) で100Hz制限を選ぶ場合だけ、PIT+I/O時の判定に縮小する。この場合 NB=230・約133Hz のIRQテンポ再現は保証せず、未処理期間のtimerフラグは合流して100Hz以下となる。過去の割込みを架空の連発で補償しない。選択未決の間はV86 timer完全互換を受入済みとしない。
仮想IRQはホストIRQからguest stackを直接変更せず、安全な復帰点で仮想IF/STI shadowと既存 `v86_pic_should_inject(12,&vector)` を通す。IMR中はlevel保持、EOI後再評価、status clearで解除。end/killは起床源解除→worker停止→PCM abort→pending/latch消去→bitmap復元→snd復帰。巻戻しも同じ順。

## 7. 大きさ・移植性
見積り（未ビルド）: synth.mod text 50–100 KiB、const 表 50–100 KiB、状態/scratch 16–32 KiB、ADPCM 256 KiB、既存 DMA/staging 計32 KiB、リズムは `2*Σframes` bytes。
リズム無しの追加常駐量は概ね 372–488 KiBにイベントFIFO約80 KiBと制御timer状態を加えた約452–568 KiB（既存 PCM 32 KiB は別、FIFOは1件20B×4096想定）。波形を含む正確なピークは採用品決定後に size/map と allocator の最大値で測る。
カーネル facade/結線は概ね 2–6 KiB。Rust と波形は `/sys` の任意ロード部品に置き、起動 FD・MINIMAL・kdebug.bin に埋め込まない。facade 増分は各像の既存容量検査を通す。
IRQ stack の増分目標 <2 KiB、既存 16 KiB 内。再帰・可変長 stack 配列無し、最大発音+CSM+ADPCM 転送時の高水位を測る。
コアは endian を明示し固定幅整数のみ。C ABI のポインタ幅は現行32bit、ホスト試験は64bit用の寸法を別生成。CPU/OS の差は C adapter と target に閉じる。

## 8. ファイルごとの変更一覧・実装順
各行を 1 ファイル=1 段とし原則半日〜1日、500行超見積りは機能境界で再分割してから割り当てる。ヘッダ/manifest/生成表は共有データの例外。下記は実装時の予定で本ラリーでは編集しない。

| 段・ファイル（基点から） | 新/改・行数概算・責務 | 依存/並列 |
|---|---|---|
| A `lib/os32_opna/opna.h` | 新100、ABI/誤り/状態寸法の include | 最初 |
| A `lib/os32_opna/PORT.md` | 新100、移植対応・差分・由来 | ABI と並行可 |
| A `lib/os32_opna/fmgen_readme.txt` | 新、原文を無改変複製 | 独立 |
| A `lib/os32_opna/fmgen_readme_kai.txt` | 新、原文を無改変複製 | 独立 |
| A `lib/os32_opna/fmgen_readme_np2.txt` | 新、N LICENSES/fmgen から無改変複製 | 独立 |
| A `lib/os32_opna/README.OS32` | 新80、元版/hash・改変箇所・条件3の改変記録、zlib同様の案内 | A後 |
| A `THIRD_PARTY.md` | 改約10、fmgenの版/条件/所在/改変と配布対象を追加 | A後 |
| A `lib/os32_opna/fmgen_readme_np21w.txt` | 新、移植元改変記録を無改変複製 | 独立 |
| B `tools/gen_opna_tables.cpp` | 新300、元 C++ の表を抽出 | A 後 |
| B `lib/os32_opna/src/tables.rs` | 新、生成 const（行数上限の対象外） | 生成のみ |
| B `lib/os32_opna/src/state.rs` | 新220、固定状態/借用波形 | A 後 |
| C `lib/os32_opna/src/operator.rs` | 新450、位相/EG/SSG-EG | B 後 |
| C `lib/os32_opna/src/channel.rs` | 新350、algorithm/feedback/FM 混合 | operator 後 |
| C `lib/os32_opna/src/psg.rs` | 新350、tone/noise/envelope | B 後、並列可 |
| C `lib/os32_opna/src/adpcm.rs` | 新450、RAM port/codec/EOS | B 後、並列可 |
| C `lib/os32_opna/src/rhythm.rs` | 新180、6音の位置/level/pan | B 後、並列可 |
| C `lib/os32_opna/src/timer.rs` | 新180、A/B/CSM/status | B 後、並列可 |
| D `lib/os32_opna/src/registers.rs` | 新400、OPNA decode/prescaler | C 後 |
| D `lib/os32_opna/src/lib.rs` | 新250、FFI/初期化/混合/整数変換 | C/D 後 |
| D `lib/os32_opna/Cargo.toml` | 新40、staticlib/host feature | A 後 |
| D `sdk/rust/i686-os32-mod.json` | 新約30、static target | A 後、既存があれば改 |
| D `build/synth-module.mk` | 新100、cargo/link/map/386検査 | D 後 |
| E `drivers/sound/synth/synth_service.c` | 新400、DMA目標/通常pump/排他 | コア差分試験後 |
| E `drivers/sound/synth/synth_samples.c` | 新180、通常文脈のWAV読込み/解放 | E 契約後並列可 |
| E `drivers/sound/synth/synth_module.c` | 新200、常駐 facade/load/unload | E と契約固定後並列可 |
| E `drivers/sound/synth/synth_module.h` | 新100、private API 表 | E 前に固定 |
| E `drivers/sound/synth/synth_body.c` | 新60、export/診断付き panic 終端 | E 後 |
| E `drivers/sound/cs4231/pcm_source.inc` | 新200、P8 owned submit/4口adapter/再生位置snapshot | E 契約後並列可 |
| E `drivers/sound/cs4231/pcm_cs4231.c` | 改約25、include/hook/共通開始停止接点のみ | source 後 |
| E `drivers/sound/cs4231/pcm_module.h` | 改約20、private source slots/版 | source 後 |
| E `drivers/sound/cs4231/pcm_body.c` | 改約10、source export | private API 後 |
| E `drivers/sound/cs4231/pcm_module.c` | 改約50、source facade/quiet連結 | private API 後 |
| F `drivers/sound/fm/fm_hw.c` | 新約90、既存実 I/O 関数の機械的分離 | コアと並列可 |
| F `drivers/sound/fm/fm_backend.c` | 新300、probe/設定/HW/SOFT/NONE/quiet | E 後 |
| F `drivers/sound/fm/fm_backend.h` | 新50、選択/書込み/quiet の共通宣言 | F 前に固定 |
| F `drivers/sound/fm/fm.c` | 改約30、backend 呼出しへ置換 | F 後 |
| F `build/fm-module.mk` | 改約20、backend/import 登録 | F 後 |
| G `kernel/v86/v86_opna.c` | 新300、latch/幅変換/pending/session | E 後、F と並列可 |
| G `kernel/v86/v86_opna.h` | 新40、V86 adapter 宣言 | G 前に固定 |
| G `platform/pc98/legacy/v86_io.c` | 改約25、bitmap と優先分岐だけ | G 後 |
| G `arch/x86/legacy/v86.c` | 改約30、begin/end/IRQ12選択/復帰口 | G 後 |
| F `drivers/sound/fm/snd_engine.c` | 改約80、開始/終端/ownerと通常文脈source要求 | F後 |
| G `drivers/sound/synth/synth_timer.c` | 新250、timer制御/FIFO/期限起床adapter、§10(c)決定後 | E後 |
| H `tools/tests/opna_host/compiler.h` | 新60、ホスト専用最小shim | H前 |
| H `tools/tests/opna_oracle.cpp` | 新250、元 C++ の決定的入力 runner | B と並列可 |
| H `tools/tests/test_opna.py` | 新400、差分/ABI/変異/manifest | C から順次 |
| H `tools/tests/test_synth_source.py` | 新300、供給/所有者/停止/時刻 | E と並列可 |
| H `userland/tests/opna_pcm_test.c` | 新300、SOFTを明示して決定的レジスタ列/適用frameログを出すゲスト | E/F後 |
| H `tools/checks/check_opna.py` | 新100、CHECKで全入力/生成物/guest試験依存を宣言してhost testsを起動 | H後 |
| H `tools/tests/test_v86_opna.py` | 新250、trap/IRQ/復帰 | G と並列可 |

H の oracle は `-DSUPPORT_FMGEN` と専用 include path `tools/tests/opna_host/` を指定する。shim compiler.h は標準 stdint/stdio/string と必要な型・calling convention・MAX_PATH等だけを定義し、NP2の巨大なcompiler.h/OSヘッダを引かない。必要な識別子は対象翻訳単位のコンパイルで列挙してPORT.mdへ固定する。fmgen_types.h自身のuint/int32等を重複定義しない。FileIOがリンク上必要なら元fmgen_file.cppをホストだけにリンクし、製品に混ぜない。64bitホストのulong幅はstatic assert/比較で確認し、32bit型と混同しない。
配布ソースでは4本のreadmeを `lib/os32_opna/` に保存し、`README.OS32` が移植元・Rust化・確保除去・表化・timer分離の改変を記す。配布像では `/sys/licenses/fmgen/` に4原文とREADME.OS32を置き、THIRD_PARTY.mdのfmgen行から案内する。既存の像作成manifestへ配置を追加し、原文hash一致をcheckする。波形素材は別条件で、ライセンス文書に混ぜない。

ビルド・モジュール import の正典定義への各登録はそれぞれ独立の小段とし、実装着手時に生成器の入力ファイルを列挙する。生成物を手修正しない。公開 KAPI の追加は不要。

## 9. 受入 → 試験・変異（未実行）
コア、PCM供給、V86を別々に合格判定する。clock/rate/prescaler/gain/波形hash、Count/event/Mix順とイベント適用frameを証拠として保存する。

| 受入 | 試験・合格条件 | 落とす変異 |
|---|---|---|
| 全音源移植一致 | 元C++ oracleと全frame完全一致。FM全algorithm/feedback/LFO/SSG-EG、SSG全envelope/noise、rhythm全6音、ADPCM RAM/repeat/EOS/pan、CSM | operator順/符号shift/noise周期/nibble順/EOS mask変更 |
| 表/Count | §4全表・p全3値、1/17/256 frame分割、32767/32768µsと長時間経過をnext_eventで分割、u32 wrap | 表1要素、Count二重実行、長いCountの直接cast |
| ABI/障害/形式 | C harness寸法/align、無効入力の状態不変、fuzz、RAM末尾、左右別列・正負飽和・s16le | bank/bounds検査除去、左右交換、途中飽和、BE化 |
| DMA時計と余裕 | hostでstart後C=4096・staging=0を再現し、その後2048を補充。独立DMA/µs時計を±50/±100ppmで各1時間進め、境界直前/直後pump、wrap、partial、満杯を検証。期限内ではunderrun/repeat/resync=0 | µs供給時計へ戻す、初期staging補充削除、CをDとして使う |
| source寿命 | アイドル時PCM open成功、BGM/SE同時・終了tail・persist・owner終了・PCM先取り・kill・停止失敗。IRQ currentが別ownerでも保持ownerでsubmit、世代失効はSTALE | current owner使用、token検査削除、解除後callback、常時open |
| 選択 | NP21/W 26K=HW候補、86+MATE-X auto=HW、同じ環境でsoft強制=SOFT、無し/unknown/PCM無しの診断、FF固定/BUSY固定 | 検出未実装でNONE、soft指定を無視 |
| NP21/W PCM md5 | 下記guest→DMA比較。usefmgenは成否条件でない | 旧半面反復、1frame欠落、区間開始ずらし |
| V86 | bank/read/FFh ID/A460h 70→71→70/拡張mask、幅2/4、IF/STI/IMR/EOI/clear、busy loopでもpump、実OPNA I/Oゼロ、終了復元 | trap漏れ、A460h固定FF、observe優先、実IRQ二重注入 |
| timer起床 | 次期限方式ではNB=230、ゲストが音源I/Oをせず10秒間IRQ12を数える。IF有効・IMR解除時はoracleの周期数と測定分解能内で一致、最大latenessは1ms目標。CSM列も比較。100Hz案ならこの条件は不合格のまま制限承認を記録 | 100Hz判定だけに戻す、期限再arm欠落 |
| 実機 | Ra266の0188h/FFh ID/timer応答をHW-BASICで記録しFM有無確定。全発音+CSM+ADPCMで通常pump遅延+計算+copyが半面期限46.44ms未満（目標20ms以下）、各IF=0総区間<10ms、underrun/repeat/resync=0、stack/map/像容量 | SOFTで実BUSY読出し、期限違反を成功扱い |

NP21/W転送試験の入力源は新設 `userland/tests/opna_pcm_test.c`。SNDboard=0x64を記録し設定口で `fm.backend=soft` を要求、実際のbackend=SOFTを照合する。テスト専用private runnerへ固定register列・ADPCMパターン・自作rhythm6波形・既知左右マーカを渡し、指定frameに対する入力を通常文脈で実行する（量産の公開KAPI追加は不要）。同じmanifestをhost oracleへ渡す。リアルタイム時刻入力の別試験は実際の適用frameログをoracleへ渡して比較する。テスト用の入力方式と実演奏の時刻量子化を混同しない。
`/api/sound?pcm=1` を1秒未満間隔で採取しbase64復号、frames_totalで重複除去。開始4096ゼロと既知マーカで基点を固定、欠落0、同じ長さ/区間のs16le md5一致。sound_board/sound_rateは記録するが、FMのusefmgen/opngen切替はDMA採取の前提でも変異でもない。純粋コアの移植比較相手は§8の元C++ oracleである。md5は転送一致であり音質評価ではない。DAC muteはraw frameと別に最終出力で確認する。

各 Python 試験の依存と変異は `tools/checks/check_opna.py` のCHECK宣言からrunnerへ登録し、対象Rust/C/表生成/ヘッダ/guest manifestの変更で選択されることもlintする。実装時は対象ビルド、関連check-changed（MUTATE=0）、当該変異、lint、386/再配置/IRQ到達命令検査。公開ABI変更が必要なら[ABI]生成/版上げ/clean buildはP8側と揃える。
今回行ったのは設計資料とソースの読取りおよび本書作成のみ。ビルド・エミュレータ設定変更・配備・実機試験は未実施。既存PCM E3/E5/E6残件を合格に替えない。

## 10. 未決（ユーザー判断 3 件）
- (a) **リズム6音の配布素材**: 推奨は初版にROM/WAVを同梱しない。利用可能な6本を通常文脈で読み、未指定ならrhythmのみ無音。試験は自作波形。標準同梱を求める場合は素材と配布権の確認が必要。
- (b) **OPNだけの機械のauto既定**: 推奨はHWのOPN。26Kの既存sndを維持しPCM装置を占有しない。SOFTのOPNAを既定にする案なら6ch等が使える代わりにPCM排他・CPU負荷・音声遅延が加わる。いずれも明示soft設定は提供する。
- (c) **timer IRQの起床源**: 推奨はCS4231タイマで次期限起床、使えなければPIT one-shotを設計検証する。100Hz上限を受け入れる案は実装量を抑えられるが、NB=230など100Hzを超す周期でテンポが遅れる。承認前に100Hz版を完全互換として出さない。

## 11. 反論
なし（0件）。R1〜R11とPM決定を受け入れる。

## 確定 (2026-11-05、Opus 第 2 回「C1〜C3 を書き換えれば確定してよい」、PM が反映 — この節が本文より優先)
- C1 (P2): 世代付き handle と STALE は使わない (PM 決定、P8 の REVIEW.r2 の S2)。L112 の契約は「通常文脈の synth_pump が、attach で持った owner と非公開の token で内部の pcm_source_push(token, data, frames) を呼び、受け付けた frame 数を返す。IRQ は work_pending を立てるだけ」。L137・L146 の handle は token と読む。§9 L234 の「世代失効は STALE」は「detach 後の push は拒否」と読む。
- C2 (P2): L113 は次に置き換え: 内部の口は pcm_source.inc に置く attach(owner)→token・push・finish・abort・status・detach で、すべて token で検査する。P8 の公開の 4 本 (pcm_start・pcm_finish・pcm_abort・pcm_stream_status) とは別の名前にする。
- C3 (P3、PM 決定): L133 の opna_timer_count と CSM の event を FIFO に積む処理は、IRQ の中から、guest へ戻る前に必ず通る通常文脈の trampoline (L136) へ移す。IRQ は DMA の補充と work_pending の起床だけ (L84 のとおり)。
- 残るユーザー判断は §10 の (a) リズムの素材 (b) OPN だけの機械の既定 (c) 100Hz の上限か、タイマで起こすか。Opus 第 2 回は (a)(b) の推奨を妥当、(c) は「実装前に CS4231 の I20/I21 タイマと PCM の IRQ の共存を資料で確かめ、だめなら PIT の one-shot、どちらも駄目なら 100Hz の制限を改めて諮る」の順序込みで諮るのがよいとした。
- 追記 (Opus の P8 r3 の確認、PM): :196 の「4口adapter」は確定 C2 の内部の口 (attach・push・finish・abort・status・detach、P8 の公開 4 本とは別名) と読む。P8 と同じ 4 ファイルは P8 の段 A を先に取り込み、合成器の段 E はその後 (P8 の確定 P1)。
- ユーザー決定 (2026-11-05、7 件とも推奨どおり): §10 の (a) リズム 6 音の素材は同梱しない (b) FM 音源が OPN だけの機械の既定はハードの OPN (c) V86 のタイマの割り込みは PCM のタイマで次の期限に起こす (実装前に資料で確かめ、だめなら PIT の one-shot、どちらも駄目なら 100Hz の上限を改めて諮る)。§10 の未決はこれで決まった。
