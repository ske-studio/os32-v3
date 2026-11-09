> 出所: ~/os32-tmp/run/hal-av/DESIGN.final.md (2026-11-09、Claude 起草・codex astra の 6 回のレビュー、追記 2 は 3 回)。
> 状態: **現行 (2026-11-09)** — サウンド・入力の HAL の確定設計 (確定版 + 追記 1〜3)。段の状態は票 TASK_AV_HAL が持つ。

# サウンド・入力の HAL の詳細設計 (確定版、2026-11-09)

前提 (議論しない): シングルタスク・協調切替 (守るのは割込みと前景の間だけ、並行・競合の防御は足さない。ユーザー方針「基本的に重くなければシングルタスクの前提で進めてください。重いものは判断します。」で、軽い所は単純な方を選ぶ)。9801 互換は考えない。カーネルは整数演算 (float は MMX Pentium 以上を CPU 検査で受ける)。
[C1]〜[D3]・[HW2]・[HW3]・[L1]・[ABI]。1 機能 1 ファイル。HAL の共通部は CPU に依らない。
ユーザー決定の正: ~/os32-tmp/run/hal-av/DECISIONS.md (S1〜S13・I1〜I10・2026-11-09 の第 1 回・第 2 回のチェック、合成器の置き場とミキサ、V86 の FM)。レビュー: REVIEW.r1.md〜REVIEW.r5.md。形の手本: docs/decisions/gui/HAL_V2_DESIGN.md。

## r5→r6 の変更

| ID | 直した所 (§7-6 の表への補完だけ、設計の中身は変えない) |
|---|---|
| R5-P1-1 | R0a・R0b のカーネル側: sdk/kapi.json:45 の includes の snd_engine.h、v86_io.c:264-267 の `snd_state_for_os32` (V86 の往復の戻り側)、fm_module.c:158-160・185・190 の candidate.tick・candidate.snd_set_master・snd_tick の登録、fm_body.c:18・fm_module.c:79-81 の fm_startup_sound。tick の参照の取得を外し、静止は fm_all_off・ssg_all_off・quiet に結ぶ |
| R5-P1-2 | R0c のビルドと配置: build/libs.mk:27・218-229・273 の旧アーカイブ、build/sdk.mk:13・27-31 のヘッダのコピー、userland/deploy.yaml:388-396 の 3 バイナリ |
| R5-P2-1 | tools/hwdev_layout.json:224・226・373-380 は KernelAPI のメンバーの一覧なので**廃止の 10 欄も残す**。AV0 の後の実際の構造体から再生成 (tools/gen_hwdev_contract.py:125-126 の DWARF 照合) |
| R5-P2-2 | 試験と登録: gshell の host 試験 (wm_tests.rs:1708・1750-1784、mocks.rs:889・1012) の snd_focus を av_focus_set に、test_v86_module.py:31 の import の期待を全キーオフに、test_packages.py:89 の sndctl、build/kernel-objects.mk:93・tools/checks/run.py:108 の登録 |
| 独立の洗い出し | 廃止の名前を grep で洗い直し、上に無かった 9 か所を足した (README.md・build/packages.yaml・exec/exec_kapi_init.inc・hwdev/generated/hwdev_layout.txt・sdk/rust/os32api/src/kapi_generated.rs・display_cleanup_host.c・v86_gcap_abort_host.c・k5b_gshell_tdd.md・gshell/src/wm.rs・shell/sh_exec.inc の注記) |

## r4→r5 の変更

| ID | 直した所 |
|---|---|
| R4-P1-1 | 再同期は元の状態を保つ。PAUSE 中なら無音のリングだけで再始動し、実データを詰めるのは明示の start の後 (§3-4) |
| R4-P1-2 | voice の記録をバッファの境目 (と周波数・stop の変化) で分ける。voice 数と未完了の記録の数に対する容量を決め、満杯なら生成を止める。「128 frame 以上」の保証は外した (§7-3) |
| R4-P1-3 | MML の廃止の結線を R0 に全部列挙 (export 表・include・build と import の定義・shell の play/beep・残る試験と CHECK と変異)。呼び手は除去か廃止の応答へ (§7-6・§12) |
| R4-P2-1 | 位置の式を `in = in_first + (((u64)(F − out_first) × step16 + phase16) >> 16)` に固定、非零の in_first の反例試験 (§7-3・§13) |
| R4-P2-2 | V86 の幅別の結線: v86.c:247 の 66h の opsize を :265 で `v86_gp_emulate` へ渡し、IN/OUT の E5・E7・ED・EF で幅 4 と EAX を扱う。試験は IN EAX,DX (DX=0188h) と IN AL,DX (DX=018Ah) に (§9-2・§13) |
| R4-P2-3 | 廃止の口は既存の型と引数を保つ生成の body: void は無作用、`snd_bgm_is_playing` は 0、`snd_focus` は NOSYS。試験の期待値も口ごとに分ける (§7-6) |

## r3→r4 の変更

| ID | 直した所 |
|---|---|
| U-MML廃止 | ユーザー決定「MMLを一旦廃止にしても良い」。r3 §7-6 (FM の無い機械の MML) と段 M1〜M3 を外し、R3-P1-5 (MML の PCM の寿命) も MML ごと外した。カーネルの snd と MML を呼ぶ口の扱いを §7-6 の一覧に (KAPI は番号を残して NOSYS、版上げ) |
| R3-P1-1 | 連続性を失った時の作り直しを「止める → 止まった証拠 → リングとカーソルの作り直し → 初回と同じ充填で再開」に。止まらなければリングに触らず teardown (隔離)。backend の hw_restart を削除 (§3-4・§3-7) |
| R3-P1-2 | voice の位置: 生成区間ごとに出力 frame の範囲と voice の入力の位置・位相を記録 (§7-3)。stop・play・reset・LOOP の位置と残る音を固定 (§7-2) |
| R3-P1-3 | IOREAD: V86_Exit に読む対象 (bank・reg・種類・byte の位置、最大 4 件) を入れ、reply は byte ごと。v86_gp_emulate.inc の IN を「保留なら EIP を進めず戻る → 次の run で同じ IN を再実行して reply を使う」に結線し、V3 の変更に入れた (§9-2) |
| R3-P1-4 | 影の status の有効性を FIFO の通し番号で判定 (未反映の WRITE が 1 件でもあれば無効)。同期 read の時の反映の順 (全部を F_r に順に反映、render はしない、PCM が満杯でも答えられる) を固定 (§9-3) |
| R3-P2-1 | pause の copied: K の中で最後の**空でない**区間の終端、K が全部空の時だけ played (§3-4-2) |
| R3-P2-2 | SYNTH §5 の private の timer/CSM の ABI (opna_timer_write/count・opna_apply_csm) はコアに残す。削るのはサービスの口だけ。CSM_KEYON は L2 が opna_apply_csm で反映 (§8) |
| R3-P2-3 | SOFT のセッションでは実の IRQ12 を開かない (v86.c:503 を分岐)。timer の解除 (after_us=0)・ack・pending の消去を end・異常の回収で (§9-4、V3・V4) |
| R3-P2-4 | IF=0 の計測を全区間 (初回の充填 16KB・pause の消去・作り直し・advance の写し) の最大と、実機の pump の最大間隔に。期限超えは未合格としてユーザーへ (§13) |

## r2→r3 の変更

| ID | 直した所 |
|---|---|
| R2-P1-1 | IDLE からの初回の start: リングの 0 埋め・区間 0..lead に実データ・カーソルの初期値・hw_start の失敗 (§3-4-1)。v1 は 2 半面 4096 を満たしてから開始 (今と同じ) |
| R2-P1-2 | pause: 消してはいけない区間 (再生中と margin 内の次) は残し、その終端を pause の位置に。copied・seg_n の値と直後の start を固定 (§3-4-2) |
| R2-P1-3 | HAL ライブラリの契約: 開始の担当・voice の停止/reset/破棄・バッファの寿命・部分受付の保持と再送・played からの完了 (§7-1〜7-3) |
| R2-P1-4 | V86 の FM の捕捉: SOFT のセッションで 0188h–018Fh・A460h を TSS の bitmap で拒否 (終わりに戻す)、v86_io_in/out の分岐の先頭に FM、幅 2/4・bank・mask (§9-1)。置き場を platform/pc98/legacy/ に訂正 |
| R2-P1-5 | V86 のセッション begin/run/end、文脈の保存、命令の完了後の FIFO の受け渡し、I/O の無い busy loop の slice での戻り、V86_Exit の検査と KAPI (§9-2) |
| R2-P1-6 | 読みを分けた: timer の status は模型、音由来の status (EOS・BRDY ほか) はアプリが渡す影、ADPCM RAM の読みはアプリへの同期の往復。CSM の発音は FIFO で順に (§9-3) |
| R2-P1-7 | 合成器 (V86 の timer の模型) の起床源は 11-05 の決定のまま: CS4231 の次期限 → だめなら PIT の one-shot → 双方だめなら制限を諮る (§9-4)。backend に `hw_timer_arm` |
| R2-P2-1 | pos と wait を同じ snapshot (played_now・epoch) に統一 (§3-5) |
| R2-P2-2 | open の検査の順・period と lead の範囲・cap の上限・v1 の例外。caps の lead_max を削り ring_frames から計算 (§3-1・§3-5) |
| R2-P2-3 | 合成器の入力時刻: 原点・生成済みの前線・同じ frame の順・遅着・再同期を式で (§7-4)。SYNTH §5 の入力時刻の契約を置き換え |
| R2-P2-4 | 影響節に SYNTH §4・§7・段 D・samples・確定 C1〜C3・11-05 の決定を足し、コアの ABI (変えない) と adapter (新) を分けた (§8) |
| R2-P2-5 | WM: X4 では配送を止めて境目を記録だけ、UI の取消・release の合成・退避の掃除は X3。満杯でも残すキーを取り込む経路 (§5-3) |
| R2-P2-6 | Host Services の MIDI のセッションを owner に結び、正常の close と終了・STOP・切断の両方で reset と解放 (§7-5) |
| U-MML | 未決 1 を決定どおりに: FM の無い機械では fmgen の合成器 (HAL ライブラリ) で snd (MML) を鳴らす経路 (§7-6)。FM の命令の溜め場は V86 と共通の snd2_fmq |
| 名前 | HAL ライブラリは **libos32snd** (userland/lib/snd)。旧 libos32snd (snd_bgm・snd_se_* の MML の薄い包み) は廃止し、名前と置き場を新しい HAL ライブラリが引き継ぐ (ユーザー決定 2026-11-09「libos32sndを廃止して新しい仕組みで統合」。r1〜r6 の libos32snd2 はこの名前に読み替えた) |

## r1→r2 の変更

| ID | 直した所 |
|---|---|
| U-2層 | HAL を 2 層に (§2)。カーネルは列挙・能力・PCM 1 本・位置・DSP。ミキサ・変換・ソフトの FM 合成器・Host Services 越しの MIDI は /sys の共有ライブラリ libos32snd へ。カーネルの snd2_midi_* と MidiBackend は削除 |
| U-合成器 | 合成器 (SYNTH_OPNA_DESIGN の synth.mod) を libos32snd の render (引き出し) 形に移す。影響する節を §8 に列挙 |
| U-V86 | V86 の FM はポートの書きを V86 を動かすアプリへ返し、アプリが libos32snd の合成器へ渡す。V86 の側で変える所を §9 に列挙 |
| P1-1 | 補充を半面の交代から「区間 (segment) ごと + 区間の途中の追補」に作り替え。カーソル 4 本・書いてよい区間・折り返し・0 埋め・連続性を失った時の手順を §3-4 に。S8 の変更範囲を引き直し (§12) |
| P1-2 | played は**実データだけ**を数える (無音は数えない)。played ≤ written を常に保つ。epoch・待ちの失敗・距離 < 2^31 を §3-5 に |
| P1-3 | 状態の遷移と各状態で許す操作の表 (§3-3)。pause は装置を止めずに無音を流す形。v1 の自動開始は統合した口の側で再現 |
| P1-4 | 今の pcm_* を HAL の口に統合 (§3-6)。状態は sound/hal の 1 か所。v1 の振る舞い (4096 frame の自動開始・close の drain と期限・0 = 満杯) は snd2_v1.c で再現。混ぜる検査は置かない (ユーザー決定) |
| P1-5 | 後片付けを `snd2_teardown` 1 か所に (§3-7)。止まった証拠が無ければ DMA を隔離し装置を FAULTED に、部品の取り外しを断る |
| P1-6 | 前景は WM (GUI_SHELL_OWNER かつ gui_register 済み) だけが変えられる。CUI は exec のネストで自動。park/resume では変えない (§4) |
| P1-7 | マウスの独占はアプリが in2_acquire で選ぶ。WM は `in2_wm_state` を見て配送と UI 操作を止め、物理ボタンは追い続け、解除で端を同期 (§5-3) |
| P1-8 | 独占・解除・前景の境目で kbd_raw_buf と WM の pending_raw を「残すキー」以外捨て、WM が配った押下に break を合成 (§5-3)。退避が満杯でも STOP・GRPH+TAB を残す |
| P1-9 | in2 の採取を merge の前 (src ごと) に移す。合成後のフックは置かない (§5-4) |
| P1-10 | `in2_reclaim(owner)` を定義 (§5-6)。前景の交代でも累積移動を 0 に |
| P2-1 | 流れの欄の持ち主・IRQ との区間・部分受理と検査の順・容量の上限 (§3-5) |
| P2-2 | backend に dsp_param、unit の番号 1..dsp_units、unit=0 は外す (§3-2) |
| P2-3 | IN2_Event に flags、シームレスは tick で採って ABS イベントと相対を両方 (§5-2・§5-4) |
| P2-4 | LOST の理由・`focus_gen` で旧前景が喪失を知る・満杯時の挿入・seq の規則 (§5-2) |
| P2-5 | 非前景の vibrate は BUSY で無作用 (§5-1) |
| P2-6 | KAPI ごとの長さ・向き・上限・失敗時の出力 (§10)。生成・C89/Rust・clean→all を AV0 に |
| P2-7 | 統合の段 IS/IA/IP の入口・観測と、反例の変異の対応 (§13) |
| P2-8 | 既知の波形の照合・IF=0 のコピーの上限と計測・書込み停止→再供給・位置欠測の試験 (§13) |
| P2-9 | 依存を一本化: 音の実装と受入は TICK 段 B の後、MIDI は合成器の照らし直しの後 (§12) |

## 1. 実物の確認

### 1-1. 使う口 (file:line)

| 物 | 所在 | 設計での扱い |
|---|---|---|
| 割込み間の frame 数 = I14/I15 (NS-1)、今は 2047 | drivers/sound/cs4231/pcm_cs4231.h:98-99 | 区間の長さ (period) を開く時に書く |
| リング 4096 frame・16KB、REFILL_MARGIN 512 | pcm_cs4231.h:119-131 | リングの大きさは変えない |
| 連続性は DMA の位置だけで判定 | pcm_cs4231.h:186、pcm_cs4231_math.c:110 (`pcm_cont`) | 区間の番号へ一般化 |
| 補充は半面の交代時だけ 2048 frame | pcm_cs4231_math.c:271-288 (`pcm_obs`) | 区間 + 途中の追補へ作り替え (P1-1) |
| 半面の残りを 0 埋め | pcm_cs4231.c:151-164 (`pcm_copy_half`) | 区間の末尾の 0 埋めへ (書き足せば上書き) |
| write で 4096 frame 溜まれば自動開始 | pcm_cs4231.c:622-624 | snd2_v1.c で再現 |
| close: OPEN なら開始して drain、RUNNING は DRAINING | pcm_cs4231.c:679-700 | 同上 |
| 回収: 止めて証拠 (INIT=0・PEN=0)、無ければ FAULTED と DMA 隔離 | pcm_cs4231.c:472-495 (`pcm_release`)・746-779 (`pcm_reclaim`) | 証拠の取り方は backend に残し、順は `snd2_teardown` 1 か所 |
| 部品の取り外しは静止を確かめる | pcm_cs4231.c:783-787 (`pcm_module_quiet`) | FAULTED なら取り外さない (ctx の寿命) |
| 状態 PCM_ST_CLOSED〜FAULTED | include/pcm_contract.h:38-47 | 内部の状態はこれを使い続ける |
| 回収の並び (6) snd_owner_exit・(6b) pcm_reclaim | exec/exec_finish.inc:150-157 | (6b) を `snd2_reclaim` に置き換え、(6c) `in2_reclaim` |
| snd_focus に呼び手の検査は無い | kapi/kapi_generated.c:1910-1913、drivers/sound/fm/snd_engine.c:766-773 | av_focus は呼び手を検査する (P1-6) |
| WM の識別 `GUI_SHELL_OWNER 1`・`gui_register` は shell 帯だけ | kernel/gui.h:24-34 | 前景を変える権限の判定に使う |
| raw キー 32 件、満杯でも CTRL+STOP は末尾を置き換え | drivers/input/kbd.h:55、kbd.c:414-427 | 128 件に。同じ作法を WM の退避にも |
| 本体の鍵盤の配り `kbd_deliver(scancode)` | kbd.c:333 | ここで src 0 として in2 へ (USB の後は kbd_merge_event の頭) |
| WM の退避 pending_raw と STOP の除去 | userland/gshell/src/input.rs:44-58・360-409 | 境目の掃除に同じ形を使う |
| WM のマウスの取り込み | input.rs:209・278-281 (`capture`・`release_capture`) | 独占中は配らない |
| マウス `MouseState`・`mouse_poll` | include/pointer_service.h:27-34、drivers/input/mouse.c:83-102 | mouse_poll は変えない |
| V86 の I/O の許可表と分岐 | platform/pc98/legacy/v86_io.c:96-99 (0188h–018Fh を素通し)・:332 (bitmap)・:391-393 (gcap・観測が先) | §9-1 |
| V86 の実行 | arch/x86/legacy/v86.c:452 (`v86_run(ctx)`)・:470 (`v86_run_limit`)・:793-869 (`v86_boot2` は毎回 IPL を読み終わりに teardown)、userland/cmds/v86.c:180 | §9-2 |
| 合成器の契約 (DMA 時計 D・Ptarget=D+6144・owned submit) | docs/decisions/sound/SYNTH_OPNA_DESIGN.md:113-141 | §8 で置き換え |
| USB の入力の入口 | ~/os32-tmp/run/usb/DESIGN.r5.md:191-195・§4-8 | src ごとに in2 へ (§5-4) |

