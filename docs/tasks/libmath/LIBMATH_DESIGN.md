# libos32math — 整数数学ライブラリ設計書

*策定: 2026-04-27*

> この文書は、FPUを持たないPC-9801環境において浮動小数点演算を代替する
> 整数数学ライブラリ `libos32math` の設計思想・API仕様・実装計画を定義する。
> OS32の外部プログラムライブラリ群の**最も基底に位置するライブラリ**として、
> libos32gfx, libpyxel, libtilemap, libos32snd, およびゲーム本体が共通して利用する。

---

## 1. 設計背景

### 1.1 なぜ libos32math が必要か

OS32はi386プロテクトモードで動作するが、FPU (i387) の存在を前提としない。
現在、三角関数LUT (`sin_table[512]`) は `libos32gfx/geom/gfx_math.c` に閉じ込められており、
以下の問題がある:

1. **libos32gfx 以外から使えない** — libpyxel, libtilemap, libos32snd, ゲーム本体の
   ロジック層が三角関数を使いたい場合、libos32gfx への不要な依存が発生する
2. **数学関数の散在** — sqrt, atan2, 乱数等の需要があっても追加先が不明確
3. **ライブラリ間の依存関係が不透明** — 各libが独自に数値計算を実装するリスク

### 1.2 FPUなし環境の数値演算戦略

libc (`<math.h>`) の関数群は内部的に FPU 命令 (`fsin`, `fsqrt` 等) を使用するため、
FPUなし環境では以下の代替手法が必要:

| 手法 | 適用範囲 | 精度 | 速度 |
|------|---------|------|------|
| **固定小数点演算** | 四則演算全般 | 任意 (Q16.16等) | ◎ 最速 |
| **LUT (ルックアップテーブル)** | 三角関数, sqrt, log | テーブルサイズ依存 | ◎ テーブル参照のみ |
| **CORDIC アルゴリズム** | atan2, 回転 | 反復回数依存 | ○ シフト+加算のみ |
| **ニュートン法** | sqrt, 逆数 | 反復回数依存 | ○ 乗除算あり |
| **ソフトウェア浮動小数点** | 汎用 (SQLite等) | IEEE 754準拠 | △ 非常に遅い |

libos32math は上記のうち**固定小数点 + LUT + CORDIC + ニュートン法**を組み合わせ、
ゲーム開発に実用的な速度と精度を両立する。

### 1.3 SQLiteとの関係

OS32カーネルにはSQLiteが統合されており、LUTのマスターデータをDBで管理する
ハイブリッド方式も将来的に検討可能:

```
[ホスト側 Python] 高精度計算 → fep.db / math.db に格納
        ↓ deploy
[OS32 起動時] SQLite から LUT を RAM にロード
        ↓
[実行時] 固定小数点 + メモリ内 LUT で高速演算 (DBアクセスなし)
```

ただし初期実装では、LUTをコンパイル時のconst配列として組み込み、
DB連携は将来の拡張オプションとする。

---

## 2. アーキテクチャ

### 2.1 ライブラリ依存関係

```
libos32math  (依存なし — 最も基底のライブラリ)
     ↑
     ├── libos32gfx   (math + KAPI)
     ├── libos32snd   (math + KAPI)
     ├── libtilemap   (math + gfx)
     ├── libpyxel     (math + gfx)
     └── ゲーム本体    (math + 任意のlib)
```

**重要な制約**: libos32math は KernelAPI (`os32api.h`) に依存しない。
純粋なC89の整数演算のみで構成し、`#include` するだけで使える
ヘッダ+ソースの集合体とする。

### 2.2 ディレクトリ構成

```
programs/libos32math/
    libos32math.h          公開ヘッダ (全API宣言 + 型定義 + マクロ)
    fix16.c                Q16.16 固定小数点四則演算
    trig.c                 sin/cos/tan LUT (gfx_math.c から移設 + 拡張)
    sqrt.c                 整数平方根 (ニュートン法)
    atan2.c                整数 atan2 (CORDIC方式)
    recip.c                逆数LUT (高速除算)
    random.c               xorshift32 擬似乱数
    vec2.c                 2Dベクトル演算
    lerp.c                 線形補間 + イージング関数群
```

### 2.3 gfx_math.c との移行方針

