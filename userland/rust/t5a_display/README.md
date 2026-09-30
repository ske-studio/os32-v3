# t5a_display — 端末アプリ（con_sink 表示、ゲスト受入待ち）

> **2026-09-12 (票 K6C-A)**: 固定 fixture 表示アプリを **端末アプリ** に作り替えた。
> 以下「K6C-A の差分」より下の節は T5a 提出時点の記述で、`guest.rs` に関する部分
> （二窓構成、キー 1〜5 での fixture 切替、ゲスト操作台本）は**もう当たらない**。
> `state.rs` / `view.rs` / `paint.rs` / `boundary.rs` / `storage.rs` の記述は有効。

## K6C-A の差分

- `sink.rs`（新規）: `con_sink` のワイヤ形式（`CON_SINK_REC_PRINT/CLEAR/CURSOR`）を
  レコード列へ解く純パーサ。`no_std`・KAPI 非依存で、ホスト試験の対象。
  空 / len 0 / 未知 type / 上限超えの len / 3 バイト UTF-8 / バッファ末尾ちょうどで
  終わるレコード / 途中で切れたレコードを試験する。
- `state.rs`: `Fixture::Live`（空で始まり sink の出力だけが入る）と `feed_live` /
  `place` / `place_cursor` を追加。既存 fixture の挙動と試験は変えていない。
- `session.rs`: `apply(Record)` を追加。`CLEAR` と「画面が埋まったら畳む」を
  `select(Fixture::Live)` の 1 経路にまとめ、畳んだ回数 (`wraps`) と捨てた文字
  (`lost`)、無視した `CURSOR` を呼び側へ返す。
  T4 モデルはスクロールしないので、**畳まないと 64 行で端末が死ぬ**。
- `status.rs`: `SinkStatus`（読んだバイト / レコード数 / `dropped` / `ring` /
  形式違反 / `wraps` / `lost` / 最後のエラー）と Live 用の状態行。
  `dropped` と読み手拒否は必ず状態行に出す。
- `guest.rs`: fixture 供給を sink 供給へ置換。**待ちは GetMessage 方式のまま**で、
  吸い出しは 100ms（`Timer::repeating`、10 tick）の反復タイマの中だけ。busy loop は無い。
  1 周あたり 8KB を上限に、1KB の私有バッファ（`>= CON_SINK_REC_MAX` = 203）で
  空になるまで `con_sink_read` する。`OS32_ERR_EXIST`（読み手は先客）は状態行に
  出したまま次の周も試し、それ以外の負値は以後読まない。末尾追従を既定にし、
  `j` `k` `g` で解け `e` で戻る。T5a の cover 窓は落とした（端末に余分な窓は要らない）。
- `build/app.conf`: `userland/tests/t5a_display` を **KAPI 47** で登録（K7-A で 46 → 47）。

## K7-A の差分（票 TASK_K7_input.md §5 R2 / §6）

- `inject.rs`（新規）: 打鍵 → 注入バイト列の純変換。`no_std`・KAPI 非依存でホスト試験の対象。
  `GUI_EV_TEXT` は `sub` の下位 7 bit を長さとして payload をそのまま渡す（FEP の確定文字を含む）。
  `GUI_EV_KEY` は押下の制御キーだけ: **Enter → 0x0D**、BS → 0x08、TAB → 0x09。
  ESC は端末自身の終了に使うので注がず、矢印・ファンクション・印字可能キーも注がない
  （印字可能は gshell が KEY と TEXT を**両方**積むため、KEY 側で注ぐと二重になる）。
  Enter を `\n` にしない根拠は `inject.rs` 冒頭と票 §8。
- `guest.rs`: **タイマ登録の前**に `con_sink_read` を 1 回呼んで読み手権限を確立する（R2）。
  `OS32_ERR_EXIST`（先客）なら `reader = false` のまま — 状態行は busy、以後の打鍵は**捨てる**
  （この判定は一度きりで反転させない）。注入は `on_raw`（TEXT）と `on_key`（制御キー）から。
  **ローカルエコーはしない**（CUI 側の出力が con_sink 経由で戻る）。