### 1-2. 食い違い (r1 の 5 件のまま)
1kHz の段 B と `ktime` はまだ無い / KAPI は版 83 / kbd.c は 999 行 / USB r5 の段 1 はパッドの kind と報告の解析器を持たない / RA266.md の FM の矛盾はユーザー決定「PCM だけ」で解決。

## 2. 層の図と境目 (U-2層)

```
 Win32 のゲーム                     DOS のゲーム              OS32 ネイティブ
   │ DirectSound(+DS3D/DSFX スタブ)・waveOut・midiOut・DMusic スタブ   │
   │ DirectInput8・XInput            │ (V86 を動かすアプリ)     │
 ┌─┴ 互換層 (ユーザーランド、[L1]) ─┐  │                          │
 │ HAL ライブラリを呼ぶだけ         │  │                          │
 └─┬───────────────────────────────┘  │                          │
 ┌─┴──── HAL ライブラリ (/sys の共有ライブラリ、MIT の自作) ──────┴──────┐
 │ libos32snd: ミキサ・周波数の変換 (16.16)・音量/パン (dB 表)・         │
 │             ソフトの FM 合成器 (render)・Host Services 越しの MIDI・  │
 │             MIDI 出力装置の列挙                                         │
 │ libos32in2map: PC-98 の sc → DIK・VK の表                               │
 └─┬──────────────────────────────────────────────────────────────────┘
 ══╪════ KAPI (os32_snd2.h・os32_in2.h・av_focus、C89) ════════════════
 ┌─┴ sound/hal/ (カーネル、共通) ─┐ ┌ input/hal/ (カーネル、共通) ─┐ kernel/av_focus.c
 │ 列挙・能力・PCM 1 本・位置・DSP │ │ 装置・raw 128・独占・パッド 4 │
 │ **ミキサ無し**                  │ │                               │
 └─┬ SndBackend ─────────────────┘ └─┬ in2_src_* ──────────────────┘
 drivers/sound/cs4231/pcm_hw.c       drivers/input/kbd_hal.c・mouse_hal.c・(USB の後) xgip/xusb/hid_pad
```

| 境目 | 内側が持つ | 外側が持つ |
|---|---|---|
| backend ↔ sound/hal | レジスタ・DMA の設定・IRQ の要因・位置の読み・止めた証拠 | 状態・カーソル・ステージング・区間・owner・回収の順 |
| sound/hal ↔ KAPI | PCM 1 本 (変換しない、混ぜない) | 形式の選択・混ぜ・合成・MIDI (HAL ライブラリ) |
| HAL ライブラリ ↔ 互換層 | 混ぜの声 (voice)・合成器・MIDI 装置 | DS/waveOut/midiOut の型・GUID・スタブ |
| 入口 ↔ input/hal | 報告の解釈 (sc・相対量・XInput の形) | 時刻・seq・リング・前景・独占・回収 |
| 共通 ↔ PC-98 | `sound/hal`・`input/hal` は include/ の型だけ (番人 tools/check_av_portable.py) | I/O ポート・8237・8251・バスマウス |

- カーネルにミキサは置かない (ユーザー原文「カーネルに直にミキサーを実装するのは反対です」)。HAL ライブラリの状態はアプリの中にあり、アプリの終了で消える。カーネルの回収は PCM の流れだけ。

## 3. サウンドの口 (カーネル)

### 3-1. 列挙と能力 (公開 sdk/include/os32/os32_snd2.h、C89、大きさは AV0 で固定)
```c
#define SND2_KIND_PCM_OUT 1
#define SND2_DEV_PRESENT 0x01  #define SND2_DEV_DEFAULT 0x02  #define SND2_DEV_READY 0x04  #define SND2_DEV_FAULTED 0x08
#define SND2_ENC_S16LE 1  #define SND2_ENC_U8 2
#define SND2_MAX_FMT 16
typedef struct { u32 rate; u8 bits; u8 channels; u8 enc; u8 flags; } SND2_Fmt;   /* 8B */
typedef struct { u32 size, dev, kind, flags; char name[32]; char driver[16]; } SND2_DevInfo; /* 64B */
typedef struct {                                   /* 256B */
    u32 size, dev, kind, fmt_count;                /* 0 */
    SND2_Fmt fmt[SND2_MAX_FMT];                    /* 16 */
    u32 period_min, period_max, ring_frames;       /* 144: period は 2 の冪 */
    u32 rsv_lead, latency_min_us, volume_steps;    /* 156: rsv_lead は 0 (lead の上限は ring_frames/period − 1) */
    u32 hw_mix_streams;                            /* 168: 0 (S8) */
    u32 hw_3d_streams;                             /* 172: 0 (S7) */
    u32 dsp_units;                                 /* 176: 0 = DSP 無し (S13) */
    u32 reserved[19];                              /* 180 → 256 */
} SND2_Caps;
int snd2_dev_count(void);  int snd2_dev_info(u32 dev, SND2_DevInfo *o);
int snd2_caps(u32 dev, SND2_Caps *o, u32 size);    /* size の前方だけ埋める */
```
- CS4231: fmt = {44100,16,2}・{22050,16,2}、`ring_frames=4096`・`period_min=256`・`period_max=2048`、`volume_steps=64`、hw_mix・hw_3d・dsp は 0。表に無い形式は INVAL、カーネルは変換しない (S5)。
- MIDI の出力装置はカーネルの列挙に出ない (libos32snd が列挙する、§7)。

### 3-2. DSP の口 (S13)
```c
#define SND2_DSP_MODE_INSERT 0x01  #define SND2_DSP_MODE_PROCESS 0x02
/* kind: REVERB 1・CHORUS 2・ECHO 3・EQ 4・COMP 5・OTHER 255 */
typedef struct { u32 id, kind, modes, in_fmt_mask, out_fmt_mask, param_count, max_frames, reserved; } SND2_DspUnit; /* 32B */
typedef struct { u32 id; i32 min, max, def; char name[16]; } SND2_DspParam;  /* 32B */
#define SND2_DSP_MAX_PARAMS 16
int snd2_dsp_unit(u32 dev, u32 unit, SND2_DspUnit *o);                  /* unit = 1..dsp_units */
int snd2_dsp_param(u32 dev, u32 unit, u32 i, SND2_DspParam *o);         /* i < param_count */
int snd2_dsp_insert(u32 stream, u32 unit, const i32 *params, u32 n);    /* unit=0 で外す。持ち主だけ */
int snd2_dsp_process(u32 dev, u32 unit, const i32 *params, u32 n,
                     const void *in, void *out, u32 frames, u32 fmt_index);
```
- 番号の範囲外・`n > param_count`・`frames > max_frames` は INVAL。`dsp_units == 0` なら全部 NOSYS (中核で判定)。表は backend の不変の表 (dsp_unit・dsp_param) から引く。最初の実装は表と口の形だけ。

### 3-3. 流れの状態と許す操作 (P1-3)
```c
#define SND2_ST_IDLE 0   /* 開いた・まだ鳴らさない (内部 PCM_ST_OPEN) */
#define SND2_ST_RUN 1    /* RUNNING・RS_STOP・RS_RESTART を含む */
#define SND2_ST_PAUSE 2  /* 装置は回したまま無音、データのカーソルは止まる */
#define SND2_ST_DRAIN 3  /* written まで鳴らして閉じる */
#define SND2_ST_FAULTED 4 /* 止めた証拠が無い。装置ごと使えない */
```
| 操作 \ 状態 | IDLE | RUN | PAUSE | DRAIN | FAULTED | 閉じた番号 |
|---|---|---|---|---|---|---|
| write | 受ける | 受ける | 受ける | INVAL | IO | STALE |
| start | RUN へ (§3-4-1 の初回の充填) | 0 | RUN へ | INVAL | IO | STALE |
| pause | INVAL | PAUSE へ | 0 | INVAL | IO | STALE |
| close(DRAIN) | 未再生 0 なら即閉じ、他は RUN→DRAIN | DRAIN へ | DRAIN へ (再開して) | 0 | 回収へ | STALE |
| close(ABORT) | 回収 | 回収 | 回収 | 回収 | 回収 | STALE |
| pos・wait・volume | 可 | 可 | 可 | 可 | IO | STALE |
- pause と再開の手順は §3-4-2。
- DRAIN の終わり: `played == written` かつ最後のデータの区間が過ぎた時に `snd2_teardown(…, DRAIN_DONE)`。期限は今の `pcm_close_ticks` の式 (pcm_cs4231_math.c:54) で、超えたら ABORT と同じ。close(DRAIN) は期限まで協調で待ってから返る (今の pcm_close と同じ)。

### 3-4. 区間と補充 (P1-1、TICK 段 B の後)
```
リング 4096 frame = nseg 区間 (nseg = 4096 / period、period ∈ {256,512,1024,2048})。区間はリングの端を跨がない。
カーソル (どれも流れの通し frame 番号、u32):
  played ≤ copied ≤ written、 written − played ≤ cap
  played : 鳴り終えた実データ   copied : リングへ写した実データ   written : 受けた実データ
  hw     : DMA の位置 (リング内の frame、backend の hw_pos)
区間ごと: seg_first[s] (最初の実データの番号)、seg_n[s] (実データの数 0..period)
```
- 書いてよい区間: 再生中の区間 `p = hw / period` は書かない。`p+1` は `hw % period < period − margin` の時だけ (margin = min(512, period/2)、今の REFILL_MARGIN と同じ考え)。`p+1 .. p+lead` の範囲。
- 写す規則: 実データは区間の順に詰める。「最初の満ちていない区間」にだけ追補し (末尾の 0 を上書き)、満ちたら次へ。1 回の advance で写すのは最大 `lead × period` frame (CS4231 の低遅延で 768 frame = 3KB、v1 で 2048 frame = 8KB、今の半面の写しと同じ上限)。ステージングの折り返しは `pcm_split` で 2 回に分ける。
- 区間が過ぎた時 (advance が p の変化を見た時、過ぎた区間ごとに): `played += seg_n[s]`、RUN かつ未 pause かつ `seg_n[s] < period` かつ後ろにまだ実データが無かった → `underruns++`。その区間を 0 埋めして `seg_n=0`。
- 途中の位置: `played_now = played + min(hw % period, seg_n[p])` (pos の問い合わせだけ。無音は数えない → **played ≤ written を常に保つ**)。
- 連続性を失った時 (今の `pcm_cont` の偽・watchdog・観測の間隔がリング 1 周を超えた、R3-P1-1): 今の RS_STOP → RS_RESTART の 2 段のまま、順を次に固定する。(1) **止める**: `hw_stop` (IF=0、上限つき)。(2) **止まった証拠**が無ければリングにもカーソルにも触らず `snd2_teardown` (DMA の隔離・FAULTED)。(3) 証拠があれば: `epoch++`、リング全体を 0 埋め・全区間 `seg_n=0`、**`copied = played`** に巻き戻し (ステージングは played まで捨てないので失わない)。(4) **元の状態を保って**再始動 (R4-P1-1): RUN・DRAIN は §3-4-1 と同じ充填で `hw_start`。PAUSE はリングを無音のまま (実データを写さず `copied = played`) `hw_start` し、状態は PAUSE のまま。実データは明示の start (再開) の後の advance から写す。IDLE では DMA が止まっているので起きない。played は失った区間の分を数えない (鳴ったか分からない物は「鳴っていない」)。DMA が走っている間にリングを書かない。
- 補充の担い手: PI (区間ごと) と論理 tick の両方から `snd2_advance(dev)`。段 B の後は物理 1kHz からも。**段 B の前は period ≥ 1024 だけを開く** (S4 の前提、§12)。
- 遅延: `cap = (lead+1) × period` (v2)。低遅延 period 256・lead 3 → cap 1024 frame = 23.2ms @44.1k。既定 period 2048・lead 1 → 4096 frame (92.9ms)。

| 設定 | period | lead | cap (最悪の遅延 @44.1k) | IRQ の間隔 |
|---|---|---|---|---|
| v1 (pcm_*) | 2048 | 1 | 8192 (186ms、今と同じ) | 46.4ms |
| v2 の既定 | 2048 | 1 | 4096 (92.9ms) | 46.4ms |
| 低遅延 (S4) | 256 | 3 | 1024 (23.2ms) | 5.8ms |

#### 3-4-1. 初回の start (IDLE → RUN、R2-P1-1)
- IF=0 で: リング全体を 0 埋め、全区間 `seg_n=0` → 区間 0..lead (v1 は lead=1 なので 2 半面 = 4096 frame、今の pcm_start と同じ) に `copied` から順に実データを写す (`seg_first[s] = copied`、`seg_n[s] = 写した数`、`copied += 写した数`、ステージングの折り返しは pcm_split) → `last_hw = 0`・`hw` の区間 p = 0 → `hw_start`。
- 区間 0 は開始の瞬間から「再生中」なので、区間 0 に入れた分がそのまま最初に鳴る。0 frame の start は全区間が無音で始まる (underrun は数えない: 開始後に一度も実データが無い区間は数えない)。
- `hw_start` の失敗: 証拠つき (DMA を始める前に失敗) なら `hw_stop` は呼ばず、リングを 0 埋め・`copied = played` に戻して IDLE のまま IO を返す (データは失わない)。開始したか分からない失敗は `snd2_teardown` (FAULTED の判定へ)。

#### 3-4-2. pause と再開 (R2-P1-2)
- 消してはいけない区間 K: 再生中の区間 p と、`hw % period ≥ period − margin` の時の p+1 (DMA が触る直前)。
- pause (IF=0): `paused=1`。K より後ろの区間を 0 埋めして `seg_n=0`。`copied` は K の中で**最後の空でない区間** k の終端 `seg_first[k] + seg_n[k]` (例: p にデータ・p+1 が空なら p の終端)。K が全部空の時だけ `played` (K に無いデータはステージングに残っているので失わない、R3-P2-1)。
- pause の位置: played は K の実データを鳴らし終えた所で止まる (最大 2 区間、低遅延で 11.6ms、v1 で 92.9ms)。PAUSE の間の advance は区間の通過だけ数え、写さない (underrun も数えない)。
- 再開 (start): `paused=0`。次の advance から、書いてよい区間 (§3-4 の規則) へ `copied` から写す。pause の位置と再開の間は無音 (数えない)。

### 3-5. 流れの口と欄の持ち主 (P1-2・P2-1)
```c
typedef struct { u32 size, dev; SND2_Fmt fmt; u32 period_frames, lead_periods, flags, reserved; } SND2_StreamDesc; /* 32B */
typedef struct {                                   /* 32B */
    u32 size, state, played, written, cap, underruns, epoch, time_ms;
} SND2_Pos;
int snd2_stream_open(const SND2_StreamDesc *d, u32 *stream);   /* 持ち主がいれば BUSY、装置が FAULTED なら IO */
int snd2_stream_write(u32 stream, const void *buf, u32 bytes); /* 受けた byte 数 (≥0)、誤りは負 */
int snd2_stream_start(u32 stream);  int snd2_stream_pause(u32 stream);
int snd2_stream_pos(u32 stream, SND2_Pos *o);
int snd2_stream_wait(u32 stream, u32 frame, u32 timeout_ms);
int snd2_stream_volume(u32 stream, u32 left, u32 right);       /* 0..volume_steps */
int snd2_stream_close(u32 stream, u32 how);                    /* SND2_CLOSE_DRAIN 0・ABORT 1 */
```
```c
typedef struct Snd2Stream {          /* sound/hal/snd2_int.h。書くのは sound/hal だけ */
    u32 gen; u8 state, paused, v1, closing; int owner;   /* 前景の文脈が書く */
    SND2_Fmt fmt; u32 period, nseg, lead, cap, margin;   /* open で決め、閉じるまで不変 */
    u32 written;                     /* 前景が IF=0 の短い区間で publish */
    u32 played, copied, epoch, underruns;                /* advance (IF=0) だけが書く */
    u32 seg_first[16]; u16 seg_n[16];                    /* advance だけ */
    u8 *stg; u32 stg_frames;         /* ステージング 8192 frame = 32KB (kmalloc、DMA しない) */
    u8 *ring; u32 last_hw, last_obs_ms;                  /* ring は backend が渡す DMA の面 */
} Snd2Stream;
```
- write の検査の順: 番号 (STALE) → 持ち主 (INVAL) → 状態 (§3-3) → buf の NULL (INVAL) → `bytes / frame_bytes` (0 なら 0)。受ける量 = `min(frames, cap − (written − played), stg の空き)`。ステージングへの写しは IF=1、`written += n` だけ IF=0。部分受理は正の byte 数、満杯は 0。
- open の検査の順 (R2-P2-2): desc の size → dev (範囲・PRESENT・FAULTED なら IO) → fmt が caps の表にある → period (0 は既定、2 の冪で `period_min ≤ period ≤ period_max`、段 B の前は ≥ 1024) → lead (0 は 1、`1 ≤ lead ≤ ring_frames/period − 1`) → `cap = (lead+1) × period` (≤ ring_frames ≤ 4096 なので溢れない) → 持ち主 (BUSY)。v1 だけは cap = 8192 (ステージングの大きさ) の例外。
- **公開の位置は 1 つ** (R2-P2-1): IF=0 で `played_now = played + min(hw % period, seg_n[p])` と `epoch` を組で写し取る (snapshot)。pos はこの played_now を `played` として返し、wait も同じ関数で比べる (pos で届いた frame の wait が TIMEOUT にならない)。`done(f) = (i32)(played_now − f) >= 0`。距離: `written − played ≤ cap ≤ 8192` なので比べは常に < 2^31。
- wait (同じ snapshot で比べる): `(i32)(frame − written) > 0` は INVAL (まだ書いていない frame を待つとシングルタスクでは終わらない)。結果: 0 = 届いた / TIMEOUT / STALE (待ちの間に閉じた・回収) / IO (FAULTED)。待ちは sys_yield を回して played を見るだけ (起こす旗は持たない)。
- epoch が変わったら互換層は位置の対応を取り直す (WAVEHDR の完了は played で判定するので、失った区間の分は遅れて完了する)。

