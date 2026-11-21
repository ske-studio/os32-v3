# AV0 実装追記 r2（2026-11-21）

> 出所: `~/os32-tmp/run/av0-addendum/DESIGN.final.md`
> 状態: **確定 (2026-11-21)** — KAPI v90 で実装する AV0 の追記。

## r1→r2 の変更

- P2 §1　装置解放、状態、生産経路の根拠を補った。
- P1 §2　321 の配列引数をポインタと固定長で表した。
- P2 §2・4　Rust 型の担当と両機種の呼出試験を決めた。
- P1 §2・3　各口の範囲検査と仮口・実体の失敗時動作を分けた。
- P1 R0a　CPL3 の文字列検査から廃止口を外す。
- P1 R0c　F1 と gshell の前景通知を同時に着地させる。
- P1 R0b　各中間段でリンクできる順に撤去する。
- P2 入力　M5〜S4 の後に N2 の配送・読取位置・LOST 変換を確定する。
- P2 §3・4　機種別リンク、既存検査、通常・R1 の比較対象を明記した。

対象は 2026-11-09 確定の AV_HAL_DESIGN 本体とチップ操作の追補。ファイル位置は指定の v89 コミット 09eb7d048 を基準とする。
照合元は origin/wip/kapi-v89。

## 1. 確定設計と現状の食い違い（8 件）

1. 原設計 §10（AV_HAL_DESIGN.md）の仮番号は失効した。sdk/kapi.json の generations・version・func_capacity で ABI 世代 2・v89・容量 384、同じ表の io_pump が slot 297。v90 は 337 口使用、残り 47 口となる。
2. N1・N4 は新設扱いだった。input/hal/in2_dev.c の sources は 16 枠と再利用時の generation を持ち、同じファイルの input_src_releaseは世代を照合して入力状態も回収する。N4 の状態更新は input/hal/in2_state.c の input_capture にある。公開の列挙用情報をここに足す。
3. N2 のリングと公開イベントは別形式。input/hal/in2_int.h の InputEvent の内部形式は offset 4 が generation、原設計の IN2_Event の公開形式は seq。input/hal/in2_ring.c の sequences の別配列から seq を得て、種別ごとに公開形式へ写す。
4. N6 の時計は input/hal/in2_time.c の in2_time_ms で tick_clock_ms() に接続済み。AV0 の weak 定義・新しい時刻源は不要。音と入力の時刻を同じ口から取る。
5. 原設計 §7-6 が外すとした KAPI include の実名は sdk/kapi.json の includes の snd_contract.h。廃止 body の生成後、残る宣言の利用先を確かめて includes から外す。
6. FM 部品の build/fm-module.mk の FM_DEF_SOURCES・FM_OBJS・snd_engine.o の規則 に加え、build/kernel-objects.mk の CORE_KERNEL_INCLUDES は CORE に snd_engine.o 自体をリンクする。ヘッダ一覧だけの掃除では CORE が組めない。
7. 原設計 §7-6 の since 85 と追記 3 の最終整理予定は、v90 着手時の整理時期を §5 のとおり適用する。sdk/kapi.json の version の版と POLICY_DEV §10 に合わせ、今回の実装案は §5 に示す。
8. 原設計の段の表 の D1/D2 新設時の前提から入力側が進んだ。未統合の wip/s4-impl は f13232896（M5/M6）、4512a11af・2800873d3（M7）、f673a4678（S4/S4i）を含む。既設の生産経路は drivers/input/kbd.c の kbd_deliver と drivers/input/mouse_bus.c の mouse_hal_capture 呼び出し。AV0 に枝の配送処理を取り込まない。

## 2. v90 の公開宣言

番号は本体の C 宣言順（§3 音、§4 前景、§5 入力、§9 V86）、続いて追補の宣言順とする。
全行の戻り型は int。表中の N は OS32_ERR_NOSYS。仮の実体は入力を参照せず、出力・装置・所有状態に触れない。