- `status.rs`: `kbd_inject` の負の戻り値（`inject rc=N`）と、あふれで消えたバイト数
  （`injdrop=N`、`0 <= rc < len` の差）を状態行 1 行目に足す。描き直すのは値が変わった周だけ。
- `input.rs`: 表示操作を注入しないスキャンコード（UP/DOWN/ROLLUP/ROLLDOWN/HOME）へ移した `nav()`。
  読み手を取れているあいだ ASCII の `j k g e q` は解釈しない（票に無い判断、要レビュー）。

## T7-A の差分（票 TASK_T7_terminal_cmd.md §2 E2〜E5）

- `prompt.rs`（新規）: プロンプト行の編集（印字可能 ASCII / `GUI_EV_TEXT` の UTF-8 / BS、
  多バイト文字を割らない）、確定した行の解釈（空行 / `exit` / 名前 + 引数）、候補パス
  （`/` 始まりはそのまま、名前は `/usr/bin/<名>.bin` → `/bin/<名>.bin`。すでに `.bin` なら
  重ねない）、`session_launch` へ渡す値の組み立て、モード遷移 `step()` の純関数。
  `no_std`・KAPI 非依存でホスト試験の対象。
- `sink.rs`: `EXIT`（type **4**、`[type][id u8]`、票 E1）を `Record::Exit(id)` として解く。
  カーネル側が `CON_SINK_REC_EXIT` / `CON_SINK_HDR_EXIT` を足すまではここの定義が唯一。
  `CON_SINK_REC_MAX` は不変（PRINT が最長のまま）。
- `view.rs`: 最下行を端末のもの（プロンプト行）として確保。出力は `body_rows()` = `rows() - 1`
  で、`view()` の高さも clip 下端も `prompt_y()` で止まる。窓の最小高さは出力 1 行 + プロンプト行。
- `guest.rs`: `Mode::Prompt` / `Mode::Attached`。プロンプト中は**1 バイトも注入しない**（打鍵は
  行に入る）。Enter で候補を `sys_open` → `sys_close` で存在確認し、見つかった絶対パス + 引数を
  `session_launch` へ。見つからなければ `command not found: <名>` をローカル出力（con_sink は
  通らない）、`ERR_FULL` は `busy` を出して**行を残す**。受理されたら接続モードへ移り、打鍵は
  K7-A の経路で `kbd_inject`。`EXIT` を受けたらプロンプトへ戻る（ID は照合しない）。
  接続モードの ESC は**プロンプトへ戻るだけ**（子には注がない）、プロンプトの ESC と `exit` は
  従来どおり端末自身の終了。打鍵ごとの描き直しはプロンプト行だけ `invalidate`。
- 票に無い判断（要レビュー）: 確定した行を `> <行>` として出力領域にも残す（接続モードでは
  プロンプトが消えるため）。接続モード中の最下行に `[running] ESC=prompt` を出す。
  プロンプト表示中は ASCII の表示操作（`j k g e q`）を解釈しない（その打鍵は行に入る）。

ホスト検査: `cargo check --release -p t5a_display` と host テスト 48 件（T7-A で 36 → 48）が通る。
**`make` / 配備 / エミュレータ実行は未実施**（コーダーの範囲外）。

---

# （以下 T5a 提出時点の記述）

編集範囲はこの新規ディレクトリのみ。既存workspace/build/deploy、共有クレート、
共有docsは変更していない。staticlibのmanifestには追加の`[workspace]`を置かない。
配備・エミュレータ・commit/push・他エージェントは実施していない。

## 実装と検証の区別

- `state.rs`: Terminal、fixture識別、top、消費バイト数、全入力長、停止段階。
- `session.rs`: 外部保存領域の唯一の借用所有者。fixture切替時の破棄・再生成。
- `storage.rs`: 静的未初期化領域を一度だけ貸出し、Cellをその場で初期化。
- `view.rs` / `boundary.rs`: client行数・Paint交差・GUI座標範囲・二窓寸法・JIS検査。
- `paint.rs`: 純粋なアプリ→既存renderer接続。Glyphs/Sinkを注入。
- `input.rs` / `status.rs`: 固定キー操作、ASCII停止表示。
- `guest.rs`: 実在GUI/Unicode/KAPIへの接続、Window所有、on_key/on_close/on_paint。
- `lib.rs`: no_std/no_main staticlibのC entry。