### 3-6. v1 (pcm_*) の統合 (P1-4)
| v1 の口 | 統合した口 (sound/hal/snd2_v1.c) |
|---|---|
| pcm_open(rate) | `open({rate,16,2,S16LE}, period 2048, lead 1, v1=1)`、cap 8192 |
| pcm_write | stream_write と同じ。IDLE で `written − played ≥ 4096` になったら start (pcm_cs4231.c:622 の再現) |
| pcm_status(free, counters) | free = `(cap − (written − played)) × 4`、counters は `pcm_counters_pack(underruns, repeats, resyncs)` |
| pcm_set_volume(%) | stream_volume (`pcm_vol_reg` の換算) |
| pcm_close | IDLE で未再生があれば start、close(DRAIN)、期限は `pcm_close_ticks` |
| pcm_reclaim | 削除 (exec は snd2_reclaim を呼ぶ) |
- v1 と v2 の口は同じ流れを操作する。どちらから開いても持ち主は 1 人。混ぜる検査は置かない (ユーザー決定)。KAPI の番号は変えない (pcm_* の target を snd2_v1.c へ向け直すだけ)。

### 3-7. backend と後片付け 1 か所 (P1-5)
```c
typedef struct SndBackend {          /* sound/hal/snd2_int.h、SND_HAL2_ABI 1 */
    u32 abi;
    int  (*caps)(void *ctx, SND2_Caps *o);
    int  (*hw_open)(void *ctx, const SND2_Fmt *f, u32 period, u8 **ring);   /* リングは [HW2] を守った DMA の面 */
    int  (*hw_start)(void *ctx);                    /* リングは中核が埋めてから */
    int  (*hw_pos)(void *ctx, u32 *frame);          /* IF=0。読めなければ <0 */
    int  (*hw_ack)(void *ctx);                      /* IRQ。自分の要因なら 1 */
    int  (*hw_stop)(void *ctx);                     /* IF=0、上限つき。0 = 止まった証拠あり、<0 = 無し */
    void (*hw_release)(void *ctx, int leak);        /* leak=1: dma_mark_leaked でリングを隔離 */
    int  (*hw_volume)(void *ctx, u32 l, u32 r);
    int  (*hw_timer_arm)(void *ctx, u32 after_us);  /* NULL = 無い。after_us=0 で解除。V86 の FM の起床 (§9-4) */
    void (*hw_timer_ack)(void *ctx);                /* タイマの要因を消す (IRQ の中、backend が要因を分けた後) */
    int  (*dsp_unit)(void *ctx, u32 unit, SND2_DspUnit *o);   /* NULL = DSP 無し */
    int  (*dsp_param)(void *ctx, u32 unit, u32 i, SND2_DspParam *o);
    int  (*dsp_insert)(void *ctx, u32 unit, const i32 *p, u32 n);
    int  (*dsp_process)(void *ctx, u32 unit, const i32 *p, u32 n, const void *in, void *out, u32 frames, u32 fi);
} SndBackend;
int  snd2_register(const SndBackend *be, void *ctx, const SND2_DevInfo *info);  /* 戻り dev */
int  snd2_unregister(u32 dev);       /* 流れがあれば teardown。FAULTED なら BUSY を返し ctx を解放させない */
void snd2_advance(u32 dev);          /* IRQ・tick から */
void snd2_reclaim(int owner);        /* exec の (6b) */
static void snd2_teardown(Snd2Stream *s, int why);   /* 唯一の後片付け */
```
- `snd2_teardown` の順 (close(ABORT)・DRAIN の終わり・期限切れ・回収・open の失敗・unregister の全部がここ): IF=0 で `closing=1` (以後 advance は装置にもリングにも触らない) → `hw_stop` → 証拠 → IF を戻す → `hw_release(leak = 証拠なし)` → ステージングを kfree → `gen++`・`owner=0` → 証拠ありは IDLE の装置へ、無しは装置を FAULTED (以後の open は IO、unregister は BUSY、`pcm_module_quiet` は 0 を返して部品は残る)。今の pcm_release (pcm_cs4231.c:472-495) と同じ証拠・隔離の規則で、置き場が 1 か所になる。
- 割込みでする事: `hw_ack`・`snd2_advance` (観測・区間の通過・写し ≤ lead×period)。遅らせる事: 無い (変換・混ぜ・合成はカーネルに無い)。

## 4. 前景 (kernel/av_focus.c、P1-6)
```c
int  av_focus_set(int owner);  /* KAPI。res_owner_get()==GUI_SHELL_OWNER かつ gui_register 済みだけ。他は INVAL */
int  av_focus_get(void);       /* KAPI。0 = 前景なし */
int  av_focus_owner(void);     /* 内部。割込みは 1 語を読むだけ */
void av_focus_exec_enter(int id);  void av_focus_exec_leave(int id, int parent);  /* exec から、GUI でない時だけ効く */
```
| 場面 | 前景 |
|---|---|
| CUI でアプリを起動 (ネストを含む) | 起動したアプリ |
| CUI で子が終わる | 親 (ネストを戻る) |
| GUI | WM が av_focus_set で決める (multiapp.rs の sync_snd_focus の横で呼ぶ)。owner=0 (机) も可 |
| GUI の park / resume | 変えない (WM が決めるまで) |
| 前景のアプリが終わる (GUI) | 0。次は WM が set |
- 替わる時に `in2_on_focus(old, new)` を呼ぶ (§5-6)。音は前景で変えない (S3、今の作り)。

## 5. 入力の口

### 5-1. ゲームパッド (XInput の形、I1・I7)
```c
#define IN2_MAX_PADS 4
/* IN2_BTN_DPAD_UP 0x0001・DOWN 0x0002・LEFT 0x0004・RIGHT 0x0008・START 0x0010・BACK 0x0020・LTHUMB 0x0040・
   RTHUMB 0x0080・LSHOULDER 0x0100・RSHOULDER 0x0200・A 0x1000・B 0x2000・X 0x4000・Y 0x8000 (XINPUT_GAMEPAD と同じ値) */
typedef struct { u16 buttons; u8 lt, rt; i16 lx, ly, rx, ry; } IN2_Gamepad;          /* 12B */
typedef struct { u32 packet; IN2_Gamepad pad; u32 time_ms; } IN2_PadState;           /* 20B */
typedef struct { u16 left, right; } IN2_Vibration;                                   /* 4B */
typedef struct { u32 size; u8 type, subtype; u16 flags; IN2_Gamepad caps; IN2_Vibration vib;
                 u16 vid, pid; u8 source, map_src; u16 rsv; char name[32]; } IN2_PadCaps; /* 64B */
int in2_pad_state(u32 slot, IN2_PadState *o);   /* 未接続 NOTFOUND。非前景は全 0、packet は動かない */
int in2_pad_caps(u32 slot, IN2_PadCaps *o);     /* 未接続 NOTFOUND */
int in2_pad_vibrate(u32 slot, const IN2_Vibration *v); /* 非前景は BUSY で無作用 (P2-5)。振動の無い装置は 0 で捨てる */
```
- 振動を止めるのは前景の離脱と回収だけ (§5-6)。

### 5-2. Raw Input 相当 (I4・I5・I6・I9、P2-3・P2-4)
```c
/* IN2_DEV_KBD 1・MOUSE 2・PAD 3 */
/* IN2_EV_KEY 1・MOVE 2・BUTTON 3・WHEEL 4・PADBTN 5・ABS 6・LOST 7 */
/* IN2_LOST_OVERFLOW 1・FOCUS 2・EXCL 3 (独占の境目) */
/* flags: IN2_EVF_SEAMLESS 0x01 (MOVE がシームレスの差から作られた) */
typedef struct { u32 size; u8 dev, kind, source, flags; u16 vid, pid; char name[20]; } IN2_DevInfo; /* 32B */
typedef struct {                                   /* 16B */
    u32 time_ms; u16 seq; u8 dev, type;
    union { struct { u8 sc, down, mods, rsv; } key;   struct { i16 dx, dy; } move;
            struct { u8 down, changed; i16 rsv; } btn; struct { i16 delta, rsv; } wheel;
            struct { u16 buttons, changed; } pad;  struct { u16 x, y; } abs;   /* 0..32767 */
            struct { u16 reason, count; } lost; } u;
    u16 flags, rsv;
} IN2_Event;
typedef struct { u32 size, acquired, focus_owner, focus_gen, dropped, rsv[3]; } IN2_Status; /* 32B */
typedef struct { i32 dx, dy; u32 buttons; i32 wheel; } IN2_MouseState;                       /* 16B */
int in2_dev_count(void);  int in2_dev_info(u32 i, IN2_DevInfo *o);
int in2_read(IN2_Event *out, u32 max);         /* max ≤ 128。非前景は BUSY */
int in2_key_state(u8 out[128]);                 /* 非前景は BUSY */
int in2_mouse_state(IN2_MouseState *o);         /* 累積、読むと 0。非前景は BUSY */
int in2_acquire(u32 flags);   /* IN2_EXCL_KBD 0x01・IN2_EXCL_MOUSE 0x02 (アプリが選ぶ、P1-7)。非前景は BUSY */
int in2_unacquire(void);  int in2_status(IN2_Status *o);
```
- リング 128 件、全装置で 1 本。`seq` は積んだ 1 件ごとに +1 (LOST も、u16 で折り返す)。MOVE の合算は末尾が同じ dev の MOVE の時だけ (seq・flags は先の物のまま、time は新しい方)。
- 満杯: 捨てて `drop_pending++`・`dropped++`。空きが出た最初の積みの前に `LOST{OVERFLOW, count}` を 1 件 (その 1 件で埋まれば、来たイベントは捨てて count に足す)。CTRL+STOP の make は満杯でも末尾を置き換えて残す (kbd.c:421-426 の作法)。
- 旧前景の喪失: `focus_gen` は前景の交代と独占の境目で +1。アプリは acquire 時の値を覚え、`in2_status` で違えば INPUTLOST (互換層)。新しい前景のリングの先頭には `LOST{FOCUS}`。
- KBD_BUF_SIZE (kbd.h:55) も 32 → 128 (I6)。

### 5-3. 独占と WM (I8・S10・P1-7・P1-8)
```c
typedef struct { u32 size, gen, excl, owner; } IN2_WmState;   /* 16B */
int in2_wm_state(IN2_WmState *o);   /* KAPI。WM (GUI_SHELL_OWNER) だけ */
```
| 状態 | アプリの in2 | kbd_raw_buf (WM へ) | WM のマウス |
|---|---|---|---|
| 非独占 | 全部 | 全部 (今と同じ) | 今と同じ |
| EXCL_KBD | 全部のキー | **STOP・CTRL・GRPH・TAB だけ** (残すキー) | 今と同じ |
| EXCL_MOUSE | 全部のマウス | — | mouse_poll は回して物理ボタンを追う。Pointer/Button を配らず、ドラッグ・閉じる等の UI 操作を始めない。カーソルを隠す |
- 境目 (acquire・unacquire・前景の交代) でカーネルがする事: `gen++`、kbd_raw_buf から残すキー以外を捨てる (`kbd_raw_purge_keep`、kbd_hal.c から kbd.c の 1 行の口を通す)、`LOST{EXCL}` を in2 へ。
- WM がする事 (R2-P2-5、input.rs:8-11 の「X4 では WM の状態機械を進めない」を守る):
  - **X4** (Ctx::Pump): in2_wm_state の gen が変わったら、独占の配送の止め (excl の bit) を**すぐ**効かせ、境目を `excl_gen_seen` に記録するだけ。UI の状態機械・pending_raw・配った押下の表には触らない。
  - **X3** (wm_ui): 記録した境目があれば (1) pending_raw から残すキー以外を捨てる (input.rs:44-58 の discard_stop と同じ形) (2) 前の配り先に、配った押下の break とボタンの離しを合成して送り、進行中のドラッグ・閉じるを取り消す (3) 解除なら、物理ボタンの今の値を基準にして端を作らず、今押されているキーは break まで無視する。
- 退避が満杯の時 (input.rs:366 で読む前に止まる所): 独占中か境目の記録がある間は、満杯でも raw を読み、残すキー (STOP の CTRL 付き make・GRPH・TAB・CTRL) だけを退避の末尾の残すキー以外と置き換えて取り込み、他は数えて捨てる (独占中の kbd_raw_buf には残すキーしか来ないので、捨てるのは境目の前の物だけ)。
- CTRL+STOP は ISR の中断要求 (kbd.c:441) のまま、GRPH+TAB は WM の kbdnav のまま。切替で前景が替われば独占は解ける (§5-6)。

### 5-4. 入口 (input/hal/in2_int.h、P1-9)
```c
#define IN2_SRC_MAX 8
int  in2_src_alloc(u8 dev_kind, u8 source, const IN2_DevInfo *info); /* 戻り dev 1..8、尽きたら FULL */
void in2_src_key(u8 dev, u8 sc, int down, u8 mods);   /* 割込みから可。dev ごとの押下表で繰り返しの make は捨てる */
void in2_src_move(u8 dev, i16 dx, i16 dy, u16 flags);
void in2_src_abs(u8 dev, u16 x, u16 y);
void in2_src_button(u8 dev, u8 down_mask);  void in2_src_wheel(u8 dev, i16 delta);
void in2_src_release(u8 dev);   /* 抜去: 押下とボタンの離しを積んで枠を返す */
void in2_tick(void);            /* 論理 tick から。シームレスの採取 */
u32  in2_time_ms(void);
int  in2_pad_attach(u8 dev, const IN2_PadCaps *caps, const struct Pad2Map *map,
                    void (*rumble)(void *ctx, u16 l, u16 r), void *ctx);   /* 戻り slot */
void in2_pad_report_native(int slot, const IN2_Gamepad *g);
void in2_pad_report_raw(int slot, const struct Pad2Raw *r);
void in2_pad_detach(int slot);
```
| 源 | 採る所 (merge の前、src ごと) |
|---|---|
| 本体の鍵盤 | kbd.c:333 の `kbd_deliver` の前に `kbd_hal_key(scancode)` (src 0)。USB 1D の後は `kbd_merge_event(src, sc, down)` の頭 (src 0 と USB の src の両方がここを通る) |
| USB の鍵盤 | 同上 (usbp_input_key → kbd_merge_event)。合成後の `kbd_deliver_internal` では採らない (二重採取しない)。繰り返し (`usbp_input_key_repeat`) は in2 へ流さない |
| バスマウス | ISR (mouse_bus.c) から `in2_src_move/button` |
| USB のマウス | `usbp_input_mouse(src, …)` で mouse_ext と in2 の両方へ (src → dev は 1 対 1) |
| シームレス (NP21/W) | `in2_tick` が seamless_mouse_poll を読む。最初の 1 回は基準だけ。差があれば ABS と、mouse.c と同じ換算の画素の差で MOVE (flags SEAMLESS)。CUI でも tick が回すので更新源がある |
- 割込みでする事: リングへ積む・押下表・パッドの状態の写し。遅らせる事: 振動の送り (USB の usb_service の文脈)・列挙。

### 5-5. XInput 以外の寄せ方と種類の計画 (I2・I3・I10、r1 と同じ)
- SDL2 の GameController の対応表の文法と SDL_GameControllerDB。使用許諾は zlib の見込み、段 P0 で LICENSE を引いて記録してから使う。`tools/gen_padmap.py` が `padmap.bin` (Pad2Map 64B の表) に変換。カーネルは文字列を解かない。写しは整数 (軸 i16、トリガ `(a+32768)>>8`、hat は 4 bit)。デッドゾーンは掛けない。
```c
typedef struct Pad2Map { u16 vid, pid, ver, flags; u8 btn_src[16]; u8 axis_src[6]; u8 rsv[34]; } Pad2Map; /* 64B */
typedef struct Pad2Raw { u32 nbtn, naxis, nhat, btn; i16 axis[8]; u8 hat[2]; u16 rsv; } Pad2Raw;  /* 36B */
```
- 順: Xbox One/Series (GIP) → Xbox 360 (XUSB、固定 20B) → 一般の HID (報告の解析器、FreeBSD hid.c を手本) → 古い物は padmap に載せるだけ。PC-98 の SSG ポートの 2 ボタンは作らない。