| slot | 名前 | 引数型と順序 | 仮の戻り |
|---:|---|---|---|
| 298 | `snd2_dev_count` | `void` | N |
| 299 | `snd2_dev_info` | `u32 dev, SND2_DevInfo *o` | N |
| 300 | `snd2_caps` | `u32 dev, SND2_Caps *o, u32 size` | N |
| 301 | `snd2_dsp_unit` | `u32 dev, u32 unit, SND2_DspUnit *o` | N |
| 302 | `snd2_dsp_param` | `u32 dev, u32 unit, u32 i, SND2_DspParam *o` | N |
| 303 | `snd2_dsp_insert` | `u32 stream, u32 unit, const i32 *params, u32 n` | N |
| 304 | `snd2_dsp_process` | `u32 dev, u32 unit, const i32 *params, u32 n, const void *in, void *out, u32 frames, u32 fmt_index` | N |
| 305 | `snd2_stream_open` | `const SND2_StreamDesc *d, u32 *stream` | N |
| 306 | `snd2_stream_write` | `u32 stream, const void *buf, u32 bytes` | N |
| 307 | `snd2_stream_start` | `u32 stream` | N |
| 308 | `snd2_stream_pause` | `u32 stream` | N |
| 309 | `snd2_stream_pos` | `u32 stream, SND2_Pos *o` | N |
| 310 | `snd2_stream_wait` | `u32 stream, u32 frame, u32 timeout_ms` | N |
| 311 | `snd2_stream_volume` | `u32 stream, u32 left, u32 right` | N |
| 312 | `snd2_stream_close` | `u32 stream, u32 how` | N |
| 313 | `av_focus_set` | `int owner` | N |
| 314 | `av_focus_get` | `void` | N |
| 315 | `in2_pad_state` | `u32 slot, IN2_PadState *o` | N |
| 316 | `in2_pad_caps` | `u32 slot, IN2_PadCaps *o` | N |
| 317 | `in2_pad_vibrate` | `u32 slot, const IN2_Vibration *v` | N |
| 318 | `in2_dev_count` | `void` | N |
| 319 | `in2_dev_info` | `u32 i, IN2_DevInfo *o` | N |
| 320 | `in2_read` | `IN2_Event *out, u32 max` | N |
| 321 | `in2_key_state` | `u8 *out` | N |
| 322 | `in2_mouse_state` | `IN2_MouseState *o` | N |
| 323 | `in2_acquire` | `u32 flags` | N |
| 324 | `in2_unacquire` | `void` | N |
| 325 | `in2_status` | `IN2_Status *o` | N |
| 326 | `in2_wm_state` | `IN2_WmState *o` | N |
| 327 | `v86s_begin` | `const V86_Begin *b, u32 *sid` | N |
| 328 | `v86s_run` | `u32 sid, const V86_RunIn *in, V86_Exit *ex` | N |
| 329 | `v86s_fm_drain` | `u32 sid, SND2_FmEv *out, u32 max, u32 *last_seq` | N |
| 330 | `v86s_end` | `u32 sid` | N |
| 331 | `snd2_chip_caps` | `u32 dev, SND2_ChipCaps *o, u32 size` | N |
| 332 | `snd2_chip_open` | `u32 dev, u32 *h` | N |
| 333 | `snd2_chip_write` | `u32 h, const SND2_RegW *w, u32 n` | N |
| 334 | `snd2_chip_read` | `u32 h, u32 bank, u32 what, u32 reg` | N |
| 335 | `snd2_chip_reset` | `u32 h` | N |
| 336 | `snd2_chip_close` | `u32 h` | N |

公開ヘッダは固定幅の SDK 型と C89 の宣言だけを使い、内部の InputEvent、装置ヘッダ、ポインタ幅依存の構造体を公開しない。
AV0 の SDK 担当が os32api に公開構造体・共用体の #[repr(C)] 型を置き、sdk/kapi_rust_gen.py の型対応で const 入力と可変出力を区別する。321 の JSON は u8 *out、out.size は 128。生成した関数ポインタをこの型で代入してコンパイルする。

