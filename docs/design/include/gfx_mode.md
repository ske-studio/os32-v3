# include/gfx_mode.h — 設計
> 状態: **現行 (2026-10-31)** — 出所: GFX_MODES_DESIGN (D1 で移した)
ソース: `include/gfx_mode.h`

- row は最小行幅。実 pitch は mode.pitch を採り、row 以下でないこと・必要な装置アラインを検査する。
- include/gfx_mode.h は形式・モードの内部橋渡し。SDK 形式番号を参照し、数値の別定義を作らない。

## 3. ドライバの能力の表と HAL 境界

```c
/* include/gfx_mode.h: 純データ。タイミングのバイト列は装置側に残す。 */
typedef struct GfxMode {
    u16 width, height;
    u8 format, dac_bits, hw_mode, reserved;
    u32 pitch;                 /* 1プレーンまたは packed 1行の実バイト数 */
} GfxMode;
static inline int gfx_mode_layout(const GfxMode *m, u32 *plane_bytes,
                                  u32 *total_bytes);
```

- hw_mode はドライバ内のタイミング ID。画素寸法から GDC SYNC 列を合成しない。今のレジスタ書込み順・待ち上限は維持。
- DAC 幅は物理出力の値。アプリの RGB 0..15、既存の色キューブ・階調・変換丸めを変更しない。
