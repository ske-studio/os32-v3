# drivers/video/pc98/backend_pegc.c — PEGC 画面バックエンド

> 状態: **現行 (2026-10-31)**

PEGC のモード設定、画面と表示を HAL に提供する。

## 落とし穴

- **実機 ROM とモード列**: BIOS の残り状態やエミュレータの値に頼ると、実機で 640×480 が崩れうる。
  `drivers/video/pc98/backend_pegc.c:320` の GDC とクロックの列を実機 ROM の OUT 列と比較する。
  `tools/tests/pegc_mode_host.c:381` の除外表には A0h DFh / A2h 28h の未解明の差がある。`make check-pegc-mode-host` と実機表示の両方で確かめる。
- **窓の RAM 判定**: 高位 RAM があっても低位に穴があるため、RAM 上端で窓を拒否しない。
  `drivers/video/pc98/backend_pegc.c:568` は対象範囲を `pgalloc_range_has_ram` に問い合わせる。`tools/tests/test_pgalloc_range.py` が範囲判定を検査する。

## 設計の補足

| バックエンド | 画素形式 | VRAM | NP21/W で検証 |
|---|---|---|---|
| 9821 PEGC (256 色, 640x480) | 8bpp パックド。1 ラインは 640 バイトで 16 色プレーンの約 2 倍 | 512KB。0xF00000 リニア窓 (E0102h=1 で出現) または A8000h/B0000h の 32KB バンク窓 ×2 | 可 (`SUPPORT_PEGC`) |

> [要確認] F00000h 限定の記述と現行の高位窓対応を照合する。

### 3.1 PEGC の資料上の仕様 (PC-9801 Bible §3-2、`C:\WATCOM\docs\PC9800Bible\3-2_グラフィック256色表示.md`)

- モード切替は 6Ah のモード F/F2: `07h` (拡張モード変更可) → `21h` (拡張グラフィックモード) /
  `20h` (標準へ戻す)。現在値は 09A0h に `0Ah` を書いてから読む (bit0 = 1 で拡張モード)。
- 同期周波数: 09A8h に `1` で 31KHz (640x480)、`0` で 24KHz。
- VRAM 形式: メモリマップト I/O E0100h に `00h` = パックドピクセル、`01h` = プレーン。
  E0004h / E0006h に 32KB 単位のバンク番号 (0〜0Fh) を書くと A8000h / B0000h の窓に現れる。
  E0102h に `0001h` で VRAM 全体 (512KB) が F00000h に出現する。
- アドレス計算: `ADR = y * 640 + x`。
- パレット: A8h に番号 (0〜255)、AAh / ACh / AEh に G / R / B を 0〜255 で書く
  (16 色時と同じポート、輝度が 8bit に拡張)。読み戻しも可。
- 拡張 BIOS INT 18h: AH=30h (画面モード設定: 640x480 + テキスト 30 行)、AH=31h (取得)、
  AH=4Dh (標準/拡張モード切替)。OS32 は BIOS を使わないので I/O 直接で行う。

  `pegc_query` は `GFX_CAP_TEXT_OVERLAY` を返す (`drivers/video/pc98/backend_pegc.c`)。

| 観点 | PEGC 256 色 |
|---|---|
| present 1 ピクセルあたりの VRAM 書き込み | **1 バイト** |

PEGC:

- 1 ピクセル 1 バイトなので、同じダーティ面積で VRAM 書き込みが 9801 の 2 倍
  (全画面 640x480 で 307KB)。R1〜R3 はそのまま通用するが、**ダーティ面積の予算は半分**。
- 32KB バンク窓 (A8000h) を使うと 51 ライン毎にバンク切替の MMIO 書き込みが要り、矩形が
  バンク境界で割れる。**F00000h のリニア窓を使う** (ページングで張る)。
- パレット 256 色の初期化は OUT 1024 本。起動時 1 回に限り、毎フレームは不可。
- CPU 側は楽になる: 8bpp はビット操作が無く、バックバッファ合成と文字描画はプレーンより軽い。

```c
static const GfxMode pegc_modes[] = {
    {PEGC_WIDTH, PEGC_HEIGHT_480, GFX_FMT_PACKED8, 8, 0, 0, PEGC_PITCH}
};
```

```c
/* 既存初期化子末尾: .modes=pegc_modes, .mode_count=ARRAY_COUNT(pegc_modes),
                     .default_mode=0, .selected_mode=0 */
```

- init 時に mode->height を現在高さへ反映し、query の高さは必ず gfx_current_height を読む。