| slot | CPL3 の生成ガード（入＝読、出＝書）と target の担当 |
|---|---|
|299,301,302,309,315,316,319,321,322,325,326|生成で順に 64,32,32,32,20,64,32,128,16,32,16 B 出。実体で dev・slot・前景等の資格を検査|
|305,317,327,328,332|生成で順に 32 B 入＋4 B 出、4 B 入、136 B 入＋4 B 出、32 B 入＋48 B 出、4 B 出。実体で構造体・owner を検査|
|300,331|size が正の4の倍数、順に 256,64 以下を先に確認し、size B 出|
|303,304|n ≤ 16 の後 4n B 入。304 の音声 in/out は JSON で target 担当。実体で dev・unit・fmt_index を確認して max_frames と frame_bytes を取得し、frames の上限、u64 積、アドレス長への収まり、入出力全域の順に確認して処理|
|306|target が §3-5 の番号・owner・状態・NULL の順で確認し、受理する長さを決めて読める範囲を検査して写す|
|320,329,333|max ≤ 128、max ≤ 512、n ≤ 256 を先に確認し、順に 16max B 出、12max B 出と独立の last_seq 4 B 出、4n B 入。333 は実体で全件の bank を確認してから書く|
|298,307,308,310–314,318,323,324,330,334–336|ポインタなし。実体が各口の番号・状態・owner 等を確認|

可変長の公開上限・整列違反は範囲ガードに先立って INVAL。長さの積は切り詰める前に検査する。静的に決まる範囲は生成側で検査し、CPL3 の不正な非空範囲は既存規約の ring3_fault_kill。target 指定の範囲は生成器が飛ばすため、実体がアクセス前に同じ保護を行う。AV0 の弱い target は装置依存の長さを推測せず NOSYS。実体化後は設計各節の資格・状態エラーを返し、事前検査の失敗で出力や装置を更新しない。
AV0 の仮口を通った有効な呼び出しは、件数を返す口も N。既存の PCM 5 口は S 段で代替実体を接続するまで現行経路を使う。

## 3. 段の割り当て

R0a → AV0 → R0b・R0c の順に統合する。R0b は以下の FM → V86 → 回収の順に、依存する変更を同じリンク単位で着地させる。
新規 .c の object・対象機種のリンク一覧、新規試験の CHECK と入力宣言はその作成段に含める。利用側の削除は提供側を消す前に揃える。
生成物は入力ファイルを変えた段で一括更新する。途中の分割を個別の公開版にはせず、統合した一組だけを v90 として配備する。185 の廃止だけを先行配備せず、F1 の強い実体と gshell の切替を初回配備の条件とする。

- **R0a・生成器**　sdk/gen_kapi.py に retired の処理を追加。通常の引数検査や target 呼び出しの前に、型どおりの廃止 body を生成する。
- **R0a・定義**　sdk/kapi.json の 42・43・118・119・121〜124 は void の無作用、120 は 0、185 は NOSYS。since は 90。同じ段で kapi/kapi_strings.c の 43・118 と JSON の対応する文字列注記を外し、syscall 入口から文字列を読まない。126〜131 の直接 FM/SSG 6 口はそのまま使う。
- **AV0・宣言**　公開の音・入力・前景・V86 型と内部の backend 契約を各ヘッダの所有者で定義する。入力 HAL の既存内部ヘッダには公開型との変換に要る宣言だけを追加する。
- **AV0・実体**　音と入力の a0_weak.c に未実装の口だけを定義する。共有の前景と V86 の仮口も所有者を一つに定め、両機種の実リンクから到達させる。
- **AV0・生成**　sdk/kapi.json 末尾に §2 を追加し、version と os32_kapi_shared.h の KAPI_VERSION を 90 にする。容量 384、データ開始 0x608、ABI 世代 2、crt 実名 os32_kapi_v89 を保つ。
- **AV0・[ABI]**　python3 sdk/gen_kapi.py と python3 sdk/kapi_rust_gen.py で C/Rust 宣言、slot、wrap、exec 初期化表を生成する。KAPI_SPEC の表と新 API 利用プログラムの要求版を同期する。
- **AV0・配置**　HWDEV の構造体表と指紋を実配置から再生成し DWARF と照合する。PowerPC の生成表・呼出規約も同じ番号と型で確認する。
- **R0b・FM**　fm_body.c、fm_module.h、fm_module.c の提供側・利用側・登録と CORE 内蔵版を一緒に変更する。ops 世代を 2 とし、MML、tick 登録と master を除き、quiet と全キーオフ、直接操作を残す。
- **R0b・回収**　起動音、exec 終了時の snd_owner_exit、FM 部品と CORE のリンク・import・ヘッダ参照を外してから snd_engine.c/.h を削除。PCM の回収は維持する。
- **R0b・V86**　platform/pc98/legacy/v86_io.c の snd_state_for_guest・snd_state_for_os32 の両方で fm_all_off()、ssg_all_off() を呼ぶ。fm_contract.h と v86_imports.def を合わせ、往復双方の既存の静止関数名を保つ。
- **V5 への引渡し**　AV0 は v86s_* を宣言して NOSYS まで。V5 が legacy_call_dispatch と PC-98 の slot switch で owner・引数を検査し、PowerPC の platform/none は NOSYS を返す。
- **R0c・利用側**　shell の play・MML に依存する beep、bench、sndctl・sndtest 群を外す。F1 の強い set/get と同じ着地で gshell は av_focus_set に切り替え、WM の偽実体と期待も同じ段で移す。
- **R0c・配布**　旧 userland/lib/snd のヘッダ・実体、libs.mk、SDK と配布一覧、試験登録を利用側と合わせて除く。L1〜L5 が userland/lib/snd と libos32snd の名を引き継ぐ。
- **入力の後続段**　PowerPC の lz4 後、M5〜S4 が main に入ってから N 系を始める。AV0・R0a〜c はこの統合を待たない。N1 は既存枠に列挙情報、N2 は既存リングに公開形式への変換、N4 は既存の押下・移動に公開スナップショットを足す。N6 は時刻の照合で閉じる。
- **入力の統合**　F1・N3・N5・N7 は前景・独占・パッド・回収を担当。K2 は新しい公開口を結び、D1/D2 と G1 は M5〜S4 着地後の生産側・WM に足りない接続だけを加える。
- **入力の競合**　取り込み時に同じ物理イベントが二度積まれないこと、WM と公開 read が同じ項目を奪い合わないことを確認する。N2 は統合後の生産側を再照合し、既設の捕捉を再登録しない。WM と前景の公開 read は各自の読取位置だけを進める。N2 が内部 LOST の x/y を下位/上位各16 bit として u32 に戻し、公開 count を 65535 で飽和させる。累積 dropped は32 bit のまま。

