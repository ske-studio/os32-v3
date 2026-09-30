# C2: クライアントスタブ、U3 ループ、ウィジェット木、箱レイアウト、gui_demo 書き換え

> 発行: PM (2026-09-05) / 状態: **受入完了 (2026-09-06)**
> レーン: C / 前提: C1 完了 + K1 完了 (共有ヘッダ確定)。動作確認は W1 と結合後
> 親: [TASKS.md](../../tasks/gui/TASKS.md) / 契約: T1〜T5, T7, U1〜U3, U5〜U7, U9 / 設計: [DESIGN.md](../../tasks/gui/DESIGN.md) §9.4
> 排他: `userland/rust/libos32gui/**`、`userland/rust/gui_demo/**`

## ゴール

アプリから見た libos32gui を契約どおりの形にする。アプリは `gui_call` を直接触らず、
Rust の所有型 (`Window` / `Widget` / `Timer`、`Drop` で destroy) とハンドラ
(`on_click` 等) だけを書く。既存 gui_demo を U3 のループに書き換えて、ゲート G2 の
アプリ側にする。

## 作業

1. **スタブ** (`client.rs`): `gui_call` KAPI の薄い包み。`OP_INIT` でスロット番号を受け、
   `MEM_SHM_GUI_BASE + n × 16KB` を要求 / 応答 / リング / 引数バッファに切る (K1 のオフセット)。
   `proto_version` 照合 (`ERR_VERSION` なら即 exit、理由を `dbg_print`)。
   戻り値 `< 0` は `Result<_, GuiErr>` に。**GetLastError は作らない** (T7)。
2. **イベント取り出し** (契約 T3): `OP_POLL` の戻り値 (件数) 分をリングから `[GuiEvent; 128]`
   に写し、**`head = tail` に進める** (`tail` は WM のもの。触らない)。導出型 (`Paint` /
   `Configure` / `Timer`) も同じリングに入って来るので区別しない。`OVERFLOW` なら `dropped` を
   `dbg_print` に出し、押下中と仮定していたキー状態を捨てる (必要なら `kbd_is_pressed`)。
   `window` の generation が自分の表と一致しないイベントは捨てる (U2。破棄後に同じ index で
   作られた窓へ古いイベントが届かない)。`Paint` は矩形ごとに 1 件来るので、`paint_damaged` は各矩形を基底
   クリップ (C1 の `set_base_clip`) にしてウィジェット木を描く。
3. **U3 ループ** (`app.rs`): `run(app: &mut impl App)`:
   ```
   loop { poll → handle (状態更新 + invalidate だけ) → paint_damaged → commit_all → wait(next_timer) }
   ```
   ハンドラ中の `wait` 呼び出しは `debug_assert`。`Quit` を受けたら速やかに戻る。
4. **ウィンドウ所有型** (`window.rs`): `create_window(spec)` 〜 `set_focus` (U1)。座標は
   持たず `Configure` で受けた `client_rect` からクライアント面のサーフェス (C1) を作り直す。
5. **ウィジェット木** (`widget.rs`、既存 libos32gui のウィジェット部を G API の上に書き直し):
   button / label / checkbox / textbox / listbox + `row` / `column`。プロパティ変更 →
   自分の矩形を `invalidate`。`Pointer` / `Button` / `Key` / `Text` から `Widget{kind}` を
   合成してハンドラ (`on_click(widget, fn)` 等) へ。固定配列 64 / リスト項目 128。
   テキストボックスは `Text` で挿入、`Key` で編集キー (BS / DEL / 矢印 / HOME) と Tab 移動。
   フォーカスが乗ったら `SET_TEXT_CURSOR` を送る (W2 の FEP が候補窓の位置に使う)。
6. **箱レイアウト** (`layout.rs`): `Fixed(px)` / `Flex(weight)` / `Absolute(rect)`、`min`、
   `padding`。`Configure` で再計算 (整数のみ)。400 / 480 行で同じ gui_demo が崩れないこと
   (H2 前は `screen_info` を偽装するテストで確認)。
7. **タイマ** (`timer.rs`): `set_timer(id, ticks, repeat)` / `kill_timer`、`Timer` イベント。
8. **gui_demo 書き換え**: 窓 2 枚 (Widgets / Help) は現行のまま、中身を U3 ループ +
   所有型 + `row`/`column` に。ESC で終了。**`gui_pump` / `gui_draw` の旧 API は消す**
   (アプリ 1 本の中で WM を動かす構成は終了)。
9. **`lease_test`** (`userland/rust/lease_test/`、W2 の検証用): フォーカス中に 14 色を
   リースして独自パレットの絵を描き、`Palette{active:false}` で代替色に描き直す。
10. **`gui_bench`** (`userland/rust/gui_bench/`、契約 P2 の測定器): `/api/key` からの入力を
   受け、その `serial` の**取り込み tick** (WM がスロット予備領域に記録) から `commit`
   完了までの差と `gfx_stats` の差分を代表操作 (1 文字入力 / ドラッグ / メニュー /
   リストスクロール) ごとに tvram に出す。WM とリングの待ち時間を含む「入力 → 表示」。NP21/W では転送量、
   実機では遅延を読む。

## 鉄則

- **プリミティブごとに syscall しない。** 1 周の `gui_call` は `POLL` + `COMMIT` + `WAIT`
  (+ 状態変更) だけ (契約 P)。描画はサーフェスへ直接 (C1)。
- ポインタを SHM に載せない。文字列は長さ前置で引数バッファへ。
- 入れ子ループ禁止。モーダル (W2) の完了は `Modal` イベントで受ける。
- 世代付きハンドルを守る: 破棄後の ID は `ERR_STALE` で返るので、所有型の `Drop` 後に
  使えない型にする。

## 完了条件 (ゲート G2 の C 側)

- gshell (W1) 上で `gui_demo` が窓 2 枚・ドラッグ・フォーカス・ボタン・チェックボックス・
  テキストボックス (ASCII)・リストボックス・Tab 移動を動かす。
- 1 周の `gui_call` 回数が 3 (+ 状態変更) であること (`gfx_stats` / WM 側の op カウンタ)。
- `screen_info` を 640×480 に偽装しても gui_demo のレイアウトが崩れない (row/column)。
- `gui_demo` を CTRL+STOP で kill → 窓が消える (owner 回収は W1)。

## PM への連絡

- `build/programs.mk` / `userland/deploy.yaml` に `lease_test`。
- 契約に無いウィジェット種別やイベント種別が要ると分かったら PM へ (末尾追記)。