`libos32gfx/geom/gfx_math.c` の sin_table と関数を libos32math に移設し、
gfx_math.c は薄いラッパーとして互換性を維持する:

```c
/* gfx_math.c — 移行後 (互換ラッパー) */
#include "libos32math.h"

i32 gfx_isin(int angle)    { return isin(angle); }
i32 gfx_icos(int angle)    { return icos(angle); }
int gfx_deg_to_idx(int d)  { return deg_to_idx(d); }
```

これにより、既存の libos32gfx コードは変更不要。

---

## 3. API仕様

### 3.1 型定義と定数

```c
/* Q16.16 固定小数点型 */
typedef int32_t  fix16_t;

/* 定数 */
#define FIX16_ONE       65536       /* 1.0 */
#define FIX16_HALF      32768       /* 0.5 */
#define FIX16_PI        205887      /* π ≒ 3.14159 */
#define FIX16_2PI       411775      /* 2π */
#define FIX16_HALF_PI   102944      /* π/2 */

/* 変換マクロ */
#define FIX16_FROM_INT(x)   ((fix16_t)(x) << 16)
#define FIX16_TO_INT(x)     ((x) >> 16)
#define FIX16_FRAC(x)       ((x) & 0xFFFF)

/* sin/cos LUT のスケール */
#define ISIN_SCALE      32767       /* sin/cos の 1.0 に相当する値 */
#define ISIN_ENTRIES    512         /* テーブルエントリ数 (360° = 512) */
```

### 3.2 固定小数点演算 (fix16.c)

| 関数 | シグネチャ | 説明 |
|------|-----------|------|
| `fix16_mul` | `fix16_t fix16_mul(fix16_t a, fix16_t b)` | Q16.16 乗算 |
| `fix16_div` | `fix16_t fix16_div(fix16_t a, fix16_t b)` | Q16.16 除算 |
| `fix16_from_frac` | `fix16_t fix16_from_frac(int num, int den)` | 分数→固定小数点 |
| `fix16_abs` | `fix16_t fix16_abs(fix16_t x)` | 絶対値 |
| `fix16_ceil` | `fix16_t fix16_ceil(fix16_t x)` | 切り上げ |
| `fix16_floor` | `fix16_t fix16_floor(fix16_t x)` | 切り捨て |
| `fix16_round` | `fix16_t fix16_round(fix16_t x)` | 四捨五入 |
| `fix16_min` | `fix16_t fix16_min(fix16_t a, fix16_t b)` | 最小値 |
| `fix16_max` | `fix16_t fix16_max(fix16_t a, fix16_t b)` | 最大値 |
| `fix16_clamp` | `fix16_t fix16_clamp(fix16_t x, fix16_t lo, fix16_t hi)` | 範囲クランプ |

### 3.3 三角関数 (trig.c) — gfx_math.c から移設

| 関数 | シグネチャ | 説明 |
|------|-----------|------|
| `isin` | `i32 isin(int angle)` | 整数sin (512分割, 値域 -32767〜+32767) |
| `icos` | `i32 icos(int angle)` | 整数cos (同上) |
| `deg_to_idx` | `int deg_to_idx(int deg)` | 度数→LUTインデックス変換 |
| `rad256_to_idx` | `int rad256_to_idx(int rad256)` | 256分割角度→512分割変換 |

**LUTサイズ**: sin_table[512] × sizeof(i16) = 1,024バイト (既存と同一)

### 3.4 平方根 (sqrt.c)

| 関数 | シグネチャ | 説明 |
|------|-----------|------|
| `isqrt` | `u32 isqrt(u32 n)` | 整数平方根 (ニュートン法) |
| `fix16_sqrt` | `fix16_t fix16_sqrt(fix16_t x)` | Q16.16 平方根 |
| `fast_distance` | `u32 fast_distance(int dx, int dy)` | 2点間距離の高速近似 |
| `fast_distance_sq` | `u32 fast_distance_sq(int dx, int dy)` | 距離の二乗 (sqrtなし比較用) |

### 3.5 atan2 (atan2.c)

| 関数 | シグネチャ | 説明 |
|------|-----------|------|
| `iatan2` | `int iatan2(i32 y, i32 x)` | 整数atan2 (戻り値: 0〜511, CORDIC方式) |
| `angle_between` | `int angle_between(int x0, int y0, int x1, int y1)` | 2点間角度 |

