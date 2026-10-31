# drivers/video/cirrus/wab_glue.h — 設計
> 状態: **現行 (2026-10-31)** — 出所: DESIGN (D1 で移した)
ソース: `drivers/video/cirrus/wab_glue.h`

HAL の下は 2 層に分ける:

```
libos32gui ──► HAL (バックエンド表 + 能力ビット)
                 ├─ 9801 プレーン      (CPU 直書き、現行 backend_pc98.c)
                 ├─ PEGC 8bpp          (CPU 直書き、E0000h MMIO + F00000h 窓)
                 └─ Cirrus GD54xx      = チップドライバ (3C0h 系の論理番号だけで書く)
                                        + ボードグルー (Xe10 内蔵 / PC-9801-96 / メルコ / ...)
```

3. **層を越えない**: チップドライバにポート番号を書かない。グルーにチップの知識を書かない。
   GUI からドライバのヘッダを include しない。
