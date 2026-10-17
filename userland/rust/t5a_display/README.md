# t5a_display — GUI 端末アプリ

`con_sink` の出力を表示し、起動要求表を通じてコマンドを実行する端末。
現行の登録は `build/app.conf:200` の `userland/tests/t5a_display` (launcher 宣言)。
Rust workspace・ビルド・配備にも登録済み (`userland/rust/Cargo.toml:25`、
`build/programs.mk:633`、`userland/deploy.yaml:461`)。
旧提出報告・段別差分・二窓の操作台本は [TDD_LOG の過去記録](TDD_LOG.md#旧-readme-の提出記録-2026-10-17-保存) に分けた。

## 現在の操作と起動要求

- プロンプトで名前と引数を入力し Enter で実行する。絶対パス以外は
  `/usr/bin/<名>.bin` → `/bin/<名>.bin` の順に探す。
  `launch_req` で要求を積み、返った token を保持する (`src/guest.rs:337`)。
  表が満杯なら `busy` と表示し、入力行を残す。
- 接続中の打鍵は `kbd_inject` へ渡す。ローカルエコーはせず、出力は `con_sink` から読む。
  完了は token に対する `launch_poll` で確認する。`EXIT` レコードだけで切断しない。
- 接続中の ESC は `launch_cancel` を一度送り、接続モードのまま `DONE` を待ってから
  プロンプトへ戻る (`src/guest.rs:424`)。子へ ESC を注入しない。
  プロンプトの ESC と `exit` は端末自身の終了。
- 表示移動は UP/DOWN/ROLLUP/ROLLDOWN/HOME。F9 はコピー、F10 は貼り付け。
  読み手権限が取れない場合は busy を表示し、打鍵を注入しない。

## 表示と入力の境界

`sink.rs` は `con_sink` のレコード列を解く純パーサ。EXIT の共有定義は
`sdk/include/os32/os32_kapi_shared.h:691` (`CON_SINK_REC_EXIT`) と
同 `:697` (`CON_SINK_HDR_EXIT`) にある。
`session.rs` は CLEAR と容量満杯時の再初期化を `Fixture::Live` にまとめ、
畳んだ回数・捨てた文字・無視した CURSOR を状態行へ渡す。履歴を黙って消さない。
読み出しは反復タイマで行い、待ちは GUI の GetMessage 経路を使う。
最下行は端末のプロンプト行で、本文の描画・clip に含めない。

文字幅・字形は [描画アダプタ](../../libos32term_render/README.md#文字とマーク) の契約に従う。
ANSI/VT は解釈しない。TAB/BS/CR/LF は [端末モデル](../../libos32term/README.md#実装前に定める境界動作) が処理する。

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

## PaintとFFI境界

窓は一枚。client 寸法から `Layout` を作り、本文と最下行のプロンプトを分ける。
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

## 検証の範囲と未確認

ホスト試験は本体の純粋モジュールを `#[path]` で参照し、コピーは持たない。
実行入口は `make check-t5a-host`。RED/GREEN と提出時点の結果は [TDD_LOG](TDD_LOG.md) にある。
ホスト試験だけでは guest.rs の実行、CPL3 KAPI、実字形、WM のイベント配送、
clip の寿命、再露出、終了後の復元、性能を保証しない。