### 5-6. 回収と前景の交代 (P1-10)
```c
void in2_reclaim(int owner);           /* exec の (6c)。ABORTED (STOP) も同じ経路 */
void in2_on_focus(int old, int neu);   /* av_focus から */
```
| 何を | in2_reclaim(owner) | in2_on_focus(old, new) |
|---|---|---|
| 独占 | owner が持っていれば解く (gen++、§5-3 の境目) | old の独占を解く |
| 振動 | owner が前景なら全パッドを 0 | 全パッドを 0 |
| リング・累積移動 (macc)・ホイール | owner が前景なら空・0 | 空・0、先頭に LOST{FOCUS} |
| focus_gen | owner が前景なら +1 | +1 |
- 状態の持ち主: `in2_acq_owner`・`in2_excl`・`in2_gen`・`focus_gen` は前景の文脈 (KAPI・exec・av_focus) だけが書く。割込みは読むだけ。

### 5-7. 互換層との境目 (入力、r1 と同じ)
XInput は in2_pad_* をそのまま / DI8 のキーボードは in2_key_state・in2_read + libos32in2map (sc → DIK) / マウスは in2_mouse_state・in2_read / ジョイスティックは XInput の状態から組み立てる / GetDeviceData は time_ms・seq / SetCooperativeLevel は in2_acquire と focus_gen / FF は作らない、束ねは libos32input。

## 6. 時刻 (I5、r1 と同じ)
`u32 in2_time_ms(void)`: 段 B の前は `tick_count * TICK_MS`、段 B の後は ktime の物理 1kHz。差し替えは input/hal/in2_time.c の 1 行。SND2_Pos.time_ms も同じ。

## 7. HAL ライブラリ libos32snd (U-2層、ユーザー空間、/sys の共有ライブラリ)

状態はすべてアプリの中。カーネルの口は os32_snd2.h (と §9 の V86 の口) だけを使う。シングルタスクなので、ライブラリの中に排他は置かない (呼ぶのはアプリの 1 本の流れだけ)。

### 7-1. 開始と pump (R2-P1-3)
```c
/* userland/lib/snd/libos32snd.h (C89) */
int  sndl_open(const SND2_Fmt *out, u32 period, u32 lead);   /* snd2_stream_open。まだ鳴らさない */
int  sndl_pump(void);    /* 鳴らす物を render → 混ぜ → write。初回は cap を満たしてから start */
int  sndl_close(u32 how);   /* DRAIN: 全 voice の完了まで pump してから stream_close(DRAIN)。ABORT: 即 */
```
- pump の手順: (1) 前回の残り (scratch) を先に write、受けなければ戻る。(2) 空き `cap − (written − played)` を pos で取る。(3) その分を区切り (最大 1024 frame) ごとに、各 voice・合成器から引き出して整数で足す (飽和)。鳴る物が無ければ 0 を足した無音。(4) write。受けた分だけ前進し、残りは scratch に保つ (**再 render しない**)。(5) 状態が IDLE で `written − played ≥ cap` になったら `snd2_stream_start` (初回の開始はライブラリが担当)。(6) 完了の判定 (§7-3)。
- 無音も書くので、pump が間に合う限り流れは underrun しない。呼ぶ時機はアプリのループ・sndl の待ちの口。低遅延 (cap 23ms) では 10ms 以内ごとに呼ぶ必要がある (受入で測る)。

### 7-2. voice (DS の二次バッファ・waveOut、R3-P1-2)
```c
int  sndl_voice_new(const SND2_Fmt *src, u32 *voice);
int  sndl_voice_submit(u32 voice, const void *pcm, u32 bytes, u32 flags, u32 *buf_id); /* LOOP。pcm はアプリが完了まで保つ */
int  sndl_voice_set(u32 voice, u32 freq_hz, i32 vol_cb, i32 pan_cb);  /* 1/100 dB、16.16 の位相、dB は表引き */
int  sndl_voice_play(u32 voice);  int sndl_voice_stop(u32 voice);
int  sndl_voice_reset(u32 voice); int sndl_voice_delete(u32 voice);
int  sndl_voice_pos(u32 voice, u32 *buf_id, u32 *play, u32 *gen);  /* play = 鳴った入力の位置、gen = 生成の位置 (どちらも frame) */
int  sndl_voice_done(u32 voice, u32 *buf_id, u32 *how);            /* how: PLAYED・RESET。無ければ AGAIN */
```
voice は「生成の位置」(次に混ぜる入力の frame と 16.16 の位相) を持つ。生成は流れの空きの分だけ先に進む (最大 cap、低遅延で 23ms・v1 の流れで 186ms)。**生成済みの音は取り消さない** (カーネルは書いた物を戻せない、単純な方を選ぶ)。

| 操作 | 生成の位置 | 生成済みで未再生の音 | 鳴った位置 (pos の play) | バッファ |
|---|---|---|---|---|
| play | そこから混ぜ始める | — | §7-3 の対応から | — |
| stop | その場で止める (次の生成から混ぜない) | そのまま鳴る (≤ cap) | 生成の位置まで進んで止まる | 保つ (LOOP も) |
| play (stop の後) | 止めた生成の位置から続ける | — | 続きから | — |
| reset | 待ち行列を空にし、生成の位置を 0 に | そのまま鳴る (≤ cap) | 残る音が鳴り終えるまで旧の位置、その後 0 | 未完了を全部 how=RESET で done (即座) |
| delete | reset と同じの後に枠を返す | そのまま鳴る | — | 同上 |
- LOOP: そのバッファは reset・delete・LOOP 無しの submit で次へ進む時まで回り、PLAYED の done にはならない。位置はバッファの長さで折り返す。
- バッファの寿命: submit から done で返るまでアプリが保つ。RESET の done の後も、残る音は**ライブラリが混ぜ終えた後**なので、アプリは即座に解放してよい (混ぜた結果は流れの中にある)。

### 7-3. 鳴った位置と完了の対応 (R3-P1-2・R4-P1-2)
```c
typedef struct {            /* 1 記録 = 1 voice × 1 バッファ × 一定の周波数の連続した生成 */
    u32 out_first, out_n;   /* 流れの frame の範囲 (written の番号) */
    u32 buf_id, in_first;   /* そのバッファと、範囲の先頭で使った入力の frame */
    u32 phase16, step16;    /* 先頭の位相と 1 frame あたりの進み (16.16、周波数比) */
    u32 buf_len;            /* LOOP の折り返し用 (LOOP でなければ 0) */
    u8  last;               /* このバッファの最後の入力 frame をこの記録で使った */
} SndlGenRec;
#define SNDL_VOICE_MAX 32
#define SNDL_REC_MAX   512  /* ライブラリ全体の輪。1 件 32B で 16KB */
```
- 記録の切り方: 生成の区切りの中でも、**バッファの境目・周波数の変更・stop/play・LOOP の折り返し**のたびに新しい記録にする (1 記録は 1 つのバッファの中の、一定の step16 の連続した区間だけ)。
- 鳴った位置: `F = played_now`。F を含む記録 r について **`in = r.in_first + (((u64)(F − r.out_first) * r.step16 + r.phase16) >> 16)`** (R4-P2-1)、LOOP なら `in % buf_len`。F がどの記録にも入らない (無音・止まっている) 時は、その voice の最後に鳴った記録の終わりの位置。
- 完了: `last` の立った記録の `end_frame = out_first + out_n` が `(i32)(played_now − end_frame) >= 0` になったら、そのバッファを PLAYED で done。epoch が変わっても式はそのまま。
- 容量: 未完了の記録 = `out_first + out_n > played_now` の物 (それ以前の物は消せる)。pump は生成の前に空きを見て、**次の記録を置けなければその voice の生成をそこで止める** (この pump では空きの分まで無音を足す)。played が進んで記録が消えた次の pump から続ける。1 frame のバッファを大量に送るような使い方では間に合わず無音が入る (互換層の作り方の制限として記録し、受入で 1 voice あたりの最小のバッファの長さを測る)。

### 7-4. 合成器の入力の時刻 (R2-P2-3、SYNTH §5 の入力時刻の契約を置き換え)
```
F_r   : 合成器が次に生成する流れの frame (生成済みの前線)。render のたびに進む
u_p   : 前回の pump の時刻 (sys_time_now の µs、u32 の差で扱う)、u_n : 今回の pump の時刻
N     : 今回の pump で合成器から引き出す frame 数
入力 e (at_us、FIFO の順) の適用 frame:
  f(e) = F_r + clamp( (at_us − u_p) × N / max(u_n − u_p, 1) , 0, N−1 )   (64 bit の整数演算)
```
- 同じ frame に入る入力は FIFO の順で適用。`at_us < u_p` (遅着、前の pump の区間) は `F_r` に適用し、**生成済みの音は直さない**。`at_us > u_n` は次の pump へ残す。
- pause・underrun・epoch の変化は F_r に影響しない (F_r は written 側の数)。合成器の内部時計は生成した frame だけで進む。
- 最初の pump: `u_p = u_n` として全部を F_r へ。

### 7-5. MIDI の出力装置 (S9、R2-P2-6)
```c
int sndl_midi_count(void);  int sndl_midi_info(u32 i, SNDL_MidiInfo *o);  /* 0: ソフトの FM 合成器、1: Host Services (使える時) */
int sndl_midi_open(u32 i, u32 *h);  int sndl_midi_short(u32 h, u32 msg, u32 at_us);
int sndl_midi_long(u32 h, const void *p, u32 n);  int sndl_midi_reset(u32 h);  int sndl_midi_close(u32 h);
```
- ソフトの FM 合成器: GM の音色を OPN の音色表で鳴らす合成器の voice。入力の時刻は §7-4。
- Host Services 越し: 既存の host_call の `midi` サービスにセッションを開く。**セッションはアプリ (owner) に結ぶ**。正常の close: all notes off・reset controllers を送ってから閉じる。アプリの終了・STOP・異常終了: カーネルの Host Services の owner の回収がセッションを閉じ、host_agent.py は**セッションの close と回線の切断の両方で**ホストの MIDI 出力に all notes off と reset を送る。担当は L4 (ライブラリ側) と host_agent.py (ホスト側)。ホストのライブラリの使用許諾は L4 で確かめる。

### 7-6. MML (snd) の廃止 (U-MML廃止)
ユーザー決定「MMLを一旦廃止にしても良い」。カーネルの MML の機関と、それを呼ぶ口を外す。KAPI は [ABI] どおり**番号を消さず**、既存の型と引数を保ったまま生成した body で廃止の応答を返し、版を 1 つ上げる (本書の追加と同じ版上げに含める、R4-P2-3)。kapi.json の各口に `"retired": {"since": 85, "ret": "noop"|"zero"|"nosys"}` を足し、sdk/gen_kapi.py が wrap_* の body を `(void)引数; return ...;` で作る (target は呼ばない。gen_kapi.py:574-581 の出力の検査はそのまま通す)。

| 物 | 扱い |
|---|---|
| KAPI の void の口 `fm_play_mml`・`fm_startup_sound`・`snd_bgm_play`・`snd_bgm_stop`・`snd_se_play`・`snd_se_play_raw`・`snd_set_master`・`snd_bgm_set_persist` (kapi.json:381・386・983〜1023 の型どおり) | `ret: noop` (無作用で戻る) |
| KAPI `snd_bgm_is_playing` | `ret: zero` (0 = 鳴っていない) |
| KAPI `snd_focus` (kapi.json:1419) | `ret: nosys` (OS32_ERR_NOSYS) |
| KAPI `fm_note_on`・`fm_note_off`・`fm_set_tone_num`・`ssg_tone`・`ssg_volume`・`ssg_all_off` (kapi.json:1038〜1078) | **残す** (HW の FM を直に叩く口で MML ではない) |
| 生成物 kapi/kapi_generated.c・include/kapi_call_generated.h・sdk/include/os32/os32_kapi_generated.h | 再生成 (clean → all) |
| drivers/sound/fm/snd_engine.c・snd_engine.h | 削除 |
| drivers/sound/fm/fm_body.c:18・21-35 | `.fm_startup_sound`・`.fm_play_mml`・`.fm_play_mml_kapi`・`.snd_*` の 13 個と `.tick = snd_tick`・`snd_init()` を外す (tick は無し、`.quiet = fm_quiet` は残す) |
| drivers/sound/fm/fm_module.h:5 | `#include "snd_engine.h"` を外し、ops の構造体から同じ欄を外して `FM_MODULE_OPS_VERSION` を 1→2 |
| drivers/sound/fm/fm_module.c:26・79-81・83〜 | `snd_tick`・`fm_startup_sound`・`fm_play_mml(_kapi)`・`snd_*` の包みを削除 |
| drivers/sound/fm/fm_module.c:158-160・185・190 | `candidate.tick` の取得と `tick_target` を外す。取り外しの静止 (:185 の `candidate.snd_set_master(0)`) は `candidate.fm_all_off(); candidate.ssg_all_off();` と `quiet` に。`tick_register(PC98_TICK_SND, snd_tick)` (:190) の登録を外す (番号の定義は残してよい) |
| exec/exec_kapi_init.inc:43-44・119〜 | 廃止の口の `wrap_*` の extern はそのまま (wrap は R0a の生成の body として残る) |
| drivers/sound/fm/fm.c:364・397-456、include/fm_contract.h:50-52 | `fm_startup_sound`・`fm_play_mml_wait`・`fm_play_mml(_kapi)` と宣言を削除 (`fm_all_off`:286・`ssg_all_off`:324 は残す) |
| init/kernel.c:64・233-236 | include を外し、起動の記録の文言を「fm module」に |
| exec/exec.c:37 | `#include "snd_engine.h"` を外す |
| exec/exec_finish.inc:150 | (6) `snd_owner_exit` の行を削除 |
| platform/pc98/legacy/v86_io.c:17・249-261 (`snd_state_for_guest`)・264-267 (`snd_state_for_os32`) | include を fm_contract.h に。往復の**両方**を `fm_all_off(); ssg_all_off();` に (ゲストに渡す前と戻った後の全キーオフ。BGM の機関は無いので master の 0→1 は要らない) |
| sdk/kapi.json:45 の includes の "snd_engine.h" | 外す (R0a) |
| tools/prototypes/v86_imports.def:21-22 | `snd_set_master` を `fm_all_off`・`ssg_all_off` に |
| tools/prototypes/fm_imports.def:7、build/fm-module.mk:4-14 | snd_engine.h を FM_DEF_SOURCES から、snd_engine.o を FM_OBJS から、その規則を削除 |
| tools/hwdev_layout.json:224・226・373-380 → hwdev/generated/hwdev_contract.h・hwdev_layout.txt | KernelAPI のメンバーの一覧なので**廃止の 10 欄も残す** (番号を残すのと同じ)。AV0 の後の実際の構造体から再生成し、tools/gen_hwdev_contract.py:125-126 の DWARF 照合を通す (R5-P2-1) |
| sdk/rust/os32api/src/kapi_generated.rs | 生成物。R0a の再生成で retired の口も型どおり残る |
| build/kernel-objects.mk:93 | CORE_KERNEL_INCLUDES の snd_engine の登録を外す |
| tools/check_arm_compile.py:107、tools/ppc/known_header_dependencies.json、tools/tests/fixtures (dep_dir_a/l01.json・dep_dir_c/manifest.json・build_profile_x86.json)、tools/tests/test_src_layout.py | snd_engine の行を外す |
| userland/shell/cmd_base.c:141-145 (beep = fm_startup_sound)・cmd_sys.c:220-224 (play = fm_play_mml) | コマンドと help の行を削除 (呼び手の除去) |
| userland/tests/bench/main.c:175 | FM の MML の計測の項を削除 |
| userland/tests/raw_wait_test.c:41-83 の `fm` のモード | モードを削除 (STOP で待ちを抜ける試験は kbd・serial・db・log のモードで残る)。tools/tests/raw_wait_guest_host.c の fm の場合も削除 |
| tools/tests/test_raw_wait.py:35-36 (`mml wait probe`・`mml key off` の CASES) と :220-221 (audit.MUTATIONS から next() で取る所)、tools/tests/raw_wait_tdd.md の fm のモードの記述 | 2 件を CASES と変異の一覧から削除し (next() が見つからずに落ちないよう両方から)、raw_wait_tdd.md の記述も外す (R0c) |
| userland/gshell/src/multiapp.rs:1298-1308 `sync_snd_focus` | `av_focus_set` だけを呼ぶ |
| userland/lib/snd の旧 libos32snd.c・libos32snd.h・userland/system/sndctl.c・sndtest.c・sndtest2.c | 削除 (ディレクトリ userland/lib/snd と名前 libos32snd は L1〜L5 の新しい HAL ライブラリが引き継ぐ。R0c と L5 の間は libos32snd が無い期間で、使う側も R0c で消えるので壊れない) |
| build/libs.mk:27・218-229・273 | 旧 libos32snd.a の規則と束ねの行を外す (`INC_libos32snd`・`LIBSND_*` の名前は L5 が新しい中身で足し直す) |
| build/sdk.mk:13・27-31 | `SDK_LIB_HEADER_DIRS` から snd を外し、ヘッダのコピーの行を外す |
| userland/deploy.yaml:388-396 | sndctl.bin・sndtest.bin・sndtest2.bin の配置を外す |
| build/packages.yaml:24 | 注記の sndctl を外す |
| tools/checks/run.py:108 | "snd/libos32snd" の登録を外す (L5 で新しい中身の登録を足し直す) |
| tools/tests/test_packages.py:89 | sndctl の期待を外す |
| userland/gshell/src/wm.rs:1243 | `sync_snd_focus` の呼びを、名前を変えた `sync_av_focus` に (multiapp.rs と同じ段) |
| userland/gshell/host/wm_tests.rs:1708・1750-1784、mocks.rs:889・1012 | snd_focus の偽と期待を av_focus_set に (試験の名前も `av_focus_is_called_once_per_focus_change`) |
| tools/tests/test_v86_module.py:31 | `snd_set_master` の import の期待を `fm_all_off`・`ssg_all_off` に |
| tools/tests/display_cleanup_host.c:29・v86_gcap_abort_host.c:148 | 同名のスタブ `snd_state_for_os32` は**保持する** (v86_io.c の static 関数の名前は変えず中身だけ全キーオフにするので、包みを持たない試験の抽出がこのスタブを要る。スタブだけを消すと未定義になる) |
| tools/tests/k5b_gshell_tdd.md | snd_focus の記述を av_focus_set に (記録の文書) |
| README.md:117 | コマンドの一覧から `sndctl`・`play` を外す |
| userland/shell/sh_exec.inc:164 | 注記の例を sndctl でない物に |
| userland/rust/gui_demo/src/lib.rs:256-258 | BGM の行を削除 |
| userland/tests/audit_test.c・kstr_boundary.c | 口ごとの期待値 (void は戻ること・is_playing は 0・snd_focus は NOSYS) に直す。kstr の文字列引数の検査から fm_play_mml を外す |
| 試験: tools/tests/test_fm_module.py・fm_module_host.c (snd の欄の検査と変異を外し OPS_VERSION 2 を確かめる)、fm_playback_host.c (MML の再生の試験、削除)、kapi_audit_host.c・test_kapi_audit.py (retired の 3 種の期待値)、test_kapi_strings.py (fm_play_mml の文字列の検査を外す)、test_tick.py (snd_tick を外す)、test_exec_r1.py・test_exec_hwb_abort.py・exec_image_host.c (snd_owner_exit の期待を外す)、x87_lifecycle_host.c・font_source_host.c (snd の偽の実体を外す) | 各ファイルの CHECK と変異のうち snd/MML を対象にする物は削除、残す物は廃止の応答を期待に |
| SYNTH §6 の snd の SOFT・persist・借用 ch | 設計から外す (§8) |
- 再開する時は libos32snd の上にユーザー空間の MML として作る (別票)。
- 洗い出し (2026-11-09、PM の依頼で独立に): `grep -rlE 'snd_(bgm|se_|set_master|focus|tick|init|cleanup|owner_exit|engine|state_for)|fm_play_mml|fm_startup_sound|sndctl|sndtest|libos32snd|lib/snd'` (.git・build/out・docs・game・apps を除く) の 70 ファイルは、上の表の行と生成物 (kapi_generated.c・kapi_call_generated.h・os32_kapi_generated.h・kapi_generated.rs・hwdev_contract.h・hwdev_layout.txt) で全部覆った。表の外の残りは無い (docs は PM の文書の更新、game は範囲外)。

