# userland/rust/libos32gui/src/ — 設計
> 状態: **現行 (2026-10-31)** — 出所: API_CONTRACTS・DESIGN (D1 で移した)
ソース: `userland/rust/libos32gui/src/`

- **ステートレス**: 描画呼び出しは毎回 `Style` を受け取る。DC 相当の「選択状態」は持たない。
- **ポインタは API に現れない**。バッファは ID で参照する。
- 浮動小数なし。曲線は v1 の契約に含めない (libos32gfx を直接使う道は残す)。

- ソフトウェアバックエンドではクライアントが共有バックバッファへ直接書く。
  **クリップの強制はクライアントライブラリの責任** (v1 の信頼モデル、DESIGN §8 の表)。

C resident CRT はこの欄を除外し、gshell は retry を有効化しない。

### clip

| 関数 | 内容 |
|---|---|
| `push_clip(rect) / pop_clip()` | クリップは常に「現在のクリップ ∩ サーフェス」。深さ 8 |

- **クリップの基底は「処理中の `Paint` の矩形」** (2026-09-05): ライブラリは `Paint{rect}` を
  処理する間、基底クリップを `rect` に固定し、`push_clip` はその内側にしか広げられない。
  `Paint` の外で描く手段は無い (プロパティ変更は `invalidate` → 次の `Paint`)。これで重なった
  前面ウィンドウを背面が上書きすることを防ぐ (G4 の可視領域と対)。

- `Paint{window, rect}` を受けたときだけクライアント面に描く (`rect` にクリップ済み)。

### surface

#### G3. サーフェス

- `SurfaceId` は「描画先」の抽象。v1 の実体は (a) ウィンドウのクライアント面 = 全画面
  バックバッファの部分矩形、(b) オフスクリーン = 主記憶 (既存 `GFX_Surface`)。

- `create_surface(w, h) -> SurfaceId` / `destroy_surface(id)`。上限 16。

**全画面 1 枚のバックバッファ + クリップ**。per-window バッファは持たない

シングルタスク前提なら不要。0x6A000 の 128KB をそのまま使う。

> [要確認] 0x6A000 固定の記述に対し、現行の `userland/lib/gfx/libos32gfx_core.c:103` 以降は CLIENT 面を query / lease し返された番地を使う。共有 BB・per-window の前提は要照合。

| 採用 | 解く欠点 | 386 でのコスト |
|---|---|---|
| **クライアントが共有バックバッファへ直接描く** | プリミティブごとの syscall | クリップは共有ライブラリの善意 (v1)。API は「サーフェス」抽象で切り |

### client

- **`OP_POLL` の手順**: (1) WM が導出型を追記 → (2) 戻り値 = `tail - head` → (3) アプリは
  `[head, tail)` を自分の配列 (128 件) へ写し、**`head = tail` に進める** (消費確認はこれだけ。
  WM は `head` を読んで空きを判断する)。

> [要確認] 指定範囲は測定器の集計の説明で、`client.rs:395` の `trace_tick(serial)` が serial/tick 表を読む機構の句はない。

- [取り込み tick の測定器 (gui_bench)](../../gui_bench/src/README.md#lib)

### timer

- [クライアント側の所有型 (window.rs)](window.md#所有型)

`set_timer(id: u8, interval_ticks: u16, repeat: bool)` / `kill_timer(id)`。

- [タイマの期限管理 (gshell timer.rs)](../../../../../../userland/gshell/src/timer.rs)

### modal

`open_modal(spec) -> DialogId`

### textcore

- 桁の数え方と切れ目は `libos32gui::textcore` の 1 本に寄せ

#### U9. テキスト

UTF-8 入力、KCG の 8x16 / 16x16 セル、幅は半角セル数 × 8。プロポーショナルは v1 に無い。
切り詰めは UTF-8 の境界で行う (CLAUDE.md の注意事項)。

> [要確認] 「KCG の 8x16 / 16x16 セル」に対し、現行 `draw.rs:727` は 字形 engine で描き、欠字・失敗時に KCG へ戻る。

### layout

#### U7. レイアウト (箱 1 種類)

- `row` / `column` コンテナ。子は `Fixed(px)` か `Flex(weight)`、`min` と `padding` を持つ。
- `Configure` を受けたときに再計算 (整数のみ)。400 / 480 / それ以上で同じアプリが崩れない。
- 絶対座標配置も残す (`Absolute(rect)`)。

| 採用 | 解く欠点 | 386 でのコスト |
|---|---|---|
| **箱レイアウト** (行・列、固定/伸縮) | 固定座標 | 整数演算のみ |

### uistate

shlib は `trim_events / trim_done_sent`

- [TRIM の実行と hook カウンタ (app.rs)](app.md#trim-と-allocator-の再試行)

リスト項目プール 128 (既存値)。

## 索引

- [draw.rs](draw.md)
- [window.rs](window.md)
- [app.rs](app.md)
- [shlib.rs](shlib.md)
- [widget.rs](widget.md)
