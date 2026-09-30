# C1: G 描画 (GDI 相当) を libos32gfx の上に

> 発行: PM (2026-09-05) / 状態: **受入完了 (2026-09-06)**
> レーン: C (Rust, `userland/rust/libos32gui/`) / 前提: なし (今すぐ着手可)
> 親: [TASKS.md](../../tasks/gui/TASKS.md) / 契約: G1〜G7 / 設計: [DESIGN.md](../../tasks/gui/DESIGN.md) §2, §8, §9.4
> 排他: `userland/rust/libos32gui/**`、`userland/rust/gui_demo/**`、`sdk/rust/os32api/src/gui/**` (新規)

## ゴール

契約 G の型と関数を Rust で起こす。**ステートレス** (毎回 `Style`)、**ポインタ無し**
(ID 参照)、**クリップは自分で守る** (v1 の信頼モデル)。下は既存 libos32gfx を FFI
(v2 C8)。WM や `gui_call` が無くても単体で動く (フルスクリーンのテストプログラム
`gdi_test` で検証) ので、他レーンと並列に始められる。

既存 libos32gui のウィジェット描画コード (枠、ボタン面、テキストボックス) はこの G API の
上に**書き直す**。それが C2 の前提。

## 作業

1. **`sdk/rust/os32api/src/gui/`**: `types.rs` (Rect / Color / Style / SurfaceId / BitmapId /
   FontId、`#[repr(C)]`)、`proto.rs` (K1 の `os32_gui_shared.h` を写す: op 番号、イベント種別、
   SHM オフセット、エラー番号、`GUI_COLOR_*`、`Style.flags`)。K1 が終わるまでは TASKS.md §4 の
   提案値で書き、K1 確定後に合わせる。PM の `check_gui_proto.py` が突き合わせる。
2. **描画** (`libos32gui/src/draw.rs`): `fill_rect` / `draw_rect` / `hline` / `vline` / `line` /
   `blit` / `text` / `measure_text` / `push_clip` / `pop_clip` (深さ 8)。
   - `surface` が全画面バックバッファの部分矩形 (窓のクライアント面) のときは、
     クリップ = 「現在のクリップ ∩ サーフェス矩形」を**必ず**掛けてから libos32gfx を呼ぶ。
   - **基底クリップは処理中の `Paint` 矩形** (契約 G2 改訂): C2 のループが `Paint{rect}` を
     処理する間、C1 の `set_base_clip(surface, rect)` で基底を固定し、`push_clip` はその
     内側にしか効かない。`Paint` の外 (基底未設定) では窓面への描画を拒む (`debug_assert` +
     no-op)。WM が `Paint` を可視領域の内側にしか出さないので (G4)、これで重なった前面を
     背面が上書きしない。
   - `Style.flags`: `TRANSPARENT_BG` (文字だけ)、`XOR` (枠ドラッグ、`gfx` の XOR 描画が無ければ
     C1 で足す: 読み戻しは**バックバッファ**に対して行う。VRAM は読まない)、`DOTTED`、`DITHER50`
     (市松は 2 色のパターン塗り)。
   - `text` は UTF-8 → KCG (`kcg_draw_utf8` 系)。半角 8px / 全角 16px、ベースライン規約は
     gen_font16 のもの。切り詰めは UTF-8 境界 (CLAUDE.md)。`utf8_set_jis_table_ready` は
     既知対応を数点検証してから立てる (CLAUDE.md の漢字の罠)。
3. **サーフェス** (`surface.rs`): `create_surface(w, h)` / `destroy_surface` (上限 16、世代付き
   ID)。実体は既存 `GFX_Surface` (主記憶)。窓のクライアント面は W1 が `Configure` で渡す
   矩形から C2 が作る (C1 は「矩形 + バックバッファ記述子」からサーフェスを作る口を用意)。
4. **能力とカウンタ**: `screen_info()` (KAPI v40) と `stats()` (K1/H1 の `gfx_stats`)。
   8bpp (`GFX_FMT_PACKED8`) のときの塗り・文字は **C1 の CPU 実装で分岐** (プレーンは
   libos32gfx、8bpp は `bb_base + y*pitch + x` への直接書き)。v1.1 前半は PLANAR4 のみ
   動けばよいが、分岐の口は最初から切る (決め打ち禁止)。
5. **`gdi_test`** (`userland/rust/gdi_test/`、C1 が置く): G6 の 16 色見本帯 (役割名付き)、
   `Style.flags` 4 種の見本、文字 (ANK / 漢字 / 混在)、クリップの入れ子、`stats()` の表示。
   参照画像 `docs/tasks/gui/ref/g1.png` を PM が最初の合格時に固定する。

## 鉄則

- 外部クレート無し、no_std、固定配列 (v2 C8)。
- **ポインタを API に出さない。** `&[u8]` は Rust 内だけ、C ABI 側は `(ptr, len)` を
  受けても SHM / イベントには載せない。
- 曲線・浮動小数は契約外 (libos32gfx を直接使う道を塞がない)。
- 640×400 / 16 色 / プレーンを書かない。`screen_info()` を信じる。

## 完了条件 (ゲート G1)

- `gdi_test` の screenshot が参照画像と一致 (初回は目視で PM が合格を出す)。
- クリップ違反テスト: サーフェス外への `fill_rect` / `text` が**1 ピクセルも**外へ出ない
  (画面端 4 辺で確認)。
- `stats().present_bytes` が全画面 1 回で 128000 (H1)。
- 既存 `gui_demo` は C1 の時点では**触らない** (C2 で書き換える)。ビルドが通ること。

## PM への連絡

- `build/programs.mk` に `gdi_test` (`DEFINE_RUST_PROGRAM`)、`userland/deploy.yaml` に登録。
- `check_gui_proto.py` の対象ファイル名 (`proto.rs`) を確定したら PM へ。
