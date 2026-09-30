# H1: HAL バックエンド表と 9801 プレーン実装

> 発行: PM (2026-09-05) / 状態: **受入完了 (2026-09-06)**
> レーン: H (C, カーネル `gfx/`) / 前提: なし (今すぐ着手可)
> 親: [TASKS.md](../../tasks/gui/TASKS.md) / 設計: [DESIGN.md](../../tasks/gui/DESIGN.md) §2 B1〜B3, §6, §7, §8 / 契約: G5, G7, G8
> 排他: `gfx/**`、`include/gfx_hal.h` (新規)

## ゴール

現行の `gfx/` (主記憶バックバッファ 0x6A000 + `gfx_vram.c` のプレーン転送) を、
**バックエンド関数表の 9801 実装**に組み替える。GUI (W / C レーン) はこの表と
`gfx_screen_info()` だけを見て動き、機種を知らない。H2 (PEGC) と H3 (Cirrus) は
この表にもう 1 枚ずつ実装を足すだけで済む形にする。

**外から見た KAPI の挙動は変えない** (既存 GFX プログラムの回帰ゼロが完了条件)。

## 作業

1. **`include/gfx_hal.h`**: バックエンド表の型を切る (DESIGN §7-1 の契約どおり)。
   ```
   typedef struct GfxBackend {
       const char *name;
       int  (*probe)(void);                 /* 機種検出。1 = 使える */
       int  (*init)(GFX_ScreenInfo *info);  /* 能力ビットと画面情報を埋める */
       void (*shutdown)(void);
       void (*present_rect)(int x, int y, int w, int h);
       void (*set_palette)(int first, int count, const u8 *rgb);
       void (*enter)(void); void (*leave)(void);   /* 表示出力の切替 (フルスクリーン GFX 前後) */
       /* 描画プリミティブ。NULL = CPU 共通実装へフォールバック */
       int  (*fill_rect)(int x, int y, int w, int h, u8 color);
       int  (*blit)(int dx, int dy, int sx, int sy, int w, int h);
       /* バックバッファ記述子 (ソフトウェア系のみ) */
       u8  *bb_base; u32 bb_pitch; u8 bb_format;
   } GfxBackend;
   ```
   文字とビットマップのプリミティブは v1 では CPU 共通実装 (libos32gfx 側) に任せ、
   表には塗りと矩形転送だけを置く (契約 G2 の対応表)。
2. **9801 バックエンド** (`gfx/backend_pc98.c`、実体は現行 `gfx_vram.c` の移設):
   `probe` は常に 1、`init` は 640×400 (または 200 行モード) / bpp 4 / `GFX_FMT_PLANAR4` /
   `GFX_CAP_TEXT_OVERLAY | PAGE_FLIP` / `lease_mask = 0xFF7E` (index 1-6・8-15 の 14 色, 0 と 7 のみ不可侵)。`fill_rect` / `blit` は
   NULL (CPU 実装)。`enter` / `leave` は空。
3. **`gfx_core.c` を表経由に**: `gfx_present*` / `gfx_set_palette*` / `gfx_screen_info` /
   `gfx_hw_*` が `g_backend->...` を呼ぶ。`gfx_init` で `probe` 順に最初の 1 枚を選ぶ
   (v1 は 9801 のみ)。
4. **カウンタ** (契約 G7、DESIGN §8): `present_bytes` / `hw_ops` / `io_accesses` /
   `commits` を `present_rect` / `fill_rect` / `blit` / パレット設定の中で加算。
   `gfx_stats(void *out)` で `GFX_Stats` 構造体 (u32 × 4 + 予備 4) を返す。
   構造体は `os32_kapi_shared.h` に置くので **K1 へ依頼** (§「K への依頼」)。
5. **パレット リース** (契約 G8): `gfx_lease_palette(first, count, rgb)` は `lease_mask` /
   `lease_first` / `lease_count` の範囲外を `OS32_ERR_INVAL` で弾き、範囲内だけ
   `set_palette` へ通す。システム色へ戻すのは WM が同じ関数で行う (H1 は状態を持たない)。
6. **`enter` / `leave` の呼び口**: `gfx_init` を `enter`、`gfx_shutdown` を `leave` に
   対応させる。WM がフルスクリーン GFX プログラムを起動する前後はこの 2 つで済む
   (DESIGN §5 の「enter/leave フック」はこれで解消)。

## 鉄則

- [HW1] は 9801 バックエンドに適用。表の `fill_rect` / `blit` は「アクセラレータが
  あれば」の枠で、9801 実装では NULL のまま。
- 決め打ちを `gfx_core.c` に残さない (`GFX_WIDTH` 等の定数は 9801 バックエンドの中へ)。
  W / C レーンが `gfx_screen_info()` を信じられるのは H1 が正直に申告するから (DESIGN §7-2)。
- VRAM を読まない (DESIGN §8)。カウンタは書き込みバイト数だけ数える。
- 生成物 (`kapi_generated.*`) と `kapi.json` は触らない。KAPI が要るときは K1 へ。

## K への依頼 (K1 の票に載せてある)

- `GFX_Stats` 型と `gfx_stats(void *out)`、`gfx_lease_palette(int first, int count, const u8 *rgb)`
  の KAPI 追加 (v41)。H1 は関数本体を `gfx_core.c` に用意し、K1 が `kapi.json` に載せる。
  順序は H1 の実装 → K1 の追記 → `make clean && make all`。

## 完了条件 (ゲート G1 の H 側)

- `gfx_screen_info()` の内容が現行と同じ (hal_test で確認: 640×400、bpp 4、PLANAR4、
  TEXT_OVERLAY|PAGE_FLIP、lease_mask 0xFF7E)。
- `gfx_stats()` が `present_bytes` を返し、`gfx_present()` 1 回で 128000 (640×400×4/8)、
  `gfx_present_rect(0,0,32,16)` で 256 増える。
- 既存 GFX プログラムに回帰なし: `gfx_demo`、`blit_test`、`blit_test2`、`gfx200_test`、
  `hello_gfx` (Rust)、`gui_demo` の screenshot が H1 前と一致 (検証層が比較)。
- `make check` (check_privileged / check_constraints) を通る。kselftest 42/0。

## 検証手順 (検証層向け、`os32-cycle`)

```
os32-cycle deploy                       # NHD 配備 (NP21/W 停止中に)
os32-cycle run hal_test                 # screen_info と NOSYS 確認
os32-cycle demo gfx_demo                # screenshot を H1 前の参照画像と比較
os32-cycle run gdi_test                 # (C1 完了後) stats の表示を tvram で読む
```
