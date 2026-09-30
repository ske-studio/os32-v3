# 回転ブリット設計仕様 (gfx_blit_rotated)
Status: Done
Assignee: Antigravity

## 1. 概要

GFX_Surface を任意角度で回転（＋拡大縮小）してバックバッファに描画する機能。

### ユースケース

- ボスキャラの回転演出
- 回転する弾・エフェクト
- UIの回転アイコン・ゲージ
- 静止画の回転表示

---

## 2. 既存インフラの活用

| コンポーネント | 用途 |
|---------------|------|
| `gfx_isin(angle)` / `gfx_icos(angle)` | 512分割 sin/cos LUT (15bit精度, `gfx_math.c`) |
| `GFX_SIN_SCALE` (32767) | 固定小数点スケール |
| `GFX_Surface` | プレーナー形式ソースデータ |
| `gfx_fb.planes[4]` | BBプレーンポインタ |
| `gfx_add_dirty_rect()` | 変更領域の登録 |
| `gfx_pixel()` / `gfx_pixel_nodirty()` | ピクセル書き込み（C版フォールバック） |

### 重要な制約: プレーナー形式

BB・ソースともに **4プレーン × 1bit/pixel/plane** 形式。
1ピクセルの読み書きには4プレーンそれぞれへのアクセスが必要。

```
ソース GFX_Surface (32×32, pitch=4)
  plane[0]: [byte0][byte1][byte2][byte3] × 32行  ← B
  plane[1]: [byte0][byte1][byte2][byte3] × 32行  ← R
  plane[2]: [byte0][byte1][byte2][byte3] × 32行  ← G
  plane[3]: [byte0][byte1][byte2][byte3] × 32行  ← E
```

---

## 3. アルゴリズム

### 3-1. 逆アフィン変換方式

描画先のバウンディングボックス内の各ピクセルについて、
逆アフィン変換でソース座標を求め、テクセルを読み取る。

```
【セットアップ】
  cos_v = gfx_icos(angle)    // 15bit固定小数点
  sin_v = gfx_isin(angle)
  cx = src_w / 2              // 回転中心 (ソース中央)
  cy = src_h / 2

  // ピクセル増分 (8.8固定小数点に変換)
  du_x =  cos_v >> 7          // X方向1px進む時のu増分
  dv_x =  sin_v >> 7
  du_y = -sin_v >> 7          // Y方向1px進む時のu増分
  dv_y =  cos_v >> 7

【スキャンラインループ】
  FOR dy = 0 to bbox_h-1:
      u = u_row_start
      v = v_row_start

      FOR dx = 0 to bbox_w-1:
          sx = u >> 8
          sy = v >> 8

          IF 0 <= sx < src_w AND 0 <= sy < src_h:
              color = read_planar_pixel(src, sx, sy)
              IF color != colorkey:
                  write_planar_pixel(bb, dst_x+dx, dst_y+dy, color)

          u += du_x
          v += dv_x

      u_row_start += du_y
      v_row_start += dv_y
```

### 3-2. ソースピクセル読み取り（プレーナー）

```c
static u8 _read_pixel(const GFX_Surface *s, int x, int y)
{
    int off = y * s->pitch + (x >> 3);
    u8 bit = 0x80 >> (x & 7);
    u8 color = 0;
    int p;
    for (p = 0; p < 4; p++) {
        if (s->planes[p][off] & bit) color |= (1 << p);
    }
    return color;
}
```

### 3-3. バウンディングボックス計算

回転後の4頂点から最小外接矩形を求める:

```
32×32 スプライトの場合:
  0°:   32×32 (等倍)
  45°:  46×46 (≈ 32√2)
  最大:  46×46
```

---

## 4. 性能分析

### 4-1. ピクセル単価（386SX 16MHz）

処理はすべてメインメモリ（BB）上で行われるため、
**スプライトサイズによるピクセル単価の差はない。**
コストは純粋にピクセル数に比例する。

**C版（ピクセル単位処理）:**

| 処理 | cycles/pixel |
|------|-------------|
| u += du_x, v += dv_x (ADD×2) | 4 |
| sx = u >> 8, sy = v >> 8 (MOV+SAR×2) | 10 |
| 範囲チェック (CMP×4 + Jcc×2) | 16 |
| ソース4プレーン読み取り | 28 |
| 透明チェック | 5 |
| BB 4プレーン書き込み (RMW×4) | 40 |
| **合計（描画pixel）** | **~103** |
| **合計（スキップpixel）** | **~35** |

**NASM最適化版（目標）:**

| 最適化 | 削減効果 |
|--------|---------|
| レジスタにプレーンポインタをキャッシュ | -10 cyc/px |
| 4プレーンOR→透明判定を先行 | スキップ高速化 |
| ビット操作の最適化 | -15 cyc/px |
| **目標** | **~65 cyc/px (描画), ~20 cyc/px (スキップ)** |

### 4-2. サイズ別所要時間

描画率70%として:

