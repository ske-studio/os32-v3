# H2: PEGC 256 色バックエンド (9821)

> 発行: PM (2026-09-05) / 状態: **受入完了 (2026-09-06)**
> レーン: H / 前提: H1 完了
> 親: [TASKS.md](../../tasks/gui/TASKS.md) / 設計: [DESIGN.md](../../tasks/gui/DESIGN.md) §3.1, §8 (PEGC), §10-2 / 契約: G5, G8
> 排他: `gfx/backend_pegc.c` (新規)、`include/pegc.h` (新規、ポート定数)
> 位置づけ: v1.1 後半。ゲート G5 の前半

## ゴール

NP21/W の 9821 モードで、GUI が **640×480 / 256 色 (packed 8bpp)** で動く。GUI と WM は
無変更 (`gfx_screen_info()` の値が変わるだけ)。

## 作業

1. **`include/pegc.h`**: DESIGN §3.1 のポートと MMIO を三層定数で。出典コメント必須
   (`docs/hw/undocumented/io_pegc.md` 等)。
   モード 6Ah (07h→21h)、読み戻し 09A0h / 0Ah、31KHz 09A8h、MMIO E0004h / E0006h (バンク)、
   E0100h (画素形式)、E0102h (リニア窓 F00000h)、パレット A8h / AAh / ACh / AEh。
2. **probe**: 9821 判定 (資料の機種判定ポート) + `043Bh` bit2 で 16MB 空間の可否。
   9801 では 0 を返して H1 の 9801 実装に落ちる。
3. **リニア窓**: `F00000h` を **ページングで張る** (K に依頼: `paging` は K 排他。
   `paging_map_range(F00000h, 480 ライン分, RW+USER)` 相当の 1 関数)。バンク窓 A8000h は
   使わない (DESIGN §8: 51 ライン毎の MMIO 切替で矩形が割れる)。
4. **バックバッファ**: 8bpp 640×480 = 300KB。主記憶 0x6A000 (現行 4 プレーン用 128KB) には
   入らないので、`pgalloc` から確保 (K に依頼、または `MEM_GFX_BB8` を memmap.h に切る)。
   `bb_format = GFX_FMT_PACKED8`、`bb_pitch = 640`。
5. **present_rect**: バックバッファ → リニア窓へ `rep movsd` (16bit バスなので回数は
   変わらないが命令数は減る)。カウンタは 1 バイト/px で加算。
6. **パレット**: 256 色。起動時に 0〜15 をシステム色、16〜255 は初期値 (グレースケール
   または 6×6×6 立方)。`lease_first = 16`, `lease_count = 240`。
7. **能力ビット**: `TEXT_OVERLAY` は **DESIGN §5 の未確認事項**。NP21/W の 256 色モードで
   TVRAM に文字を置いて重なるかを最初に確かめ、結果で立てるか決める (結果を DESIGN §5 に
   書き戻す)。`PAGE_FLIP` は 0 (PEGC のページ切替は v1 で使わない)。

## 鉄則

- ポート番号は `pegc.h` にだけ書く。`gfx_core.c` と GUI 側に 9821 の文字を書かない。
- ページテーブルを H が直接触らない。K に 1 関数を切ってもらう。
- 物理メモリの上限判定 (F00000h〜FFFFFFh は RAM ではない) は probe で行い、8MB 構成でも
  16MB 構成でも正しく張れることを NP21/W の 2 設定で確認する。

## 完了条件

- NP21/W 9821 設定で `hal_test` が 640×480 / bpp 8 / PACKED8 / lease 16〜240 を表示する。
- C1 の `gdi_test` が **無変更で** 256 色モードで同じ絵を出す (16 色部分は同一、
  16〜255 の見本帯が増える)。
- 9801 設定で回帰なし (probe が 0 を返し、H1 の実装に落ちる)。
- `gfx_stats().present_bytes` が全画面 present で 307200。

## 検証手順

- np21x64w.ini の機種切替は **[D2] 承認事項**。検証層は PM が用意した ini 2 種を切り替えて
  `os32-cycle deploy` → `os32-cycle demo gdi_test` を両方で回し、screenshot を比較する。