| 段 | PC-98 通常・R1 | PowerMac |
|---|---|---|
|AV0|build/sources/common.mk と order.mk に共有仮口・ガードを登録し、生成 wrap からリンク|C_COMMON_EXEC と arch/ppc/kapi_call.c のアダプタから共有口へ接続し、V86 は platform/none 経由|
|R0b FM|build/fm-module.mk の部品と build/kernel-objects.mk の内蔵版で ops v2 を同時更新|PC-98 FM はリンク対象外。共有契約を再コンパイル|
|R0b V86|往復の静止、提供側、v86_imports.def の import と部品を同時更新|platform/none のリンクを維持|
|R0b 回収|最後の参照と両リンク一覧の snd_engine を同時に削除|共有 exec 終了経路を旧音源への参照なしでリンク|

## 4. 受入と変異

- [ABI] の最終変更後に make clean → make all。make ppc-ci-image で共通ソースのコンパイルと PowerPC リンクを確認する。外部 apps/game は H-7 の再開決定後に再構築する。
- 両対象で公開ヘッダを GNU89 と Rust から使い、sizeof と offsetof を照合する。両機種で公開 C/Rust 表から slot 304 に識別可能な8引数を渡し、試験 target で順序・値を照合する。321 は可変 u8 ポインタを渡す。例は IN2_Event 16 B（seq は 4、flags は 12）、SND2_Caps 256 B、V86_Begin/RunIn/Exit 136/32/48 B。
- ABI 試験で 297 の io_pump、39 口すべての型・番号・戻りを確認し、出力に詰めた番兵が残ることを確認する。変異は隣接 slot の入替え、仮口の成功返し、出力への 1 byte 書込み。
- 範囲試験は AV0 で上限ちょうど・超過、321 の末尾と 329 の第2出力だけが範囲外の場合、304 の仮口 NOSYS を確認する。実体化時は 304 の積の桁あふれ・片側だけ範囲外で処理開始前に拒否されることを確認し、積の縮小・last_seq 検査除去を変異にする。
- 入力の実体化時には物理入力1件を WM と公開 read がどちらの順でも各1件だけ読み、LOST 65535・65536 で公開 count がともに65535になることを確認する。さらに generation と seq を異なる値にし、末尾の第 16 枠、再割当て、LOST と STOP を含めて変換を試す。変異は seq を generation に置換、探索上限を 15 に縮小。
- R0a は CPL3 の 43・118 に読めない番地を渡し、次のユーザー命令へ復帰することを観測する。文字列走査を各口に戻す変異で失敗させる。他の変異は廃止口から旧 target を呼ぶ、120 を NOSYS にする、185 を 0 にする。直接操作 6 口は偽 FM backend への到達を確かめる。
- R0b の FM 部品試験は ops v2、tick 非登録、静止を観測。V86 は引渡し・復帰それぞれの FM と SSG を記録し、片側の全キーオフを一つ外す変異を検出する。
- R0c は raw_wait の廃止したケースと変異の参照を同時に外す。gshell の前景交代につき通知 1 回と F1 の保持する前景 owner が新 owner へ更新されること、旧ライブラリ無しで SDK と配布イメージが組めることを確認する。
- 新しい検査は設計節を basis とし、変更した検査を変異付きで実行して各変異が該当する観測で RED になることを記録する。最後に python3 -B tools/checks/run.py --lint。
- dep-dir-a-l01-host の依存期待を snd_engine の撤去段で更新し、FM 部品検査は ops v2 の登録、V86 の import・往復検査は静止の切替段で追従させる。