### 7-7. 互換層からの使い方 (r1 §3-7 の表を HAL ライブラリの口に合わせた)
| Win32 の口 | libos32snd の口 | 互換層の中 |
|---|---|---|
| DirectSoundEnumerate・GetCaps | snd2_dev_*・snd2_caps (hw_mix 0・hw_3d 0) | GUID・説明 |
| 一次バッファ・協調レベル | sndl_open (fmt を caps の表から選ぶ) | PRIORITY の選び直し |
| 二次バッファ・Lock/Play/Stop/SetFrequency/Volume/Pan・GetCurrentPosition・Notify | sndl_voice_* | カーソル・通知のイベント |
| DS3D・DSFX | — (DSFX は snd2_dsp_* の表が空なら使わない) | スタブ (S7・S13-2) |
| waveOut (Open/Write/Pause/Reset/Close) | sndl_voice_submit/stop/reset/delete・done | WAVEHDR の WHDR_DONE |
| midiOut | sndl_midi_* | GM の既定 |

## 8. 合成器の置き場の変更で影響する節 (SYNTH_OPNA_DESIGN、U-合成器・R2-P2-4)

分け方: **コア** = lib/os32_opna (Rust、整数の fmgen 移植、段 A〜D、FFI は SYNTH §3 の lib.rs のまま) は**変えない**。**adapter** = libos32snd の fmvoice.c (新) がコアの FFI を voice と §7-4 の時刻に合わせる。

| 節 | 今 | 変える向き | 担当 |
|---|---|---|---|
| §2 分担 | synth.mod (カーネル部品) + C サービス | コアはそのまま、包みを libos32snd へ (ユーザーの共有ライブラリに静的に結ぶ) | L2 |
| §3 C ABI | synth_open/write/read/pump/wake_irq/finish/abort | サービスの口は削除。adapter が `fmv_new/fmv_write/fmv_read/fmv_status/fmv_render/fmv_reset/fmv_csm` を持ち、コアの FFI を呼ぶ | L2 |
| §5 の private の timer/CSM の ABI (`opna_timer_write`・`opna_timer_count`・`opna_apply_csm`) | コアの FFI | **コアに残す** (宣言は lib/os32_opna/opna.h、段 A)。カーネルの模型 (V1) は C で別に持つので timer_write/count は使わない。CSM_KEYON は L2 が `opna_apply_csm` で反映 (R3-P2-2) | A・L2 |
| §4 表・ビルド・障害 | 最終の synth.elf・再配置・カーネルの panic の終端 | ユーザーの共有ライブラリとして結ぶ (再配置は SDK の共有ライブラリの規則)。Rust の panic はアプリの異常終了 (カーネルの panic にしない) | L5 |
| §5 DMA 時計・PCM の接続・所有者 (サービスの部分) | D・Ptarget=D+6144・owned submit・PCM と排他 | 削除 (上の private の ABI は残す)。量は §7-1 の空き、時刻は §7-4、所有は PCM の流れ | L2・L3 |
| §5 timer の IRQ と CS4231 の次期限 (R3) | カーネルの IRQ で timer を進める | V86 の timer の模型へ (§9-4)、**起床源の順は 11-05 の決定のまま**。ネイティブの MIDI・MML では timer は要らない | V1 |
| §6 snd・装置の選択 | snd の SOFT が synth.mod を使う | MML ごと廃止 (§7-6)。FM の検出 (HW/SOFT/NONE) は V86 のセッションの FM_SOFT の既定を決めるのに残す | V2 |
| §6 V86 の trap・A460h・bank | カーネルの synth へ直に | §9-1・§9-3 | V2 |
| §7 大きさ・移植性 | 常駐の量・IRQ の stack・配置 | 常駐 0 (アプリのメモリ)、IRQ の stack は使わない | — |
| §8 段 D (`sdk/rust/i686-os32-mod.json`・`build/synth-module.mk`) | カーネル部品の target | ユーザーの静的ライブラリの target と build/os32snd.mk に替える | L5 |
| §8 段 E (synth_service・synth_samples・synth_module・synth_body・pcm_source.inc・pcm_cs4231.c の接点) | カーネルの段 | 削除。samples (ユーザーが作った波形の cache を読む) は L2 の中でアプリの文脈で読み、寿命はアプリと同じ | L2 |
| §9 受入 | synth.mod の load/unload・PCM の BUSY・停止証明 | コアの差分試験は残す。adapter の時刻の式・pump・V86 の FIFO・読みの往復を足す | §13 |
| 確定 C1・C2 (token・attach/push/finish/abort/status/detach) | カーネル内の口 | 不要になり削除 | — |
| 確定 C3 (timer の Count と CSM の FIFO 積みは trampoline へ) | 合成器の中 | V86 の timer の模型で同じ規則 (IRQ は起床の印だけ、Count と CSM の積みは guest へ戻る前の通常の文脈) | V1 |
| 11-05 のユーザー決定 (a) リズムの素材は同梱しない (b) OPN だけの機械は HW (c) timer は PCM のタイマ → PIT one-shot → 諮る | — | **全部そのまま継承** | — |

## 9. V86 の FM (U-V86、R2-P1-4〜P1-7)

形: V86 を動かすアプリ (userland/cmds/v86.c) が実行のループを持ち、カーネルの V86 は FM のポートの書きを snd2_fmq に積んでアプリへ返す。アプリが libos32snd の合成器へ渡し pump する。V86 もアプリとして PCM を占有する。

### 9-1. 捕捉 (platform/pc98/legacy/v86_io.c、R2-P1-4)
- 今は v86_io.c:96-99 の許可表が 0188h–018Fh を TSS で素通しにし (読みの BUSY のポーリングを速くするため)、:332 以降で bitmap に立てる。分岐は :391 (gcap) → :393 (観測) が先。
- SOFT のセッション (`v86s_begin` の flags に FM_SOFT) では: bitmap を作る所で **0188h–018Fh と A460h を拒否** (セッションの終わりに今の表へ戻す、v86_io_reset_policy と同じ所)。`v86_io_in/out` の**先頭**に `if (v86_fm_soft && v86_fm_is_port(port)) return v86_fm_in/out(...)` (gcap・観測より先)。
- 新 platform/pc98/legacy/v86_fm_io.c: 幅 2/4 は連続 byte に分けて順に。0188h/018Ah = bank0 の addr/data、018Ch/018Eh = bank1 の addr/data、奇数のポートは読み FF・書き捨て。A460h は SYNTH §6 の規則 (ID 70h|bit0、bit0 だけ保持、0 なら bank1 を mask: 読み FF・書き無視)。bank ごとの addr の latch。
- 読みの BUSY のポーリングが罠になって遅くなる件: 罠 1 回は数 µs で、P100 で BUSY の読みが続くゲームは遅くなる。受入で測り、遅ければ報告する (重ければユーザーが判断)。

### 9-2. セッション (R2-P1-5)
```c
typedef struct { u32 size; char path[64]; char second[64]; u32 flags; } V86_Begin;      /* 136B、flags: FM_SOFT */
typedef struct { u32 size, slice_ms, reply_valid; u8 reply[4]; u32 snd_status, shadow_seq, rsv[2]; } V86_RunIn; /* 32B */
typedef struct { u8 bank, reg, kind, byte; } V86_ReadQ;   /* kind: STATUS 1・DATA 2、byte = IN の中の位置 0..3 */
typedef struct { u32 size, reason, port, width, fifo_n, guest_us, code, nq; V86_ReadQ q[4]; } V86_Exit; /* 48B */
/* reason: SLICE 1・FIFO 2・IOREAD 3・END 4・FAULT 5 */
int v86s_begin(const V86_Begin *b, u32 *sid);       /* 1 セッションだけ (他にあれば BUSY)。IPL を読み文脈を作る (今の boot2 の前半) */
int v86s_run(u32 sid, const V86_RunIn *in, V86_Exit *ex);
int v86s_fm_drain(u32 sid, SND2_FmEv *out, u32 max, u32 *last_seq); /* last_seq = 渡した最後の通し番号 */
int v86s_end(u32 sid);                              /* 今の boot2 の終わりの teardown (v86.c:793-869 の後半) */
```
- 保存: セッションの間、カーネルが `struct v86_context` (汎用レジスタ・セグメント・EFLAGS・仮想 IF と STI の影)・v86 の PIC・PIT・鍵盤・メモリの写像・FM の模型と bank の latch を持ち続ける。run の間に teardown しない。今の `v86_run_limit(ctx, tick_limit)` (v86.c:470) を「戻った時に ctx を書き戻す」形にして使う。
- 戻る所は命令の境目だけ: (a) FM の OUT を**終えた後**、FIFO が半分以上 → FIFO。(b) PIT の tick の割込みで V86 から監視へ来た時に slice を過ぎていれば → SLICE (I/O の無い busy loop もこれで戻る)。(c) §9-3 の同期の読み → IOREAD。(d) FIFO が満杯の時の FM の OUT は実行せずに FIFO で戻る (次の run で再実行、落とさない)。
- 幅の結線 (R4-P2-2): v86.c:247 は 66h で `opsize16 = 0` にするが :265 の `v86_gp_emulate(frame, pc, len)` に渡していない。`v86_gp_emulate(frame, pc, len, opsize16)` にし、E5・ED (IN) と E7・EF (OUT) は `opsize16 ? 2 : 4` の幅で `v86_set_ax` / `v86_set_eax` (新、EAX 全体) を使う。幅 4 の IN は 4 つの連続ポート (例: DX=0188h なら 0188h・0189h・018Ah・018Bh)。
- IOREAD の結線 (R3-P1-3、arch/x86/legacy/v86_gp_emulate.inc:9-28 の IN の 4 形): `v86_io_in` を `v86_io_in2(port, size, &v)` (戻り 0 = 値あり・1 = 保留) にし、保留なら **AL/AX/EAX も EIP も変えずに** `V86_EMU_PEND` を返す。監視は IOREAD で run を戻る (Exit の `port・width・nq・q[]` に、幅 2/4 の IN を byte に分けた中で同期が要る byte だけ。他の byte はカーネルが答える)。次の run で `reply_valid=1` なら、`v86_fm_io` が reply を「この IN の答え」として持ち、**同じ IN を再実行**して完了させ EIP を進める (命令の途中の状態を持たない)。reply は使った時に消す。INS (文字列の IN) の FM のポートは同期せず影の値で答える (資料のある使い方が無い)。
- 検査: size・sid・呼び手が begin の owner (他は INVAL)・reply_valid は IOREAD の直後だけ (他は INVAL)・max ≤ 512。アプリの終了で `v86s_end` 相当を回収の並び (exec_finish.inc の V86 の行) で。STOP は今の V86 の中断と同じ。
- 今の `v86_boot2` は残す (FM_SOFT 無しの begin→run(無期限)→end と同じ振る舞い)。

### 9-3. 読みを分ける (R2-P1-6・R3-P1-4)
```c
/* sound/hal/snd2_fmq.c (共通、CPU 非依存、V86 だけが使う) */
typedef struct { u8 kind, bank, reg, val; u32 at_us; u32 seq; } SND2_FmEv;   /* 12B。kind: WRITE 1・CSM_KEYON 2 */
#define SND2_FMQ_LEN 512
int snd2_fmq_push(SND2_FmQ *q, u8 kind, u8 bank, u8 reg, u8 val, u32 at_us);  /* 満杯は 0。seq は push ごとに +1 */
u32 snd2_fmq_drain(SND2_FmQ *q, SND2_FmEv *out, u32 max);
```
| 読み | 答える所 |
|---|---|
| status (0188h・018Ch) の BUSY | 常に 0 |
| status の timer A/B の flag | カーネルの timer の模型 (§9-4) |
| bank0 の status (0188h) | 上の 2 つだけで即答 (BUSY のポーリングは速いまま) |
| bank1 の status (018Ch) の音由来の bit (EOS・BRDY・ZERO・PCMBSY) | 影が**有効**なら影、無効なら IOREAD (kind STATUS) |
| data の読みで合成器の状態が要る物 (bank1 reg 08h の ADPCM RAM ほか、SYNTH §3 の read の表) | 常に IOREAD (kind DATA、bank・reg は latch の値) |
- 影の有効性: カーネルは `pushed_seq` (最後に積んだ WRITE・CSM の seq) を持つ。アプリは run のたびに `shadow_seq` (影を作る前に合成器へ反映した最後の seq) と `snd_status` を渡す。**`shadow_seq == pushed_seq` の時だけ有効** (FIFO に未反映の WRITE が 1 件でもあれば無効)。ADPCM の開始の OUT の直後の status の読みは必ず同期になる。
- アプリの同期 read の順 (V6): (1) `v86s_fm_drain` で FIFO を全部取る (2) 取った入力を**全部 F_r に順に反映** (§7-4 の遅着と同じ扱い、render はしない) (3) `fmv_read` / `fmv_status` で q[] の各 byte を答える (4) 影を作り直し `shadow_seq = last_seq` (5) 次の run の reply に入れる。render をしないので **PCM が満杯 (N=0) でも答えられる**。合成器の内部の時刻 (ADPCM の進み) は生成した frame だけで進む (§7-4)。
- FIFO が満杯の時 (§9-2 (d)): run は OUT を実行せずに FIFO で戻る。アプリは drain → 反映 (同じく F_r) → pump (空きがあれば) → run。満杯でも書きは落ちない。
- CSM: 模型の timer A が CSM のモードで溢れたら `CSM_KEYON` を同じ FIFO へ (書きとの順を保つ)。満杯なら溢れの印を模型に残し、次の run の最初の機会に積む。

### 9-4. 起床源 (R2-P1-7、11-05 の決定のまま)
- 模型の次の期限 (timer A/B の溢れ) で V86 に仮想 IRQ を入れる。起床は **CS4231 の I20/I21 のタイマで次の期限** → (資料で共存が駄目なら) **PIT の one-shot** → (どちらも駄目なら) **100Hz の制限をユーザーに諮る**。実装前に資料で確かめる段を置く (V0)。
- SndBackend に任意の口 `hw_timer_arm(ctx, after_us)` (NULL = 無い、0 で解除) と `hw_timer_ack`。IRQ は backend が要因を分け、`v86_fm_timer_wake()` で印を立てるだけ。Count・CSM の積み・仮想 IRQ の判定は guest へ戻る前の通常の文脈 (確定 C3 と同じ)、既存の `v86_pic_should_inject(12, &vector)` を通す。
- 実の IRQ12 (R3-P2-3): SOFT のセッションでは v86.c:503 の `irq_enable(V86_IRQ_SOUND)` を**呼ばない** (FM の無い機械の空の線を開けず、仮想 IRQ は模型だけから)。HW のセッション (FM_SOFT 無し) は今のまま。
- 後片付け (end・STOP・アプリの異常終了の回収の全部で同じ 1 か所 `v86_fm_session_end`): `hw_timer_arm(ctx, 0)` で解除 → 模型の pending と仮想 IRQ の要求を消す → FIFO を捨てる → bitmap を今の表へ戻す (§9-1) → SOFT で開けていない IRQ12 は触らない。

## 10. KAPI の追加と検査 (仮、番号と版は PM。末尾追加、AV0 では全部 NOSYS、P2-6)