| 方式 | cycles/pixel (加重平均) | 16×16 (256px) | 32×32 (1024px) |
|------|----------------------|---------------|----------------|
| C版 | ~83 | ~1.3ms | ~5.3ms |
| NASM版 | ~52 | ~0.8ms | ~3.3ms |

> ピクセル単価が一定なので、32×32 は 16×16 の正確に **4倍** の時間がかかる。

### 4-3. フレーム予算

60fps (16.6ms) からVRAM転送・ゲームロジック分を引いた残り **~12ms** を
回転描画に使えるとして:

| 方式 | 総ピクセル予算/frame | 32×32 換算 | 16×16 換算 |
|------|---------------------|-----------|-----------|
| C版 | ~144,000 px | ~3枚 | ~12枚 |
| NASM版 | ~230,000 px | ~5枚 | ~22枚 |

> ※ dirty rect で回転スプライト領域のみVRAM転送されるため、
> VRAM転送コストは回転の有無に依存しない。

---

## 5. API設計

```c
/* ===== 回転ブリット ===== */

/* Surface を回転してBBに描画 (colorkey=0 固定の透過合成) */
void gfx_blit_rotated(int dx, int dy,
                       const GFX_Surface *src,
                       int angle);       /* 0-511 (gfx_isin互換) */

/* Surface を回転+拡大縮小してBBに描画 (汎用版) */
void gfx_blit_affine(int dx, int dy,
                      const GFX_Surface *src,
                      int angle,         /* 0-511 */
                      int scale,         /* 8.8固定小数点, 256=等倍 */
                      u8 colorkey);
```

### 使用例

```c
GFX_Surface *ship = gfx_create_surface(32, 32);
/* ... shipにスプライトデータを描画 ... */

int angle = 0;
while (1) {
    gfx_blit_rotated(192, 192, ship, angle);
    gfx_present();
    angle = (angle + 2) & 511;  /* 回転アニメーション */
}
```

### パラメータ詳細

| パラメータ | 説明 |
|-----------|------|
| `dx, dy` | 描画先の中心座標（回転中心がこの位置に来る） |
| `src` | 回転元の GFX_Surface |
| `angle` | 角度 (0-511, 512分割。128=90°, 256=180°) |
| `scale` | 拡大率 (256=等倍, 128=半分, 512=2倍) |
| `colorkey` | 透過色 (通常0) |

---

## 6. ファイル構成

```
programs/libos32gfx/
├── draw/
│   ├── gfx_blit.c           /* 既存: 矩形ブリット */
│   ├── gfx_surface.c        /* 既存: Surface + blit_transparent */
│   ├── gfx_sprite.c         /* 既存: スプライト */
│   └── gfx_rotate.c         /* ★ 新規: 回転ブリット (C版) */
├── asm/
│   ├── asm_blit.asm          /* 既存: 透過ブリット NASM */
│   └── asm_rotate.asm        /* ★ 新規: 回転内部ループ NASM (Phase 2) */
├── geom/
│   └── gfx_math.c            /* 既存: sin/cos LUT (再利用) */
└── libos32gfx.h               /* API追加 */
```

---

## 7. 実装フェーズ

### Phase 1: C版基本実装

- [x] Phase 1: C言語による実装と最適化
  - `gfx_rotate.c` を作成
  - 8.8fp 固定小数点演算、逆アフィン変換によるピクセル走査
  - クロップ方式の採用
  - `rotate_test.c` による実機検証と計算式の修正

### Phase 2: 汎用版 + NASM最適化

- [ ] `gfx_blit_affine()`: 角度 + スケール + colorkey
- [ ] `asm/asm_rotate.asm`: 内部ループの NASM 最適化
  - 4プレーン読み取りの最適化
  - レジスタ割り当て最適化
- [ ] Phase 2: NASMによるループ切り出し (今後のパフォーマンス最適化課題) + NASM最適化

### Phase 3: 応用機能（検討）

- [ ] 回転済みスプライトキャッシュ（事前計算）
- [ ] libtilemap との連携（回転タイル）

---

## 8. 設計判断

### Q: なぜ独立ライブラリにしないのか？

回転ブリットは `gfx_blit` / `gfx_blit_transparent` と同じ抽象レベルの
描画プリミティブであり、同じデータ構造（GFX_Surface, gfx_fb）を操作する。
既存の sin/cos LUT (`gfx_math.c`) もそのまま再利用できるため、
libos32gfx 内の `draw/` サブディレクトリに配置するのが自然。

### Q: 中間チャンキーバッファ方式は？

プレーナー→チャンキー変換→回転→チャンキー→プレーナー変換
という多段処理も検討したが、変換オーバーヘッドが追加されるため、
プレーナーのまま直接回転する方がシンプルで効率的。

### Q: 角度は256分割ではなく512分割？

既存の `gfx_isin()` / `gfx_icos()` が 512 分割で実装済みのため、
そのまま活用する。256分割にすると別途LUTが必要になり無駄。

---

*Rotate Blit Design — 2026-04-23*