host_testsは独立workspaceで本体の各モジュールを`#[path]`参照する。コピーはない。
os32apiには独自panic_handler/global_allocatorとゲストFFIがあるためhost依存から除外した。
依存は既存in-repoのlibos32term / libos32term_renderだけ。新規外部依存はない。

リポジトリrootから実行:

```sh
cargo test --offline --manifest-path userland/rust/t5a_display/host_tests/Cargo.toml
rustfmt --check --edition 2021 userland/rust/t5a_display/src/lib.rs userland/rust/t5a_display/host_tests/src/lib.rs
```

結果: **host 18 passed / 0 failed、fmt check exit 0**。
実コマンド・出力・終了コードは[TDD_LOG.md](TDD_LOG.md)。S1〜S9の未実装stubに対する
assertion REDを実行してから実装した。S3の直接Display再生成試験は初回GREEN回帰と記録し、
新挙動REDの代用にはしていない。GUIループ再呼出し方式は実装中に取りやめ、最終の
Session切替はS9で別途RED→GREENを実行した。ゲスト境界の新規配線はhostで実行しておらず、
そのTDD/動作合格を主張しない。fmtはゲストコードの型検査・クロスリンクを代替しない。

## 保存領域と初期化の安全性

通常40列×64保存行、CAPACITY=2560セル。名前付き定数とコンパイル時容量整合検査、
Session::newの容量検査、Model::newの寸法/容量検査を使う。既存T4のCell=8Bなら20KiBだが、
本アプリの最終ELFによる実寸・BSS・stack配置はPM確認待ち。

既存Model/Terminalには保存sliceを取り出すAPIがない。大きな配列のheap生成で生じる
一時stack配列も避けるため、`static STORAGE: Storage`内の
`UnsafeCell<MaybeUninit<[Cell; CAPACITY]>>`を採用した。const初期化し、貸出時に各Cellを
ポインタ先へ直接writeする。ゲスト経路で`[Cell; CAPACITY]`のローカル生成はない。
大配列を使うテストは`cfg(test)`内だけ。実行中のglyph配列は16/32B、状態行は5×64B。

AtomicBoolのclaimはプロセス寿命中に一度だけ成功し、解除しない。再入/二回目mainは
GUI/KAPI initより前に拒否される。初期化が失敗した後も同じプロセス内で再貸出ししない。
新しいプロセスとしての再起動はローダによる静的領域の新規初期化が前提（ゲスト確認待ち）。

Sessionは外部sliceの元ポインタと`PhantomData<&'a mut [Cell]>`を保持する。
配列はSession自身のフィールドに置かれていないので自己参照構造ではない。
元のslice参照はポインタ化後に再利用しない。Terminal/Displayの所有値や可変参照を
外へ返さず、読取り参照の寿命を`&self`へ束縛する。selectは`&mut self`を必要とし、
旧Displayを`None`にして破棄した後にだけ元ポインタから再借用する。
これがunsafe再借用の根拠であり、transmuteや無根拠な`'static`延長はない。
新Terminal生成でdecoder prefix、pending、limit、top、消費位置をすべて捨てる。
Sessionの移動で保存領域の番地は変わらない。

GUI本体`app.rs::run_vt`と`shlib.rs::os32gui_ui_quit`を確認したところ、quit状態は
runの再呼出しでは解除されない。したがってfixture切替ではquit/run再開を使わず、
一度のrun内でSession::selectしてinvalidateする。Window所有型は同じまま保持する。

## 固定fixtureと停止表示