| 名前 | 長さ・向き・上限 | 失敗時の出力 |
|---|---|---|
| snd2_dev_count / dev_info (64B 出) / caps (size ≤ 256 出) | size は 4 の倍数、0 < size | 書かない |
| snd2_dsp_unit (32B 出) / dsp_param (32B 出) / dsp_insert (n ≤ 16、4n 入) / dsp_process (n ≤ 16、in・out = frames × frame_bytes、frames ≤ max_frames、乗算は u64 で溢れ検査) | 範囲は §3-2 | out を書かない |
| snd2_stream_open (32B 入・4B 出) / write (bytes 入、上限なし・受ける量で切る) / start / pause / pos (32B 出) / wait / volume / close | §3-3・§3-5 | pos は書かない |
| av_focus_set / av_focus_get | set は WM だけ | — |
| v86s_begin (136B 入・4B 出) / v86s_run (32B 入・48B 出) / v86s_fm_drain (max ≤ 512、12 × max 出) / v86s_end | §9-2、begin の owner だけ。通り道は `legacy_call_dispatch` → `v86_call_dispatch` の slot の switch (末尾の追記) | 書かない |
| in2_pad_state (20B 出) / pad_caps (64B 出) / pad_vibrate (4B 入) | slot < 4 | 書かない |
| in2_dev_count / dev_info (32B 出) / read (max ≤ 128、16 × max 出) / key_state (128B 出) / mouse_state (16B 出) / acquire / unacquire / status (32B 出) / wm_state (16B 出、WM だけ) | — | 書かない |
- 計 33 口 (snd2 15・av 2・in2 12・v86s 4)。MML の 10 口は番号を残して NOSYS (§7-6)。pcm_* の 5 口は番号を変えず target を snd2_v1.c へ。版は 1 つ上げる (仮 84→85)。
- CPL=3 のポインタ検証は kapi/kapi_snd2.c・kapi/kapi_in2.c (kapi_sys.c の形)。順は「長さの計算 (溢れ) → 範囲の検証 → 実体」。
- AV0 が受け持つ: kapi.json の追記と生成 (ラッパー・C89 ヘッダ・Rust の os32api の束縛)、`make clean` → `make all` の通過 ([ABI])、ヘッダの C89 の検査。

## 11. 大きさの見積もり

| 塊 | 行 |
|---|---|
| AV0 (ヘッダ 5・a0_weak 2・ABI 試験・番人・生成) | 750 |
| sound/hal (7 ファイル、区間・ステージングの一般化を含む) | 1,250 |
| CS4231 の backend (pcm_cs4231.c から装置の部分を残し中核を移す、−400/+250) | 250 |
| av_focus・kapi の殻 2 本 | 380 |
| input/hal (7 ファイル) | 1,000 |
| kbd_hal・mouse_hal・kbd.c/mouse.c の行・gshell (excl.rs) | 420 |
| libos32snd (混ぜ・voice・pump・完了・時刻・MIDI・合成器の adapter・build) | 1,500 |
| MML の廃止 (削除が主、残る行は NOSYS の向け直し) | −1,200 / +60 |
| V86 の FM (捕捉・模型・セッション・タイマ・アプリのループ・KAPI) | 1,050 |
| 後の段 (パッド 4 種・padmap・host_agent の midi) | 1,400 |
| 計 (足す行) | 約 7,950 |

## 12. ファイルと段 (1 ファイル = 1 段 = 1 実装者、P2-9)

順: **HAL v2 A0 の着地 → AV0 → 入力の段 (並列) → IA**。**TICK 段 B の合格 → 音の段 (並列) → IS**。**USB 段 1 の合格 → P 段 → IP**。**合成器の照らし直し (SYNTH_OPNA_DESIGN の r3、§8 の表) と合成器のコアの段 A〜D → L・V 段 → IL** (V は IS の後)。**R0a〜R0c (MML の廃止) は AV0 と同じ版上げで、R0a の後に R0b・R0c**。AV0 以外はヘッダを編集しない。部品の実装済みは host 試験と `make kernel`、完了は I の段だけ。

| 段 | ファイル | 呼ぶ | 書く共有の状態 | 行 | I | 前提 |
|---|---|---|---|---|---|---|
| AV0 | §10 の全ヘッダ・kapi.json と生成物・a0_weak.c ×2・tools/tests/av_abi_host.c・tools/check_av_portable.py・tools/checks/av_wiring.txt・av_no_weak.sh | — | — | 750 | — | HAL v2 A0 |
| F1 | kernel/av_focus.c (+exec の入口・出口 2 行) | in2_on_focus・gui の登録状態 | `av_fg_owner` | 110 | IA | AV0 |
| N1 | input/hal/in2_dev.c | — | `in2_srcs[]` | 130 | IA | AV0 |
| N2 | input/hal/in2_ring.c (128・seq・合算・LOST・STOP) | in2_time_ms | ring・drop | 190 | IA | AV0 |
| N3 | input/hal/in2_excl.c (acquire・wm_state・境目) | F1・N2・kbd_raw_purge_keep | acq・gen・focus_gen | 150 | IA | AV0 |
| N4 | input/hal/in2_state.c (押下表・macc) | — | keys・macc | 110 | IA | AV0 |
| N5 | input/hal/pad2_core.c | F1 | `pad2_slots[4]` | 180 | IA | AV0 |
| N6 | input/hal/in2_time.c | tick_count | — | 30 | IA | AV0 |
| N7 | input/hal/in2_reclaim.c (reclaim・on_focus) | N2〜N5 | §5-6 の表 | 100 | IA | AV0 |
| D1 | drivers/input/kbd_hal.c (+kbd.c:333 の 1 行・purge の口、kbd.h 128。kbd.c から同数の行を移す) | N1〜N4 | — | 150 | IA | AV0 |
| D2 | drivers/input/mouse_hal.c (+mouse_bus.c の ISR の 1 行・in2_tick のシームレス) | N1・N2・N4 | `abs_prev` | 130 | IA | AV0 |
| K2 | kapi/kapi_in2.c (+av_focus) | N*・F1 | — | 170 | IA | AV0 |
| G1 | userland/gshell/src/excl.rs (+input.rs・multiapp.rs の呼ぶ行) | in2_wm_state・av_focus_set | WM の配った押下の表 | 200 | IA | K2 |
| S1 | sound/hal/snd2_reg.c (登録・unregister・FAULTED) | be->caps | `snd2_devs[]` | 150 | IS | 段 B |
| S2 | sound/hal/snd2_caps.c (形式・周期の検査) | S1 | dev.caps | 110 | IS | 段 B |
| S3 | sound/hal/snd2_seg.c (区間・カーソル・連続性、pcm_cs4231_math.c の pcm_obs・pcm_cont を一般化して移す) | — | played・copied・seg_* | 300 | IS | 段 B |
| S4 | sound/hal/snd2_stg.c (ステージング 8192、played まで保持、pcm_stg_* を移す) | — | stg | 120 | IS | 段 B |
| S5 | sound/hal/snd2_stream.c (状態表・open・write・start・pause・close・advance・teardown) | S1〜S4・be->hw_* | state・gen・owner・written | 320 | IS | 段 B |
| S6 | sound/hal/snd2_pos.c (pos・wait) | S5 | — | 100 | IS | 段 B |
| S7 | sound/hal/snd2_dsp.c | S1・be->dsp_* | — | 120 | IS | 段 B |
| S8 | sound/hal/snd2_v1.c (pcm_* の再現) | S5・S6 | — | 150 | IS | 段 B |
| S9 | drivers/sound/cs4231/pcm_hw.c (SndBackend、pcm_cs4231.c の装置の列・証拠・IRQ を残し中核を S3〜S5 へ移す。pcm_body.c の表を差し替え) | — | 装置の内部 | 250 | IS | 段 B |
| K1 | kapi/kapi_snd2.c | S* | — | 200 | IS | 段 B |
| E1 | exec/exec_finish.inc ((6b) を snd2_reclaim、(6c) in2_reclaim) | S5・N7 | — | 4 | IA/IS | — |
| P0〜P5 | tools/gen_padmap.py・input/hal/pad2_map.c・drivers/usb/xgip_pad.c・xusb_pad.c・hid_report.c・hid_pad.c | in2_pad_* | — | 1,300 | IP | USB 段 1 |
| L1 | userland/lib/snd/mix.c (voice の表・16.16・dB 表・飽和の足し算・stop/reset/delete) | — | voice の表 | 380 | IL | 照らし直し |
| L2 | userland/lib/snd/fmvoice.c (コアの FFI の adapter・§7-4 の時刻・samples の読み・read/status) | lib/os32_opna | — | 300 | IL | 照らし直し・コア段 D |
| L3 | userland/lib/snd/pump.c (scratch の再送・初回の start・完了の対応 §7-3・close) | L1・L2・snd2_stream_* | 完了の表 | 280 | IL | IS |
| L4 | userland/lib/snd/midi_host.c (+tools/host_agent.py の midi、close・切断で reset) | host_call | セッション | 280 | IL | 照らし直し |
| L5 | build/os32snd.mk・sdk の Rust の静的ライブラリの target (SYNTH の段 D の置き換え)。C の規則は profile の char/enum の旗と `\| $(ABI_CONTRACT_ORDER)` を持ち、`check-type-contract` を受入に (末尾の追記) | — | — | 120 | IL | 照らし直し |
| Q1 | sound/hal/snd2_fmq.c (V86 の FM の溜め場、seq) | — | 溜め場 | 90 | IL | AV0 |
| R0a | §7-6 の表の KAPI の行: kapi.json の retired・sdk/gen_kapi.py の retired の body・生成物 | — | — | 80 | IA | AV0 と同じ版上げ |
| R0b | §7-6 の表のカーネル・部品・build の行 (snd_engine の削除・fm_body/fm_module (tick と静止)/fm.c/fm_contract.h・init・exec・v86_io の往復の両方・imports.def・fm-module.mk・kernel-objects.mk・hwdev_layout の再生成 (AV0 の後)・check_arm_compile と配置の表) | fm_all_off・ssg_all_off | — | −1,300 / +20 | IA | R0a |
| R0c | §7-6 の表の userland・ビルドと配置・試験の行 (shell の beep・play、bench、raw_wait、gshell と host 試験、lib/snd・system の削除、libs.mk・sdk.mk・deploy.yaml・packages.yaml・checks/run.py、gui_demo、audit・kstr、tools/tests の CHECK と変異、README) | — | — | −900 / +80 | IA | R0a |
| V0 | 資料の確認 (CS4231 の I20/I21 と PCM の IRQ の共存・PIT の one-shot)。結果を本書に追記、両方駄目ならユーザーに諮る | — | — | — | — | 照らし直し |
| V1 | platform/pc98/legacy/v86_fm_timer.c (timer A/B・CSM・status の模型・仮想 IRQ の判定・pending の消去) | v86_pic_should_inject・Q1 | 模型 | 230 | IL | V0・Q1 |
| V2 | platform/pc98/legacy/v86_fm_io.c (+v86_io.c の bitmap の拒否と戻し・in/out の先頭の分岐 2 行、`v86_io_in2` の保留・reply・影の有効性) | V1・Q1 | latch・FIFO・reply | 260 | IL | V1 |
| V3 | arch/x86/legacy/v86_session.c (begin/run/end・ctx の書き戻し・IOREAD・SLICE・`v86_fm_session_end`) + v86_gp_emulate.inc の IN/OUT の幅 (opsize16・EAX・幅 4) と IN 4 形の保留 (EIP を進めない)・v86.c:265 の opsize16 の受け渡し・v86.c:503 の SOFT の分岐・run_limit の呼ぶ行 | V2 | セッション | 320 | IL | V2 |
| V4 | drivers/sound/cs4231/pcm_timer.c (hw_timer_arm・解除・hw_timer_ack と IRQ の要因の分け) | — | 装置の内部 | 130 | IL | V0・IS |
| V5 | kapi/kapi_v86s.c (§10 の検査) + `arch/x86/legacy/v86_call.c` の slot の switch に v86s_* の 4 口、`platform/none/legacy_guest.c` で断る (末尾の追記) | V3 | — | 130 | IL | V3 |
| V6 | userland/cmds/v86.c (実行のループ: run → drain → F_r への反映 → IOREAD の答え・影と shadow_seq → pump) | v86s_*・sndl_* | — | 170 | IL | V5・L3 |
- 1,000 行を超えるファイルは無い。gshell の input.rs (1,269)・multiapp.rs (1,433) は呼ぶ行だけ、中身は excl.rs。

## 13. 受入 → 試験 → 変異 (P2-7・P2-8)

### 13-1. 統合の段
| I | 入口 (結線の対象) | 観測 | 合格 |
|---|---|---|---|
| IA | F1・N1〜N7・D1・D2・K2・G1・E1・R0a〜R0c | in2_test (新) のシリアルの出力・gshell の配送の記録 | 13-2 の入力の行が全部・av_no_weak.sh IA が 0・WM の既存の試験 (KBD_NAV・STOP) が退行しない |
| IS | S1〜S9・K1・E1 | snd2_test・pcm_play (v1)・/api/sound の取得 | 13-2 の音の行が全部・既存の PCM の試験 (TASK_PCM_CS4231 の E 系) が v1 のまま通る |
| IP | P0〜P5 | in2_test・実機の USB の回 | 3 種 × 状態・振動 |
| IL | L1〜L5・Q1・V1〜V6 | sndl_test・DOS の FM のゲーム 1 本 | 13-2 の IL の行 |

### 13-2. host (libc 無し) と変異 (製品の行を 1 つ変えて RED)
| 反例・確かめる事 | 試験 | 変異 |
|---|---|---|
| 区間の途中の追補 (P1-1): 区間 p+1 に 100 frame → 書き足しで同じ区間の末尾を上書き | snd2_seg_host | 「最初の満ちていない区間だけ」を外す |
| 無音を数えない (P1-2): underrun の後 played ≤ written | 同上 | 過ぎた区間で `played += period` |
| 連続性の喪失で copied = played、epoch+1 | 同上 | 巻き戻しを外す |
| margin の境 (p+1 を書かない) | 同上 | margin を 0 に |
| 状態表 (P1-3) の全 36 マス | snd2_stream_host | DRAIN の write を受ける |
| v1 の自動開始 4096・close の期限 (P1-4) | snd2_v1_host | 4096 を 4095 に |
| 止めた証拠が無い → FAULTED・leak=1・unregister BUSY (P1-5) | snd2_stream_host (偽の backend) | leak を 0 で渡す |
| 回収の後に古い番号は STALE、二重の回収は無害 | 同上 | gen を進めない |
| wait の未書き frame は INVAL、閉じると STALE | snd2_pos_host | 判定の符号を逆に |
| av_focus_set を WM 以外が呼ぶと INVAL (P1-6)、CUI のネストの戻り | av_focus_host | 呼び手の検査を外す |
| 境目で raw の掃除・LOST{EXCL}・gen+1 (P1-8) | in2_excl_host | TAB を残すキーから外す |
| src 2 台で同じキー: in2 は 2 件、合成は 1 件 (P1-9) | in2_src_host | dev を src から取らない |
| 回収・前景の交代で macc 0・振動 0・独占の解除 (P1-10) | in2_reclaim_host | macc を消さない |
| 満杯 → LOST{OVERFLOW,count}、STOP は残る、seq の折り返し | in2_ring_host | LOST の挿入を外す |
| 非前景の vibrate は BUSY で無作用 (P2-5) | pad2_core_host | 0 を送る |
| KAPI の長さの溢れ (frames × frame_bytes) (P2-6) | kapi_snd2_host | u64 を u32 に |
| 初回の start: v1 は 2 半面 4096、v2 は区間 0..lead、0 frame の start で underrun を数えない (R2-P1-1) | snd2_seg_host | 充填を 0..lead−1 に |
| hw_start の失敗 (証拠つき) で IDLE・copied = played (R2-P1-1) | snd2_stream_host | copied を戻さない |
| pause: margin 内の p+1 を消さない・copied = K の終端・再開で続きから (R2-P1-2) | snd2_seg_host | K から p+1 を外す |
| pos で届いた frame の wait が即 0 (R2-P2-1) | snd2_pos_host | wait を区間単位の played で比べる |
| open の検査の順・period/lead の範囲・v1 の cap 8192 (R2-P2-2) | snd2_stream_host | lead の上限を ring/period に |
| sndl: 部分受付の残りを再 render せずに再送、初回の pump で start、reset で未完了が RESET で done (R2-P1-3) | sndl_pump_host (偽の snd2) | scratch を捨てる |
| 完了の対応: end_frame と played_now で done (R2-P1-3) | sndl_pump_host | end_frame を区切りの先頭に |
| 時刻の式: 遅着は F_r、同 frame は FIFO の順、生成済みを直さない (R2-P2-3) | sndl_fmtime_host | clamp を外す |
| fmq の seq が push ごとに +1、drain の last_seq | snd2_fmq_host | seq を drain で進める |
| 作り直し: 止まった証拠の前にリングを書かない、証拠が無ければ teardown (R3-P1-1) | snd2_stream_host (偽の backend: 止まらない) | 0 埋めを hw_stop の前に |
| voice の位置: 区切りの途中・周波数比 1.5・LOOP の折り返しで play を式どおり、**in_first=128・F=out_first・phase16=0 で 128** (R3-P1-2・R4-P2-1) | sndl_voice_host | in_first も shift する括弧 |
| 64 frame のバッファ 2 本を 1 区切りで生成すると記録が 2 件、前半の完了点が前半の終わり (R4-P1-2) | sndl_voice_host | バッファの境目で切らない |
| 記録の輪が満杯で生成が止まり、played が進むと続く (R4-P1-2) | sndl_voice_host | 満杯で上書き |
| PAUSE 中の再同期で実データを写さず PAUSE のまま、start の後に続きから (R4-P1-1) | snd2_stream_host (偽の backend: 位置が飛ぶ) | 再同期で RUN の充填 |
| 廃止の口: void の 8 口は戻る・is_playing は 0・snd_focus は NOSYS (R4-P2-3) | kapi_audit_host | 全部 NOSYS に |
| stop の後の play が生成の位置から、reset で未完了が即 RESET の done・残る音は鳴る (R3-P1-2) | sndl_voice_host | reset で生成の位置を残す |
| IOREAD: `IN EAX, DX` (DX=0188h、latch = bank0 reg 07h) は q[] が {bank0, 07h, DATA, byte 2} の 1 件、byte 0 (status)・1・3 (FF) はカーネルが答え、reply で同じ IN を再実行して EAX と EIP (R3-P1-3・R4-P2-2) | v86_session_host | 保留で EIP を進める |
| `IN AL, DX` (DX=018Ah) は q[] 1 件で AL だけ、EAX の上位は変えない | v86_session_host | 幅を 2 に固定 |
| 66h 付きの IN/OUT が幅 4 で v86_gp_emulate に届く (R4-P2-2) | v86_session_host | opsize16 を渡さない |
| 影: WRITE の直後の bank1 status は IOREAD、bank0 status は即答 (R3-P1-4) | v86_fm_io_host | seq の比べを外す |
| 同期 read を PCM 満杯 (N=0) で答える、反映は F_r に順 (R3-P1-4) | sndl_fmtime_host | 反映を render 待ちに |
| SOFT で IRQ12 を開かない・end で timer 解除と pending 消去 (R3-P2-3) | v86_session_host | 解除を外す |
| V86 の SOFT で 0188h〜018Fh・A460h が拒否・終わりに戻る、分岐の先頭が FM (R2-P1-4) | v86_fm_io_host | gcap を先に |
| 幅 2/4 の分解・bank1 の mask・奇数ポート (R2-P1-4) | v86_fm_io_host | mask を無視 |
| IOREAD: IN を完了せずに戻り、reply で完了、reply 無しの run は INVAL (R2-P1-5・P1-6) | v86_session_host | EIP を先に進める |
| FIFO 満杯の OUT は実行せずに戻り次で再実行 (R2-P1-5) | v86_session_host | 満杯で捨てる |
| CSM の発音が書きと同じ FIFO で順に (R2-P1-6) | v86_fm_timer_host | 別の列に積む |
| ホストの MIDI: close と切断で all notes off・reset (R2-P2-6) | tools/tests/host_agent_midi_test.py | 切断の処理を外す |
| X4 では UI の状態機械に触らない、X3 で取消と release の合成 (R2-P2-5) | gshell の host 試験 (excl.rs) | X4 でドラッグを終える |
| 声の足し算の飽和・16.16 の位相 | sndl_mix_host | 飽和を外す |
| GameControllerDB の 10 行の写し | pad2_map_host | 反転の bit を無視 |