### 3.6 逆数テーブル (recip.c)

| 関数 | シグネチャ | 説明 |
|------|-----------|------|
| `fast_div` | `fix16_t fast_div(fix16_t a, int b)` | 逆数LUTによる高速除算 (b=1〜256) |

**LUTサイズ**: recip_table[257] × sizeof(fix16_t) = 1,028バイト

### 3.7 擬似乱数 (random.c)

| 関数 | シグネチャ | 説明 |
|------|-----------|------|
| `rng_seed` | `void rng_seed(u32 seed)` | シード設定 |
| `rng_next` | `u32 rng_next(void)` | 次の乱数値 (0〜0xFFFFFFFF) |
| `rng_range` | `int rng_range(int min, int max)` | 範囲指定乱数 [min, max] |
| `rng_fix16` | `fix16_t rng_fix16(void)` | 0.0〜1.0 のQ16.16乱数 |

**アルゴリズム**: xorshift32 (周期 2^32-1, 高速で十分な品質)

### 3.8 2Dベクトル (vec2.c)

```c
typedef struct {
    fix16_t x, y;
} Vec2;
```

| 関数 | シグネチャ | 説明 |
|------|-----------|------|
| `vec2_make` | `Vec2 vec2_make(fix16_t x, fix16_t y)` | 生成 |
| `vec2_add` | `Vec2 vec2_add(Vec2 a, Vec2 b)` | 加算 |
| `vec2_sub` | `Vec2 vec2_sub(Vec2 a, Vec2 b)` | 減算 |
| `vec2_scale` | `Vec2 vec2_scale(Vec2 v, fix16_t s)` | スカラー倍 |
| `vec2_dot` | `fix16_t vec2_dot(Vec2 a, Vec2 b)` | 内積 |
| `vec2_cross` | `fix16_t vec2_cross(Vec2 a, Vec2 b)` | 外積 (2Dスカラー) |
| `vec2_length_sq` | `fix16_t vec2_length_sq(Vec2 v)` | 長さの二乗 |
| `vec2_length` | `fix16_t vec2_length(Vec2 v)` | 長さ |
| `vec2_normalize` | `Vec2 vec2_normalize(Vec2 v)` | 正規化 |
| `vec2_rotate` | `Vec2 vec2_rotate(Vec2 v, int angle)` | 回転 (512分割角度) |
| `vec2_distance` | `fix16_t vec2_distance(Vec2 a, Vec2 b)` | 2点間距離 |
| `vec2_lerp` | `Vec2 vec2_lerp(Vec2 a, Vec2 b, fix16_t t)` | ベクトル線形補間 |

### 3.9 線形補間・イージング (lerp.c)

| 関数 | シグネチャ | 説明 |
|------|-----------|------|
| `fix16_lerp` | `fix16_t fix16_lerp(fix16_t a, fix16_t b, fix16_t t)` | 線形補間 |
| `lerp_int` | `int lerp_int(int a, int b, int t, int t_max)` | 整数線形補間 |
| `ease_in_quad` | `fix16_t ease_in_quad(fix16_t t)` | 加速イージング |
| `ease_out_quad` | `fix16_t ease_out_quad(fix16_t t)` | 減速イージング |
| `ease_in_out_quad` | `fix16_t ease_in_out_quad(fix16_t t)` | S字イージング |
| `ease_in_cubic` | `fix16_t ease_in_cubic(fix16_t t)` | 3次加速 |
| `ease_out_cubic` | `fix16_t ease_out_cubic(fix16_t t)` | 3次減速 |
| `ease_in_out_cubic` | `fix16_t ease_in_out_cubic(fix16_t t)` | 3次S字 |
| `ease_bounce` | `fix16_t ease_bounce(fix16_t t)` | バウンスイージング |

---

## 4. ゲーム開発での実用シナリオ

### 4.1 弾幕シューティング

```c
/* 敵→自機への角度を取得して弾を発射 */
int angle = iatan2(player_y - enemy_y, player_x - enemy_x);
bullet->vx = fix16_mul(bullet_speed, FIX16_FROM_INT(icos(angle))) / ISIN_SCALE;
bullet->vy = fix16_mul(bullet_speed, FIX16_FROM_INT(isin(angle))) / ISIN_SCALE;
```