| キー | 入力 | 期待停止・消費 |
|---|---|---|
| 1 | ASCII、日本語、全角英数、半角カナ、円記号、NUL/ESC、不正UTF8、TAB/BS/CR/LF、40行の固定履歴 | complete、229 bytes、50保存行 |
| 2 | 2560個のA（40×64） | complete、2560 bytes、cursor=(40,63) |
| 3 | fixture 2 + XY | feed:Full、consumed=2561、pending=0058、unconsumed=1 |
| 4 | 1列×1行で「日!」 | feed:TooWide、consumed=3、pending=65E5、unconsumed=1 |
| 5 | fixture 2 + E3 81（末尾未完了UTF8） | finish:Full、consumed=2562、pending=FFFD、unconsumed=0 |

入力容量停止時は次のfeed/finishを試みない。表示を待つpendingと、まだ受け取っていない
末尾バイトは別物。consumedは「消費した入力バイト数」であり表示済み文字数ではない。
完了時は`pending=----,----`。数字はUnicode scalarの16進表記。
本文外の上5行にfixture/停止段階、consumed、unconsumed、pending、top/保存行数/
前回Paint本文run数をASCIIで表示する。`prev_runs`は時間や総描画数ではなく、直前Paintの
本文hline呼出数。打切り上限は設けない。性能閾値・ゲスト実測は未確定。

文字幅とglyph方針は既存rendererをそのまま使う:
ASCII/半角カナ/円記号はSingle、全角英数はWide枠内にANKを中央配置。
NUL/ESC/DELなどはSingleの枠＋斜線、U+FFFD/変換不能文字はWideの同じマーク。
TAB/BS/CR/LFはモデルの制御操作、ANSI解釈はしない。

## PaintとFFI境界

窓寸法はscreen_infoから算出し二枚とも画面内へ収め、i16変換を二枚分とも済ませてから
作成する。小さすぎる/GUI座標に収まらない画面はINVAL。通常画面で本文は40列。
狭いclientでは右側をclipする。client高さから
`floor((client_height - BODY_Y - MARGIN) / CELL_HEIGHT)`の表示行数を求める。
本文原点は(8,88)。topの加算はrendererが一度だけ行う。

on_paintで所有WindowとSurfaceIdを照合し、イベントRectとその時点のcurrent_clipを交差。
その後pure Layoutでclient/本文との交差とGUI範囲を検査し、renderer自身のGrid交差・
全座標/長さ/モデル幅のpreflightが完了してからGlyphs/Sinkを呼ぶ。
GUI Rectはi16、hline引数はi32。原点・最後の画素・run長はこの範囲を満たす。
状態欄もclient幅に合わせてASCII長を切り、固定5行の高さをLayoutで事前確保する。
GUIループ自身の下地描画は既存ライブラリの責務。

PaintSink型はon_paintのローカルでのみ生成し、ハンドラ外に保存しない。
Rect値を保持しただけではPaint権限にはしない。base clipの設定/解除はGUIループが行い、
アプリは操作しない。背景/前景とも既存GUI色定数を使うhlineへ送る。
物理VRAM、gfx_init、独自present経路はない。
Paintごとにモデルから再描画しdirtyセルには依存しない。モデルは共有参照だけ。
保存行不足はrendererによる背景のみ。範囲/rendererエラーはタイトルにASCIIで通知する。

FFIの根拠:

- `lib/utf8.h`: `u16 unicode_to_jis(u32)`。アプリは`lib/utf8_prog.o`へ最終リンク。
  `lib/utf8.c`の既知4組probeを使用しready強制設定を呼ばない。
- `sdk/rust/os32api/src/kapi_generated.rs`: 既存のunsafe extern C
  kcg_read_ank(u8,*mut u8)、kcg_read_kanji(u16,*mut u8)。libos32gui::init後のみ使用。
- `drivers/kcg.c`と`userland/lib/gfx/text/gfx_kcg.c`: ANK=16B、漢字=32B、
  row*2が左、+1が右、MSB-left。上位/下位を各0x21..0x7Eと検査してから漢字読出し。
  void APIには欠落通知がなく、全ゼロを勝手に未収録と見なさない。