### 13-3. NP21/W
- 音 (段 B の後): 既知の波形 (1kHz の正弦と区間ごとの印のクリック) を period 256・lead 3 で 30 秒、/api/sound の取得と照合して欠け・重なりが無い・underrun 0。書いた時刻と印が鳴った時刻の差が 30ms 未満 (位置の計算が誤っても取得側で分かる)。書込みを 200ms 止めて再開 → underrun が数えられ、played ≤ written、再開後に印がそろう。位置の欠測 (`emu_pause` で 150ms 止める) → epoch+1 で続く。IF=0 の**全区間** (初回の充填 = 16KB の 0 埋め + 写し、pause の消去、作り直し、advance の写し、teardown) の最大時間を `pcm_e4_diag` の形で記録する (R3-P2-4)。
- v1: pcm_play・pcm_test が今のまま (186ms の挙動・BUSY・close の drain)。
- 入力: /api/key・/api/mouse の入力が time_ms・seq 付き / 128 件で LOST / acquire 中の CTRL+STOP でアプリが止まり回収される / GRPH+TAB で前景が替わり、旧前景の focus_gen が違い、新前景に LOST{FOCUS} / EXCL_MOUSE 中に窓のボタンを押しても WM が反応しない、解除で端が出ない。
- MML の廃止 (R0): 廃止の口が口ごとの応答 (void は無作用・is_playing 0・snd_focus NOSYS)、shell の beep・play が無い、起動音が鳴らない以外に起動・GUI・V86 (HW の FM でゲストへ渡す前の全キーオフ) の退行が無い。V86 (IL): FM を使う DOS のゲーム 1 本を FM_SOFT で、鳴る・timer の IRQ のテンポ・BUSY のポーリングの罠の回数と速さを記録。Host MIDI: host_agent に偽の MIDI 出力をつなぎ、アプリを STOP で止めて all notes off が届く。
- 欠損: USB の部品を外して起動 → パッドは全部 NOTFOUND、PCM は 1 個。

### 13-4. 実機 (Ra266)
period 256・lead 3 で 60 秒、underrun 0 と IF=0 の全区間の最大時間と sndl の pump の最大間隔をシリアルで。期限 (IF=0 は 1 区間 5.8ms の 1/4、pump の間隔は cap − period) を超えたら**未合格**として測った値をユーザーに渡す (R3-P2-4) / バスマウスの合算と 1ms の時刻 (段 B の後) / パッドは USB の装置の回で各 1 台。

## 14. 未決 (ユーザー判断が要るものだけ)

なし。MML は一旦廃止 (§7-6)。重さで判断が要るかもしれない物は受入の計測で報告し、期限を超えたらユーザーが判断する (V86 の FM の罠の速さ §9-1、低遅延の pump の最大間隔 §7-1、IF=0 の全区間の最大時間 §13)。起床源が CS4231・PIT の両方とも使えない時の制限は V0 の結果でユーザーに諮る (11-05 の決定)。

## 反論

| 指摘 | 反論 |
|---|---|
| r1 の P1-4 の直し案「backend に v1/v2 の利用の種別を持ち交差操作を拒否」 | 採らない (r2 のまま)。ユーザー決定で状態を 1 か所に統合し、混ぜる検査は要らない。第 2 回・第 3 回のレビューも「採用してよい」とした |

## 確定 (2026-11-09、astra の第 6 回、PM が反映)
- R6-P2-1: tools/tests/test_raw_wait.py:35-36・220-221 の MML の 2 件と tools/tests/raw_wait_tdd.md の記述を R0c の削除の一覧に足した (§7-6)。
- R6-P2-2: display_cleanup_host.c・v86_gcap_abort_host.c の同名のスタブ `snd_state_for_os32` は保持すると明記した (§7-6)。