| 段 | 通常 x86 の直前段との差 | R1 の直前段との差 |
|---|---|---|
|R0a・AV0|文字列表・wrap・公開表と利用 object、それを含む ELF/bin/lz4|R1 の同じ登録先・リンク像|
|R0b|FM/V86 の変更 object・部品・内蔵 CORE とリンク像、旧 object の削除|R1 の該当 object・部品・内蔵像|
|R0c|F1 の kernel 変更を基点に利用側だけの差は kernel/module 同一。配布品は make all で確認|同じ基点で kernel/module 同一|

tools/build_compare.sh の build/out 全対象（.o/.elf/.bin/.mod/.lz4/.a、kdebug を含む）の増減を照合し、各差を function-diff.txt と変更先で説明する。

- IA/IS/IL の統合時は av_no_weak.sh と最終リンク表で担当口の強い実体を確認する。ゲストの起動、WM の STOP とキー配送、V86 往復の受入は設計 §13 の各統合段で記録する。

## 5. MML の公開欄

確定設計の追記3に従い、v90 は MML の10欄の型と番号を保ち、旧実装を撤去する。廃止口の NOSYS 相当の型別の戻りは R0a の定義に従う。公開欄の削除は v3 最後の KAPI 整理で行う。

反論なし。

## 確定 (2026-11-21、sol 第 2 回の 5 件を PM が反映 — 往復は 3 回の上限の内で、残りは直し案が具体なので PM が書いた)
以下は本文に優先する。
- **P1 R0a の廃止の口の CPL3 の検査**: 43・118 を kapi_strings から外すのに加え、生成器が出す kapi_argptr の `retired` の口のマスクを 0 にし、廃止の wrapper の前に CPL3 のポインタの検査が走らないようにする。許容の帯の外の番地を渡しても両方の口が復帰することを受入と変異に入れる。
- **P1 範囲の検査の順**: 生成器に「全域の検査より前の長さの検査」を足し、上限・整列の違反を範囲の検査より先に INVAL にする。長さの違反と範囲の違反が重なった場合も INVAL になることを受入に入れる。
- **P1 V5 への引き渡し**: PC-98 の exec_legacy_call_dispatch が処理済みを返すと通常の wrapper に進まないので、V5 の CPL3 の道でも同じ生成のガードを通す。slot 329 の last_seq だけが範囲外の場合を、実体化の後にも確かめる。
- **P1 R0b のリンクの単位**: FM の側で snd_set_master の提供を外す変更と、V86 の import と往復の呼び出しの切り替えを、同じリンクの単位 (同じ段) にまとめる。
- **P1 R0c と入力の順**: 初回の配備に要る F1 は原設計 §12 で N 系の in2_on_focus に依存する。R0c の前景の切り替えと初回の配備は、M5〜S4 の着地の後の F1・in2_on_focus の接続に従属させる (R0c は M5〜S4 と N の F1 の後)。AV0 の宣言・R0a・R0b は M5〜S4 を待たない。