- gui_demo/v12_api_test/filerとstubの実宣言・使用例から、init/run、WindowのDrop、
  client_size、on_key/on_close/on_paint、hline/textの経路を照合した。

unsafeは静的保存領域のclaim後初期化、Sessionの外部領域再借用、実C変換/KAPI読出しに限定。
ホスト試験は前二者も含むが、Miri/ゲストKAPI/共有ライブラリの実行ではない。

## PMの具体ビルド待ち条件

この提出ではguest buildとmake checkは**未実行**。未登録を迂回するためのworkspace追加、
他ファイル変更、仮guestビルドは行っていない。

1. PMが`userland/rust/Cargo.toml`へ`t5a_display`を登録する。
2. PMが既存`DEFINE_RUST_PROGRAM`形式で`t5a_display`を登録し、追加オブジェクトに
   `lib/utf8_prog.o`を指定する（kernel版utf8.oは禁止）。参考登録形:
   `$(eval $(call DEFINE_RUST_PROGRAM,t5a_display,userland/tests,lib/utf8_prog.o))`。
   programs対象/deploy登録はPMが直列に担当する。
3. `make t5a_display_rust`で本staticlibと実OS32Xを最終ビルドし、Unicode/GUI/KAPIの
   未解決symbol、main entry、ターゲットABIを確認する。
4. 最終ELF/map/disassemblyでSTORAGEがBSS等の静的領域にあり、約20KiBのセル配列が
   stackへ生成/コピーされないこと、全体BSS・heap/guard/stack配置を確認する。
5. 独立レビューとmake check。その後の配備・ゲスト受入はPM側の承認済み手順で行う。

host合格はゲスト専用guest.rsのコンパイル/型検査、CPL3 KAPI呼出し、C変換表マッピング、
GUI基底clip寿命、実glyphの見え方、WMイベント配送、再露出・終了復元を保証しない。
以上と最終OS32X/BSS確認をもってゲスト受入判定する。T5a全体完了とはまだしない。

## ゲスト操作台本（未実行）

1. PMが登録・最終ビルド・配備同一性を確認した`t5a_display`をgshell下で起動。
   同じアプリ内で本文窓と「T5a cover: drag me」が現れ、coverが一部を遮る。
2. coverのタイトルをドラッグして本文全体を露出。1の日本語/半角カナ/全角英数、
   制御マーク、TAB/BS/CRの期待表示とASCII状態欄を確認する。
3. どちらの窓にフォーカスがあっても`j`=1行下、`k`=1行上、`g`=先頭、`e`=末尾。
   `g → j → e → g`で本文とtop表示が復元すること。末尾topは50−表示行数（下限0）。
4. `2 → 3 → 4 → 5`で上表の停止理由/consumed/pending/unconsumedを照合。
   放置して無限再試行や数値の増加がないこと。4では保存行不足部分が背景だけになること。
   グリッド内背景はEDIT_BG、グリッド外はWINDOW色。4の1列グリッド外は別色で正常。
5. `3 → 1`、`5 → 1`、`4 → 1`を繰り返し、窓位置は保持され、top=0、pendingなし、
   complete/229 bytesへ戻ること。通常fixtureの後半に旧Aが残らないこと。
6. 同じcover窓を日本語の左右半分・上下端・非整列位置へ動かし、遮蔽/再露出した本文の
   画素が元通りになること。状態欄のprev_runsはPaintで変わり得るので画像比較では区別。
   状態欄だけのdamageでは本文run数が0になってよい。本文damage時の状態欄描画呼出しも
   基底clipで抑制されるが、呼出コスト自体は残る（性能は別途測定）。
   窓外、desktop、他窓の番兵領域に変化がないこと。
7. coverの×はcoverだけ、本文の×は本文だけを閉じる。両方閉じると終了。
   `q`またはESCはどちらの窓からでも全体終了。desktop/入力が復元すること。
8. PC98/PEGC/Cirrusで同じ確認を行い、実glyph、漢字表非使用時、runコスト、終了後再起動、
   バイナリ同一性の結果をPMが記録する。新規文字入力カーソルはない。