### 4.2 キャラクター移動

```c
/* パッド入力方向に正規化して移動 */
Vec2 input = vec2_make(FIX16_FROM_INT(dx), FIX16_FROM_INT(dy));
Vec2 dir = vec2_normalize(input);
Vec2 velocity = vec2_scale(dir, move_speed);
pos = vec2_add(pos, velocity);
```

### 4.3 UIアニメーション

```c
/* メニューのスライドイン */
fix16_t t = fix16_div(FIX16_FROM_INT(frame), FIX16_FROM_INT(ANIM_FRAMES));
fix16_t eased = ease_out_cubic(t);
int menu_x = FIX16_TO_INT(fix16_lerp(
    FIX16_FROM_INT(-MENU_WIDTH), FIX16_FROM_INT(0), eased));
```

### 4.4 当たり判定

```c
/* 円と円の衝突 */
u32 dist_sq = fast_distance_sq(a->x - b->x, a->y - b->y);
u32 radii = a->radius + b->radius;
if (dist_sq < radii * radii) { /* 衝突 */ }
```

---

## 5. リソース使用量の見積もり

| モジュール | コードサイズ | データサイズ (LUT) | 合計 |
|-----------|------------|-------------------|------|
| fix16.c | ~200B | 0B | ~200B |
| trig.c | ~100B | 1,024B (sin_table) | ~1,124B |
| sqrt.c | ~150B | 0B | ~150B |
| atan2.c | ~300B | ~256B (CORDIC定数) | ~556B |
| recip.c | ~80B | 1,028B (recip_table) | ~1,108B |
| random.c | ~100B | 0B | ~100B |
| vec2.c | ~400B | 0B | ~400B |
| lerp.c | ~300B | 0B | ~300B |
| **合計** | **~1,630B** | **~2,308B** | **~3,938B** |

**合計 約4KB** — OS32Xの外部プログラムサイズ制約 (256KB) に対して十分に小さい。

---

## 6. 実装計画

### Phase 1: 基盤構築 (既存移設 + fix16)

- [x] `libos32math.h` ヘッダ作成 (型定義, 定数, 全API宣言)
- [x] `fix16.c` 実装 (乗除算, 変換, clamp)
- [x] `trig.c` 実装 (gfx_math.c の sin_table + 関数を移設)
- [x] `gfx_math.c` を互換ラッパーに変更
- [x] Makefile に `LIBMATH_OBJS` 追加, リンク順序調整
- [x] ビルド確認 (既存プログラム全ての動作に影響なし)

### Phase 2: 関数拡充

- [x] `sqrt.c` 実装 (isqrt, fix16_sqrt, fast_distance)
- [x] `atan2.c` 実装 (CORDIC方式 iatan2)
- [x] `random.c` 実装 (xorshift32)
- [x] `recip.c` 実装 (逆数テーブル)

### Phase 3: ゲーム向け高レベルAPI

- [x] `vec2.c` 実装 (2Dベクトル演算一式)
- [x] `lerp.c` 実装 (補間 + イージング)
- [x] テストプログラム `math_test.c` 作成

### Phase 4: 既存ライブラリ統合

- [x] `libos32gfx/draw/gfx_rotate.c` の数値計算を libos32math に移行
- [x] `libpyxel` での利用検証 (現時点で数学関数未使用、インクルードパス設定済み)
- [x] `libos32snd` での利用検証 (現時点で数学関数未使用、インクルードパス設定済み)
- [x] ドキュメント更新 (05_drivers.md §5-8追加, INDEX.md 既存)

---

## 7. 関連ドキュメント

| ドキュメント | 内容 |
|-------------|------|
| [05_drivers.md §5-5](../../05_drivers.md) | libos32gfx 仕様 (gfx_math.c の記述あり) |
| [DEVELOPMENT.md](../../DEVELOPMENT.md) | 技術仕様ガイド |
| [POLICY_DEV.md](../../POLICY_DEV.md) | コーディング規約 (C89必須等) |

---

*この設計書は libos32math の実装に先立つ設計ドキュメントであり、*
*実装フェーズの進行に伴い更新される。*
