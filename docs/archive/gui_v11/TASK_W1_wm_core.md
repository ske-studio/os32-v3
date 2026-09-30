# W1: gshell 本体 — ウィンドウマネージャの核

> 発行: PM (2026-09-05) / 状態: **受入完了 (2026-09-06)**
> (同日改訂: 契約 T8 / G4 / T3 / T9 を反映) / レーン: W (Rust, `userland/gshell/` 新規) / 前提: K1 完了 (共有ヘッダ、`gui_register`) + H1 完了 (`gfx_stats`) + K4-0 完了 (`sys_switch_shell`)
> 親: [TASKS.md](../../tasks/gui/TASKS.md) / 契約: T2, T2a, T3, T4, U1, U2, U5, U8, G4, G6 / 設計: [DESIGN.md](../../tasks/gui/DESIGN.md) §1 R1〜R4, §9.2
> 排他: `userland/gshell/**`。`build/programs.mk` / `userland/deploy.yaml` への追加は PM 経由

## ゴール

**シェル帯 (0x300000, CPL=0) に常駐する GUI シェル**を作る。アプリから見える WM の機能
(窓・Z 順・フォーカス・入力配送・損傷統合・commit・クローム) を全部ここに置く。
既存 `userland/rust/libos32gui` のウィンドウ管理部 (`Window` 配列、Z 順、フォーカス、
枠描画、`gui_pump`) を**移設**して核にする。契約の T / U をアプリの側から満たすのが W1、
アプリの側 (スタブとウィジェット) は C2。

gshell は shell.bin と同じシェル帯 (0x300000, Level 1) に載るので **`exec` では起動できない**。
CUI の `os32gui` が `sys_switch_shell` (K4-0) でカーネルに入れ替えさせる (契約 T9)。
「CUI へ」も同じ経路で shell.bin に戻す。`system.cfg` による起動時の選択は K4-1。

## 構成

```
userland/gshell/
  Cargo.toml        staticlib, no_std, panic=abort (libos32gui と同じ)
  src/main.rs       起動: gfx_init(enter) → パレット (G6) → gui_register → デスクトップ → ループ
  src/wm.rs         Window 表 (16)、Z 順、フォーカス、所有者、Configure
  src/slot.rs       SHM スロット 4 本の割当と回収 (T2a)
  src/ring.rs       イベントリング (128 × 16B)、合成 (Pointer 畳み、Paint 統合)、OVERFLOW
  src/damage.rs     損傷矩形 (8/窓、32px 境界、隣接結合 = gfx_add_dirty_rect と同じ規則)
  src/visible.rs    可視領域 (クライアント矩形 − 上のウィンドウ、互いに素な矩形 ≤ 16/窓)。
                    Z 順・移動・表示・破棄のたびに再計算し、露出分の Paint を出す (契約 G4)
  src/chrome.rs     枠・タイトルバー・閉じるボタン・XOR ドラッグ枠 (2 色版は W2)
  src/input.rs      kbd_trygetkey / kbd_get_modifiers / mouse_poll → Key / Text / Pointer / Button
  src/timer.rs      8 本/アプリ、tick 比較、OP_WAIT の期限計算
  src/handler.rs    gui_call ハンドラ: op → 関数表 (match の巨大化を避ける)、owner 検証
  src/pump.rs       K2 が呼ぶ gui_pump(): input.rs → ring.rs への追記だけ
  src/desktop.rs    背景、(v1.2 でタスクバー)
```

- 描画は既存 libos32gfx を FFI (v2 C8)。C1 の G API が使えるようになったら共用してよい
  (gshell は CPL=0 なので経路は直接呼び出し、契約 U8)。
- 既存 libos32gui から持ってくるのは `types.rs` の `Window` / `MouseInfo` と `lib.rs` の
  ウィンドウ管理・枠描画・イベント配送。**ウィジェットは持ってこない** (C2 の領分)。

## 作業 (順に)

1. **骨格**: `gshell` コマンドで起動、`gfx_init`、G6 のパレットを `gfx_set_palette` で入れる、
   `gui_register(handler, pump)`、デスクトップを塗って `OP_WAIT` 相当のループ
   (`sys_halt`) で待つ。ESC で終了 (`gfx_shutdown` → CUI シェルへ戻る)。
2. **スロットとハンドラ**: `OP_INIT` で owner にスロットを割り当て (v1 は常に 0)、
   `proto_version` を照合 (`ERR_VERSION`)、以後の op は owner ↔ スロットを検証。
   `gui_owner_exit(owner)` (K1-5) で全回収。
3. **ウィンドウ**: `CREATE` 〜 `SET_FOCUS` (契約 U1)。要求は SHM の要求ブロックから読む。
   結果は戻り値 (ハンドル) と `Configure` イベント。座標の正はここ (アプリは持たない)。