## 確定の後の追記 1 (2026-11-09、ユーザー決定、PM が反映)
- ユーザー原文「libos32sndを廃止して新しい仕組みで統合」。旧 libos32snd (userland/lib/snd、181 行、snd_bgm_*・snd_se_*・snd_set_master の MML の包み) は R0c で消し、HAL ライブラリの名前を libos32snd2 から **libos32snd**、置き場を **userland/lib/snd/**、公開ヘッダを **libos32snd.h**、build を **build/os32snd.mk** にした (本文は置換済み)。関数の接頭辞 `sndl_` は変えない (旧の `snd_*` と重ならない)。
- カーネル・KAPI の `snd2_`・`os32_snd2.h` の名前は変えない: KAPI は末尾追加 ([ABI]) で、廃止した旧 snd_* の 10 欄が NOSYS の本体として表に残るため、同じ名前を再利用すると取り違える。

## 確定の後の追記 2 (2026-11-09、カーネルの FM 音源・PSG 音源の口)
ユーザー原文「カーネルのサウンド要件にFM音源やPSG音源の口がない。ネイティブ側にあればこちらを使う様にしたい。」「ハードウェアより上位を求められればソフトウェアにするか未対応扱いにするかはユーザーの判断で変えられるように。ゲームの設定でリソースがあればソフトウェアで代用しハードウェアがあるが下位の場合はフォールダウンするなど。」
前提: シングルタスク・協調切替、カーネルにミキサ無し (§2)、整数、[HW3]、1 機能 1 ファイル。検出は SYNTH_OPNA_DESIGN §6 (R2) の probe をそのまま使う。

### 0. 実物 (main 3ebbd208)
| 事実 | 所 |
|---|---|
| 26K (YM2203) 専用。ポートは 0188h/018Ah だけ、bank1 (018Ch/018Eh)・A460h の定義が無い | drivers/sound/fm/fm.h:13-15 |
| 検出が無く opn_init は無条件。BUSY の待ちは 1 書き 10ms の上限で、失敗を捨てて次を書く → FM の無い Ra266 では 1 書きごとに 10ms 止まる | fm.c:46・54-86・227・255 |
| 部品 fm.mod、KAPI の fm_note_*・ssg_* は fm_module.c の包み。R0b の後の形が土台 | fm_body.c:3・fm_module.c:33-77・177・212 |
| V86 は 0188h–018Fh を TSS で素通し、往復で全キーオフ | v86_io.c:96-99・328・377 |

### 1. 列挙と能力 (os32_snd2.h に追記、C89、AV0 で大きさを固定)
1 チップ = 1 装置。OPN の SSG は同じチップなので PSG は `ssg_ch` で出し、単体の PSG は種類だけ予約。
```c
#define SND2_KIND_FM 2   #define SND2_KIND_PSG 3 /* 予約 */
#define SND2_CHIP_OPN 1  #define SND2_CHIP_OPNA 2  #define SND2_CHIP_OPN3 3 /* 予約 */  #define SND2_CHIP_OPL3 4 /* 予約 */
#define SND2_CHF_RHYTHM 0x01  #define SND2_CHF_ADPCM 0x02  #define SND2_CHF_TIMER 0x04  #define SND2_CHF_PROBED 0x08
typedef struct {                 /* 64B */
    u32 size, dev, kind, chip;   /* 0 */
    u32 clock_hz;                /* 16: 26K 3993600、86 7987200 (C0 で資料を確かめる) */
    u16 base_port, id_port;      /* 20: 0x0188、86 は 0xA460 (無ければ 0) */
    u8  banks, fm_ch, ssg_ch, rhythm_ch;   /* 24: OPN 1/3/3/0、OPNA 2/6/3/6 */
    u32 flags, adpcm_bytes;      /* 28: CHF_*。adpcm_bytes は初版 0 (RAM の検査をしない) */
    u32 reserved[7];             /* 36 → 64 */
} SND2_ChipCaps;
int snd2_chip_caps(u32 dev, SND2_ChipCaps *o, u32 size);  /* kind が FM/PSG 以外は INVAL。snd2_caps を FM の dev に呼ぶと INVAL */
```
- 検出で確定した時だけ登録 (absent/unknown は kprintf に残す)。`snd2_dev_count`・`dev_info` に kind で並ぶ。ソフトの fmgen はカーネルに出ない。

### 2. 口: レジスタの書きをバッチで
```c
typedef struct { u8 bank, reg, val, rsv; } SND2_RegW;      /* 4B、n ≤ 256 */
int snd2_chip_open(u32 dev, u32 *h);    /* 空きなら呼び手が占有、他の owner なら BUSY、FAULTED なら IO。be->open (A460h の保存・bit0=1、静止の列) */
int snd2_chip_write(u32 h, const SND2_RegW *w, u32 n);  /* 戻り: 0..n = 書けた件数 (置換・無視も 1 件)。< n は実行中の故障で以後 FAULTED */
int snd2_chip_read(u32 h, u32 bank, u32 what, u32 reg); /* what: STATUS 1・DATA 2。戻り 0..255 か負 */
int snd2_chip_reset(u32 h);  /* 静止の列だけ。h・owner・gen は保つ */
int snd2_chip_close(u32 h);  /* 静止の列 → be->close (A460h を戻す) → 占有を外す・gen+1 */
```
- **事前の検査** (h・owner・n・全件の bank の範囲) が 1 件でも駄目なら INVAL で 1 件も書かない。**実行中** に BUSY の上限が切れたらそこで止め、件数を返し装置を FAULTED。
- 時刻はアプリが持つ (書いた時が鳴る時)。カーネルは BUSY と Bible のウェイト (fm.c:69-77) だけ入れる。
- 書きの扱い (置換 / 無視 / 拒否):

| 書き | 扱い | 理由 |
|---|---|---|
| bank0 07h の bit7:6 | **置換**: 常に `10b` | SSG の I/O ポートはジョイスティック (SSG_MIXER_IO_FLAG) |
| bank0 0Eh・0Fh | **無視** (件数に入れる) | 同上 (ポートのデータ) |
| 27h (タイマの load・enable・reset) | **そのまま** (enable を消すと flag も立たない、YM2608 §2-8-3) | status のポーリングでタイマを使う |
| OPNA の 29h の bit1:0 (TA・TB の IRQ 許可) | **置換**: 0 (bit7 SCH ほかは通す) | IRQ だけを切り flag は残す (C0 で資料を確かめる) |
| OPN の IRQ | チップで分けられない → IRQ12 を PIC で閉じたまま (ネイティブは開けない)。PCM の装置と線を共有する機種では 27h の enable を **置換** 0、caps の TIMER を外す | 線の取り合いをしない |
| bank1 (OPN の時) | **拒否**: 事前の検査で INVAL | 無い bank |
| A460h | 口に出さない (be->open/close) | ボードのポート |
- **旧の 6 口** (fm_note_on・fm_note_off・fm_set_tone_num・ssg_tone・ssg_volume・ssg_all_off、番号・型そのまま) は fm_module.c の包みで `snd2_chip_implicit(owner)` (空きなら暗黙に open、他の owner なら無作用) → 不在・FAULTED なら即戻る → 本体は opn_write_checked を通し**最初の失敗で FAULTED にして即戻る** (fm.c:86・227・255 の続行をやめる)。fmgen へは回さない。SDK の文書で「新しいアプリは sndl_chip_*」。

### 3. HAL ライブラリ (libos32snd) の選び方
#### 3-1. 要求と合成順
```c
#define SNDL_POL_END 0  #define SNDL_POL_SOFT 1  #define SNDL_POL_FALLDOWN 2  #define SNDL_POL_FAIL 3   /* 他の値は INVAL */
#define SNDL_REQ_PREFER_SOFT 0x01   /* 満たす本物があっても SOFT (v86 の --fm=soft) */
typedef struct { u32 size, chip, need, flags; u8 fm_ch, ssg_ch, banks, rsv; u8 pol[4]; } SNDL_ChipReq;  /* 0 の欄は型の既定 */
typedef struct { u32 size, backend, chip, fm_ch, ssg_ch, flags, lost_flags, lost_fm_ch; } SNDL_ChipGot; /* backend: HW 1・SOFT 2・HW_FALL 3 */
int sndl_chip_open(const SNDL_ChipReq *rq, u32 *h, SNDL_ChipGot *got);
int sndl_chip_write(u32 h, const SND2_RegW *w, u32 n, u32 at_us);  /* HW: 即 (at_us は見ない)。SOFT: 保留の列へ (§7-4 の時刻)。HW_FALL: §3-2 で濾して HW */
int sndl_chip_read(u32 h, u32 bank, u32 what, u32 reg);  int sndl_chip_close(u32 h);
```
| 型の既定の要求 | fm_ch | ssg_ch | banks | need |
|---|---|---|---|---|
| OPN | 3 | 3 | 1 | 0 |
| OPNA | 6 | 3 | 2 | 0 (リズム・ADPCM は need で足す、need は既定に足すだけ) |
- 満たす = 型の順位 (OPN < OPNA、OPL は別の系で順位なし) と fm_ch・ssg_ch・banks が要求以上、need の各 bit が caps にある (ADPCM は adpcm_bytes > 0)。
- 合成順 (上から決まる。pol 全 0 は既定 {SOFT, FALLDOWN, FAIL}):

| 順 | 条件 | 結果 |
|---|---|---|
| 1 | 機械の `snd.fm=none` | NOTSUP |
| 2 | 機械の `soft` | HW と FALLDOWN を候補から外す (pol に SOFT が無い・資源不足なら NOTSUP) |
| 3 | 機械の `hw` | SOFT を候補から外す (PREFER_SOFT も無視) |
| 4 | PREFER_SOFT で SOFT の資源あり | SOFT (無ければ 5 へ) |
| 5 | 本物が満たし空き | HW (pol を見ない) |
| 6 | pol[] を前から: SOFT は資源あり、FALLDOWN は §3-2 の表にあり空き、FAIL は常に | 最初に通った物。尽きたら NOTSUP |
- SOFT の資源: (a) PCM の装置が列挙にあり、このライブラリが流れを持つか取れる (他のアプリが持てば BUSY で不可) (b) CPU が Pentium 以上 (HAL の CPU の段、§8 の決定 3)。
- v86 (§4) と MIDI 出力装置 (§3-4) も同じ表を通す。

#### 3-2. FALLDOWN の組合せ (他は FALLDOWN 不可)
| 要求 → 本物 | 捨てる書き (件数に入れる) | 読みの代わり | lost |
|---|---|---|---|
| OPNA → OPN | bank1 の全部・bank0 10h–1Dh (リズム)・29h | bank1 の status = 00h、bank1 の data = FFh | flags RHYTHM・ADPCM、fm_ch 3 |
| OPNA + ADPCM → OPNA (RAM 無し、86) | bank1 00h–10h (ADPCM) | bank1 の status の EOS・BRDY = 1 (待ちのループを抜ける)、bank1 08h の data = 00h | flags ADPCM |
| OPNA + ADPCM → OPN | 上の 1 行目 | bank1 の status の EOS・BRDY = 1 (他の bit は 0、待ちのループを抜ける)、bank1 08h の data = 00h、他の bank1 の data = FFh | RHYTHM・ADPCM、fm_ch 3 |
- F-number と SSG の周期は 26K・86 で同じ値で同じ音程 (分周 72 と 144 で揃う、C0 で資料を確かめる)。濾しは libos32snd で、カーネルには届けない。

#### 3-3. SOFT の PCM の共有と読み
- ライブラリは 1 アプリに PCM の流れを 1 本だけ持つ (§7-1 の sndl_open の流れ)。利用者 (voice・chip・MIDI) の参照数を持ち、chip の SOFT の open で流れが無ければ caps の最初の形式 (CS4231 は 44100/16/2)・period 256・lead 3 で sndl_open、合成器を 1 つの voice として混ぜに足す。最後の利用者の close で sndl_close(DRAIN)。pump はアプリの sndl_pump のまま。
- **タイマ時計 (render と独立)**: chip の SOFT の open で原点 `t0_us` = 今の時刻 (sndl_chip_write の at_us と同じ µs の時計、§7-4)・`tm_us` = t0_us (タイマの模型を進めた最後の時刻)。模型を時刻 t まで進める = `opna_timer_count(t − tm_us)` (µs をそのまま渡す。長い経過の期限での分け方は SYNTH_OPNA_DESIGN §3・§5 の既存の規則のまま)・`tm_us = t`。render の frame 数 N・PCM の満杯・pump の有無に依らない (音の生成の F_r とは別の時計)。
- **書きの適用**: 保留の列は at_us の順 (同じ at_us は積んだ順の FIFO)。1 件を適用する時は、先に模型を その at_us まで旧の状態で進めてから書く (タイマの load・reset が正しい時点で効く)。過去の at_us (< tm_us) は tm_us の時点の書きとして適用。音の生成への反映は §7-4 (F_r) のまま。
- **read (時刻 r = 今)**: (1) at_us ≤ r の保留だけを上の規則で順に適用 (2) 模型を r まで進める (3) `fmv_read`・`fmv_status` で答える。at_us > r の書きは列に残し次の pump (§7-4 の「未来の入力は次へ」) で扱う。render をしないので PCM が満杯でも pump の前でも答え、status の待ちは時刻とともに進む。

#### 3-4. 設定・MIDI
- `snd.fm=auto|hw|soft|none` は SYNTH §6 の fm.backend を libos32snd と v86 に移した物 (§3-1 の 1〜3)。
- MIDI 出力装置 0 番「FM 合成器」: GM → OPN のレジスタの変換を `midi_fm.c` に分け、出口を `sndl_chip_write` に (要求 OPNA・既定の pol、発音数 got.fm_ch)。装置の数・番号は変えない。

### 4. V86 の FM: 本物があれば素通し
- HW のセッション (FM_SOFT 無し): begin で `snd2_chip_open` を V86 のアプリの owner で取れたら、今の I/O bitmap のまま 0188h–018Fh を許す。理由: status の BUSY・タイマの flag・IRQ12 を本物が答え模型の誤差が無い、BUSY のポーリングが I/O の 7〜8 割で罠は P100 で遅い (§9-1)、中継は順序の手間だけ増える。罠にして本物へ中継する案は採らない。
- 往復の全キーオフ (v86_io.c:249-267・328・377) は **chip を取れた HW のセッションだけ** `snd2_chip_reset`、end は `snd2_chip_close`。SOFT・取れなかった時は往復とも**本物に 1 回も触らない** (他の owner の音を消さない)。
- v86.c の既定: 要求 {chip OPN, pol {SOFT, FAIL}} を §3-1 に通す (本物が OPN/OPNA → HW、11-05 (b)。無し → FM_SOFT)。`--fm=soft` は PREFER_SOFT + 要求 OPNA。結果が NOTSUP・BUSY なら FM のポートを拒否 (読み FF・書き捨て) で FM 無しとして走らせる。今の `v86_boot2` は HW のセッションと同じく chip を取り、取れなければ同じく FM 無し。A460h・IRQ12 は HW のセッションで今の扱いのまま。

### 5. 回収と部品の寿命
| 場面 | する事 |
|---|---|
| reset | 静止の列だけ (h・owner・gen を保つ) |
| close・アプリの終了 (exec (6b) の `snd2_reclaim(owner)` が FM も)・STOP・V86 の end・暗黙の open の owner の終了 | 静止の列 → be->close → 占有を外す・gen+1 (古い h は STALE) |
| 静止の列 (opn_chip.c に 1 か所) | FM 全 ch の key-off (28h、OPNA は ch4-6 も) → TL=7Fh → SSG の音量 0・07h=BFh → 27h=30h → OPNA: リズム 10h=BFh・ADPCM bank1 00h=01h→00h |
| 静止の証拠が無い (BUSY の上限切れ) | 装置を FAULTED。以後 open は IO、unregister は BUSY を返し部品は残る (確定版 §3-7 と同じ規則) |
| 前景の交代 | 変えない (§8 の決定 1) |
| 部品: fm_start | C0 の probe → 見つかれば `snd2_chip_register` |
| 部品: fm_quiesce/detach (fm_module.c:177・212) | 占有中なら close の列 → `snd2_chip_unregister` (BUSY なら detach を失敗にして本体を残す) |
```c
typedef struct SndChipBackend { u32 abi;                                  /* sound/hal/snd2_int.h */
    int (*caps)(void *c, SND2_ChipCaps *o);  int (*open)(void *c);  int (*close)(void *c);   /* open: A460h 保存・bit0=1・静止 */
    int (*write)(void *c, const SND2_RegW *w, u32 n);  int (*read)(void *c, u32 bank, u32 what, u32 reg);
    int (*quiet)(void *c);   /* 静止の列。0 = 証拠あり */
} SndChipBackend;
int snd2_chip_register(const SndChipBackend *be, void *c, const SND2_DevInfo *i);  int snd2_chip_unregister(u32 dev);
```

### 6. KAPI (末尾、AV0 と同じ版上げ、AV0 では NOSYS)
| 名前 | 長さ・向き・上限 | 失敗時の出力 |
|---|---|---|
| snd2_chip_caps (size ≤ 64 出) | size は 4 の倍数、0 < size | 書かない |
| snd2_chip_open (4B 出) | dev が FM/PSG | 書かない |
| snd2_chip_write (n ≤ 256、4n 入) | h の owner、全件の bank を先に検査 | 事前の失敗は 1 件も書かない |
| snd2_chip_read / reset / close | h の owner、what ∈ {1,2} | — |
- 計 6 口 (§10 の 33 → 39)。旧の 6 口は番号・型そのまま (include/fm_contract.h:35-46・kapi.json:1038-1078 と照合)。検証は kapi/kapi_snd2_chip.c (長さ → 範囲 → 実体)。

### 7. 段・試験・変異
| 段 | ファイル | 呼ぶ | 書く共有の状態 | 行 | I | 前提 |
|---|---|---|---|---|---|---|
| C0 | drivers/sound/fm/opn_probe.c (SYNTH §6 の probe、0188h だけ。fm.h に bank1・A460h・クロック。29h・分周は資料で確かめ本書に追記) | inp/outp | 検出の結果 | 180 | IS | R0b |
| C1 | drivers/sound/fm/opn_chip.c (SndChipBackend: 扱いの表・bank1・read・open/close・静止の列。opn_write_checked を bank つきに) | C0 | 装置の内部 | 240 | IS | C0 |
| C2 | sound/hal/snd2_chip.c (登録・owner・gen・implicit・FAULTED・reclaim の FM の行、CPU 非依存) | be->* | `snd2_chips[]` | 170 | IS | S1 |
| C3 | kapi/kapi_snd2_chip.c | C2 | — | 120 | IS | C2・AV0 |
| C4 | fm_body.c・fm_module.c (start の登録・detach の解除・旧 6 口の包みに implicit と即戻り)・fm.c (失敗で即戻る)・fm の imports.def (snd2_chip_register/unregister/implicit)・fm-module.mk・tools/checks/av_wiring.txt と a0_weak・av_no_weak.sh IS に snd2_chip_* | C1・C2 | — | 80 | IS | C1〜C3 |
| L6 | userland/lib/snd/chip.c (sndl_chip_*・合成順・資源・FALLDOWN の濾し・PCM の参照数・read の反映) | snd2_chip_*・L1〜L3 | h の表・参照数 | 260 | IL | C3・L3 |
| L7 | userland/lib/snd/midi_fm.c (GM → OPN、L2 から分ける、出口は L6) | L6 | voice の割当 | 250 | IL | L6 |
| V8 | userland/cmds/v86.c の既定の選び + v86_session.c・v86_io.c の HW の begin/end と往復の結線 | C2・V3 | — | 50 | IL | V3・V6 |
- 入口: IS に C0〜C4 (`snd2_test` で caps・open/write/read/close)、IL に L6・L7・V8 (`sndl_test`)。
| 確かめる事 | 試験 (host、libc 無し) | 変異 (1 つずつ) |
|---|---|---|
| FF 固定 → 登録しない・BUSY 固定 → unknown・ID 01h → OPNA・非 01h + timer B → OPN・ID だけで OPN にしない | opn_probe_host (偽のポート) | timer の照合を外す |
| 置換: 07h の bit7:6・OPNA 29h bit1:0 | opn_chip_host | 07h の置換を外す / 29h の置換を外す |
| 無視: 0E/0Fh は書かれず件数に入る | opn_chip_host | 件数から外す |
| 拒否: OPN の bank1 が 1 件でも混じると 0 件で INVAL | opn_chip_host | 検査を書きの中に移す |
| タイマ: 27h で開始 → 溢れで status の flag → reset bit で消える (偽のチップの模型) | opn_chip_host | 27h の enable を 0 に置換 |
| 実行中の BUSY 切れ: 件数 k < n・FAULTED・次の open は IO・unregister は BUSY | snd2_chip_host (偽の backend) | FAULTED を立てない |
| reset の後も同じ h で write が通る、close の後は STALE・A460h が戻る | snd2_chip_host | reset で gen+1 |
| 他の owner は BUSY、終了の回収で静止・gen+1 | snd2_chip_host | gen を進めない |
| quiet だけの失敗 (write は全部正常、偽の backend の quiet が −1) を close・reset・open (be->open の静止)・回収の 4 経路に注入: どれも FAULTED、次の open は IO、unregister は BUSY、detach が失敗し部品 (本体と be) が残る | snd2_chip_host・fm_module_host | quiet の戻り値を捨てる / FAULTED でも unregister を通す |
| 静止の列の順と全 ch (OPNA 6ch・リズム・ADPCM) | opn_chip_host | ch4-6 を外す |
| 旧の口: 不在で inp 0 回、最初の失敗の後は inp 0 回 | fm_module_host | 即戻りを外す |
| 合成順の全行・未知の pol は INVAL・既定 {SOFT,FALLDOWN,FAIL}・486 で SOFT 不可 | sndl_chip_host (偽の snd2・偽の CPU の段) | (i) 順 3 の SOFT 除外を外す: `hw` + 本物無し + Pentium + PCM 有りで SOFT になり RED (正は NOTSUP) / (ii) 順 2 の HW 除外を外す: `soft` + OPNA の本物で HW になり RED / (iii) 順 5 を順 4 の前に: PREFER_SOFT + OPNA の本物で HW になり RED |
| FALLDOWN の 3 行: 捨てる書きが kernel に届かない・読みの代わり・lost・件数 n | sndl_chip_host | 10h–1Dh の濾しを外す |
| ADPCM の終わりを status で待つ利用者 (EOS→BRDY の順のポーリング、上限 1,000 回) が RAM 無し OPNA と OPN の両方で抜ける | sndl_chip_host | OPN の行の EOS・BRDY を 0 に |
| SOFT: 本物の I/O が 0 回 (sndl と V86 の SOFT のセッション)、往復の全キーオフも 0 回 | sndl_chip_host・v86_session_host | 往復を SOFT でも呼ぶ |
| PCM の共有: voice と chip の SOFT で流れ 1 本、最後の close で閉じる | sndl_chip_host | 参照数を見ず閉じる |
| SOFT の read: 保留の書きの反映の後に答える、PCM 満杯 (N=0)・pump の前でも答える | sndl_fmtime_host | 反映を render 待ちに |
| タイマ時計: timer A を load・開始 → render 0 のまま時刻を溢れの手前・後に置いた read で flag 0→1。期待の溢れ時刻は既存の µs の契約 (SYNTH §3・§5) から試験の側で独立に求め、その時刻の手前 1µs で 0・時刻で 1 | sndl_fmtime_host | 時計を F_r (render の frame) から取る |
| 書きの適用の時点: t=100 の reset と t=50 の load が保留の時、t=80 の read は load だけ反映し flag の時刻は load の時点から、t=100 以降は reset が効く・同じ at_us の 2 件は積んだ順 | sndl_fmtime_host | 適用の前に模型を進めない / at_us > r も反映 / 同時刻を後ろから |
| write の n=257・4n の溢れ | kapi_audit_host | 上限を外す |
- NP21/W: SNDboard を 26K・86・無しの 3 構成 (ini は --live-apply + restore) で probe・caps、/api/sound で HW の書きが鳴る、V86 の HW で FM のゲーム 1 本、86 + `--fm=soft` で SOFT。実機 Ra266: 不在の記録、fm_note_on 100 回の時間 (10ms × 回でない)、sndl_chip が SOFT。シリアルで判定。
- 足す行 約 1,350 (§11 の計に足す)。1,000 行を超えるファイルは無い。

### 8. 決定 (PM 決定、推奨の (a))
| # | 論点 | 決定 |
|---|---|---|
| 1 | 前景の交代で本物の FM | 変えない (確定版 §4 の S3 と同じ、占有は終了・close まで) |
| 2 | 検出するポート | 0188h だけ (26K・86 の既定)。118 (OPN3+OPL3) は種類の予約だけ |
| 3 | SOFT を許す CPU | Pentium 以上 (HAL の CPU の段)。合成器のコアの段 D の実測で足りなければユーザーに諮る |

### 反論
なし。

### 確定 (2026-11-09、astra の第 3 回、PM 決定で反映)
- REVIEWA2.r3 の条件 (タイマの引数の単位): opna_timer_count に µs (t − tm_us) をそのまま渡し、既存の期限の分け方を維持、fm_rate/10⁶ の換算と tm_rem を削除。§7 のタイマの試験の期待時刻は既存の µs の契約から独立に求める。

## 確定の後の追記 3 (2026-11-09、ユーザーの承認時の指示、PM が反映)
- **ミキサの層**: ミキサ (`userland/lib/snd/mix.c`、段 L1) をカーネルと HAL ライブラリの上位の部分の間の層として明記する。カーネルの PCM の流れ (`snd2_stream_*`) に書くのは**ミキサだけ**。voice (§7-2)・ソフトの FM 合成器 (L2・追記 2 の SOFT)・MIDI の合成 (L7) はどれもミキサの入力として登録し、ミキサが周波数の変換・音量・パン・飽和の足し算をして 1 本にする。追記 2 §3-3 の「PCM の共有」はこの形 (同じアプリの PCM と合成器がミキサの入力に並ぶ) を指す。カーネルにはミキサを置かない (ユーザー決定) ことは変えない。
  ```
  互換層 (DS・waveOut・midiOut) / アプリ
     │ voice・合成器・MIDI (HAL ライブラリの上位)
  ┌──┴──────────── ミキサ (mix.c、カーネルの流れの唯一の書き手) ───┐
  └──┬───────────────────────────────────────────────────────────┘
  ═══╪══ KAPI snd2_stream_* (PCM 1 本) ═══ カーネル (ミキサ無し)
  ```
- **MML の KAPI の 10 欄**: §7-6 の「中身の無い欄として残す」は v3 の最後の KAPI の整理で**削除する対象**。それまでは NOSYS の本体のまま。

## 確定の後の追記 (2026-11-10、今の main cfb9cec3 との前提の照らし直し、PM 決定)
- **v86s_* の KAPI の通り道 (§9-2・§10、V3・V5)**: V86 を走らせる口は今 `exec/exec_syscall.inc:42` の `legacy_call_dispatch(slot, wrapper, src, count, result)` → `arch/x86/legacy/guest.c:27-30` → `v86_call_dispatch` (`arch/x86/legacy/v86_call.c:78-89` の slot の switch) を通り、`arch_kapi_read_block`・`shared_stack`・呼び手の pin を検査してから `arch_kapi_invoke` する (第 3 段 D2 で引数が `args, bytes` → `struct kapi_arg_source *src, unsigned count` に変わった)。v86s_* の 4 口の slot は `KAPI_SLOT_V86_BOOT2` と同じくこの switch に足す (begin は path 2 本を持つ 136B の構造体 1 本、run は 3 引数)。PPC は `platform/none/legacy_guest.c:18-` の switch で断る。`kapi/kapi_v86s.c` は検査だけを持ち、`arch/x86/legacy` を直に呼ばない。
- **第 4 段 B の型の契約 (§7-6・§12 の R0c・L5・合成器の段)**: `build/abi_contract.mk` が C のコンパイル規則に order-only の `| $(ABI_CONTRACT_ORDER)` を付け、`test_type_contract.py` (covers `build/**/*.mk`) が `make -n -B` の全 C の呼出しの char/enum の旗と probe の前提を検査する (解析できない呼出しは fail closed)。R0c で外す `build/libs.mk:27・218-229・273` の範囲は不変 (`:226` の規則に `| $(ABI_CONTRACT_ORDER)` が付いた)。L5 で足し直す libs.mk の規則と新しい `build/os32snd.mk` (と合成器の段の .mk) の C の規則は、`KERNEL_CFLAGS`/`CFLAGS_BASE`/`PROGRAM_FLAGS` 由来の profile の旗と `| $(ABI_CONTRACT_ORDER)` を持ち、object を `ALL_TARGET_OBJECTS` 等の一覧に載せる。`check-type-contract` (`tools/checks/run.py:387`) を L5・合成器の段の受入に加える。