4. **イベントリングと入力** (契約 T3): `input.rs` が `Key` (スキャンコード + ch + mods) と
   `Text` (印字可能文字、FEP は W2) と `Pointer` / `Button` を作り、フォーカス窓の
   所有者のスロットへ追記 (`tail` だけ書く。`head` はアプリが進める)。`Pointer` は最新
   1 件に畳む。`serial` を振り、取り込み tick を serial ごとに直近 64 件、スロット予備領域に
   記録する (P2)。満杯なら `Pointer` を落とし、それでも無ければ**カーネルの待ち行列から
   読まない** (打鍵はそこに残る)。取り込みのたびに `kbd_dropped_count()` の差分を `dropped` に
   足して `OVERFLOW` (契約 T3。マウスの押下 + 解放が取り込みの間に完結した分は数えられない)。
   `Paint` / `Configure` / `Timer` は**導出型**: `OP_POLL` の中で損傷 ∩ 可視領域・最新矩形・
   期限切れタイマから作って**同じリングへ追記**し (入らない分は状態のまま持ち越し)、
   戻り値 = `tail - head`。`OVERFLOW` / `dropped` は `OP_POLL` で渡したら消す。
   イベントの `window` は完全な ID (index | generation) を載せる (U2)。破棄後の index は
   generation を進めて再利用してよい。
   `OP_WAIT(timeout)` は「未読なし かつ 配送できる導出型なし (dirty ∩ 可視領域が空、Configure
   通知済み、期限切れタイマなし) かつ 期限前」のときだけ `sys_halt`。完全に隠れた窓の dirty
   では起きない (G0b-3c)。
5. **損傷と commit** (契約 G4 の 3 状態): `INVALIDATE` は dirty に足すだけ。`OP_POLL` で
   dirty ∩ 可視領域のうちリングに入った矩形を **issued** へ移して `Paint` にする (入らない分と
   16 超は dirty のまま)。`COMMIT` は **issued だけ**を `gfx_present_rect` (H1 表経由) して
   空にし、その矩形にかかるカーソルを退避・再描画。dirty は転送しない。全画面 present は WM だけ (起動時、
   フルスクリーン GFX からの復帰)。`gfx_stats().commits` が 1 周 1 回であることをここで守る。
5b. **実行文脈** (契約 T8。票の「鉄則」と対): ハンドラ (X1) は状態更新とリング追記だけ。
   `OP_COMMIT` (X2) は present とカーソルだけ。**WM 自身の操作 (ドラッグ、閉じる、メニュー、
   フォーカス切替、リース切替) とクローム・デスクトップの present は `OP_WAIT` の中 (X3)
   でだけ行う**。gshell 単独時はこの周期を自分のループで回す。`pump` (X4、K2) は入力と
   カーソルスプライトだけ。
6. **クローム**: 枠・タイトルバー・閉じるボタン (色は G6 の役割名)。ドラッグ中は XOR 枠だけ、
   ドロップで 1 回 `Configure` + `Paint` (R2)。閉じるボタンは `Close` イベント (破棄は
   アプリ)。9801 バックエンド (`TEXT_OVERLAY` あり) ではタイトル文字を TVRAM で描いてよい
   (R4、任意)。
7. **マウスカーソル**: 既存スプライト機構 (libos32gfx) で。移動は損傷に含めない
   (カーソルだけ別経路で復元・描画)。
8. **タイマ**: `SET` / `KILL`、`Timer` イベント。`OP_WAIT` の期限は次のタイマまで。

## 鉄則

- **アプリへコールバックしない** (T1)。すべてイベント。
- **描く場所と待つ場所は契約 T8 の 4 文脈に限る。** X1 (WAIT / COMMIT 以外のハンドラ) では
  描かない・待たない、数十 µs で戻る。描画と待機は X2 (COMMIT: present のみ) と X3 (WAIT:
  WM の全周期) で。
- `pump` (X4、K2 が syscall 境界で呼ぶ) は `input.rs → ring.rs` の追記とカーソルスプライトの
  移動だけ。KAPI を呼ばない、他の描画をしない、状態機械を進めない (K2 票の契約)。
- 640×400 / 16 色を書かない。`gfx_screen_info()` を起動時に 1 回読む。
- `get_tick` スピン禁止。待ちは `sys_halt` のみ。
- Rust: no_std、外部クレート無し、固定配列。

## 完了条件 (ゲート G2 の W 側)

- CUI で `os32gui` → デスクトップが出る → 「CUI へ」で shell.bin に戻る、を 10 往復して
  `kselftest` / rshell が生きている (契約 T9、G0b-5)。
- 重なった 2 窓 (G0b-2): 背面の全面 `invalidate` で前面が壊れない、前面を閉じると露出分だけ
  `Paint` が出る (screenshot と `gfx_stats` の present バイト数で確認)。
- リング満杯 (G0b-3): `gui_busy` (K2) で印字可能キー 60 連打 → 120 件が全部後から届く。
  200 連打 → `OVERFLOW` と `dropped ≥ 1`、先頭分は届き、`head` が進むと取り込み再開。
- generation (G0b-3d): 一括取得 → A を破棄 → 途中で再 `OP_POLL` → 同じ index で B を作成 →
  A 宛の残りは generation 不一致でクライアントが捨て、B には届かない。
- 隠れた窓 (G0b-3c): 完全に覆われた A の `invalidate` で A の `OP_WAIT` が空回りしない
  (`gfx_stats().commits` が増えない)。
- C2 の `gui_demo` (CPL=3、別プロセス) が窓 2 枚を出し、XOR ドラッグ・前面化・フォーカス・
  閉じるが動く。`gui_demo` を CTRL+STOP で kill しても窓が全部消える (owner 回収)。
- `gfx_stats()` で 1 周あたり `commits = 1`、`gui_call` の回数が `POLL + COMMIT + WAIT` の
  3 回 (+ 状態変更分) であること (P)。
- マウス移動 100 回に対しリングの `Pointer` が畳まれて数件になる。

## PM への連絡

- `build/programs.mk` に gshell のエントリ (shell と同じ 0x300000 リンク、`DEFINE_RUST_PROGRAM`
  の常駐版が要る)、`userland/deploy.yaml` に `/bin/gshell.bin`。
- 契約に無い op が要ると分かったら**実装せず** PM へ (末尾追記で入れる)。
